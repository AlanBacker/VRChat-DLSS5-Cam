// VRChat DLSS5 Cam - DLSS pre-pass through the official nvngx_dlss.dll: super resolution from a render size to a
// larger output size, or DLAA (temporal anti-aliasing) when the two sizes are equal.
// This software contains source code provided by NVIDIA Corporation.
#pragma once
#include "ngx/NgxCore.h"

namespace vdc {

class DlssFeature {
public:
    // renderW x renderH: the colour, depth and motion vectors handed to Evaluate; outW x outH: the output. Equal
    // sizes make a DLAA feature, otherwise quality is the NVSDK_NGX_PerfQuality_Value the render size was chosen for
    // (see NgxCore::DlssOptimalSettings). preset: the DLSS render preset hint (0 = the runtime's default).
    bool Create(NgxCore& core, ID3D12GraphicsCommandList* cmd, UINT renderW, UINT renderH, UINT outW, UINT outH,
                int quality, int preset, std::string& error);
    void Release(NgxCore& core);
    bool Created() const { return m_feature != nullptr; }
    bool Evaluate(ID3D12GraphicsCommandList* cmd, ID3D12Resource* color, ID3D12Resource* mvec, ID3D12Resource* depth,
                  ID3D12Resource* output, bool reset, float frameTimeMs, std::string& error);
    UINT RenderWidth() const { return m_renderW; }
    UINT RenderHeight() const { return m_renderH; }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    int  Quality() const { return m_quality; }
    bool Upscales() const { return m_renderW != m_width || m_renderH != m_height; }
    static const char* QualityName(int quality);   // "DLAA", "Quality", "Balanced", ...

private:
    NVSDK_NGX_Parameter* m_params = nullptr;
    NVSDK_NGX_Handle*    m_feature = nullptr;
    UINT                 m_renderW = 0, m_renderH = 0, m_width = 0, m_height = 0;
    int                  m_quality = NVSDK_NGX_PerfQuality_Value_DLAA;
};

} // namespace vdc
