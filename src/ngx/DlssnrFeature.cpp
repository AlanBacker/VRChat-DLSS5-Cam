#include "ngx/DlssnrFeature.h"
#include "core/Log.h"
#include <atomic>
#include <cstring>

namespace vdc {

namespace {

// Signed-snippet exports (the same ABI the NGX core uses to talk to feature DLLs).
using SnippetInitExtFn = NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version, const NVSDK_NGX_Parameter*);
using CreateFeatureFn  = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using EvaluateFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);
using ReleaseFeatureFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using ShutdownFn       = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
using GetModuleFileNameWFn = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);

constexpr unsigned long long kApplicationId = 0x0876232Cull;
constexpr NVSDK_NGX_Feature  kFeatureDlssnr = static_cast<NVSDK_NGX_Feature>(18);

SnippetInitExtFn  g_initExt = nullptr;
CreateFeatureFn   g_create = nullptr;
EvaluateFeatureFn g_evaluate = nullptr;
ReleaseFeatureFn  g_release = nullptr;
ShutdownFn        g_shutdown = nullptr;

std::atomic<HMODULE>              g_callerModule{ nullptr };
std::atomic<GetModuleFileNameWFn> g_originalGetModuleFileNameW{ nullptr };

// The snippet verifies the file name of the module that calls into it. Answer "nvngx.dll" for our own module.
DWORD WINAPI ShimGetModuleFileNameW(HMODULE module, LPWSTR fileName, DWORD size) noexcept {
    if (module && module == g_callerModule.load(std::memory_order_acquire)) {
        static const wchar_t kName[] = L"nvngx.dll";
        constexpr DWORD kLen = (DWORD)(sizeof(kName) / sizeof(kName[0]) - 1);
        if (!fileName || !size) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
        if (size <= kLen) {
            if (size > 1) std::memcpy(fileName, kName, (size - 1) * sizeof(wchar_t));
            fileName[size - 1] = L'\0';
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return size;
        }
        std::memcpy(fileName, kName, sizeof(kName));
        return kLen;
    }
    const GetModuleFileNameWFn original = g_originalGetModuleFileNameW.load(std::memory_order_acquire);
    if (original) return original(module, fileName, size);
    SetLastError(ERROR_INVALID_FUNCTION);
    return 0;
}

// Locate the import address table slot of `functionName` (kernel32 / api-set library loader) in `module`.
void** FindImportSlot(HMODULE module, const char* functionName) noexcept {
    if (!module) return nullptr;
    auto* base = reinterpret_cast<unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size || dir.VirtualAddress >= imageSize || dir.Size > imageSize ||
        dir.VirtualAddress > imageSize - dir.Size)
        return nullptr;
    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    const auto* descEnd = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress + dir.Size);
    for (; desc < descEnd && desc->Name; ++desc) {
        if (desc->Name >= imageSize) continue;
        const char* lib = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(lib, "KERNEL32.dll") != 0 &&
            _stricmp(lib, "api-ms-win-core-libraryloader-l1-2-0.dll") != 0 &&
            _stricmp(lib, "api-ms-win-core-libraryloader-l1-1-0.dll") != 0)
            continue;
        if (!desc->OriginalFirstThunk || !desc->FirstThunk) continue;
        auto* nameThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto* addrThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; nameThunk->u1.AddressOfData; ++nameThunk, ++addrThunk) {
            if (IMAGE_SNAP_BY_ORDINAL64(nameThunk->u1.Ordinal)) continue;
            const DWORD rva = static_cast<DWORD>(nameThunk->u1.AddressOfData);
            if (rva >= imageSize) return nullptr;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + rva);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), functionName) == 0)
                return reinterpret_cast<void**>(&addrThunk->u1.Function);
        }
    }
    return nullptr;
}

template <typename T> void* FnAddress(T fn) noexcept {
    void* out = nullptr;
    static_assert(sizeof(fn) == sizeof(out), "function pointer size");
    std::memcpy(&out, &fn, sizeof(out));
    return out;
}

// --- SEH wrappers (no C++ objects inside) --------------------------------------------------------

NVSDK_NGX_Result SafeInitExt(SnippetInitExtFn fn, const wchar_t* appDir, ID3D12Device* device, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(kApplicationId, appDir, device, NVSDK_NGX_Version_API, nullptr); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeCreate(CreateFeatureFn fn, ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                            NVSDK_NGX_Handle** handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(cmd, kFeatureDlssnr, params, handle); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeEvaluate(EvaluateFeatureFn fn, ID3D12GraphicsCommandList* cmd, const NVSDK_NGX_Handle* handle,
                              const NVSDK_NGX_Parameter* params, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(cmd, handle, params, nullptr); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeRelease(ReleaseFeatureFn fn, NVSDK_NGX_Handle* handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(handle); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result SafeShutdown(ShutdownFn fn, ID3D12Device* device, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(device); }
    VDC_SEH_EXCEPT(*seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

NVSDK_NGX_Result NVSDK_CONV ScalingRatioCallback(NVSDK_NGX_Parameter* params) noexcept {
    unsigned long seh = 0;
    VDC_SEH_TRY {
        if (!params) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        params->Set("DLSSNR.ScalingRatio", 1.0f);
        return NVSDK_NGX_Result_Success;
    }
    VDC_SEH_EXCEPT(seh) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}

struct CreateValues { unsigned inW, inH, outW, outH; int preset; };

bool SafeSetCreateParams(NVSDK_NGX_Parameter* p, const CreateValues* v, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY {
        p->Reset();   // a recycled parameter object must not carry a previous instance's entries
        const bool upscaling = v->inW != v->outW || v->inH != v->outH;
        p->Set("DLSSNR.Width", v->inW);
        p->Set("DLSSNR.Height", v->inH);
        p->Set("DLSSNR.InputWidth", v->inW);
        p->Set("DLSSNR.InputHeight", v->inH);
        p->Set("DLSSNR.OutputWidth", v->outW);
        p->Set("DLSSNR.OutputHeight", v->outH);
        p->Set("DLSSNR.Output.Width", v->outW);
        p->Set("DLSSNR.Output.Height", v->outH);
        p->Set("DLSSNR.Upscaling", upscaling ? 1u : 0u);
        p->Set("DLSSNR.Scale", upscaling ? (float)v->outW / (float)v->inW : 1.0f);
        p->Set("DLSSNR.ScalingRatio", upscaling ? (float)v->outW / (float)v->inW : 1.0f);
        p->Set("DLSSNRComputeScalingRatioCallback", FnAddress(&ScalingRatioCallback));
        p->Set("DLSSNR.Hint.Render.Preset", v->preset);
        p->Set(NVSDK_NGX_Parameter_Width, v->inW);
        p->Set(NVSDK_NGX_Parameter_Height, v->inH);
        p->Set(NVSDK_NGX_Parameter_OutWidth, v->outW);
        p->Set(NVSDK_NGX_Parameter_OutHeight, v->outH);
        p->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
        p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
        p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
        return true;
    }
    VDC_SEH_EXCEPT(*seh) { return false; }
}

struct EvalValues {
    ID3D12Resource* color; ID3D12Resource* depth; ID3D12Resource* mvec; ID3D12Resource* output;
    unsigned inW, inH, outW, outH;
    int reset, style, preset, autoMask, uiCorrection;
    float intensity, globalTone, localTone, localStructure, skinStructure;
};

bool SafeSetEvalParams(NVSDK_NGX_Parameter* p, const EvalValues* v, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY {
        p->Set("DLSSNR.Color", v->color);
        p->Set("DLSSNR.Output", v->output);
        p->Set("DLSSNR.MVec", v->mvec);
        p->Set("DLSSNR.Depth", v->depth);
        p->Set("DLSSNR.ColorSubrectBaseX", 0u);  p->Set("DLSSNR.ColorSubrectBaseY", 0u);
        p->Set("DLSSNR.ColorSubrectWidth", v->inW);  p->Set("DLSSNR.ColorSubrectHeight", v->inH);
        p->Set("DLSSNR.OutputSubrectBaseX", 0u); p->Set("DLSSNR.OutputSubrectBaseY", 0u);
        p->Set("DLSSNR.OutputSubrectWidth", v->outW); p->Set("DLSSNR.OutputSubrectHeight", v->outH);
        p->Set("DLSSNR.MVecSubrectBaseX", 0u);   p->Set("DLSSNR.MVecSubrectBaseY", 0u);
        p->Set("DLSSNR.MVecSubrectWidth", v->inW);   p->Set("DLSSNR.MVecSubrectHeight", v->inH);
        p->Set("DLSSNR.DepthSubrectBaseX", 0u);  p->Set("DLSSNR.DepthSubrectBaseY", 0u);
        p->Set("DLSSNR.DepthSubrectWidth", v->inW);  p->Set("DLSSNR.DepthSubrectHeight", v->inH);
        p->Set("DLSSNR.MVecScaleX", 1.0f);
        p->Set("DLSSNR.MVecScaleY", 1.0f);
        p->Set("DLSSNR.DepthInverted", 1);
        p->Set("DLSS.Indicator.Invert.X.Axis", 0);
        p->Set("DLSS.Indicator.Invert.Y.Axis", 0);
        p->Set("DLSSNR.Enabled", 1);
        p->Set("DLSSNR.Reset", v->reset);
        p->Set("DLSSNR.Hint.Render.Preset", v->preset);
        p->Set("DLSSNR.Style", v->style);
        p->Set("DLSSNR.Intensity", v->intensity);
        p->Set("DLSSNR.GlobalToneStrength", v->globalTone);
        p->Set("DLSSNR.LocalToneStrength", v->localTone);
        p->Set("DLSSNR.LocalStructureStrength", v->localStructure);
        p->Set("DLSSNR.SkinStructureStrength", v->skinStructure);
        p->Set("DLSSNR.UseAutoMask", v->autoMask);
        p->Set("DLSSNR.UICorrection", v->uiCorrection);
        return true;
    }
    VDC_SEH_EXCEPT(*seh) { return false; }
}

} // namespace

// ---------------------------------------------------------------------------

bool DlssnrFeature::InstallCallerShim(std::string& error) {
    m_iatSlot = FindImportSlot(m_module, "GetModuleFileNameW");
    if (!m_iatSlot) { error = "nvngx_dlssnr.dll has no GetModuleFileNameW import"; return false; }

    HMODULE caller = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(FnAddress(&ShimGetModuleFileNameW)), &caller) || !caller) {
        error = "Could not resolve the host module: " + LastErrorText();
        return false;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(m_iatSlot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        error = "VirtualProtect (IAT) failed: " + LastErrorText();
        return false;
    }
    g_callerModule.store(caller, std::memory_order_release);
    void* original = InterlockedExchangePointer(reinterpret_cast<void* volatile*>(m_iatSlot), FnAddress(&ShimGetModuleFileNameW));
    GetModuleFileNameWFn originalFn = nullptr;
    std::memcpy(&originalFn, &original, sizeof(originalFn));
    g_originalGetModuleFileNameW.store(originalFn, std::memory_order_release);
    m_shimInstalled = true;
    DWORD ignored = 0;
    VirtualProtect(m_iatSlot, sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), m_iatSlot, sizeof(void*));
    if (!originalFn) { error = "GetModuleFileNameW import slot was empty"; return false; }
    return true;
}

void DlssnrFeature::RemoveCallerShim() {
    if (!m_shimInstalled || !m_iatSlot) { m_shimInstalled = false; return; }
    const GetModuleFileNameWFn original = g_originalGetModuleFileNameW.load(std::memory_order_acquire);
    DWORD oldProtect = 0;
    if (VirtualProtect(m_iatSlot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(m_iatSlot), FnAddress(original));
        DWORD ignored = 0;
        VirtualProtect(m_iatSlot, sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), m_iatSlot, sizeof(void*));
    }
    g_originalGetModuleFileNameW.store(nullptr, std::memory_order_release);
    g_callerModule.store(nullptr, std::memory_order_release);
    m_shimInstalled = false;
    m_iatSlot = nullptr;
}

bool DlssnrFeature::LoadRuntime(ID3D12Device* device, const std::wstring& dllPath, const std::wstring& appDir, std::string& error) {
    if (RuntimeLoaded()) return true;
    UnloadRuntime();
    m_device = device;
    if (!FileExists(dllPath)) { error = "nvngx_dlssnr.dll not found"; return false; }

    m_module = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!m_module) { error = "LoadLibrary(nvngx_dlssnr.dll) failed: " + LastErrorText(); return false; }
    m_runtimePath = dllPath;
    m_runtimeVersion = GetFileVersionString(dllPath);

    g_initExt  = reinterpret_cast<SnippetInitExtFn>(GetProcAddress(m_module, "NVSDK_NGX_D3D12_Init_Ext"));
    g_create   = reinterpret_cast<CreateFeatureFn>(GetProcAddress(m_module, "NVSDK_NGX_D3D12_CreateFeature"));
    g_evaluate = reinterpret_cast<EvaluateFeatureFn>(GetProcAddress(m_module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    g_release  = reinterpret_cast<ReleaseFeatureFn>(GetProcAddress(m_module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    g_shutdown = reinterpret_cast<ShutdownFn>(GetProcAddress(m_module, "NVSDK_NGX_D3D12_Shutdown1"));
    if (!g_initExt || !g_create || !g_evaluate || !g_release || !g_shutdown) {
        error = "nvngx_dlssnr.dll does not export the expected NGX D3D12 entry points";
        UnloadRuntime();
        return false;
    }
    if (!InstallCallerShim(error)) { UnloadRuntime(); return false; }

    unsigned long seh = 0;
    const NVSDK_NGX_Result r = SafeInitExt(g_initExt, appDir.c_str(), device, &seh);
    if (seh) { error = StrPrintf("DLSSNR Init_Ext raised exception 0x%08lx", seh); UnloadRuntime(); return false; }
    if (NVSDK_NGX_FAILED(r)) { error = StrPrintf("DLSSNR Init_Ext failed (%s, 0x%08x)", NgxCore::ResultName(r), (unsigned)r); UnloadRuntime(); return false; }
    m_snippetInitialized = true;
    Log::Info("DLSSNR runtime loaded: %s (version %s)", WideToUtf8(dllPath).c_str(), m_runtimeVersion.c_str());
    return true;
}

void DlssnrFeature::UnloadRuntime() {
    if (m_feature) {
        unsigned long seh = 0;
        if (!m_useCore && g_release) SafeRelease(g_release, m_feature, &seh);
        m_feature = nullptr;
    }
    if (m_snippetInitialized && g_shutdown && m_device) {
        unsigned long seh = 0;
        const NVSDK_NGX_Result r = SafeShutdown(g_shutdown, m_device, &seh);
        if (seh) Log::Warn("DLSSNR Shutdown1 raised exception 0x%08lx", seh);
        else if (NVSDK_NGX_FAILED(r)) Log::Warn("DLSSNR Shutdown1 failed (%s)", NgxCore::ResultName(r));
        m_snippetInitialized = false;
    }
    RemoveCallerShim();
    if (m_module) { FreeLibrary(m_module); m_module = nullptr; }
    g_initExt = nullptr; g_create = nullptr; g_evaluate = nullptr; g_release = nullptr; g_shutdown = nullptr;
    m_runtimePath.clear();
    m_runtimeVersion.clear();
}

bool DlssnrFeature::Create(NgxCore& core, ID3D12GraphicsCommandList* cmd, UINT inW, UINT inH, UINT outW, UINT outH,
                           int preset, bool useCore, std::string& error) {
    Release(core);
    if (!core.Initialized()) { error = "NGX core is not initialized"; return false; }
    if (!useCore && !RuntimeLoaded()) { error = "DLSSNR runtime is not loaded"; return false; }

    m_params = core.AllocateParameters(error);
    if (!m_params) return false;

    const CreateValues cv{ inW, inH, outW, outH, preset };
    unsigned long seh = 0;
    if (!SafeSetCreateParams(m_params, &cv, &seh)) {
        error = StrPrintf("DLSSNR parameter setup raised exception 0x%08lx", seh);
        core.DestroyParameters(m_params); m_params = nullptr;
        return false;
    }
    const CreateFeatureFn createFn = useCore ? static_cast<CreateFeatureFn>(&NVSDK_NGX_D3D12_CreateFeature) : g_create;
    const NVSDK_NGX_Result r = SafeCreate(createFn, cmd, m_params, &m_feature, &seh);
    if (seh || NVSDK_NGX_FAILED(r) || !m_feature) {
        error = seh ? StrPrintf("DLSSNR CreateFeature raised exception 0x%08lx", seh)
                    : StrPrintf("DLSSNR CreateFeature failed (%s, 0x%08x)", NgxCore::ResultName(r), (unsigned)r);
        m_feature = nullptr;
        core.DestroyParameters(m_params); m_params = nullptr;
        return false;
    }
    m_useCore = useCore;
    m_inW = inW; m_inH = inH; m_outW = outW; m_outH = outH;
    m_evaluateCount = 0;
    m_failureCount = 0;
    Log::Info("DLSSNR feature created (%s): %ux%u -> %ux%u preset %d", useCore ? "NGX core" : "signed snippet",
              inW, inH, outW, outH, preset);
    return true;
}

void DlssnrFeature::Release(NgxCore& core) {
    if (m_feature) {
        unsigned long seh = 0;
        const ReleaseFeatureFn fn = m_useCore ? static_cast<ReleaseFeatureFn>(&NVSDK_NGX_D3D12_ReleaseFeature) : g_release;
        if (fn) {
            const NVSDK_NGX_Result r = SafeRelease(fn, m_feature, &seh);
            if (seh) Log::Warn("DLSSNR ReleaseFeature raised exception 0x%08lx", seh);
            else if (NVSDK_NGX_FAILED(r)) Log::Warn("DLSSNR ReleaseFeature failed (%s)", NgxCore::ResultName(r));
        }
        m_feature = nullptr;
    }
    if (m_params) { core.DestroyParameters(m_params); m_params = nullptr; }
}

bool DlssnrFeature::Evaluate(ID3D12GraphicsCommandList* cmd, const Inputs& in, const Params& p, std::string& error) {
    if (!m_feature || !m_params) { error = "DLSSNR feature not created"; return false; }
    EvalValues v{};
    v.color = in.color; v.depth = in.depth; v.mvec = in.mvec; v.output = in.output;
    v.inW = m_inW; v.inH = m_inH; v.outW = m_outW; v.outH = m_outH;
    v.reset = in.reset ? 1 : 0;
    v.style = p.style; v.preset = p.preset;
    v.autoMask = p.autoMask ? 1 : 0; v.uiCorrection = p.uiCorrection ? 1 : 0;
    v.intensity = p.intensity; v.globalTone = p.globalTone; v.localTone = p.localTone;
    v.localStructure = p.localStructure; v.skinStructure = p.skinStructure;
    unsigned long seh = 0;
    if (!SafeSetEvalParams(m_params, &v, &seh)) {
        error = StrPrintf("DLSSNR evaluate parameters raised exception 0x%08lx", seh);
        ++m_failureCount;
        return false;
    }
    const EvaluateFeatureFn fn = m_useCore ? static_cast<EvaluateFeatureFn>(&NVSDK_NGX_D3D12_EvaluateFeature) : g_evaluate;
    const NVSDK_NGX_Result r = SafeEvaluate(fn, cmd, m_feature, m_params, &seh);
    ++m_evaluateCount;
    if (seh) { error = StrPrintf("DLSSNR EvaluateFeature raised exception 0x%08lx", seh); ++m_failureCount; return false; }
    if (NVSDK_NGX_FAILED(r)) {
        error = StrPrintf("DLSSNR EvaluateFeature failed (%s, 0x%08x)", NgxCore::ResultName(r), (unsigned)r);
        ++m_failureCount;
        return false;
    }
    return true;
}

} // namespace vdc
