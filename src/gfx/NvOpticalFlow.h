// VRChat DLSS5 Cam - NVIDIA Optical Flow (nvofapi64.dll, D3D11 interface) provider.
// The ABI declarations below are adapted from the MIT-licensed dlss5-bridge project (see THIRD_PARTY_NOTICES.md).
#pragma once
#include "gfx/Device.h"
#include <string>

namespace vdc {

class NvOpticalFlow {
public:
    ~NvOpticalFlow() { Shutdown(); }

    static bool LibraryAvailable();

    // Inputs are R8_UNORM (luma) or B8G8R8A8_UNORM textures (width x height). grid = 1/2/4 output grid size, perfLevel 5 (slow) / 10 / 20 (fast).
    bool Init(Device& device, UINT width, UINT height, DXGI_FORMAT inputFormat, UINT grid, UINT perfLevel, std::string& error);
    void Shutdown();
    bool Ready() const { return m_session != nullptr; }

    // Computes flow from Input(currentIndex) to Input(1 - currentIndex) (i.e. current -> previous).
    bool Execute(int currentIndex, std::string& error);

    ID3D11Texture2D* Input(int index) const { return m_inputs[index & 1].Get(); }
    ID3D11Texture2D* FlowTexture() const { return m_flow.Get(); }
    ID3D11Texture2D* CostTexture() const { return m_cost.Get(); }
    bool HasCost() const { return m_cost != nullptr; }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    UINT Grid() const { return m_grid; }
    DXGI_FORMAT InputFormat() const { return m_inputFormat; }
    UINT FlowWidth() const { return m_flowW; }
    UINT FlowHeight() const { return m_flowH; }
    UINT64 ExecuteCount() const { return m_executeCount; }

private:
    bool CreateSessionAndRegister(Device& device, bool withCost, std::string& error);
    std::string LastError() const;

    ComPtr<ID3D11Texture2D> m_inputs[2];
    ComPtr<ID3D11Texture2D> m_flow;
    ComPtr<ID3D11Texture2D> m_cost;
    void*  m_session = nullptr;
    void*  m_inputHandles[2] = { nullptr, nullptr };
    void*  m_flowHandle = nullptr;
    void*  m_costHandle = nullptr;
    UINT   m_width = 0, m_height = 0, m_grid = 2, m_flowW = 0, m_flowH = 0, m_perf = 10;
    DXGI_FORMAT m_inputFormat = DXGI_FORMAT_R8_UNORM;
    UINT64 m_executeCount = 0;
};

} // namespace vdc
