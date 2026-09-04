#include "core/SpoutReceiver.h"
#include "core/Log.h"
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

} // namespace

bool SpoutReceiver::Init(Device& device) {
    // Route Spout's D3D11 work through the D3D11On12 device so that the shared queue orders everything.
    if (!m_spout.OpenDirectX11(device.D3D11())) {
        Log::Error("Spout: OpenDirectX11 failed");
        return false;
    }
    m_open = true;
    m_spout.SetReceiverName(nullptr);
    Log::Info("Spout receiver ready (Spout %s)", "2.007");
    return true;
}

void SpoutReceiver::Shutdown(Device& device) {
    if (!m_open) return;
    m_spout.ReleaseReceiver();
    ReleaseTexture(device);
    m_spout.CloseDirectX11();
    m_open = false;
}

void SpoutReceiver::SetRequestedSender(const std::string& name) {
    m_requested = name;
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

bool SpoutReceiver::CreateTexture(Device& device, UINT w, UINT h, DXGI_FORMAT fmt) {
    ReleaseTexture(device);
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

void SpoutReceiver::ReleaseTexture(Device& device) {
    if (m_tex11) device.DeferRelease(m_tex11);
    if (m_tex12) device.DeferRelease(m_tex12);
    m_tex11.Reset();
    m_tex12.Reset();
    if (m_srv.ptr) { device.FreeStaging(m_srv); m_srv = {}; }
    m_width = m_height = 0;
}

bool SpoutReceiver::Receive(Device& device, bool& changed, bool& fresh) {
    changed = false;
    fresh = false;
    if (!m_open) return false;

    const double now = NowSeconds();

    // Apply a changed user request.
    if (!m_requested.empty()) {
        if (m_requested != m_applied) {
            ApplySenderName(m_requested.c_str());
            ReleaseTexture(device);
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
            ReleaseTexture(device);
            changed = true;
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
            ReleaseTexture(device);
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
        if (w == 0 || h == 0 || !CreateTexture(device, w, h, fmt)) {
            m_connected = false;
            return false;
        }
        m_connected = true;
        m_freshFrames = 0;
        changed = true;
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
    m_senderFps = m_spout.GetSenderFps();
    // Make the D3D11 copy visible to the D3D12 queue in order.
    device.Context11()->Flush();
    return m_freshFrames > 0;
}

} // namespace vdc
