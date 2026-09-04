#include "ngx/NgxCore.h"
#include "core/Log.h"

namespace vdc {

namespace {

constexpr const char* kProjectId = "e4c0b9a2-3f6d-4a2e-9b1c-7d5a8f2c6e11";
constexpr const char* kEngineVersion = "VRChatDLSS5Cam-" APP_VERSION_STRING;

void NVSDK_CONV NgxLogCallback(const char* message, NVSDK_NGX_Logging_Level /*level*/, NVSDK_NGX_Feature /*source*/) {
    if (!message || !*message) return;
    std::string s(message);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    Log::Info("NGX: %s", s.c_str());
}

// SEH wrappers: plain functions without C++ objects so __try is legal (MSVC).
NVSDK_NGX_Result SafeInit(const wchar_t* dataPath, ID3D12Device* device, const NVSDK_NGX_FeatureCommonInfo* info,
                          unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY {
        return NVSDK_NGX_D3D12_Init_with_ProjectID(kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, dataPath,
                                                   device, info, NVSDK_NGX_Version_API);
    }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeGetCaps(NVSDK_NGX_Parameter** out, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return NVSDK_NGX_D3D12_GetCapabilityParameters(out); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeAlloc(NVSDK_NGX_Parameter** out, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return NVSDK_NGX_D3D12_AllocateParameters(out); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeDestroyParams(NVSDK_NGX_Parameter* p, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return NVSDK_NGX_D3D12_DestroyParameters(p); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeShutdown(ID3D12Device* device, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return NVSDK_NGX_D3D12_Shutdown1(device); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

bool SafeGetInt(NVSDK_NGX_Parameter* p, const char* key, int* out, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return p->Get(key, out) == NVSDK_NGX_Result_Success; }
    VDC_SEH_EXCEPT(*seh) { return false; }
}

} // namespace

const char* NgxCore::ResultName(NVSDK_NGX_Result r) {
    switch (r) {
        case NVSDK_NGX_Result_Success: return "Success";
        case NVSDK_NGX_Result_Fail: return "Fail";
        case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FeatureNotSupported";
        case NVSDK_NGX_Result_FAIL_PlatformError: return "PlatformError";
        case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists: return "FeatureAlreadyExists";
        case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FeatureNotFound";
        case NVSDK_NGX_Result_FAIL_InvalidParameter: return "InvalidParameter";
        case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall: return "ScratchBufferTooSmall";
        case NVSDK_NGX_Result_FAIL_NotInitialized: return "NotInitialized";
        case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "UnsupportedInputFormat";
        case NVSDK_NGX_Result_FAIL_RWFlagMissing: return "RWFlagMissing";
        case NVSDK_NGX_Result_FAIL_MissingInput: return "MissingInput";
        case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "UnableToInitializeFeature";
        case NVSDK_NGX_Result_FAIL_OutOfDate: return "OutOfDate";
        case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "OutOfGPUMemory";
        case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "UnsupportedFormat";
        case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath: return "UnableToWriteToAppDataPath";
        case NVSDK_NGX_Result_FAIL_UnsupportedParameter: return "UnsupportedParameter";
        case NVSDK_NGX_Result_FAIL_Denied: return "Denied";
        case NVSDK_NGX_Result_FAIL_NotImplemented: return "NotImplemented";
        default: return "Unknown";
    }
}

bool NgxCore::Init(Device& device, const std::wstring& exeDir, const std::wstring& appDataDir) {
    if (m_initialized) return true;
    m_device = device.D3D12();
    m_exeDir = exeDir;

    if (!device.Info().IsNvidia()) {
        m_status = "NVIDIA GPU required";
        Log::Warn("NGX: not an NVIDIA adapter, DLSS features unavailable");
        return false;
    }

    const wchar_t* paths[] = { m_exeDir.c_str() };
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    info.LoggingInfo.LoggingCallback = &NgxLogCallback;
    info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    info.LoggingInfo.DisableOtherLoggingSinks = true;

    unsigned long seh = 0;
    NVSDK_NGX_Result r = SafeInit(appDataDir.c_str(), m_device, &info, &seh);
    if (seh) {
        m_status = StrPrintf("NGX init raised exception 0x%08lx", seh);
        Log::Error("%s", m_status.c_str());
        return false;
    }
    if (NVSDK_NGX_FAILED(r)) {
        m_status = StrPrintf("NGX init failed (%s, 0x%08x)", ResultName(r), (unsigned)r);
        Log::Error("%s", m_status.c_str());
        return false;
    }
    m_initialized = true;

    r = SafeGetCaps(&m_caps, &seh);
    if (seh || NVSDK_NGX_FAILED(r) || !m_caps) {
        m_caps = nullptr;
        Log::Warn("NGX: GetCapabilityParameters failed (%s)", ResultName(r));
    } else {
        int avail = 0, needsUpdate = 0, minMajor = 0, minMinor = 0;
        if (SafeGetInt(m_caps, NVSDK_NGX_Parameter_SuperSampling_Available, &avail, &seh)) m_dlssAvailable = avail != 0;
        if (SafeGetInt(m_caps, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdate, &seh)) m_needsDriverUpdate = needsUpdate != 0;
        SafeGetInt(m_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minMajor, &seh);
        SafeGetInt(m_caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minMinor, &seh);
        Log::Info("NGX: DLSS available=%d needsDriverUpdate=%d minDriver=%d.%d", avail, needsUpdate, minMajor, minMinor);
    }
    m_status = m_dlssAvailable ? "OK" : (m_needsDriverUpdate ? "Driver update required" : "DLSS unavailable");
    Log::Info("NGX core initialized (%s)", m_status.c_str());
    return true;
}

NVSDK_NGX_Parameter* NgxCore::AllocateParameters(std::string& error) {
    if (!m_initialized) { error = "NGX not initialized"; return nullptr; }
    NVSDK_NGX_Parameter* p = nullptr;
    unsigned long seh = 0;
    const NVSDK_NGX_Result r = SafeAlloc(&p, &seh);
    if (seh) { error = StrPrintf("AllocateParameters raised exception 0x%08lx", seh); return nullptr; }
    if (NVSDK_NGX_FAILED(r) || !p) { error = StrPrintf("AllocateParameters failed (%s)", ResultName(r)); return nullptr; }
    return p;
}

void NgxCore::DestroyParameters(NVSDK_NGX_Parameter* params) {
    if (!params || !m_initialized) return;
    unsigned long seh = 0;
    SafeDestroyParams(params, &seh);
    if (seh) Log::Warn("NGX: DestroyParameters raised exception 0x%08lx", seh);
}

void NgxCore::Shutdown() {
    if (!m_initialized) return;
    unsigned long seh = 0;
    if (m_caps) { SafeDestroyParams(m_caps, &seh); m_caps = nullptr; }
    const NVSDK_NGX_Result r = SafeShutdown(m_device, &seh);
    if (seh) Log::Warn("NGX: Shutdown1 raised exception 0x%08lx", seh);
    else if (NVSDK_NGX_FAILED(r)) Log::Warn("NGX: Shutdown1 failed (%s)", ResultName(r));
    m_initialized = false;
    m_dlssAvailable = false;
}

} // namespace vdc
