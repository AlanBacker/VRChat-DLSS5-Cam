#include "ngx/DlaaFeature.h"
#include "core/Log.h"

namespace vdc {

namespace {

struct DlaaCreateValues { unsigned width, height; int preset; };

NVSDK_NGX_Result SafeCreateDlaa(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, const DlaaCreateValues* v,
                                NVSDK_NGX_Handle** handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY {
        NVSDK_NGX_DLSS_Create_Params cp{};
        cp.Feature.InWidth = v->width;
        cp.Feature.InHeight = v->height;
        cp.Feature.InTargetWidth = v->width;
        cp.Feature.InTargetHeight = v->height;
        cp.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
        cp.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        cp.InEnableOutputSubrects = false;
        params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, v->preset);
        return NGX_D3D12_CREATE_DLSS_EXT(cmd, 1, 1, handle, params, &cp);
    }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

struct DlaaEvalValues {
    ID3D12Resource* color; ID3D12Resource* mvec; ID3D12Resource* depth; ID3D12Resource* output;
    unsigned width, height; int reset; float frameTimeMs;
};

NVSDK_NGX_Result SafeEvaluateDlaa(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Handle* handle, NVSDK_NGX_Parameter* params,
                                  const DlaaEvalValues* v, unsigned long* seh) noexcept {
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
        ep.InRenderSubrectDimensions.Width = v->width;
        ep.InRenderSubrectDimensions.Height = v->height;
        ep.InReset = v->reset;
        ep.InMVScaleX = 1.0f;
        ep.InMVScaleY = 1.0f;
        ep.InFrameTimeDeltaInMsec = v->frameTimeMs;
        return NGX_D3D12_EVALUATE_DLSS_EXT(cmd, handle, params, &ep);
    }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeReleaseDlaa(NVSDK_NGX_Handle* handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return NVSDK_NGX_D3D12_ReleaseFeature(handle); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

} // namespace

bool DlaaFeature::Create(NgxCore& core, ID3D12GraphicsCommandList* cmd, UINT width, UINT height, int preset, std::string& error) {
    Release(core);
    if (!core.Initialized()) { error = "NGX core is not initialized"; return false; }
    if (!core.DlssAvailable()) { error = "DLSS is not available on this system"; return false; }
    m_params = core.AllocateParameters(error);
    if (!m_params) return false;
    const DlaaCreateValues v{ width, height, preset };
    unsigned long seh = 0;
    const NVSDK_NGX_Result r = SafeCreateDlaa(cmd, m_params, &v, &m_feature, &seh);
    if (seh || NVSDK_NGX_FAILED(r) || !m_feature) {
        error = seh ? StrPrintf("DLAA CreateFeature raised exception 0x%08lx", seh)
                    : StrPrintf("DLAA CreateFeature failed (%s, 0x%08x)", NgxCore::ResultName(r), (unsigned)r);
        m_feature = nullptr;
        core.DestroyParameters(m_params); m_params = nullptr;
        return false;
    }
    m_width = width; m_height = height;
    Log::Info("DLAA feature created: %ux%u preset %d", width, height, preset);
    return true;
}

void DlaaFeature::Release(NgxCore& core) {
    if (m_feature) {
        unsigned long seh = 0;
        const NVSDK_NGX_Result r = SafeReleaseDlaa(m_feature, &seh);
        if (seh) Log::Warn("DLAA ReleaseFeature raised exception 0x%08lx", seh);
        else if (NVSDK_NGX_FAILED(r)) Log::Warn("DLAA ReleaseFeature failed (%s)", NgxCore::ResultName(r));
        m_feature = nullptr;
    }
    if (m_params) { core.DestroyParameters(m_params); m_params = nullptr; }
}

bool DlaaFeature::Evaluate(ID3D12GraphicsCommandList* cmd, ID3D12Resource* color, ID3D12Resource* mvec, ID3D12Resource* depth,
                           ID3D12Resource* output, bool reset, float frameTimeMs, std::string& error) {
    if (!m_feature) { error = "DLAA feature not created"; return false; }
    const DlaaEvalValues v{ color, mvec, depth, output, m_width, m_height, reset ? 1 : 0, frameTimeMs };
    unsigned long seh = 0;
    const NVSDK_NGX_Result r = SafeEvaluateDlaa(cmd, m_feature, m_params, &v, &seh);
    if (seh) { error = StrPrintf("DLAA EvaluateFeature raised exception 0x%08lx", seh); return false; }
    if (NVSDK_NGX_FAILED(r)) { error = StrPrintf("DLAA EvaluateFeature failed (%s, 0x%08x)", NgxCore::ResultName(r), (unsigned)r); return false; }
    return true;
}

} // namespace vdc
