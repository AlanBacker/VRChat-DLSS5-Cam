// VRChat DLSS5 Cam - D3D12 device, two command queues (present + processing), D3D11On12 interop, swap chain, descriptors.
#pragma once
#include "core/Util.h"
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace vdc {

struct DescriptorPair {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    bool Valid() const { return cpu.ptr != 0; }
};

struct AdapterInfo {
    std::wstring name;
    UINT         vendorId = 0;
    UINT         deviceId = 0;
    SIZE_T       dedicatedVideoMemory = 0;
    std::wstring driverVersion;      // "32.0.15.6614"
    std::wstring nvidiaDriverVersion; // "566.14" (NVIDIA only)
    LUID         luid{};
    bool IsNvidia() const { return vendorId == 0x10DE; }
};

// GPU timer slots (timestamp pairs per frame). Frame/Ui are measured on the present queue, the rest on the
// processing queue.
enum class GpuTimer : UINT { Frame = 0, Convert, Guidance, OpticalFlow, Dlaa, Neural, Composite, Ui, Count };

class Device;

// One direct command queue with everything a frame loop needs: fence, command allocator ring, command list,
// shader-visible descriptor heap (a static region plus a per-frame ring), timestamp queries and deferred releases.
// The app runs two of them - the present queue on the UI thread and the processing queue on the worker thread -
// so a slow processing frame never stalls the interface.
//
// Threading: the frame-loop methods (BeginFrame/SubmitAndContinue/Execute/FinishFrame/AllocFrame/Timer*) belong to
// the owning thread. Signal/IsFenceComplete/WaitForFence/WaitOnGpu/AllocStatic/FreeStatic/DeferRelease/DeferFreeStatic
// are safe from any thread.
class GpuContext {
public:
    static constexpr UINT kFramesInFlight   = 3;
    static constexpr UINT kFrameDescriptors = 1024;

    GpuContext() = default;
    ~GpuContext() { Shutdown(); }
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    bool Init(Device& device, const char* name, D3D12_COMMAND_QUEUE_PRIORITY priority, UINT staticDescriptors,
              std::wstring& error);
    void Shutdown();

    Device&                    Dev() const { return *m_device; }
    ID3D12CommandQueue*        Queue() const { return m_queue.Get(); }
    ID3D12Fence*               Fence() const { return m_fence.Get(); }
    ID3D12DescriptorHeap*      SrvHeap() const { return m_srvHeap.Get(); }
    ID3D12GraphicsCommandList* Cmd() const { return m_cmd.Get(); }
    const char*                Name() const { return m_name.c_str(); }

    // Frame loop. BeginFrame waits for the slot used kFramesInFlight frames ago, runs the deferred releases and
    // opens the command list. Execute closes and executes the list (no signal); FinishFrame signals the fence and
    // advances the frame counter, returning the fence value that completes the frame. EndFrame does both.
    ID3D12GraphicsCommandList* BeginFrame();
    ID3D12GraphicsCommandList* SubmitAndContinue();
    void   Execute();
    UINT64 FinishFrame();
    UINT64 EndFrame() { Execute(); return FinishFrame(); }
    UINT   FrameIndex() const { return m_frameIndex; }
    UINT64 FrameNumber() const { return m_frameNumber; }

    // Descriptors in this context's shader-visible heap.
    DescriptorPair AllocStatic();                              // persistent (thread-safe)
    void           FreeStatic(const DescriptorPair& d);
    void           DeferFreeStatic(const DescriptorPair& d);   // freed once the next frame's fence passes
    DescriptorPair AllocFrame(UINT count);                     // valid for the current frame only

    // Synchronisation.
    UINT64 Signal();
    bool   IsFenceComplete(UINT64 value) const;
    void   WaitForFence(UINT64 value);
    void   WaitIdle();
    UINT64 LastSignaled() const { return m_lastSignaled.load(); }
    void   WaitOnGpu(ID3D12Fence* fence, UINT64 value);        // queue-side wait, the CPU does not block
    void   DeferRelease(ComPtr<IUnknown> obj);                 // released once the next frame's fence passes

    // GPU timestamps (per frame, read back kFramesInFlight frames later).
    void   TimerBegin(ID3D12GraphicsCommandList* cmd, GpuTimer t);
    void   TimerEnd(ID3D12GraphicsCommandList* cmd, GpuTimer t);
    double TimerMs(GpuTimer t) const { return m_timerMs[(UINT)t]; }

private:
    struct Deferred {
        ComPtr<IUnknown> obj;
        DescriptorPair   staticSlot;
    };
    struct FrameSlot {
        ComPtr<ID3D12CommandAllocator> allocator;
        UINT64                         fenceValue = 0;
        UINT                           frameDescCursor = 0;
        std::vector<Deferred>          deferred;
    };

    void ReleaseDeferred(std::vector<Deferred>& list);
    void ResolveTimers(ID3D12GraphicsCommandList* cmd);
    void ReadTimers();

    Device*                           m_device = nullptr;
    std::string                       m_name;
    ComPtr<ID3D12CommandQueue>        m_queue;
    ComPtr<ID3D12Fence>               m_fence;
    HANDLE                            m_fenceEvent = nullptr;
    UINT64                            m_fenceValue = 0;
    std::atomic<UINT64>               m_lastSignaled{0};
    mutable std::mutex                m_fenceMutex;
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    FrameSlot                         m_frames[kFramesInFlight];
    UINT                              m_frameIndex = 0;
    UINT64                            m_frameNumber = 0;
    bool                              m_cmdOpen = false;
    bool                              m_timerUsed[(UINT)GpuTimer::Count]{};

    ComPtr<ID3D12DescriptorHeap>      m_srvHeap;
    UINT                              m_staticCount = 0;
    UINT                              m_descSize = 0;
    std::vector<UINT>                 m_staticFree;
    std::mutex                        m_staticMutex;
    std::mutex                        m_deferMutex;
    std::vector<Deferred>             m_pendingDeferred;

    ComPtr<ID3D12QueryHeap>           m_queryHeap;
    ComPtr<ID3D12Resource>            m_queryReadback;
    UINT64                            m_timestampFreq = 0;
    double                            m_timerMs[(UINT)GpuTimer::Count]{};
};

class Device {
public:
    static constexpr UINT        kFramesInFlight     = GpuContext::kFramesInFlight;
    static constexpr UINT        kBackBuffers        = 3;
    static constexpr UINT        kStaticDescriptors  = 256;   // ImGui textures + preview views (present heap)
    static constexpr UINT        kStagingDescriptors = 512;   // non shader visible
    static constexpr DXGI_FORMAT kBackBufferFormat   = DXGI_FORMAT_R8G8B8A8_UNORM;

    Device() = default;
    ~Device() { Shutdown(); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    bool Init(HWND hwnd, bool debugLayer, std::wstring& error);
    void Shutdown();

    ID3D12Device*        D3D12() const { return m_device.Get(); }
    ID3D11Device*        D3D11() const { return m_device11.Get(); }
    ID3D11DeviceContext* Context11() const { return m_context11.Get(); }
    ID3D11On12Device*    On12() const { return m_on12.Get(); }
    IDXGIAdapter1*       Adapter() const { return m_adapter.Get(); }
    const AdapterInfo&   Info() const { return m_info; }
    bool                 DeviceRemoved() const { return m_deviceRemoved.load(); }
    bool                 TearingSupported() const { return m_tearing; }
    bool                 TypedUavStoreSupported(DXGI_FORMAT fmt) const;
    void                 NoteDeviceRemoved(const char* who);

    // The present queue (UI thread, high priority) and the processing queue (worker thread). The D3D11On12 device
    // used by Spout runs on the processing queue.
    GpuContext& Ui() { return m_ui; }
    GpuContext& Proc() { return m_proc; }

    // UI frame loop on the present queue -------------------------------------
    ID3D12GraphicsCommandList* BeginFrame() { return m_ui.BeginFrame(); }
    ID3D12GraphicsCommandList* SubmitAndContinue() { return m_ui.SubmitAndContinue(); }
    UINT64 EndFrame(bool vsync);            // execute, present, signal; returns the frame's fence value
    UINT   FrameIndex() const { return m_ui.FrameIndex(); }
    UINT64 FrameNumber() const { return m_ui.FrameNumber(); }
    double TimerMs(GpuTimer t) const { return m_ui.TimerMs(t); }
    void   TimerBegin(ID3D12GraphicsCommandList* cmd, GpuTimer t) { m_ui.TimerBegin(cmd, t); }
    void   TimerEnd(ID3D12GraphicsCommandList* cmd, GpuTimer t) { m_ui.TimerEnd(cmd, t); }

    // Back buffer ------------------------------------------------------------
    bool Resize(UINT width, UINT height);
    UINT BackBufferWidth() const { return m_width; }
    UINT BackBufferHeight() const { return m_height; }
    ID3D12Resource* CurrentBackBuffer() const { return m_backBuffers[m_backBufferIndex].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv() const;

    // Descriptors -------------------------------------------------------------
    // Static = the present heap's static region (ImGui textures, preview SRVs). Staging = CPU-only heap holding the
    // persistent views that Shaders::Dispatch copies into the per-frame rings. All thread-safe.
    ID3D12DescriptorHeap*       SrvHeap() const { return m_ui.SrvHeap(); }
    UINT                        SrvDescriptorSize() const { return m_srvDescSize; }
    DescriptorPair              AllocStatic() { return m_ui.AllocStatic(); }
    void                        FreeStatic(const DescriptorPair& d) { m_ui.FreeStatic(d); }
    D3D12_CPU_DESCRIPTOR_HANDLE AllocStaging();
    void                        FreeStaging(D3D12_CPU_DESCRIPTOR_HANDLE h);
    D3D12_CPU_DESCRIPTOR_HANDLE NullSrv() const { return m_nullSrv; }
    D3D12_CPU_DESCRIPTOR_HANDLE NullUav() const { return m_nullUav; }

    // Helpers ------------------------------------------------------------------
    static void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    static void UavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res);

private:
    bool CreateSwapChain();
    void ReleaseBackBuffers();
    bool CreateBackBuffers();
    void SelectAdapter(IDXGIFactory6* factory, bool debug);

    HWND                          m_hwnd = nullptr;
    ComPtr<IDXGIFactory6>         m_factory;
    ComPtr<IDXGIAdapter1>         m_adapter;
    ComPtr<ID3D12Device>          m_device;
    GpuContext                    m_ui;
    GpuContext                    m_proc;
    ComPtr<IDXGISwapChain3>       m_swapChain;
    ComPtr<ID3D11Device>          m_device11;
    ComPtr<ID3D11DeviceContext>   m_context11;
    ComPtr<ID3D11On12Device>      m_on12;
    AdapterInfo                   m_info;
    bool                          m_tearing = false;
    std::atomic<bool>             m_deviceRemoved{false};

    UINT                          m_width = 0, m_height = 0;
    ComPtr<ID3D12Resource>        m_backBuffers[kBackBuffers];
    ComPtr<ID3D12DescriptorHeap>  m_rtvHeap;
    UINT                          m_rtvDescSize = 0;
    UINT                          m_backBufferIndex = 0;

    UINT                          m_srvDescSize = 0;
    ComPtr<ID3D12DescriptorHeap>  m_stagingHeap;
    std::vector<UINT>             m_stagingFree;
    std::mutex                    m_stagingMutex;
    D3D12_CPU_DESCRIPTOR_HANDLE   m_nullSrv{};
    D3D12_CPU_DESCRIPTOR_HANDLE   m_nullUav{};
};

} // namespace vdc
