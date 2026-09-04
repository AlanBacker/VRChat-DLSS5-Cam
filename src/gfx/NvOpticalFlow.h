// VRChat DLSS5 Cam - NVIDIA Optical Flow (nvofapi64.dll, D3D11 interface) provider.
// Driven through the NVIDIA Optical Flow SDK 5.0 interface headers (third_party/nvof, MIT); with a 5.0 driver the forward and
// backward flow come out of a single pass, older drivers get a second pass for the backward flow.
//
// The NVOF D3D11 entry point only accepts a native NVIDIA D3D11 device (it rejects the D3D11On12 mapping layer with
// UNSUPPORTED_DEVICE), so this provider owns a private D3D11 device on the same adapter as the D3D12 device. Frames cross
// D3D12 -> D3D11 and results cross D3D11 -> D3D12 through NT-handle shared textures created on the D3D11 side, and the two
// devices are ordered with a shared fence.
#pragma once
#include "gfx/Device.h"
#include <d3d11_4.h>
#include <string>

namespace vdc {

class NvOpticalFlow {
public:
    ~NvOpticalFlow() { Shutdown(); }

    static bool LibraryAvailable();
    static UINT ApiVersion();   // 0x50 / 0x20 once the library is loaded, 0 otherwise

    // Inputs are R8_UNORM (luma) or B8G8R8A8_UNORM textures (width x height). grid = 1/2/4 output grid size, perfLevel 5 (slow) / 10 / 20 (fast).
    // bidirectional = also compute the backward flow (previous -> current) so that a forward/backward consistency check can be applied.
    bool Init(GpuContext& gpu, UINT width, UINT height, DXGI_FORMAT inputFormat, UINT grid, UINT perfLevel, bool bidirectional, std::string& error);
    void Shutdown();
    bool Ready() const { return m_session != nullptr && m_fence12 != nullptr; }

    // D3D12 views of the shared textures. The pipeline copies the current frame into Input12() (COPY_DEST) and reads the results
    // (NON_PIXEL_SHADER_RESOURCE); every D3D12 use must end with a transition back to COMMON in the same command list, because the
    // D3D11 side accesses the textures between submissions.
    ID3D12Resource* Input12() const { return m_sharedInput12.Get(); }
    ID3D12Resource* Flow12() const { return m_sharedFlow12.Get(); }
    ID3D12Resource* Cost12() const { return m_sharedCost12.Get(); }
    ID3D12Resource* FlowBack12() const { return m_sharedFlowBack12.Get(); }
    ID3D12Resource* CostBack12() const { return m_sharedCostBack12.Get(); }

    // Computes the flow for the frame the pipeline has just copied into Input12(); the copy must already be submitted to the queue.
    // Waits on the GPU for that submission, runs the forward (current -> previous) and, when available, backward pass, publishes
    // the results in Flow12()/Cost12()/FlowBack12()/CostBack12() and makes the D3D12 queue wait for them. resetHints disables the
    // temporal hints for this frame (scene cut / history reset). Returns false (with an empty error) while only priming.
    bool Execute(GpuContext& gpu, bool resetHints, std::string& error);

    bool HasCost() const { return m_sharedCost12 != nullptr; }
    bool Bidirectional() const { return m_sharedFlowBack12 != nullptr; }
    bool SinglePassBidirectional() const { return Bidirectional() && !m_twoPass; }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    UINT Grid() const { return m_grid; }
    DXGI_FORMAT InputFormat() const { return m_inputFormat; }
    UINT FlowWidth() const { return m_flowW; }
    UINT FlowHeight() const { return m_flowH; }
    UINT64 ExecuteCount() const { return m_executeCount; }

private:
    bool EnsureDevice(GpuContext& gpu, std::string& error);
    bool CreateSharedTexture(GpuContext& gpu, UINT w, UINT h, DXGI_FORMAT fmt, ComPtr<ID3D11Texture2D>& tex11, ComPtr<ID3D12Resource>& res12,
                             const char* what, std::string& error);
    bool CreateSessionAndRegister(GpuContext& gpu, bool withCost, bool bidirectional, bool verbose, std::string& error);
    void ReleaseSession();
    void WaitForD3D11();
    std::string LastError() const;

    // Private native D3D11 device (kept across re-initialisations) and the shared fence.
    ComPtr<ID3D11Device5>        m_dev11;
    ComPtr<ID3D11DeviceContext4> m_ctx11;
    ComPtr<ID3D11Fence>          m_fence11;
    ComPtr<ID3D12Fence>          m_fence12;
    UINT64                       m_fenceValue = 0;
    LUID                         m_devLuid{};

    // Textures registered with the optical flow session (plain D3D11 textures).
    ComPtr<ID3D11Texture2D> m_inputs[2];
    ComPtr<ID3D11Texture2D> m_flow, m_flowBack;
    ComPtr<ID3D11Texture2D> m_cost, m_costBack;
    // Shared textures (created on D3D11, opened on D3D12).
    ComPtr<ID3D11Texture2D> m_sharedInput11, m_sharedFlow11, m_sharedCost11, m_sharedFlowBack11, m_sharedCostBack11;
    ComPtr<ID3D12Resource>  m_sharedInput12, m_sharedFlow12, m_sharedCost12, m_sharedFlowBack12, m_sharedCostBack12;

    void*  m_session = nullptr;
    void*  m_inputHandles[2] = { nullptr, nullptr };
    void*  m_flowHandle = nullptr;
    void*  m_costHandle = nullptr;
    void*  m_flowBackHandle = nullptr;
    void*  m_costBackHandle = nullptr;
    bool   m_wantBidirectional = false;
    bool   m_twoPass = false;              // backward flow computed by a second Execute (API 2.0 layout)
    UINT   m_width = 0, m_height = 0, m_grid = 2, m_flowW = 0, m_flowH = 0, m_perf = 10;
    DXGI_FORMAT m_inputFormat = DXGI_FORMAT_R8_UNORM;
    UINT64 m_executeCount = 0;
};

} // namespace vdc
