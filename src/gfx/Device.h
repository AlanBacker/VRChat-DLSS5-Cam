// VRChat DLSS5 Cam - D3D12 device, swap chain, D3D11On12 interop, descriptor heaps, fences and GPU timers.
#pragma once
#include "core/Util.h"
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_6.h>
#include <vector>
#include <mutex>
#include <string>

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

// GPU timer slots (timestamp pairs per frame).
enum class GpuTimer : UINT { Frame = 0, Convert, Guidance, OpticalFlow, Dlaa, Neural, Composite, Ui, Count };

class Device {
public:
    static constexpr UINT kFramesInFlight = 3;
    static constexpr UINT kBackBuffers = 3;
    static constexpr UINT kStaticDescriptors = 256;   // ImGui + persistent shader-visible views
    static constexpr UINT kFrameDescriptors = 1024;   // per frame ring
    static constexpr UINT kStagingDescriptors = 512;  // non shader visible
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    Device() = default;
    ~Device() { Shutdown(); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    bool Init(HWND hwnd, bool debugLayer, std::wstring& error);
    void Shutdown();

    ID3D12Device*              D3D12() const { return m_device.Get(); }
    ID3D12CommandQueue*        Queue() const { return m_queue.Get(); }
    ID3D11Device*              D3D11() const { return m_device11.Get(); }
    ID3D11DeviceContext*       Context11() const { return m_context11.Get(); }
    ID3D11On12Device*          On12() const { return m_on12.Get(); }
    IDXGIAdapter1*             Adapter() const { return m_adapter.Get(); }
    const AdapterInfo&         Info() const { return m_info; }
    bool                       DeviceRemoved() const { return m_deviceRemoved; }
    bool                       TearingSupported() const { return m_tearing; }
    bool                       TypedUavStoreSupported(DXGI_FORMAT fmt) const;

    // Frame loop ---------------------------------------------------------
    // Waits until the frame slot is free, resets its allocator, opens the command list.
    ID3D12GraphicsCommandList* BeginFrame();
    // Closes and executes whatever is recorded, then re-opens the command list on the same allocator.
    ID3D12GraphicsCommandList* SubmitAndContinue();
    // Closes, executes, presents and signals the frame fence.
    void EndFrame(bool vsync);
    UINT FrameIndex() const { return m_frameIndex; }
    UINT64 FrameNumber() const { return m_frameNumber; }

    // Back buffer -----------------------------------------------------------
    bool Resize(UINT width, UINT height);
    UINT BackBufferWidth() const { return m_width; }
    UINT BackBufferHeight() const { return m_height; }
    ID3D12Resource* CurrentBackBuffer() const { return m_backBuffers[m_backBufferIndex].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRtv() const;

    // Descriptors -----------------------------------------------------------
    ID3D12DescriptorHeap* SrvHeap() const { return m_srvHeap.Get(); }
    UINT SrvDescriptorSize() const { return m_srvDescSize; }
    DescriptorPair AllocStatic();                 // shader visible, persistent
    void FreeStatic(const DescriptorPair& d);
    DescriptorPair AllocFrame(UINT count);        // shader visible, valid for this frame only
    D3D12_CPU_DESCRIPTOR_HANDLE AllocStaging();   // non shader visible, persistent
    void FreeStaging(D3D12_CPU_DESCRIPTOR_HANDLE h);
    D3D12_CPU_DESCRIPTOR_HANDLE NullSrv() const { return m_nullSrv; }
    D3D12_CPU_DESCRIPTOR_HANDLE NullUav() const { return m_nullUav; }

    // Synchronisation -------------------------------------------------------
    UINT64 Signal();
    bool   IsFenceComplete(UINT64 value);
    void   WaitForFence(UINT64 value);
    void   WaitIdle();
    UINT64 LastSignaled() const { return m_fenceValue; }
    void   DeferRelease(ComPtr<IUnknown> obj);   // released once the current frame's fence passes

    // GPU timers -------------------------------------------------------------
    void   TimerBegin(ID3D12GraphicsCommandList* cmd, GpuTimer t);
    void   TimerEnd(ID3D12GraphicsCommandList* cmd, GpuTimer t);
    double TimerMs(GpuTimer t) const { return m_timerMs[(UINT)t]; }

    // Helpers ----------------------------------------------------------------
    static void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    static void UavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res);

private:
    bool CreateSwapChain();
    void ReleaseBackBuffers();
    bool CreateBackBuffers();
    void ResolveTimers(ID3D12GraphicsCommandList* cmd);
    void ReadTimers();
    void ProcessDeferredReleases();
    void SelectAdapter(IDXGIFactory6* factory, bool debug);

    HWND                          m_hwnd = nullptr;
    ComPtr<IDXGIFactory6>         m_factory;
    ComPtr<IDXGIAdapter1>         m_adapter;
    ComPtr<ID3D12Device>          m_device;
    ComPtr<ID3D12CommandQueue>    m_queue;
    ComPtr<IDXGISwapChain3>       m_swapChain;
    ComPtr<ID3D11Device>          m_device11;
    ComPtr<ID3D11DeviceContext>   m_context11;
    ComPtr<ID3D11On12Device>      m_on12;
    AdapterInfo                   m_info;
    bool                          m_tearing = false;
    bool                          m_deviceRemoved = false;

    UINT                          m_width = 0, m_height = 0;
    ComPtr<ID3D12Resource>        m_backBuffers[kBackBuffers];
    ComPtr<ID3D12DescriptorHeap>  m_rtvHeap;
    UINT                          m_rtvDescSize = 0;
    UINT                          m_backBufferIndex = 0;

    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        UINT64 fenceValue = 0;
        UINT   frameDescCursor = 0;
        std::vector<ComPtr<IUnknown>> deferred;
    };
    Frame                         m_frames[kFramesInFlight];
    UINT                          m_frameIndex = 0;
    UINT64                        m_frameNumber = 0;
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    bool                          m_cmdOpen = false;

    ComPtr<ID3D12Fence>           m_fence;
    UINT64                        m_fenceValue = 0;
    HANDLE                        m_fenceEvent = nullptr;
    std::mutex                    m_fenceMutex;

    ComPtr<ID3D12DescriptorHeap>  m_srvHeap;      // shader visible
    ComPtr<ID3D12DescriptorHeap>  m_stagingHeap;  // cpu only
    UINT                          m_srvDescSize = 0;
    std::vector<UINT>             m_staticFree;
    std::vector<UINT>             m_stagingFree;
    D3D12_CPU_DESCRIPTOR_HANDLE   m_nullSrv{};
    D3D12_CPU_DESCRIPTOR_HANDLE   m_nullUav{};

    ComPtr<ID3D12QueryHeap>       m_queryHeap;
    ComPtr<ID3D12Resource>        m_queryReadback;
    UINT64                        m_timestampFreq = 0;
    double                        m_timerMs[(UINT)GpuTimer::Count]{};
    bool                          m_timerUsed[(UINT)GpuTimer::Count]{};
};

} // namespace vdc
