#include "ngx/DlssFeature.h"
#include "core/Log.h"
#include "core/Util.h"

namespace vdc {

namespace {

// The render preset hint is a parameter per quality mode.
const char* PresetKey(int quality) {
    switch (quality) {
    case NVSDK_NGX_PerfQuality_Value_MaxPerf:          return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance;
    case NVSDK_NGX_PerfQuality_Value_Balanced:         return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced;
    case NVSDK_NGX_PerfQuality_Value_MaxQuality:       return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality;
    case NVSDK_NGX_PerfQuality_Value_UltraPerformance: return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance;
    case NVSDK_NGX_PerfQuality_Value_UltraQuality:     return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality;
    default:                                           return NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA;
    }
}

struct DlssCreateValues { unsigned renderW, renderH, outW, outH; int quality, preset; };

NVSDK_NGX_Result SafeCreateDlss(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, const DlssCreateValues* v,
                                NVSDK_NGX_Handle** handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY {
        NVSDK_NGX_DLSS_Create_Params cp{};
        cp.Feature.InWidth = v->renderW;
        cp.Feature.InHeight = v->renderH;
        cp.Feature.InTargetWidth = v->outW;
        cp.Feature.InTargetHeight = v->outH;
        cp.Feature.InPerfQualityValue = (NVSDK_NGX_PerfQuality_Value)v->quality;
        // The depth is inverted (1 = near). When the feature upscales, the motion vectors come at the render size
        // like the colour and the depth (their values in render pixels).
        int flags = NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        if (v->renderW != v->outW || v->renderH != v->outH) flags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        cp.InFeatureCreateFlags = flags;
        cp.InEnableOutputSubrects = false;
        params->Set(PresetKey(v->quality), v->preset);
        return NGX_D3D12_CREATE_DLSS_EXT(cmd, 1, 1, handle, params, &cp);
    }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

struct DlssEvalValues {
    ID3D12Resource* color; ID3D12Resource* mvec; ID3D12Resource* depth; ID3D12Resource* output;
    unsigned renderW, renderH; int reset; float frameTimeMs;
};

NVSDK_NGX_Result SafeEvaluateDlss(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Handle* handle, NVSDK_NGX_Parameter* params,
                                  const DlssEvalValues* v, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY {
        NVSDK_NGX_D3D12_DLSS_Eval_Params ep{};
        ep.Feature.pInColor = v->color;
        ep.Feature.pInOutput = v->output;
        ep.Feature.InSharpness = 0.0f;
        ep.pInDepth = v->depth;
        ep.pInMotionVectors = v->mvec;
        ep.InJitterOffsetX = 0.0f;
        ep.InJitterOffsetY = 0.0f;
        ep.InRenderSubrectDimensions.Width = v->renderW;
        ep.InRenderSubrectDimensions.Height = v->renderH;
        ep.InReset = v->reset;
        ep.InMVScaleX = 1.0f;
        ep.InMVScaleY = 1.0f;
        ep.InFrameTimeDeltaInMsec = v->frameTimeMs;
        return NGX_D3D12_EVALUATE_DLSS_EXT(cmd, handle, params, &ep);
    }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeReleaseDlss(NVSDK_NGX_Handle* handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return NVSDK_NGX_D3D12_ReleaseFeature(handle); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

} // namespace

const char* DlssFeature::QualityName(int quality) {
    switch (quality) {
    case NVSDK_NGX_PerfQuality_Value_MaxPerf:          return "Performance";
    case NVSDK_NGX_PerfQuality_Value_Balanced:         return "Balanced";
    case NVSDK_NGX_PerfQuality_Value_MaxQuality:       return "Quality";
    case NVSDK_NGX_PerfQuality_Value_UltraPerformance: return "Ultra performance";
    case NVSDK_NGX_PerfQuality_Value_UltraQuality:     return "Ultra quality";
    case NVSDK_NGX_PerfQuality_Value_DLAA:             return "DLAA";
    default:                                           return "?";
    }
}

bool DlssFeature::Create(NgxCore& core, ID3D12GraphicsCommandList* cmd, UINT renderW, UINT renderH, UINT outW, UINT outH,
                         int quality, int preset, std::string& error) {
    Release(core);
    if (!core.Initialized()) { error = "NGX core is not initialized"; return false; }
    if (!core.DlssAvailable()) { error = "DLSS is not available on this system"; return false; }
    m_params = core.AllocateParameters(error);
    if (!m_params) return false;
    if (renderW == outW && renderH == outH) quality = NVSDK_NGX_PerfQuality_Value_DLAA;
    const DlssCreateValues v{ renderW, renderH, outW, outH, quality, preset };
    unsigned long seh = 0;
    const NVSDK_NGX_Result r = SafeCreateDlss(cmd, m_params, &v, &m_feature, &seh);
    if (seh || NVSDK_NGX_FAILED(r) || !m_feature) {
        error = seh ? StrPrintf("DLSS CreateFeature raised exception 0x%08lx", seh)
                    : StrPrintf("DLSS CreateFeature failed (%s, 0x%08x)", NgxCore::ResultName(r), (unsigned)r);
        m_feature = nullptr;
        core.DestroyParameters(m_params); m_params = nullptr;
        return false;
    }
    m_renderW = renderW; m_renderH = renderH; m_width = outW; m_height = outH; m_quality = quality;
    if (Upscales()) Log::Info("DLSS super resolution feature created: %ux%u -> %ux%u (%s, preset %d)", renderW, renderH, outW, outH, QualityName(quality), preset);
    else Log::Info("DLAA feature created: %ux%u preset %d", outW, outH, preset);
    return true;
}

void DlssFeature::Release(NgxCore& core) {
    if (m_feature) {
        unsigned long seh = 0;
        const NVSDK_NGX_Result r = SafeReleaseDlss(m_feature, &seh);
        if (seh) Log::Warn("DLSS ReleaseFeature raised exception 0x%08lx", seh);
        else if (NVSDK_NGX_FAILED(r)) Log::Warn("DLSS ReleaseFeature failed (%s)", NgxCore::ResultName(r));
        m_feature = nullptr;
    }
    if (m_params) { core.DestroyParameters(m_params); m_params = nullptr; }
}

bool DlssFeature::Evaluate(ID3D12GraphicsCommandList* cmd, ID3D12Resource* color, ID3D12Resource* mvec, ID3D12Resource* depth,
                           ID3D12Resource* output, bool reset, float frameTimeMs, std::string& error) {
    if (!m_feature) { error = "DLSS feature not created"; return false; }
    const DlssEvalValues v{ color, mvec, depth, output, m_renderW, m_renderH, reset ? 1 : 0, frameTimeMs };
    unsigned long seh = 0;
    const NVSDK_NGX_Result r = SafeEvaluateDlss(cmd, m_feature, m_params, &v, &seh);
    if (seh) { error = StrPrintf("DLSS EvaluateFeature raised exception 0x%08lx", seh); return false; }
    if (NVSDK_NGX_FAILED(r)) { error = StrPrintf("DLSS EvaluateFeature failed (%s, 0x%08x)", NgxCore::ResultName(r), (unsigned)r); return false; }
    return true;
}

} // namespace vdc
