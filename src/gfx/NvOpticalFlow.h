// VRChat DLSS5 Cam - NVIDIA Optical Flow (nvofapi64.dll, D3D12 interface) provider.
// Driven through the NVIDIA Optical Flow SDK 5.0 interface headers (third_party/nvof, MIT): the forward and backward flow come
// out of a single engine pass, and every pass is ordered against the program's D3D12 queue with fence points, so the vectors a
// frame reads are the ones computed for that frame.
//
// Before 2.2.0 the engine was driven through the D3D11 interface on a private device, with the results crossing over to D3D12
// through shared textures. That interface has no completion signal: the copy that published a result ran before the engine had
// finished with it, so every frame used the previous frame's vectors.
//
// Two things the driver (R58x, 5090) does not survive shape the teardown: the fence given to the engine is referenced from the
// graphics queue after the session is gone, so it lives as long as the wrapper; and nvOFDestroy leaves the driver reading freed
// memory on the next submission once any session has executed, so sessions are never destroyed. Shutdown() unregisters and
// releases the textures and parks the session, and the next Init() with the same parameters takes it back (see the .cpp).
#pragma once
#include "gfx/Device.h"
#include <string>
#include <vector>

namespace vdc {

class NvOpticalFlow {
public:
    ~NvOpticalFlow();

    static bool LibraryAvailable();
    static UINT ApiVersion();   // 0x50 once the library is loaded, 0 otherwise

    // Inputs are R8_UNORM (luma) or B8G8R8A8_UNORM textures (width x height). grid = 1/2/4 output grid size, perfLevel 5 (slow) / 10 / 20 (fast).
    // bidirectional = also compute the backward flow (previous -> current) so that a forward/backward consistency check can be applied.
    bool Init(GpuContext& gpu, UINT width, UINT height, DXGI_FORMAT inputFormat, UINT grid, UINT perfLevel, bool bidirectional, std::string& error);
    void Shutdown();   // releases the textures and parks the session; the fence stays until the wrapper is destroyed (see above)
    bool Ready() const { return m_session != nullptr && m_fence != nullptr; }

    // The textures registered with the engine. The pipeline copies the current frame into Input12(InputIndex()) (COPY_DEST; the
    // other input holds the previous frame) and reads the results (NON_PIXEL_SHADER_RESOURCE); every D3D12 use must end with a
    // transition back to COMMON in the same command list, because the engine accesses the textures between submissions.
    ID3D12Resource* Input12(int i) const { return m_inputs[i & 1].Get(); }
    UINT            InputIndex() const { return static_cast<UINT>(m_executeCount & 1); }
    ID3D12Resource* Flow12() const { return m_flow.Get(); }
    ID3D12Resource* Cost12() const { return m_cost.Get(); }
    ID3D12Resource* FlowBack12() const { return m_flowBack.Get(); }
    ID3D12Resource* CostBack12() const { return m_costBack.Get(); }

    // Computes the flow for the frame the pipeline has just copied into the current input; the copy must already be submitted to
    // the queue. The engine waits on the GPU for that submission, runs the forward (current -> previous) and, when available,
    // backward pass into Flow12()/Cost12()/FlowBack12()/CostBack12(), and the D3D12 queue waits for its completion. resetHints
    // disables the temporal hints for this frame (scene cut / history reset). Returns false (with an empty error) while only priming.
    bool Execute(GpuContext& gpu, bool resetHints, std::string& error);

    bool HasCost() const { return m_cost != nullptr; }
    bool Bidirectional() const { return m_flowBack != nullptr; }
    bool SinglePassBidirectional() const { return Bidirectional() && !m_twoPass; }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    UINT Grid() const { return m_grid; }
    DXGI_FORMAT InputFormat() const { return m_inputFormat; }
    UINT FlowWidth() const { return m_flowW; }
    UINT FlowHeight() const { return m_flowH; }
    UINT64 ExecuteCount() const { return m_executeCount; }

private:
    bool EnsureFence(GpuContext& gpu, std::string& error);
    bool CreateTexture(GpuContext& gpu, UINT w, UINT h, DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& res, const wchar_t* name, const char* what, std::string& error);
    bool Register(ID3D12Resource* res, void** handle, const char* what, std::string& error);
    bool CreateSessionAndRegister(GpuContext& gpu, bool withCost, bool bidirectional, bool verbose, std::string& error);
    void ReleaseSession();
    void WaitForEngine();
    std::string LastError() const;

    // Fence shared between the queue and the engine (kept across re-initialisations). m_fenceValue is the last value handed out;
    // m_engineValue the last one the engine was asked to signal, so a drain never waits for a value nobody will reach.
    ComPtr<ID3D12Fence> m_fence;
    UINT64              m_fenceValue = 0;
    UINT64              m_engineValue = 0;
    HANDLE              m_fenceEvent = nullptr;

    // Initialised sessions that are no longer in use. They are parked instead of destroyed (see ReleaseSession) and taken
    // again by an Init() with the same parameters.
    struct Parked {
        void*       session = nullptr;
        UINT        width = 0, height = 0, grid = 0, effectiveGrid = 0, perf = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        bool        cost = false, bidirectional = false, twoPass = false;
    };
    std::vector<Parked> m_parked;
    Parked              m_key;      // the parameters of the current session, for parking it
    bool TakeParked(bool withCost, bool bidirectional);
    bool CreateAndInitSession(GpuContext& gpu, bool withCost, bool bidirectional, bool verbose, std::string& error);

    // Textures registered with the engine.
    ComPtr<ID3D12Resource> m_inputs[2];
    ComPtr<ID3D12Resource> m_flow, m_flowBack;
    ComPtr<ID3D12Resource> m_cost, m_costBack;

    void*  m_session = nullptr;
    void*  m_inputHandles[2] = { nullptr, nullptr };
    void*  m_flowHandle = nullptr;
    void*  m_costHandle = nullptr;
    void*  m_flowBackHandle = nullptr;
    void*  m_costBackHandle = nullptr;
    bool   m_wantBidirectional = false;
    bool   m_twoPass = false;              // backward flow computed by a second pass (driver refused the single pass)
    UINT   m_width = 0, m_height = 0, m_grid = 2, m_flowW = 0, m_flowH = 0, m_perf = 10;
    DXGI_FORMAT m_inputFormat = DXGI_FORMAT_R8_UNORM;
    UINT64 m_executeCount = 0;
};

} // namespace vdc
