#include "core/SpoutReceiver.h"
#include "core/Log.h"
#include "core/Util.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>

namespace vdc {

namespace {

// Pick the shader-visible view format for a sender texture format.
DXGI_FORMAT ViewFormatFor(DXGI_FORMAT f, bool& linear) {
    linear = false;
    switch (f) {
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:   return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:   return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:   return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: linear = true; return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: linear = true; return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
            linear = true;
            return f;
        default:
            return f;
    }
}

bool IsVrchatSender(const char* name) {
    return name && (_strnicmp(name, "VRC", 3) == 0 || _strnicmp(name, "VRChat", 6) == 0);
}

// The refresh rate of the fastest active display: a picture cannot change on a screen more often than that.
double FastestRefresh() {
    double best = 0.0;
    DISPLAY_DEVICEA dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i, dd.cb = sizeof(dd)) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        DEVMODEA dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsA(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
            best = std::max(best, (double)dm.dmDisplayFrequency);
    }
    return best > 0.0 ? std::clamp(best, 30.0, 500.0) : 60.0;
}

// The new-frame test's hash of a whole picture: each thread folds a 4x4 block, a group of 16x16 threads adds its
// blocks together in shared memory, and one thread per group adds that to the two sums. The position goes into
// every pixel's value, so content that only moves changes the sums too.
const char kProbeShader[] = R"(
Texture2D<float4> src : register(t0);
RWByteAddressBuffer sums : register(u0);
cbuffer Size : register(b0) { uint width; uint height; uint2 unused; };
groupshared uint2 part[256];
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    uint2 acc = uint2(0, 0);
    for (uint y = id.y * 4; y < id.y * 4 + 4; ++y) {
        for (uint x = id.x * 4; x < id.x * 4 + 4; ++x) {
            if (x >= width || y >= height) continue;
            const uint4 v = asuint(src.Load(int3(x, y, 0)));
            const uint h = (v.x * 0x9E3779B1u) ^ (v.y * 0x85EBCA77u) ^ (v.z * 0xC2B2AE3Du) ^ (v.w * 0x27D4EB2Fu) ^
                           (x * 0x165667B1u + y * 0xD3A2646Cu);
            acc += uint2(h, (h ^ (h >> 15)) * 0x2C1B3C6Du);
        }
    }
    part[gi] = acc;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 128; s > 0; s >>= 1) {
        if (gi < s) part[gi] += part[gi + s];
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0) {
        uint previous;
        sums.InterlockedAdd(0, part[0].x, previous);
        sums.InterlockedAdd(4, part[0].y, previous);
    }
}
)";

} // namespace

bool SpoutReceiver::Init(Device& device) {
    // Route Spout's D3D11 work through the D3D11On12 device so that the processing queue orders everything.
    if (!m_spout.OpenDirectX11(device.D3D11())) {
        Log::Error("Spout: OpenDirectX11 failed");
        return false;
    }
    m_open = true;
    m_spout.SetReceiverName(nullptr);
    Log::Info("Spout receiver ready (Spout %s)", "2.007");
    return true;
}

void SpoutReceiver::Shutdown(GpuContext& gpu) {
    if (!m_open) return;
    m_spout.ReleaseReceiver();
    ReleaseTexture(gpu);
    m_probeShader.Reset(); m_probeSums.Reset(); m_probeReadback.Reset(); m_probeConstants.Reset(); m_probeUav.Reset();
    m_probeContext.Reset(); m_probeDevice.Reset();
    m_spout.CloseDirectX11();
    m_open = false;
}

void SpoutReceiver::SetRequestedSender(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_requestedShared = name;
}

SourceFrame SpoutReceiver::Frame() const {
    SourceFrame f;
    if (!m_connected || !m_tex12) return f;
    f.texture = m_tex12.Get();
    f.srv = m_srv;
    f.width = m_width;
    f.height = m_height;
    f.format = m_format;
    f.viewFormat = m_viewFormat;
    f.linear = m_linear;
    f.stillImage = false;
    f.hasFrame = m_freshFrames > 0;
    return f;
}

void SpoutReceiver::ApplySenderName(const char* name) {
    m_spout.SetReceiverName((name && *name) ? name : nullptr);
    m_spout.ReleaseReceiver();
    m_applied = (name && *name) ? name : "";
    m_connected = false;
}

std::vector<std::string> SpoutReceiver::EnumerateSenders() {
    std::vector<std::string> out;
    if (!m_open) return out;
    const int n = m_spout.GetSenderCount();
    for (int i = 0; i < n; ++i) {
        char name[256] = {};
        if (m_spout.GetSender(i, name, 256) && name[0]) out.emplace_back(name);
    }
    return out;
}

bool SpoutReceiver::CreateTexture(GpuContext& gpu, UINT w, UINT h, DXGI_FORMAT fmt) {
    Device& device = gpu.Dev();
    ReleaseTexture(gpu);
    if (fmt == DXGI_FORMAT_UNKNOWN) fmt = DXGI_FORMAT_B8G8R8A8_UNORM;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = device.D3D12()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_tex12));
    if (FAILED(hr)) { Log::Hr(LogLevel::Error, "Spout: create receive texture", hr); return false; }
    m_tex12->SetName(L"Spout receive");

    D3D11_RESOURCE_FLAGS flags11{};
    flags11.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = device.On12()->CreateWrappedResource(m_tex12.Get(), &flags11, D3D12_RESOURCE_STATE_COMMON,
                                              D3D12_RESOURCE_STATE_COMMON, IID_PPV_ARGS(&m_tex11));
    if (FAILED(hr)) { Log::Hr(LogLevel::Error, "Spout: CreateWrappedResource", hr); m_tex12.Reset(); return false; }

    m_viewFormat = ViewFormatFor(fmt, m_linear);
    m_srv = device.AllocStaging();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = m_viewFormat;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device.D3D12()->CreateShaderResourceView(m_tex12.Get(), &sd, m_srv);

    m_width = w;
    m_height = h;
    m_format = fmt;
    Log::Info("Spout: receiving %s %ux%u format %d", m_senderName.c_str(), w, h, (int)fmt);
    return true;
}

bool SpoutReceiver::ProbeOpen(Device& device) {
    ProbeClose();
    if (m_probeUnavailable) return false;
    if (!m_probeDevice) {
        auto fail = [&](const char* what, HRESULT hr) {
            Log::Hr(LogLevel::Warn, StrPrintf("Spout new-frame test: %s", what).c_str(), hr);
            m_probeUnavailable = true;
            m_probeShader.Reset(); m_probeSums.Reset(); m_probeReadback.Reset(); m_probeConstants.Reset(); m_probeUav.Reset();
            m_probeContext.Reset(); m_probeDevice.Reset();
            return false;
        };
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(device.Adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                                       &m_probeDevice, nullptr, &m_probeContext);
        if (FAILED(hr)) return fail("D3D11CreateDevice", hr);
        ComPtr<ID3DBlob> code, errors;
        hr = D3DCompile(kProbeShader, sizeof(kProbeShader) - 1, "spout_probe", nullptr, nullptr, "main", "cs_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(hr)) {
            if (errors) Log::Warn("Spout new-frame test: %s", (const char*)errors->GetBufferPointer());
            return fail("D3DCompile", hr);
        }
        hr = m_probeDevice->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &m_probeShader);
        if (FAILED(hr)) return fail("CreateComputeShader", hr);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 8;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        hr = m_probeDevice->CreateBuffer(&bd, nullptr, &m_probeSums);
        if (FAILED(hr)) return fail("CreateBuffer (sums)", hr);
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R32_TYPELESS;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = 2;
        ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        hr = m_probeDevice->CreateUnorderedAccessView(m_probeSums.Get(), &ud, &m_probeUav);
        if (FAILED(hr)) return fail("CreateUnorderedAccessView", hr);
        bd = {};
        bd.ByteWidth = 8;
        bd.Usage = D3D11_USAGE_STAGING;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = m_probeDevice->CreateBuffer(&bd, nullptr, &m_probeReadback);
        if (FAILED(hr)) return fail("CreateBuffer (readback)", hr);
        bd = {};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = m_probeDevice->CreateBuffer(&bd, nullptr, &m_probeConstants);
        if (FAILED(hr)) return fail("CreateBuffer (constants)", hr);
    }

    HANDLE handle = m_spout.GetSenderHandle();
    if (!handle) return false;
    HRESULT hr = m_probeDevice->OpenSharedResource(handle, IID_PPV_ARGS(&m_probeTexture));
    if (FAILED(hr)) { Log::Hr(LogLevel::Warn, "Spout new-frame test: OpenSharedResource", hr); m_probeTexture.Reset(); return false; }
    D3D11_TEXTURE2D_DESC td{};
    m_probeTexture->GetDesc(&td);
    bool linear = false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = ViewFormatFor(td.Format, linear);
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    hr = m_probeDevice->CreateShaderResourceView(m_probeTexture.Get(), &sd, &m_probeSrv);
    if (FAILED(hr)) { Log::Hr(LogLevel::Warn, "Spout new-frame test: CreateShaderResourceView", hr); ProbeClose(); return false; }
    m_probeWidth = td.Width;
    m_probeHeight = td.Height;
    m_probeRateStart = 0.0;
    m_probeRateCount = 0;
    m_senderFps = 0.0;
    const UINT size[4] = { td.Width, td.Height, 0, 0 };
    m_probeContext->UpdateSubresource(m_probeConstants.Get(), 0, nullptr, size, 0, 0);
    m_probeReady = true;
    return true;
}

void SpoutReceiver::ProbeClose() {
    m_probeSrv.Reset();
    m_probeTexture.Reset();
    m_probeReady = m_probePending = m_probeKnown = false;
}

void SpoutReceiver::ProbeStart() {
    ID3D11DeviceContext* c = m_probeContext.Get();
    const UINT zero[4] = {};
    c->ClearUnorderedAccessViewUint(m_probeUav.Get(), zero);
    c->CSSetShader(m_probeShader.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = m_probeSrv.Get();
    ID3D11UnorderedAccessView* uav = m_probeUav.Get();
    ID3D11Buffer* cb = m_probeConstants.Get();
    c->CSSetShaderResources(0, 1, &srv);
    c->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    c->CSSetConstantBuffers(0, 1, &cb);
    c->Dispatch((m_probeWidth + 63) / 64, (m_probeHeight + 63) / 64, 1);
    srv = nullptr;
    uav = nullptr;
    c->CSSetShaderResources(0, 1, &srv);
    c->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    c->CopyResource(m_probeReadback.Get(), m_probeSums.Get());
    c->Flush();
    m_probePending = true;
}

int SpoutReceiver::ProbePoll() {
    D3D11_MAPPED_SUBRESOURCE m{};
    const HRESULT hr = m_probeContext->Map(m_probeReadback.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return -1;
    m_probePending = false;
    if (FAILED(hr)) {
        Log::Hr(LogLevel::Warn, "Spout new-frame test: Map", hr);
        ProbeClose();
        return 1;
    }
    uint32_t sums[2] = {};
    std::memcpy(sums, m.pData, sizeof(sums));
    m_probeContext->Unmap(m_probeReadback.Get(), 0);
    const uint64_t hash = ((uint64_t)sums[1] << 32) | sums[0];
    const bool fresh = !m_probeKnown || hash != m_probeLast;
    m_probeLast = hash;
    m_probeKnown = true;
    return fresh ? 1 : 0;
}

void SpoutReceiver::ReleaseTexture(GpuContext& gpu) {
    ProbeClose();
    if (m_tex11) gpu.DeferRelease(m_tex11);
    if (m_tex12) gpu.DeferRelease(m_tex12);
    m_tex11.Reset();
    m_tex12.Reset();
    if (m_srv.ptr) { gpu.Dev().FreeStaging(m_srv); m_srv = {}; }
    m_width = m_height = 0;
}

bool SpoutReceiver::Receive(GpuContext& gpu, bool& changed, bool& fresh) {
    changed = false;
    fresh = false;
    if (!m_open) return false;
    Device& device = gpu.Dev();

    const double now = NowSeconds();
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requested = m_requestedShared;
    }

    // Apply a changed user request.
    if (!m_requested.empty()) {
        if (m_requested != m_applied) {
            ApplySenderName(m_requested.c_str());
            ReleaseTexture(gpu);
            changed = true;
        }
    } else if (now - m_lastScan > 1.0) {
        // Automatic mode: prefer the VRChat sender when several senders exist, otherwise the active sender.
        m_lastScan = now;
        std::string vrc;
        const int n = m_spout.GetSenderCount();
        for (int i = 0; i < n; ++i) {
            char name[256] = {};
            if (m_spout.GetSender(i, name, 256) && IsVrchatSender(name)) { vrc = name; break; }
        }
        if (vrc != m_applied) {
            ApplySenderName(vrc.empty() ? nullptr : vrc.c_str());
            ReleaseTexture(gpu);
            changed = true;
        }
    }

    // Without Spout's frame count (off unless it is switched on in the Spout settings program) a receiver cannot tell
    // a new frame from the last one: every receive copies the whole texture again and reads as new, and the pipeline
    // would process one picture thousands of times a second. Such a sender is checked once per refresh of the
    // fastest display (the most often a picture can change on a screen), and received when the new-frame test finds
    // a different picture; without the test, on every check.
    if (!changed && m_tex11 && m_connected && !m_spout.IsFrameCountEnabled()) {
        // Without the frame count Spout's figure is only the display's refresh: the rate of the new pictures the test
        // finds is the sender's own (none while the picture stands still).
        if (m_probeReady && m_probeRateStart > 0.0 && now - m_probeRateStart >= 1.0) {
            m_senderFps = m_probeRateCount / (now - m_probeRateStart);
            m_probeRateStart = now;
            m_probeRateCount = 0;
        }
        if (m_probeReady && m_probePending) {
            if (ProbePoll() <= 0) return m_freshFrames > 0;   // no answer yet, or the same picture again
        } else {
            if (now < m_uncountedNext) return m_freshFrames > 0;
            m_uncountedNext = now - m_uncountedNext < m_uncountedInterval ? m_uncountedNext + m_uncountedInterval : now + m_uncountedInterval;
            if (m_probeReady) {
                ProbeStart();
                return m_freshFrames > 0;                     // the answer comes with a later call
            }
        }
    }

    ID3D11Texture2D* tex = m_tex11.Get();
    ID3D11Resource* wrapped[] = { m_tex11.Get() };
    if (m_tex11) device.On12()->AcquireWrappedResources(wrapped, 1);
    const bool ok = m_spout.ReceiveTexture(&tex);
    if (m_tex11) device.On12()->ReleaseWrappedResources(wrapped, 1);

    if (!ok) {
        if (m_connected) {
            Log::Info("Spout: sender %s disconnected", m_senderName.c_str());
            ReleaseTexture(gpu);
            changed = true;
        }
        m_connected = false;
        m_senderName.clear();
        m_senderFps = 0.0;
        return false;
    }

    if (m_spout.IsUpdated()) {
        // Sender is new or changed: (re)create the receiving texture to match it.
        m_senderName = m_spout.GetSenderName() ? m_spout.GetSenderName() : "";
        const UINT w = m_spout.GetSenderWidth();
        const UINT h = m_spout.GetSenderHeight();
        const DXGI_FORMAT fmt = m_spout.GetSenderFormat();
        if (w == 0 || h == 0 || !CreateTexture(gpu, w, h, fmt)) {
            m_connected = false;
            return false;
        }
        m_connected = true;
        m_freshFrames = 0;
        changed = true;
        if (!m_spout.IsFrameCountEnabled()) {
            const double refresh = FastestRefresh();
            m_uncountedInterval = 1.0 / refresh;
            m_uncountedNext = 0.0;
            if (ProbeOpen(device))
                Log::Info("Spout: frame counting is off in the Spout settings; new frames of %s are found by a hash of the picture, "
                          "checked up to %.0f times a second (the fastest display's refresh)", m_senderName.c_str(), refresh);
            else
                Log::Info("Spout: frame counting is off in the Spout settings and the new-frame test is not available; %s is "
                          "received at most %.0f times a second (the fastest display's refresh)", m_senderName.c_str(), refresh);
        }
        // The first frame is copied on the next call.
        device.Context11()->Flush();
        return false;
    }

    if (!m_tex11) return false;
    m_connected = true;
    if (m_spout.IsFrameNew()) {
        fresh = true;
        ++m_freshFrames;
        m_lastFrameTime = now;
    }
    if (!m_probeReady) {
        m_senderFps = m_spout.GetSenderFps();
    } else {
        if (m_probeRateStart <= 0.0) m_probeRateStart = now;
        if (fresh) ++m_probeRateCount;
    }
    // Make the D3D11 copy visible to the D3D12 queue in order.
    device.Context11()->Flush();
    return m_freshFrames > 0;
}

} // namespace vdc
