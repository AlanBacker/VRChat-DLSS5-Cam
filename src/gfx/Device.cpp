#include "gfx/Device.h"
#include "core/Log.h"
#include <d3d11.h>
#include <algorithm>

namespace vdc {

namespace {

const char* VendorName(UINT id) {
    switch (id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        default:     return "Unknown";
    }
}

} // namespace

void Device::Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                     D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    cmd->ResourceBarrier(1, &b);
}

void Device::UavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    cmd->ResourceBarrier(1, &b);
}

void Device::SelectAdapter(IDXGIFactory6* factory, bool /*debug*/) {
    ComPtr<IDXGIAdapter1> best;
    ComPtr<IDXGIAdapter1> firstNvidia;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                       IID_PPV_ARGS(&adapter))))
            break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
            continue;
        Log::Info("Adapter %u: %s (%s) %llu MB", i, WideToUtf8(desc.Description).c_str(),
                  VendorName(desc.VendorId), (unsigned long long)(desc.DedicatedVideoMemory >> 20));
        if (!best) best = adapter;
        if (!firstNvidia && desc.VendorId == 0x10DE) firstNvidia = adapter;
    }
    m_adapter = firstNvidia ? firstNvidia : best;
}

bool Device::Init(HWND hwnd, bool debugLayer, std::wstring& error) {
    m_hwnd = hwnd;
    HRESULT hr = S_OK;

    if (debugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            Log::Info("D3D12 debug layer enabled");
        }
    }

    hr = CreateDXGIFactory2(debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) { error = L"CreateDXGIFactory2 failed: " + Utf8ToWide(FormatHr(hr)); return false; }

    SelectAdapter(m_factory.Get(), debugLayer);
    if (!m_adapter) { error = L"No Direct3D 12 capable GPU was found."; return false; }

    DXGI_ADAPTER_DESC1 desc{};
    m_adapter->GetDesc1(&desc);
    m_info.name = desc.Description;
    m_info.vendorId = desc.VendorId;
    m_info.deviceId = desc.DeviceId;
    m_info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
    m_info.luid = desc.AdapterLuid;
    LARGE_INTEGER umd{};
    if (SUCCEEDED(m_adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        const WORD a = HIWORD(umd.HighPart), b = LOWORD(umd.HighPart), c = HIWORD(umd.LowPart), d = LOWORD(umd.LowPart);
        m_info.driverVersion = Utf8ToWide(StrPrintf("%u.%u.%u.%u", a, b, c, d));
        if (m_info.IsNvidia()) {
            const unsigned nv = ((c % 10u) * 10000u + d);
            m_info.nvidiaDriverVersion = Utf8ToWide(StrPrintf("%u.%02u", nv / 100u, nv % 100u));
        }
    }
    Log::Info("Using adapter: %s (%s), driver %s %s", WideToUtf8(m_info.name).c_str(), VendorName(m_info.vendorId),
              WideToUtf8(m_info.driverVersion).c_str(), WideToUtf8(m_info.nvidiaDriverVersion).c_str());

    hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) { error = L"D3D12CreateDevice failed: " + Utf8ToWide(FormatHr(hr)); return false; }

    if (debugLayer) {
        ComPtr<ID3D12InfoQueue> iq;
        if (SUCCEEDED(m_device.As(&iq))) {
            iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        }
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue));
    if (FAILED(hr)) { error = L"CreateCommandQueue failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    m_queue->SetName(L"VDC main queue");

    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) { error = L"CreateFence failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) { error = L"CreateEvent failed"; return false; }

    for (UINT i = 0; i < kFramesInFlight; ++i) {
        hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frames[i].allocator));
        if (FAILED(hr)) { error = L"CreateCommandAllocator failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    }
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].allocator.Get(), nullptr,
                                     IID_PPV_ARGS(&m_cmd));
    if (FAILED(hr)) { error = L"CreateCommandList failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    m_cmd->Close();

    // Descriptor heaps
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kStaticDescriptors + kFramesInFlight * kFrameDescriptors;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_srvHeap));
        if (FAILED(hr)) { error = L"CreateDescriptorHeap (srv) failed: " + Utf8ToWide(FormatHr(hr)); return false; }
        hd.NumDescriptors = kStagingDescriptors;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_stagingHeap));
        if (FAILED(hr)) { error = L"CreateDescriptorHeap (staging) failed: " + Utf8ToWide(FormatHr(hr)); return false; }
        m_srvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rd{};
        rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rd.NumDescriptors = kBackBuffers;
        hr = m_device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&m_rtvHeap));
        if (FAILED(hr)) { error = L"CreateDescriptorHeap (rtv) failed: " + Utf8ToWide(FormatHr(hr)); return false; }
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        m_staticFree.clear();
        for (UINT i = kStaticDescriptors; i > 0; --i) m_staticFree.push_back(i - 1);
        m_stagingFree.clear();
        for (UINT i = kStagingDescriptors; i > 0; --i) m_stagingFree.push_back(i - 1);

        // Null views used to fill unused table slots.
        m_nullSrv = AllocStaging();
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(nullptr, &sd, m_nullSrv);
        m_nullUav = AllocStaging();
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->CreateUnorderedAccessView(nullptr, nullptr, &ud, m_nullUav);
    }

    // GPU timestamp queries
    {
        const UINT count = kFramesInFlight * (UINT)GpuTimer::Count * 2;
        D3D12_QUERY_HEAP_DESC qh{};
        qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qh.Count = count;
        if (SUCCEEDED(m_device->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_queryHeap)))) {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = count * sizeof(UINT64);
            bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
            bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(m_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&m_queryReadback)))) {
                m_queryHeap.Reset();
            }
        }
        if (m_queryHeap && FAILED(m_queue->GetTimestampFrequency(&m_timestampFreq))) {
            m_queryHeap.Reset();
            m_queryReadback.Reset();
        }
        if (!m_queryHeap) Log::Warn("GPU timestamp queries unavailable");
    }

    // D3D11On12 (Spout and NVIDIA Optical Flow live in the D3D11 world)
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (debugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
        IUnknown* queues[] = { m_queue.Get() };
        hr = D3D11On12CreateDevice(m_device.Get(), flags, nullptr, 0, queues, 1, 0,
                                   &m_device11, &m_context11, nullptr);
        if (FAILED(hr) && debugLayer) {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11On12CreateDevice(m_device.Get(), flags, nullptr, 0, queues, 1, 0,
                                       &m_device11, &m_context11, nullptr);
        }
        if (FAILED(hr)) { error = L"D3D11On12CreateDevice failed: " + Utf8ToWide(FormatHr(hr)); return false; }
        hr = m_device11.As(&m_on12);
        if (FAILED(hr)) { error = L"ID3D11On12Device unavailable: " + Utf8ToWide(FormatHr(hr)); return false; }
    }

    BOOL allowTearing = FALSE;
    if (SUCCEEDED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
        m_tearing = allowTearing == TRUE;

    if (!CreateSwapChain()) { error = L"Swap chain creation failed."; return false; }
    return true;
}

bool Device::CreateSwapChain() {
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    m_width = std::max<UINT>(1, (UINT)(rc.right - rc.left));
    m_height = std::max<UINT>(1, (UINT)(rc.bottom - rc.top));

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = m_width;
    sd.Height = m_height;
    sd.Format = kBackBufferFormat;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBackBuffers;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_queue.Get(), m_hwnd, &sd, nullptr, nullptr, &sc1);
    if (FAILED(hr)) { Log::Hr(LogLevel::Error, "CreateSwapChainForHwnd", hr); return false; }
    m_factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    hr = sc1.As(&m_swapChain);
    if (FAILED(hr)) { Log::Hr(LogLevel::Error, "IDXGISwapChain3", hr); return false; }
    return CreateBackBuffers();
}

bool Device::CreateBackBuffers() {
    for (UINT i = 0; i < kBackBuffers; ++i) {
        HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        if (FAILED(hr)) { Log::Hr(LogLevel::Error, "SwapChain GetBuffer", hr); return false; }
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)i * m_rtvDescSize;
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, h);
    }
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}

void Device::ReleaseBackBuffers() {
    for (auto& b : m_backBuffers) b.Reset();
}

bool Device::Resize(UINT width, UINT height) {
    width = std::max<UINT>(1, width);
    height = std::max<UINT>(1, height);
    if (!m_swapChain || (width == m_width && height == m_height)) return true;
    WaitIdle();
    ReleaseBackBuffers();
    HRESULT hr = m_swapChain->ResizeBuffers(kBackBuffers, width, height, kBackBufferFormat,
                                            m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {
        Log::Hr(LogLevel::Error, "ResizeBuffers", hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) m_deviceRemoved = true;
        return false;
    }
    m_width = width;
    m_height = height;
    return CreateBackBuffers();
}

D3D12_CPU_DESCRIPTOR_HANDLE Device::CurrentRtv() const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)m_backBufferIndex * m_rtvDescSize;
    return h;
}

// ---------------------------------------------------------------------------
// Descriptors

DescriptorPair Device::AllocStatic() {
    DescriptorPair d{};
    if (m_staticFree.empty()) { Log::Error("Static descriptor heap exhausted"); return d; }
    const UINT idx = m_staticFree.back();
    m_staticFree.pop_back();
    d.cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    d.cpu.ptr += (SIZE_T)idx * m_srvDescSize;
    d.gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    d.gpu.ptr += (UINT64)idx * m_srvDescSize;
    return d;
}

void Device::FreeStatic(const DescriptorPair& d) {
    if (!d.Valid()) return;
    const SIZE_T base = m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    const UINT idx = (UINT)((d.cpu.ptr - base) / m_srvDescSize);
    if (idx < kStaticDescriptors) m_staticFree.push_back(idx);
}

DescriptorPair Device::AllocFrame(UINT count) {
    DescriptorPair d{};
    Frame& f = m_frames[m_frameIndex];
    if (f.frameDescCursor + count > kFrameDescriptors) {
        Log::Error("Per-frame descriptor ring exhausted");
        return d;
    }
    const UINT idx = kStaticDescriptors + m_frameIndex * kFrameDescriptors + f.frameDescCursor;
    f.frameDescCursor += count;
    d.cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    d.cpu.ptr += (SIZE_T)idx * m_srvDescSize;
    d.gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    d.gpu.ptr += (UINT64)idx * m_srvDescSize;
    return d;
}

D3D12_CPU_DESCRIPTOR_HANDLE Device::AllocStaging() {
    D3D12_CPU_DESCRIPTOR_HANDLE h{};
    if (m_stagingFree.empty()) { Log::Error("Staging descriptor heap exhausted"); return h; }
    const UINT idx = m_stagingFree.back();
    m_stagingFree.pop_back();
    h = m_stagingHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)idx * m_srvDescSize;
    return h;
}

void Device::FreeStaging(D3D12_CPU_DESCRIPTOR_HANDLE h) {
    if (!h.ptr) return;
    const SIZE_T base = m_stagingHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    const UINT idx = (UINT)((h.ptr - base) / m_srvDescSize);
    if (idx < kStagingDescriptors) m_stagingFree.push_back(idx);
}

bool Device::TypedUavStoreSupported(DXGI_FORMAT fmt) const {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{};
    fs.Format = fmt;
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs)))) return false;
    return (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
}

// ---------------------------------------------------------------------------
// Synchronisation

UINT64 Device::Signal() {
    std::lock_guard<std::mutex> lock(m_fenceMutex);
    const UINT64 v = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), v);
    return v;
}

bool Device::IsFenceComplete(UINT64 value) {
    return m_fence->GetCompletedValue() >= value;
}

void Device::WaitForFence(UINT64 value) {
    if (value == 0 || m_fence->GetCompletedValue() >= value) return;
    std::lock_guard<std::mutex> lock(m_fenceMutex);
    if (m_fence->GetCompletedValue() >= value) return;
    if (SUCCEEDED(m_fence->SetEventOnCompletion(value, m_fenceEvent)))
        WaitForSingleObject(m_fenceEvent, 5000);
}

void Device::WaitIdle() {
    if (!m_queue || !m_fence) return;
    WaitForFence(Signal());
    for (auto& f : m_frames) { f.deferred.clear(); }
}

void Device::DeferRelease(ComPtr<IUnknown> obj) {
    if (obj) m_frames[m_frameIndex].deferred.push_back(std::move(obj));
}

void Device::ProcessDeferredReleases() {
    m_frames[m_frameIndex].deferred.clear();
}

// ---------------------------------------------------------------------------
// Frame loop

ID3D12GraphicsCommandList* Device::BeginFrame() {
    if (m_device && m_device->GetDeviceRemovedReason() != S_OK && !m_deviceRemoved) {
        Log::Hr(LogLevel::Error, "Device removed", m_device->GetDeviceRemovedReason());
        m_deviceRemoved = true;
    }
    m_frameIndex = (UINT)(m_frameNumber % kFramesInFlight);
    Frame& f = m_frames[m_frameIndex];
    WaitForFence(f.fenceValue);
    ProcessDeferredReleases();
    ReadTimers();
    f.frameDescCursor = 0;
    for (auto& u : m_timerUsed) u = false;
    f.allocator->Reset();
    m_cmd->Reset(f.allocator.Get(), nullptr);
    m_cmdOpen = true;
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmd->SetDescriptorHeaps(1, heaps);
    TimerBegin(m_cmd.Get(), GpuTimer::Frame);
    return m_cmd.Get();
}

ID3D12GraphicsCommandList* Device::SubmitAndContinue() {
    if (!m_cmdOpen) return m_cmd.Get();
    m_cmd->Close();
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    m_cmd->Reset(m_frames[m_frameIndex].allocator.Get(), nullptr);
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmd->SetDescriptorHeaps(1, heaps);
    return m_cmd.Get();
}

void Device::EndFrame(bool vsync) {
    Frame& f = m_frames[m_frameIndex];
    TimerEnd(m_cmd.Get(), GpuTimer::Frame);
    ResolveTimers(m_cmd.Get());
    m_cmd->Close();
    m_cmdOpen = false;
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    const UINT flags = (!vsync && m_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = m_swapChain->Present(vsync ? 1 : 0, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Log::Hr(LogLevel::Error, "Present (device lost)", m_device->GetDeviceRemovedReason());
        m_deviceRemoved = true;
    } else if (FAILED(hr)) {
        Log::Hr(LogLevel::Warn, "Present", hr);
    }
    f.fenceValue = Signal();
    ++m_frameNumber;
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// ---------------------------------------------------------------------------
// GPU timers

void Device::TimerBegin(ID3D12GraphicsCommandList* cmd, GpuTimer t) {
    if (!m_queryHeap) return;
    const UINT idx = (m_frameIndex * (UINT)GpuTimer::Count + (UINT)t) * 2;
    cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx);
    m_timerUsed[(UINT)t] = true;
}

void Device::TimerEnd(ID3D12GraphicsCommandList* cmd, GpuTimer t) {
    if (!m_queryHeap) return;
    const UINT idx = (m_frameIndex * (UINT)GpuTimer::Count + (UINT)t) * 2 + 1;
    cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx);
}

void Device::ResolveTimers(ID3D12GraphicsCommandList* cmd) {
    if (!m_queryHeap) return;
    const UINT perFrame = (UINT)GpuTimer::Count * 2;
    const UINT first = m_frameIndex * perFrame;
    // Give unused timers a zero-length interval so the resolve reads defined data.
    for (UINT t = 0; t < (UINT)GpuTimer::Count; ++t) {
        if (!m_timerUsed[t]) {
            cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + t * 2);
            cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + t * 2 + 1);
        }
    }
    cmd->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, perFrame,
                          m_queryReadback.Get(), (UINT64)first * sizeof(UINT64));
}

void Device::ReadTimers() {
    if (!m_queryHeap || m_frameNumber < kFramesInFlight || !m_timestampFreq) return;
    const UINT perFrame = (UINT)GpuTimer::Count * 2;
    const UINT first = m_frameIndex * perFrame;
    D3D12_RANGE range{ (SIZE_T)first * sizeof(UINT64), (SIZE_T)(first + perFrame) * sizeof(UINT64) };
    void* data = nullptr;
    if (FAILED(m_queryReadback->Map(0, &range, &data)) || !data) return;
    const UINT64* ts = reinterpret_cast<const UINT64*>(data) + first;
    for (UINT t = 0; t < (UINT)GpuTimer::Count; ++t) {
        const UINT64 b = ts[t * 2], e = ts[t * 2 + 1];
        m_timerMs[t] = (e > b) ? (double)(e - b) * 1000.0 / (double)m_timestampFreq : 0.0;
    }
    D3D12_RANGE none{ 0, 0 };
    m_queryReadback->Unmap(0, &none);
}

// ---------------------------------------------------------------------------

void Device::Shutdown() {
    if (m_queue && m_fence) WaitIdle();
    for (auto& f : m_frames) { f.deferred.clear(); f.allocator.Reset(); }
    m_cmd.Reset();
    m_queryReadback.Reset();
    m_queryHeap.Reset();
    if (m_context11) { m_context11->ClearState(); m_context11->Flush(); }
    m_on12.Reset();
    m_context11.Reset();
    m_device11.Reset();
    ReleaseBackBuffers();
    m_swapChain.Reset();
    m_rtvHeap.Reset();
    m_stagingHeap.Reset();
    m_srvHeap.Reset();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    m_fence.Reset();
    m_queue.Reset();
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();
}

} // namespace vdc
