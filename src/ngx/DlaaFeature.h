// VRChat DLSS5 Cam - optional DLSS DLAA (temporal anti-aliasing) pre-pass through the official nvngx_dlss.dll.
// This software contains source code provided by NVIDIA Corporation.
#pragma once
#include "ngx/NgxCore.h"

namespace vdc {

class DlaaFeature {
public:
    bool Create(NgxCore& core, ID3D12GraphicsCommandList* cmd, UINT width, UINT height, int preset, std::string& error);
    void Release(NgxCore& core);
    bool Created() const { return m_feature != nullptr; }
    bool Evaluate(ID3D12GraphicsCommandList* cmd, ID3D12Resource* color, ID3D12Resource* mvec, ID3D12Resource* depth,
                  ID3D12Resource* output, bool reset, float frameTimeMs, std::string& error);
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }

private:
    NVSDK_NGX_Parameter* m_params = nullptr;
    NVSDK_NGX_Handle*    m_feature = nullptr;
    UINT                 m_width = 0, m_height = 0;
};

} // namespace vdc
