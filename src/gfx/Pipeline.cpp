#include "gfx/Pipeline.h"
#include "core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vdc {

namespace {
constexpr float kLambda[3] = { 0.0010f, 0.0015f, 0.0020f };   // motion penalty per level
constexpr float kMotionViewScale = 1.0f / 16.0f;
constexpr size_t kMaxReadbacks = 6;

UINT Even(UINT v) { return std::max(16u, v & ~1u); }
}

// --- resources ----------------------------------------------------------------------------

bool Pipeline::CreateTex(Device& device, Tex& t, UINT w, UINT h, DXGI_FORMAT fmt, bool uav, const wchar_t* name) {
    ReleaseTex(device, t);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w; desc.Height = h; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = fmt; desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const HRESULT hr = device.D3D12()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                               nullptr, IID_PPV_ARGS(&t.res));
    if (FAILED(hr)) { Log::Hr(LogLevel::Error, StrPrintf("CreateCommittedResource(%ls)", name).c_str(), hr); return false; }
    t.res->SetName(name);
    t.w = w; t.h = h; t.fmt = fmt; t.state = D3D12_RESOURCE_STATE_COMMON;
    t.srv = device.AllocStaging();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = fmt; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device.D3D12()->CreateShaderResourceView(t.res.Get(), &sd, t.srv);
    if (uav) {
        t.uav = device.AllocStaging();
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = fmt; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device.D3D12()->CreateUnorderedAccessView(t.res.Get(), nullptr, &ud, t.uav);
    }
    return true;
}

void Pipeline::ReleaseTex(Device& device, Tex& t) {
    if (t.srv.ptr) { device.FreeStaging(t.srv); t.srv = {}; }
    if (t.uav.ptr) { device.FreeStaging(t.uav); t.uav = {}; }
    if (t.res) { device.DeferRelease(t.res); t.res.Reset(); }
    t.w = t.h = 0; t.fmt = DXGI_FORMAT_UNKNOWN; t.state = D3D12_RESOURCE_STATE_COMMON;
}

void Pipeline::Transition(ID3D12GraphicsCommandList* cmd, Tex& t, D3D12_RESOURCE_STATES state) {
    if (!t.res) return;
    if (t.state == state) {
        if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) Device::UavBarrier(cmd, t.res.Get());
        return;
    }
    Device::Barrier(cmd, t.res.Get(), t.state, state);
    t.state = state;
}

void Pipeline::ReleaseFeatures(Device& device) {
    if (m_nr.Created() || m_dlaa.Created()) device.WaitIdle();
    m_nr.Release(m_ngx);
    m_dlaa.Release(m_ngx);
    m_nrCreatedPreset = -1;
    m_dlaaCreatedPreset = -1;
}

void Pipeline::ReleaseResources(Device& device) {
    device.WaitIdle();
    ReleaseFeatures(device);
    m_nvof.Shutdown();
    for (auto& t : m_nvofSrc11) t.Reset();
    m_flow11.Reset(); m_cost11.Reset();
    m_nvofReady = false;
    ReleaseTex(device, m_color8);
    for (auto& set : m_luma) for (auto& t : set) ReleaseTex(device, t);
    ReleaseTex(device, m_nvofIn);
    for (int i = 0; i < 3; ++i) { ReleaseTex(device, m_bm[i]); ReleaseTex(device, m_bc[i]); }
    for (auto& t : m_bmMed) ReleaseTex(device, t);
    ReleaseTex(device, m_flow); ReleaseTex(device, m_cost);
    ReleaseTex(device, m_mv); ReleaseTex(device, m_conf); ReleaseTex(device, m_depth);
    ReleaseTex(device, m_dlaaOut); ReleaseTex(device, m_nrOut);
    ReleaseTex(device, m_final); ReleaseTex(device, m_display);
    if (m_statsUav.ptr) { device.FreeStaging(m_statsUav); m_statsUav = {}; }
    if (m_statsBuf) { device.DeferRelease(m_statsBuf); m_statsBuf.Reset(); }
    for (UINT i = 0; i < Device::kFramesInFlight; ++i) {
        if (m_statsReadback[i]) { device.DeferRelease(m_statsReadback[i]); m_statsReadback[i].Reset(); }
        m_statsPending[i] = false; m_statsFence[i] = 0;
    }
    for (auto& rb : m_readbacks) { if (rb.buffer) device.DeferRelease(rb.buffer); }
    m_readbacks.clear();
    m_built = false;
    m_hasDisplay = false;
    m_haveHistory = false;
}

Pipeline::Config Pipeline::ComputeConfig(const SpoutReceiver& spout, const Settings& s) const {
    Config c;
    c.srcW = spout.Width(); c.srcH = spout.Height(); c.srcFmt = spout.Format();
    if (!c.srcW || !c.srcH) return c;
    c.outW = c.srcW; c.outH = c.srcH;
    if (s.customResolution) {
        c.outW = Even((UINT)s.customWidth);
        c.outH = s.keepAspect ? Even((UINT)std::lround((double)c.outW * c.srcH / c.srcW)) : Even((UINT)s.customHeight);
    }
    const bool upscale = s.nrUpscale && s.customResolution && (c.outW > c.srcW || c.outH > c.srcH);
    c.inW = upscale ? c.srcW : c.outW;
    c.inH = upscale ? c.srcH : c.outH;
    c.nvof = s.motionMode == MotionNvOpticalFlow;
    c.nvofGrid = (UINT)s.nvofGrid;
    c.nvofPerf = (UINT)s.nvofPerf;
    return c;
}

bool Pipeline::Rebuild(Device& device, const Config& cfg) {
    ReleaseResources(device);
    m_cfg = cfg;
    m_srcW = cfg.srcW; m_srcH = cfg.srcH; m_inW = cfg.inW; m_inH = cfg.inH; m_outW = cfg.outW; m_outH = cfg.outH;
    ID3D12Device* dev = device.D3D12();
    bool ok = true;
    ok &= CreateTex(device, m_color8, m_inW, m_inH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"color8");
    for (int l = 0; l < 3; ++l) {
        m_lumaW[l] = std::max(1u, (m_inW + (1u << l) - 1) >> l);
        m_lumaH[l] = std::max(1u, (m_inH + (1u << l) - 1) >> l);
        m_gridW[l] = (m_lumaW[l] + 7) / 8;
        m_gridH[l] = (m_lumaH[l] + 7) / 8;
        for (int p = 0; p < 2; ++p) ok &= CreateTex(device, m_luma[p][l], m_lumaW[l], m_lumaH[l], DXGI_FORMAT_R8_UNORM, true, L"luma");
        ok &= CreateTex(device, m_bm[l], m_gridW[l], m_gridH[l], DXGI_FORMAT_R32G32_FLOAT, true, L"blockMv");
        ok &= CreateTex(device, m_bc[l], m_gridW[l], m_gridH[l], DXGI_FORMAT_R32_FLOAT, true, L"blockCost");
    }
    for (int p = 0; p < 2; ++p) ok &= CreateTex(device, m_bmMed[p], m_gridW[0], m_gridH[0], DXGI_FORMAT_R32G32_FLOAT, true, L"blockMvMedian");
    ok &= CreateTex(device, m_mv, m_inW, m_inH, DXGI_FORMAT_R16G16_FLOAT, true, L"motionVectors");
    ok &= CreateTex(device, m_conf, m_inW, m_inH, DXGI_FORMAT_R8_UNORM, true, L"confidence");
    ok &= CreateTex(device, m_depth, m_inW, m_inH, DXGI_FORMAT_R32_FLOAT, true, L"depth");
    ok &= CreateTex(device, m_dlaaOut, m_inW, m_inH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"dlaaOut");
    ok &= CreateTex(device, m_nrOut, m_outW, m_outH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"nrOut");
    ok &= CreateTex(device, m_final, m_outW, m_outH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"final");
    ok &= CreateTex(device, m_display, m_outW, m_outH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"display");
    if (!ok) { ReleaseResources(device); return false; }

    // Display SRV in the shader-visible heap for ImGui.
    if (!m_displayStatic.Valid()) m_displayStatic = device.AllocStatic();
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(m_display.res.Get(), &sd, m_displayStatic.cpu);
    }

    // Stats buffer + readbacks.
    {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 256; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr = dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_statsBuf));
        if (FAILED(hr)) { Log::Hr(LogLevel::Error, "stats buffer", hr); ReleaseResources(device); return false; }
        m_statsBuf->SetName(L"stats");
        m_statsUav = device.AllocStaging();
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = 4; ud.Buffer.StructureByteStride = 4;
        dev->CreateUnorderedAccessView(m_statsBuf.Get(), nullptr, &ud, m_statsUav);
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        bd.Flags = D3D12_RESOURCE_FLAG_NONE;
        for (UINT i = 0; i < Device::kFramesInFlight; ++i) {
            hr = dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_statsReadback[i]));
            if (FAILED(hr)) { Log::Hr(LogLevel::Error, "stats readback", hr); ReleaseResources(device); return false; }
        }
    }

    // Optical flow session (prefers 8-bit luma input, falls back to BGRA8).
    m_nvofReady = false;
    m_nvofError.clear();
    m_nvofFmt = DXGI_FORMAT_UNKNOWN;
    if (cfg.nvof) {
        std::string err;
        if (m_nvof.Init(device, m_inW, m_inH, DXGI_FORMAT_R8_UNORM, cfg.nvofGrid, cfg.nvofPerf, err)) {
            m_nvofFmt = DXGI_FORMAT_R8_UNORM;
        } else {
            Log::Warn("NVOF: R8 input rejected (%s), trying BGRA8", err.c_str());
            if (device.TypedUavStoreSupported(DXGI_FORMAT_B8G8R8A8_UNORM) &&
                m_nvof.Init(device, m_inW, m_inH, DXGI_FORMAT_B8G8R8A8_UNORM, cfg.nvofGrid, cfg.nvofPerf, err)) {
                m_nvofFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
            } else {
                m_nvofError = err;
            }
        }
        if (m_nvofFmt != DXGI_FORMAT_UNKNOWN) {
            bool nvofOk = true;
            if (m_nvofFmt == DXGI_FORMAT_B8G8R8A8_UNORM)
                nvofOk &= CreateTex(device, m_nvofIn, m_inW, m_inH, DXGI_FORMAT_B8G8R8A8_UNORM, true, L"nvofIn");
            nvofOk &= CreateTex(device, m_flow, m_nvof.FlowWidth(), m_nvof.FlowHeight(), DXGI_FORMAT_R16G16_SINT, false, L"nvofFlow");
            if (m_nvof.HasCost()) nvofOk &= CreateTex(device, m_cost, m_nvof.FlowWidth(), m_nvof.FlowHeight(), DXGI_FORMAT_R8_UINT, false, L"nvofCost");
            D3D11_RESOURCE_FLAGS f11{};
            f11.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const D3D12_RESOURCE_STATES st = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            auto wrap = [&](Tex& t, ComPtr<ID3D11Texture2D>& out) {
                if (!t.res) return false;
                const HRESULT hr = device.On12()->CreateWrappedResource(t.res.Get(), &f11, st, st, IID_PPV_ARGS(&out));
                if (FAILED(hr)) { Log::Hr(LogLevel::Error, "CreateWrappedResource(nvof)", hr); return false; }
                return true;
            };
            if (m_nvofFmt == DXGI_FORMAT_R8_UNORM) {
                nvofOk &= wrap(m_luma[0][0], m_nvofSrc11[0]);
                nvofOk &= wrap(m_luma[1][0], m_nvofSrc11[1]);
            } else {
                nvofOk &= wrap(m_nvofIn, m_nvofSrc11[0]);
                m_nvofSrc11[1] = m_nvofSrc11[0];
            }
            nvofOk &= wrap(m_flow, m_flow11);
            if (m_nvof.HasCost()) nvofOk &= wrap(m_cost, m_cost11);
            if (nvofOk) {
                m_nvofReady = true;
            } else {
                m_nvofError = "failed to create optical flow interop resources";
                m_nvof.Shutdown();
                for (auto& t : m_nvofSrc11) t.Reset();
                m_flow11.Reset(); m_cost11.Reset();
            }
        }
        if (!m_nvofReady) Log::Warn("NVOF unavailable, falling back to block matching: %s", m_nvofError.c_str());
    }

    m_built = true;
    m_haveHistory = false;
    m_resetRequested = true;
    m_cur = 0;
    Log::Info("Pipeline resources: source %ux%u, input %ux%u, output %ux%u%s", m_srcW, m_srcH, m_inW, m_inH, m_outW, m_outH,
              m_nvofReady ? " (NVOF)" : "");
    return true;
}

// --- lifecycle ----------------------------------------------------------------------------

bool Pipeline::Init(Device& device, const std::wstring& exeDir, const std::wstring& appDataDir, std::wstring& error) {
    m_exeDir = exeDir;
    m_appDataDir = appDataDir;
    if (!m_shaders.Init(device, error)) return false;
    m_ngx.Init(device, exeDir, appDataDir);   // failure is non-fatal: the app still works as a viewer
    m_status.nvofAvailable = NvOpticalFlow::LibraryAvailable();
    return true;
}

void Pipeline::Shutdown(Device& device) {
    ReleaseResources(device);
    UnloadNrRuntime(device);
    if (m_displayStatic.Valid()) { device.FreeStatic(m_displayStatic); m_displayStatic = {}; }
    m_ngx.Shutdown();
    m_shaders.Shutdown();
}

bool Pipeline::LoadNrRuntime(Device& device, const std::wstring& dllPath, std::string& error) {
    if (m_nr.RuntimeLoaded() && m_nr.RuntimePath() == dllPath) return true;
    UnloadNrRuntime(device);
    m_nrFailed = false;
    m_nrError.clear();
    if (!m_nr.LoadRuntime(device.D3D12(), dllPath, m_exeDir, error)) {
        m_nrError = error;
        Log::Error("DLSSNR runtime: %s", error.c_str());
        return false;
    }
    return true;
}

void Pipeline::UnloadNrRuntime(Device& device) {
    if (m_nr.Created()) { device.WaitIdle(); m_nr.Release(m_ngx); m_nrCreatedPreset = -1; }
    if (m_nr.RuntimeLoaded()) { device.WaitIdle(); m_nr.UnloadRuntime(); }
}

void Pipeline::RequestCapture(const std::wstring& folder, bool keepAlpha, bool saveOriginal) {
    m_captureRequested = true;
    m_captureFolder = folder;
    m_captureKeepAlpha = keepAlpha;
    m_captureOriginal = saveOriginal;
}

// --- passes -------------------------------------------------------------------------------

void Pipeline::RunConvert(Device& device, ID3D12GraphicsCommandList* cmd, SpoutReceiver& spout, bool writeNvof) {
    device.TimerBegin(cmd, GpuTimer::Convert);
    Tex& luma0 = m_luma[m_cur][0];
    Transition(cmd, m_color8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, luma0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (writeNvof) Transition(cmd, m_nvofIn, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchDesc d;
    d.id = ShaderId::Convert;
    d.constants.srcWidth = m_srcW; d.constants.srcHeight = m_srcH;
    d.constants.dstWidth = m_inW; d.constants.dstHeight = m_inH;
    UINT flags = 0;
    if (spout.LinearInput()) flags |= 1;
    if (m_inW != m_srcW || m_inH != m_srcH) {
        flags |= 2;
        if (m_inW < m_srcW || m_inH < m_srcH) flags |= 16;
    }
    if (writeNvof) flags |= 4;
    d.constants.flags = flags;
    d.srv[0] = spout.Srv();
    d.uav[0] = m_color8.uav;
    d.uav[1] = luma0.uav;
    d.uav[2] = writeNvof ? m_nvofIn.uav : D3D12_CPU_DESCRIPTOR_HANDLE{};
    d.groupsX = Shaders::Groups(m_inW, 8); d.groupsY = Shaders::Groups(m_inH, 8);
    m_shaders.Dispatch(cmd, device, d);
    device.TimerEnd(cmd, GpuTimer::Convert);
}

void Pipeline::RunGuidance(Device& device, ID3D12GraphicsCommandList* cmd, const Settings& s, int motionMode, bool haveHistory) {
    const int cur = m_cur, prev = 1 - m_cur;
    // Luma pyramid.
    for (int l = 1; l < 3; ++l) {
        Transition(cmd, m_luma[cur][l - 1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_luma[cur][l], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchDesc d;
        d.id = ShaderId::Downsample;
        d.constants.srcWidth = m_lumaW[l - 1]; d.constants.srcHeight = m_lumaH[l - 1];
        d.constants.dstWidth = m_lumaW[l]; d.constants.dstHeight = m_lumaH[l];
        d.srv[0] = m_luma[cur][l - 1].srv;
        d.uav[0] = m_luma[cur][l].uav;
        d.groupsX = Shaders::Groups(m_lumaW[l], 8); d.groupsY = Shaders::Groups(m_lumaH[l], 8);
        m_shaders.Dispatch(cmd, device, d);
    }
    Transition(cmd, m_luma[cur][2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (int l = 0; l < 3; ++l) Transition(cmd, m_luma[prev][l], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_bmMed[prev], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Block matching: level 2 full search is always run (scene-cut statistics), finer levels only in compute mode.
    const int levels = (motionMode == MotionCompute) ? 3 : 1;
    for (int i = 0; i < levels; ++i) {
        const int l = 2 - i;
        Transition(cmd, m_bm[l], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(cmd, m_bc[l], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (l < 2) Transition(cmd, m_bm[l + 1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        DispatchDesc d;
        d.id = ShaderId::BlockMatch;
        d.constants.srcWidth = m_lumaW[l]; d.constants.srcHeight = m_lumaH[l];
        d.constants.level = (UINT)l;
        UINT flags = 0;
        if (l == 2) { flags |= 1; d.constants.intA = (UINT)s.searchRadius; }
        else { flags |= 2; d.constants.intB = (l == 1) ? 2u : 1u; if (l == 0) flags |= 8; }
        if (haveHistory) flags |= 4;
        d.constants.flags = flags;
        d.constants.paramB = kLambda[l];
        d.constants.extra0[0] = (float)m_gridW[0]; d.constants.extra0[1] = (float)m_gridH[0];
        if (l < 2) { d.constants.extra1[0] = (float)m_gridW[l + 1]; d.constants.extra1[1] = (float)m_gridH[l + 1]; }
        d.srv[0] = m_luma[cur][l].srv;
        d.srv[1] = m_luma[prev][l].srv;
        d.srv[2] = (l < 2) ? m_bm[l + 1].srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
        d.srv[3] = m_bmMed[prev].srv;
        d.uav[0] = m_bm[l].uav;
        d.uav[1] = m_bc[l].uav;
        d.groupsX = m_gridW[l]; d.groupsY = m_gridH[l];
        m_shaders.Dispatch(cmd, device, d);
    }
    if (motionMode == MotionCompute) {
        Transition(cmd, m_bm[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_bmMed[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchDesc d;
        d.id = ShaderId::MedianMv;
        d.constants.dstWidth = m_gridW[0]; d.constants.dstHeight = m_gridH[0];
        d.srv[0] = m_bm[0].srv;
        d.uav[0] = m_bmMed[cur].uav;
        d.groupsX = Shaders::Groups(m_gridW[0], 8); d.groupsY = Shaders::Groups(m_gridH[0], 8);
        m_shaders.Dispatch(cmd, device, d);
    }
}

bool Pipeline::RunOpticalFlow(Device& device, ID3D12GraphicsCommandList*& cmd) {
    if (!m_nvofReady) return false;
    const int cur = m_cur;
    Tex& src = (m_nvofFmt == DXGI_FORMAT_R8_UNORM) ? m_luma[cur][0] : m_nvofIn;
    Transition(cmd, src, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_flow, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (m_cost.Valid()) Transition(cmd, m_cost, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    device.TimerBegin(cmd, GpuTimer::OpticalFlow);
    cmd = device.SubmitAndContinue();

    ID3D11On12Device* on12 = device.On12();
    ID3D11DeviceContext* ctx = device.Context11();
    ID3D11Texture2D* src11 = (m_nvofFmt == DXGI_FORMAT_R8_UNORM) ? m_nvofSrc11[cur].Get() : m_nvofSrc11[0].Get();
    ID3D11Resource* acquired[3] = { src11, m_flow11.Get(), m_cost11.Get() };
    const UINT count = m_cost11 ? 3u : 2u;
    on12->AcquireWrappedResources(acquired, count);
    ctx->CopyResource(m_nvof.Input(cur), src11);
    std::string err;
    const bool ok = m_nvof.Execute(cur, err);
    if (ok) {
        ctx->CopyResource(m_flow11.Get(), m_nvof.FlowTexture());
        if (m_cost11) ctx->CopyResource(m_cost11.Get(), m_nvof.CostTexture());
    } else {
        m_nvofError = err;
        Log::Warn("NVOF execute failed: %s", err.c_str());
    }
    on12->ReleaseWrappedResources(acquired, count);
    ctx->Flush();
    device.TimerEnd(cmd, GpuTimer::OpticalFlow);
    return ok;
}

void Pipeline::RunDensify(Device& device, ID3D12GraphicsCommandList* cmd, const Settings& s, int mode) {
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_conf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchDesc d;
    d.id = ShaderId::Densify;
    d.constants.dstWidth = m_inW; d.constants.dstHeight = m_inH;
    d.constants.paramA = s.motionConfidence;
    d.constants.intB = (UINT)s.depthMode;
    d.constants.scaleX = 1.0f; d.constants.scaleY = 1.0f;
    if (mode == MotionCompute) {
        Transition(cmd, m_bmMed[m_cur], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_bc[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d.constants.flags = 2;
        d.constants.srcWidth = m_gridW[0]; d.constants.srcHeight = m_gridH[0];
        d.srv[0] = m_bmMed[m_cur].srv;
        d.srv[1] = m_bc[0].srv;
    } else if (mode == MotionNvOpticalFlow) {
        d.constants.flags = 4;
        d.constants.intA = m_nvof.Grid();
        d.constants.srcWidth = m_nvof.FlowWidth(); d.constants.srcHeight = m_nvof.FlowHeight();
        d.srv[2] = m_flow.srv;
        d.srv[3] = m_cost.Valid() ? m_cost.srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
    } else {
        d.constants.flags = 1;
    }
    d.uav[0] = m_mv.uav; d.uav[1] = m_conf.uav; d.uav[2] = m_depth.uav;
    d.groupsX = Shaders::Groups(m_inW, 8); d.groupsY = Shaders::Groups(m_inH, 8);
    m_shaders.Dispatch(cmd, device, d);
}

void Pipeline::RunStats(Device& device, ID3D12GraphicsCommandList* cmd) {
    Transition(cmd, m_bc[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_bm[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Device::UavBarrier(cmd, m_statsBuf.Get());
    DispatchDesc d;
    d.id = ShaderId::Stats;
    d.constants.srcWidth = m_gridW[2]; d.constants.srcHeight = m_gridH[2];
    d.srv[0] = m_bc[2].srv;
    d.srv[1] = m_bm[2].srv;
    d.uav[0] = m_statsUav;
    m_shaders.Dispatch(cmd, device, d);
    const UINT slot = device.FrameIndex();
    Device::Barrier(cmd, m_statsBuf.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyBufferRegion(m_statsReadback[slot].Get(), 0, m_statsBuf.Get(), 0, 16);
    Device::Barrier(cmd, m_statsBuf.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_statsPending[slot] = true;
    m_statsFence[slot] = 0;
}

void Pipeline::ReadStats(Device& device) {
    // Pick the newest completed readback.
    int best = -1; UINT64 bestFence = 0;
    for (UINT i = 0; i < Device::kFramesInFlight; ++i) {
        if (!m_statsPending[i] || !m_statsFence[i] || !device.IsFenceComplete(m_statsFence[i])) continue;
        if (m_statsFence[i] > bestFence) { bestFence = m_statsFence[i]; best = (int)i; }
    }
    if (best < 0) return;
    float v[4] = {};
    D3D12_RANGE range{ 0, 16 };
    void* p = nullptr;
    if (SUCCEEDED(m_statsReadback[best]->Map(0, &range, &p)) && p) {
        std::memcpy(v, p, sizeof(v));
        D3D12_RANGE none{ 0, 0 };
        m_statsReadback[best]->Unmap(0, &none);
        m_statAvgCost = v[0]; m_statMaxCost = v[1]; m_statAvgMotion = v[2];
    }
    for (UINT i = 0; i < Device::kFramesInFlight; ++i)
        if (m_statsPending[i] && m_statsFence[i] && m_statsFence[i] <= bestFence) m_statsPending[i] = false;
}

bool Pipeline::RunDlaa(Device& device, ID3D12GraphicsCommandList* cmd, const Settings& s, bool reset) {
    if (m_dlaaFailed || !m_ngx.Initialized() || !m_ngx.DlssAvailable()) return false;
    if (!m_dlaa.Created() || m_dlaa.Width() != m_inW || m_dlaa.Height() != m_inH || m_dlaaCreatedPreset != s.dlaaPreset) {
        if (m_dlaa.Created()) { device.WaitIdle(); m_dlaa.Release(m_ngx); }
        std::string err;
        if (!m_dlaa.Create(m_ngx, cmd, m_inW, m_inH, s.dlaaPreset, err)) {
            m_dlaaFailed = true; m_dlaaError = err;
            Log::Error("DLAA: %s", err.c_str());
            return false;
        }
        m_dlaaCreatedPreset = s.dlaaPreset;
        reset = true;
    }
    device.TimerBegin(cmd, GpuTimer::Dlaa);
    Transition(cmd, m_color8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_dlaaOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::string err;
    const bool ok = m_dlaa.Evaluate(cmd, m_color8.res.Get(), m_mv.res.Get(), m_depth.res.Get(), m_dlaaOut.res.Get(), reset,
                                    (float)m_frameIntervalMs, err);
    device.TimerEnd(cmd, GpuTimer::Dlaa);
    if (!ok) {
        m_dlaaFailed = true; m_dlaaError = err;
        Log::Error("DLAA: %s", err.c_str());
    }
    return ok;
}

bool Pipeline::RunNeural(Device& device, ID3D12GraphicsCommandList* cmd, const Settings& s, Tex& input, bool reset) {
    if (m_nrFailed || !m_ngx.Initialized()) return false;
    const bool useCore = s.nrRoute == RouteNgxCore;
    if (!useCore && !m_nr.RuntimeLoaded()) return false;
    if (!m_nr.Created() || m_nr.InputWidth() != m_inW || m_nr.InputHeight() != m_inH || m_nr.OutputWidth() != m_outW ||
        m_nr.OutputHeight() != m_outH || m_nrCreatedUseCore != useCore || m_nrCreatedPreset != s.nrPreset) {
        if (m_nr.Created()) { device.WaitIdle(); m_nr.Release(m_ngx); }
        std::string err;
        if (!m_nr.Create(m_ngx, cmd, m_inW, m_inH, m_outW, m_outH, s.nrPreset, useCore, err)) {
            m_nrFailed = true; m_nrError = err;
            Log::Error("DLSSNR: %s", err.c_str());
            return false;
        }
        m_nrCreatedUseCore = useCore;
        m_nrCreatedPreset = s.nrPreset;
        reset = true;
    }
    device.TimerBegin(cmd, GpuTimer::Neural);
    Transition(cmd, input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_nrOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DlssnrFeature::Inputs in;
    in.color = input.res.Get(); in.depth = m_depth.res.Get(); in.mvec = m_mv.res.Get(); in.output = m_nrOut.res.Get();
    in.reset = reset;
    DlssnrFeature::Params p;
    p.preset = s.nrPreset; p.style = s.nrStyle;
    p.intensity = s.nrIntensity; p.globalTone = s.nrGlobalTone; p.localTone = s.nrLocalTone;
    p.localStructure = s.nrLocalStructure; p.skinStructure = s.nrSkinStructure;
    p.autoMask = s.nrAutoMask; p.uiCorrection = s.nrUiCorrection;
    std::string err;
    const bool ok = m_nr.Evaluate(cmd, in, p, err);
    device.TimerEnd(cmd, GpuTimer::Neural);
    if (!ok) {
        m_nrFailed = true; m_nrError = err;
        Log::Error("DLSSNR: %s", err.c_str());
    }
    m_nrDirty = false;
    return ok;
}

void Pipeline::RunComposite(Device& device, ID3D12GraphicsCommandList* cmd, const Settings& s, Tex& processed, bool bypass) {
    device.TimerBegin(cmd, GpuTimer::Composite);
    Transition(cmd, m_color8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, processed, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_conf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_final, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_display, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchDesc d;
    d.id = ShaderId::Composite;
    d.constants.dstWidth = m_outW; d.constants.dstHeight = m_outH;
    UINT flags = 0;
    if (s.keepAlpha) flags |= 1;
    if (s.checkerboard) flags |= 2;
    if (bypass) flags |= 4;
    if (m_inW != m_outW || m_inH != m_outH) flags |= 32;
    d.constants.flags = flags;
    d.constants.intA = (UINT)s.compareMode;
    d.constants.paramA = s.wipePosition;
    d.constants.paramB = kMotionViewScale;
    d.srv[0] = m_color8.srv; d.srv[1] = processed.srv; d.srv[2] = m_mv.srv; d.srv[3] = m_conf.srv;
    d.uav[0] = m_final.uav; d.uav[1] = m_display.uav;
    d.groupsX = Shaders::Groups(m_outW, 8); d.groupsY = Shaders::Groups(m_outH, 8);
    m_shaders.Dispatch(cmd, device, d);
    Transition(cmd, m_display, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    device.TimerEnd(cmd, GpuTimer::Composite);
}

void Pipeline::EnqueueReadback(Device& device, ID3D12GraphicsCommandList* cmd, Tex& src, const std::wstring& path, bool keepAlpha) {
    const UINT pitch = (src.w * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    Readback* rb = nullptr;
    for (auto& r : m_readbacks) if (!r.inUse && r.w == src.w && r.h == src.h) { rb = &r; break; }
    if (!rb) {
        for (auto& r : m_readbacks) if (!r.inUse) { device.DeferRelease(r.buffer); r.buffer.Reset(); rb = &r; break; }
        if (!rb) {
            if (m_readbacks.size() >= kMaxReadbacks) { Log::Warn("Capture skipped: too many captures in flight"); return; }
            m_readbacks.emplace_back();
            rb = &m_readbacks.back();
        }
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (UINT64)pitch * src.h; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr = device.D3D12()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                   nullptr, IID_PPV_ARGS(&rb->buffer));
        if (FAILED(hr)) { Log::Hr(LogLevel::Error, "capture readback buffer", hr); rb->buffer.Reset(); rb->w = rb->h = 0; return; }
        rb->w = src.w; rb->h = src.h; rb->pitch = pitch;
    }
    Transition(cmd, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = rb->buffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = src.fmt;
    dst.PlacedFootprint.Footprint.Width = src.w;
    dst.PlacedFootprint.Footprint.Height = src.h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = pitch;
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource = src.res.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.SubresourceIndex = 0;
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &from, nullptr);
    rb->inUse = true; rb->fence = 0; rb->keepAlpha = keepAlpha; rb->path = path;
}

// --- frame --------------------------------------------------------------------------------

void Pipeline::Render(Device& device, SpoutReceiver& spout, const Settings& s, ID3D12GraphicsCommandList* cmd, bool fresh, bool sourceChanged) {
    m_status.ngxInitialized = m_ngx.Initialized();
    m_status.dlssAvailable = m_ngx.DlssAvailable();
    m_status.ngxStatus = m_ngx.Status();
    m_status.nrRuntimeLoaded = m_nr.RuntimeLoaded();
    m_status.nrRuntimeVersion = m_nr.RuntimeVersion();
    m_status.nrRuntimePath = m_nr.RuntimePath();
    m_status.sourceConnected = spout.Connected() && spout.HasFrame();
    m_status.capturesInFlight = 0;
    for (auto& r : m_readbacks) if (r.inUse) ++m_status.capturesInFlight;

    if (!spout.Connected() || !spout.Width() || !spout.Height()) {
        if (m_built) ReleaseResources(device);
        m_hasDisplay = false;
        m_status.nrActive = false; m_status.dlaaActive = false;
        m_status.srcWidth = m_status.srcHeight = 0;
        return;
    }
    const Config cfg = ComputeConfig(spout, s);
    if (!m_built || !(cfg == m_cfg) || sourceChanged) {
        if (!Rebuild(device, cfg)) { m_hasDisplay = false; return; }
        fresh = spout.HasFrame();
    }
    m_status.srcWidth = m_srcW; m_status.srcHeight = m_srcH;
    m_status.inWidth = m_inW; m_status.inHeight = m_inH;
    m_status.outWidth = m_outW; m_status.outHeight = m_outH;
    m_status.nrUpscaling = (m_inW != m_outW || m_inH != m_outH);
    m_status.nvofReady = m_nvofReady;
    m_status.nvofError = m_nvofError;
    if (!spout.HasFrame()) { m_hasDisplay = false; return; }

    Tex* processed = &m_color8;
    bool nrOk = false, dlaaOk = false;
    const bool nrWanted = s.nrEnabled;
    const bool dlaaWanted = s.dlaaEnabled;

    if (fresh || !m_hasDisplay) {
        const double now = NowSeconds();
        if (m_lastFreshTime > 0.0) m_frameIntervalMs = std::clamp((now - m_lastFreshTime) * 1000.0, 1.0, 100.0);
        m_lastFreshTime = now;

        ReadStats(device);
        bool reset = m_resetRequested || !m_haveHistory;
        bool sceneCut = false;
        if (s.autoReset && m_haveHistory && m_lastWasBlockMode && m_statAvgCost > s.cutThreshold) { sceneCut = true; reset = true; }
        m_resetRequested = false;
        if (reset) ++m_status.resets;
        m_status.sceneCut = sceneCut;

        int motionMode = s.motionMode;
        if (motionMode == MotionNvOpticalFlow && !m_nvofReady) motionMode = MotionCompute;
        const bool nvofBgra = (motionMode == MotionNvOpticalFlow) && m_nvofFmt == DXGI_FORMAT_B8G8R8A8_UNORM;

        RunConvert(device, cmd, spout, nvofBgra);

        device.TimerBegin(cmd, GpuTimer::Guidance);
        int densifyMode = MotionZero;
        if (motionMode != MotionZero) {
            RunGuidance(device, cmd, s, motionMode, m_haveHistory && !reset);
            RunStats(device, cmd);
            m_lastWasBlockMode = true;
        } else {
            m_lastWasBlockMode = false;
        }
        device.TimerEnd(cmd, GpuTimer::Guidance);
        if (m_haveHistory && !reset) {
            if (motionMode == MotionNvOpticalFlow) densifyMode = RunOpticalFlow(device, cmd) ? MotionNvOpticalFlow : MotionZero;
            else if (motionMode == MotionCompute) densifyMode = MotionCompute;
        } else if (motionMode == MotionNvOpticalFlow) {
            // Prime the optical flow reference frame without using its result.
            RunOpticalFlow(device, cmd);
        }
        device.TimerBegin(cmd, GpuTimer::Guidance);
        RunDensify(device, cmd, s, densifyMode);
        device.TimerEnd(cmd, GpuTimer::Guidance);
        m_status.motionModeActive = densifyMode;

        if (dlaaWanted) {
            dlaaOk = RunDlaa(device, cmd, s, reset);
            if (dlaaOk) processed = &m_dlaaOut;
        }
        if (nrWanted) {
            nrOk = RunNeural(device, cmd, s, *processed, reset);
            if (nrOk) processed = &m_nrOut;
        }
        m_haveHistory = true;
        m_cur = 1 - m_cur;
        ++m_status.processedFrames;
    } else {
        // No new source frame: keep the last results; re-run the neural pass only if its parameters changed.
        dlaaOk = dlaaWanted && !m_dlaaFailed && m_dlaa.Created();
        if (dlaaOk) processed = &m_dlaaOut;
        if (nrWanted && !m_nrFailed && m_nr.Created()) {
            if (m_nrDirty) nrOk = RunNeural(device, cmd, s, *processed, false);
            else nrOk = true;
            if (nrOk) processed = &m_nrOut;
        }
    }
    if (!nrWanted) { m_nrDirty = false; }
    if (!nrWanted && m_nr.Created()) { device.WaitIdle(); m_nr.Release(m_ngx); m_nrCreatedPreset = -1; }
    if (!dlaaWanted && m_dlaa.Created()) { device.WaitIdle(); m_dlaa.Release(m_ngx); m_dlaaCreatedPreset = -1; }

    RunComposite(device, cmd, s, *processed, processed == &m_color8);
    m_hasDisplay = true;

    if (m_captureRequested) {
        m_captureRequested = false;
        EnqueueReadback(device, cmd, m_final, Capture::MakeFileName(m_captureFolder, m_outW, m_outH, L""), m_captureKeepAlpha);
        if (m_captureOriginal)
            EnqueueReadback(device, cmd, m_color8, Capture::MakeFileName(m_captureFolder, m_inW, m_inH, L"_original"), m_captureKeepAlpha);
    }

    m_status.nrActive = nrOk;
    m_status.nrFailed = m_nrFailed;
    m_status.nrError = m_nrError;
    m_status.nrEvaluations = m_nr.EvaluateCount();
    m_status.dlaaActive = dlaaOk;
    m_status.dlaaFailed = m_dlaaFailed;
    m_status.dlaaError = m_dlaaError;
    m_status.statAvgCost = m_statAvgCost; m_status.statMaxCost = m_statMaxCost; m_status.statAvgMotion = m_statAvgMotion;
    m_status.frameIntervalMs = m_frameIntervalMs;
}

void Pipeline::AfterPresent(Device& device) {
    const UINT64 fence = device.LastSignaled();
    for (UINT i = 0; i < Device::kFramesInFlight; ++i)
        if (m_statsPending[i] && m_statsFence[i] == 0) m_statsFence[i] = fence;
    for (auto& r : m_readbacks)
        if (r.inUse && r.fence == 0) r.fence = fence;
}

void Pipeline::Update(Device& device, Capture& capture) {
    for (auto& r : m_readbacks) {
        if (!r.inUse || r.fence == 0 || !device.IsFenceComplete(r.fence)) continue;
        CaptureJob job;
        job.width = r.w; job.height = r.h; job.rowPitch = r.pitch; job.keepAlpha = r.keepAlpha; job.path = r.path;
        const size_t bytes = (size_t)r.pitch * r.h;
        D3D12_RANGE range{ 0, bytes };
        void* p = nullptr;
        if (SUCCEEDED(r.buffer->Map(0, &range, &p)) && p) {
            job.pixels.resize(bytes);
            std::memcpy(job.pixels.data(), p, bytes);
            D3D12_RANGE none{ 0, 0 };
            r.buffer->Unmap(0, &none);
            capture.Enqueue(std::move(job));
        } else {
            Log::Error("Capture readback map failed");
        }
        r.inUse = false; r.fence = 0;
    }
}

} // namespace vdc
