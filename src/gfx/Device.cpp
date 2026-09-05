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

// ===========================================================================
// GpuContext

bool GpuContext::Init(Device& device, const char* name, D3D12_COMMAND_QUEUE_PRIORITY priority, UINT staticDescriptors,
                      std::wstring& error) {
    m_device = &device;
    m_name = name;
    ID3D12Device* dev = device.D3D12();
    HRESULT hr = S_OK;

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = priority;
    hr = dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue));
    if (FAILED(hr) && priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL) {
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        hr = dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue));
    }
    if (FAILED(hr)) { error = L"CreateCommandQueue failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    m_queue->SetName(Utf8ToWide(std::string("VDC ") + name + " queue").c_str());

    hr = dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) { error = L"CreateFence failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) { error = L"CreateEvent failed"; return false; }

    for (UINT i = 0; i < kFramesInFlight; ++i) {
        hr = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frames[i].allocator));
        if (FAILED(hr)) { error = L"CreateCommandAllocator failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    }
    hr = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].allocator.Get(), nullptr,
                                IID_PPV_ARGS(&m_cmd));
    if (FAILED(hr)) { error = L"CreateCommandList failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    m_cmd->Close();

    m_staticCount = staticDescriptors;
    m_descSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = m_staticCount + kFramesInFlight * kFrameDescriptors;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_srvHeap));
    if (FAILED(hr)) { error = L"CreateDescriptorHeap (srv) failed: " + Utf8ToWide(FormatHr(hr)); return false; }
    m_staticFree.clear();
    for (UINT i = m_staticCount; i > 0; --i) m_staticFree.push_back(i - 1);

    // GPU timestamp queries
    {
        const UINT count = kFramesInFlight * (UINT)GpuTimer::Count * 2;
        D3D12_QUERY_HEAP_DESC qh{};
        qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qh.Count = count;
        if (SUCCEEDED(dev->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_queryHeap)))) {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = count * sizeof(UINT64);
            bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
            bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                    nullptr, IID_PPV_ARGS(&m_queryReadback)))) {
                m_queryHeap.Reset();
            }
        }
        if (m_queryHeap && FAILED(m_queue->GetTimestampFrequency(&m_timestampFreq))) {
            m_queryHeap.Reset();
            m_queryReadback.Reset();
        }
        if (!m_queryHeap) Log::Warn("GPU timestamp queries unavailable (%s)", name);
    }
    return true;
}

void GpuContext::Shutdown() {
    if (m_queue && m_fence) WaitIdle();
    for (auto& f : m_frames) { f.deferred.clear(); f.allocator.Reset(); }
    m_pendingDeferred.clear();
    m_cmd.Reset();
    m_queryReadback.Reset();
    m_queryHeap.Reset();
    m_srvHeap.Reset();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    m_fence.Reset();
    m_queue.Reset();
    m_device = nullptr;
}

// --- descriptors -----------------------------------------------------------

DescriptorPair GpuContext::AllocStatic() {
    DescriptorPair d{};
    std::lock_guard<std::mutex> lock(m_staticMutex);
    if (m_staticFree.empty()) { Log::Error("Static descriptor heap exhausted (%s)", m_name.c_str()); return d; }
    const UINT idx = m_staticFree.back();
    m_staticFree.pop_back();
    d.cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    d.cpu.ptr += (SIZE_T)idx * m_descSize;
    d.gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    d.gpu.ptr += (UINT64)idx * m_descSize;
    return d;
}

void GpuContext::FreeStatic(const DescriptorPair& d) {
    if (!d.Valid() || !m_srvHeap) return;
    std::lock_guard<std::mutex> lock(m_staticMutex);
    const SIZE_T base = m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    const UINT idx = (UINT)((d.cpu.ptr - base) / m_descSize);
    if (idx < m_staticCount) m_staticFree.push_back(idx);
}

void GpuContext::DeferFreeStatic(const DescriptorPair& d) {
    if (!d.Valid()) return;
    std::lock_guard<std::mutex> lock(m_deferMutex);
    Deferred e;
    e.staticSlot = d;
    m_pendingDeferred.push_back(std::move(e));
}

DescriptorPair GpuContext::AllocFrame(UINT count) {
    DescriptorPair d{};
    FrameSlot& f = m_frames[m_frameIndex];
    if (f.frameDescCursor + count > kFrameDescriptors) {
        Log::Error("Per-frame descriptor ring exhausted (%s)", m_name.c_str());
        return d;
    }
    const UINT idx = m_staticCount + m_frameIndex * kFrameDescriptors + f.frameDescCursor;
    f.frameDescCursor += count;
    d.cpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    d.cpu.ptr += (SIZE_T)idx * m_descSize;
    d.gpu = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    d.gpu.ptr += (UINT64)idx * m_descSize;
    return d;
}

// --- synchronisation -----------------------------------------------------------

UINT64 GpuContext::Signal() {
    std::lock_guard<std::mutex> lock(m_fenceMutex);
    const UINT64 v = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), v);
    m_lastSignaled.store(v);
    return v;
}

bool GpuContext::IsFenceComplete(UINT64 value) const {
    return value == 0 || !m_fence || m_fence->GetCompletedValue() >= value;
}

void GpuContext::WaitForFence(UINT64 value) {
    if (value == 0 || !m_fence || m_fence->GetCompletedValue() >= value) return;
    std::lock_guard<std::mutex> lock(m_fenceMutex);
    if (m_fence->GetCompletedValue() >= value) return;
    if (FAILED(m_fence->SetEventOnCompletion(value, m_fenceEvent))) return;
    // Wait until the work has really finished. Returning after a timeout would let the caller reset a command
    // allocator or free a resource the GPU is still reading, turning a stall into memory corruption and a device
    // hang. A removed device completes its fences and is detected below, so a lost GPU cannot hang here.
    for (UINT waited = 1;; ++waited) {
        if (WaitForSingleObject(m_fenceEvent, 1000) == WAIT_OBJECT_0) return;
        if (m_fence->GetCompletedValue() >= value) return;
        ID3D12Device* dev = m_device ? m_device->D3D12() : nullptr;
        if (dev && dev->GetDeviceRemovedReason() != S_OK) { m_device->NoteDeviceRemoved(m_name.c_str()); return; }
        if (waited == 5 || waited % 30 == 0)
            Log::Warn("%s queue: GPU still busy after %u s (fence %llu)", m_name.c_str(), waited, (unsigned long long)value);
    }
}

void GpuContext::WaitIdle() {
    if (!m_queue || !m_fence) return;
    WaitForFence(Signal());
    for (auto& f : m_frames) ReleaseDeferred(f.deferred);
    // Objects deferred since the last BeginFrame may still be referenced by the command list being recorded right
    // now (an upload buffer whose copy is recorded but not yet submitted, textures a rebuild replaces mid-frame).
    // While the list is open they must stay alive and ride with the next frame's fence; only a closed list makes
    // them safe to free here.
    if (m_cmdOpen) return;
    std::vector<Deferred> pending;
    {
        std::lock_guard<std::mutex> lock(m_deferMutex);
        pending.swap(m_pendingDeferred);
    }
    ReleaseDeferred(pending);
}

void GpuContext::WaitOnGpu(ID3D12Fence* fence, UINT64 value) {
    if (fence && value) m_queue->Wait(fence, value);
}

void GpuContext::DeferRelease(ComPtr<IUnknown> obj) {
    if (!obj) return;
    std::lock_guard<std::mutex> lock(m_deferMutex);
    Deferred e;
    e.obj = std::move(obj);
    m_pendingDeferred.push_back(std::move(e));
}

void GpuContext::ReleaseDeferred(std::vector<Deferred>& list) {
    for (auto& e : list) {
        e.obj.Reset();
        if (e.staticSlot.Valid()) FreeStatic(e.staticSlot);
    }
    list.clear();
}

// --- frame loop ------------------------------------------------------------------

ID3D12GraphicsCommandList* GpuContext::BeginFrame() {
    ID3D12Device* dev = m_device->D3D12();
    if (dev && dev->GetDeviceRemovedReason() != S_OK) m_device->NoteDeviceRemoved(m_name.c_str());
    m_frameIndex = (UINT)(m_frameNumber % kFramesInFlight);
    FrameSlot& f = m_frames[m_frameIndex];
    WaitForFence(f.fenceValue);
    ReleaseDeferred(f.deferred);
    // Objects retired since the previous frame (from any thread) ride with this frame: by the time its fence
    // passes every earlier frame that could still reference them is complete too.
    {
        std::lock_guard<std::mutex> lock(m_deferMutex);
        f.deferred.swap(m_pendingDeferred);
        m_pendingDeferred.clear();
    }
    ReadTimers();
    f.frameDescCursor = 0;
    for (auto& u : f.timerUsed) u = false;
    f.allocator->Reset();
    m_cmd->Reset(f.allocator.Get(), nullptr);
    m_cmdOpen = true;
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmd->SetDescriptorHeaps(1, heaps);
    TimerBegin(m_cmd.Get(), GpuTimer::Frame);
    return m_cmd.Get();
}

ID3D12GraphicsCommandList* GpuContext::SubmitAndContinue() {
    if (!m_cmdOpen) return m_cmd.Get();
    m_cmd->Close();
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    m_cmd->Reset(m_frames[m_frameIndex].allocator.Get(), nullptr);
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmd->SetDescriptorHeaps(1, heaps);
    return m_cmd.Get();
}

void GpuContext::Execute() {
    if (!m_cmdOpen) return;
    TimerEnd(m_cmd.Get(), GpuTimer::Frame);
    ResolveTimers(m_cmd.Get());
    m_cmd->Close();
    m_cmdOpen = false;
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
}

UINT64 GpuContext::FinishFrame() {
    FrameSlot& f = m_frames[m_frameIndex];
    f.fenceValue = Signal();
    ++m_frameNumber;
    return f.fenceValue;
}

// --- GPU timers ----------------------------------------------------------------

void GpuContext::TimerBegin(ID3D12GraphicsCommandList* cmd, GpuTimer t) {
    if (!m_queryHeap) return;
    const UINT idx = (m_frameIndex * (UINT)GpuTimer::Count + (UINT)t) * 2;
    cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx);
    m_frames[m_frameIndex].timerUsed[(UINT)t] = true;
}

void GpuContext::TimerEnd(ID3D12GraphicsCommandList* cmd, GpuTimer t) {
    if (!m_queryHeap) return;
    const UINT idx = (m_frameIndex * (UINT)GpuTimer::Count + (UINT)t) * 2 + 1;
    cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, idx);
}

void GpuContext::ResolveTimers(ID3D12GraphicsCommandList* cmd) {
    if (!m_queryHeap) return;
    const UINT perFrame = (UINT)GpuTimer::Count * 2;
    const UINT first = m_frameIndex * perFrame;
    // Give unused timers a zero-length interval so the resolve reads defined data.
    for (UINT t = 0; t < (UINT)GpuTimer::Count; ++t) {
        if (!m_frames[m_frameIndex].timerUsed[t]) {
            cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + t * 2);
            cmd->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first + t * 2 + 1);
        }
    }
    cmd->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, perFrame,
                          m_queryReadback.Get(), (UINT64)first * sizeof(UINT64));
}

void GpuContext::ReadTimers() {
    if (!m_queryHeap || m_frameNumber < kFramesInFlight || !m_timestampFreq) return;
    const UINT perFrame = (UINT)GpuTimer::Count * 2;
    const UINT first = m_frameIndex * perFrame;
    D3D12_RANGE range{ (SIZE_T)first * sizeof(UINT64), (SIZE_T)(first + perFrame) * sizeof(UINT64) };
    void* data = nullptr;
    if (FAILED(m_queryReadback->Map(0, &range, &data)) || !data) return;
    const UINT64* ts = reinterpret_cast<const UINT64*>(data) + first;
    // The slot still carries the flags of the frame that produced these timestamps (reset after this read). A timer
    // that frame never recorded reports zero: its placeholder pair can straddle a queue switch and read as real time.
    const bool* used = m_frames[m_frameIndex].timerUsed;
    for (UINT t = 0; t < (UINT)GpuTimer::Count; ++t) {
        const UINT64 b = ts[t * 2], e = ts[t * 2 + 1];
        m_timerMs[t] = (used[t] && e > b) ? (double)(e - b) * 1000.0 / (double)m_timestampFreq : 0.0;
    }
    D3D12_RANGE none{ 0, 0 };
    m_queryReadback->Unmap(0, &none);
}

// ===========================================================================
// Device

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

void Device::NoteDeviceRemoved(const char* who) {
    if (m_deviceRemoved.exchange(true)) return;
    Log::Hr(LogLevel::Error, StrPrintf("Device removed (%s)", who).c_str(), m_device ? m_device->GetDeviceRemovedReason() : 0);
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

    m_srvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Two queues: the interface presents from a high-priority queue so a long processing frame (optical flow, the
    // neural network, a large still image) never holds back window updates.
    if (!m_ui.Init(*this, "present", D3D12_COMMAND_QUEUE_PRIORITY_HIGH, kStaticDescriptors, error)) return false;
    if (!m_proc.Init(*this, "processing", D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, 0, error)) return false;

    // Staging heap, null views
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kStagingDescriptors;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = m_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_stagingHeap));
        if (FAILED(hr)) { error = L"CreateDescriptorHeap (staging) failed: " + Utf8ToWide(FormatHr(hr)); return false; }

        D3D12_DESCRIPTOR_HEAP_DESC rd{};
        rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rd.NumDescriptors = kBackBuffers;
        hr = m_device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&m_rtvHeap));
        if (FAILED(hr)) { error = L"CreateDescriptorHeap (rtv) failed: " + Utf8ToWide(FormatHr(hr)); return false; }
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

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

    // D3D11On12 (Spout lives in the D3D11 world). It shares the processing queue, so a received frame is ordered
    // with the compute passes that read it.
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (debugLayer) flags |= D3D11_CREATE_DEVICE_DEBUG;
        IUnknown* queues[] = { m_proc.Queue() };
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
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_ui.Queue(), m_hwnd, &sd, nullptr, nullptr, &sc1);
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
    m_ui.WaitIdle();
    ReleaseBackBuffers();
    HRESULT hr = m_swapChain->ResizeBuffers(kBackBuffers, width, height, kBackBufferFormat,
                                            m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {
        Log::Hr(LogLevel::Error, "ResizeBuffers", hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) NoteDeviceRemoved("ResizeBuffers");
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

UINT64 Device::EndFrame(bool vsync) {
    m_ui.Execute();
    const UINT flags = (!vsync && m_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = m_swapChain->Present(vsync ? 1 : 0, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        NoteDeviceRemoved("Present");
    } else if (FAILED(hr)) {
        Log::Hr(LogLevel::Warn, "Present", hr);
    }
    const UINT64 fence = m_ui.FinishFrame();
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    return fence;
}

// ---------------------------------------------------------------------------
// Descriptors

D3D12_CPU_DESCRIPTOR_HANDLE Device::AllocStaging() {
    D3D12_CPU_DESCRIPTOR_HANDLE h{};
    std::lock_guard<std::mutex> lock(m_stagingMutex);
    if (m_stagingFree.empty()) { Log::Error("Staging descriptor heap exhausted"); return h; }
    const UINT idx = m_stagingFree.back();
    m_stagingFree.pop_back();
    h = m_stagingHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)idx * m_srvDescSize;
    return h;
}

void Device::FreeStaging(D3D12_CPU_DESCRIPTOR_HANDLE h) {
    if (!h.ptr || !m_stagingHeap) return;
    std::lock_guard<std::mutex> lock(m_stagingMutex);
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

void Device::Shutdown() {
    m_ui.WaitIdle();
    m_proc.WaitIdle();
    if (m_context11) { m_context11->ClearState(); m_context11->Flush(); }
    m_on12.Reset();
    m_context11.Reset();
    m_device11.Reset();
    ReleaseBackBuffers();
    m_swapChain.Reset();
    m_rtvHeap.Reset();
    m_stagingHeap.Reset();
    m_ui.Shutdown();
    m_proc.Shutdown();
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();
}


// GeForce names carry the generation as the first digit of the model number ("GeForce RTX 4070 Ti"); workstation
// parts name the architecture ("RTX 6000 Ada Generation", "RTX PRO 6000 Blackwell", "RTX A6000").
int AdapterInfo::RtxGeneration() const {
    if (!IsNvidia()) return 0;
    if (name.find(L"Blackwell") != std::wstring::npos) return 5;
    if (name.find(L"Ada") != std::wstring::npos) return 4;
    if (name.find(L"RTX A") != std::wstring::npos) return 3;
    const size_t p = name.find(L"RTX ");
    if (p == std::wstring::npos || p + 8 > name.size()) return 0;
    const wchar_t* d = name.c_str() + p + 4;
    for (int i = 0; i < 4; ++i) if (d[i] < L'0' || d[i] > L'9') return 0;
    const int gen = d[0] - L'0';
    return (gen >= 2 && gen <= 5) ? gen : 0;
}

} // namespace vdc
