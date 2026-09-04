// VRChat DLSS5 Cam - NVIDIA Optical Flow provider (nvofapi64.dll, D3D11 interface).
// Interface headers: third_party/nvof (NVIDIA Optical Flow SDK 5.0, MIT). The runtime nvofapi64.dll is part of the NVIDIA driver.
#include "gfx/NvOpticalFlow.h"
#include "core/Log.h"
#include <nvOpticalFlowD3D11.h>
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace vdc {

namespace {

// NV_OF_STATUS widened so that a structured exception raised inside the driver can be reported as its own value.
using NvOfStatus = uint32_t;
constexpr NvOfStatus kNvOfSuccess = NV_OF_SUCCESS;
constexpr NvOfStatus kNvOfException = 0xFFFFFFFFu;
constexpr uint32_t kApi50 = 0x50;   // layout of the bundled headers: forward + backward flow in a single pass
constexpr uint32_t kApi20 = 0x20;   // oldest layout driven here (48-byte init params, forward flow only)

static_assert(NV_OF_API_VERSION == kApi50, "bundled NVOF headers must be API 5.0");
static_assert(sizeof(NV_OF_INIT_PARAMS) == 64, "NV_OF_INIT_PARAMS layout");
static_assert(sizeof(NV_OF_EXECUTE_INPUT_PARAMS) == 56, "NV_OF_EXECUTE_INPUT_PARAMS layout");
static_assert(sizeof(NV_OF_EXECUTE_OUTPUT_PARAMS) == 48, "NV_OF_EXECUTE_OUTPUT_PARAMS layout");
static_assert(sizeof(NV_OF_D3D11_API_FUNCTION_LIST) == 10 * sizeof(void*), "NV_OF_D3D11_API_FUNCTION_LIST layout");

using PfnCreateInstance = NV_OF_STATUS(NVOFAPI*)(uint32_t apiVer, NV_OF_D3D11_API_FUNCTION_LIST* functionList);
using PfnMaxApiVersion  = NV_OF_STATUS(NVOFAPI*)(uint32_t* version);

struct NvOfApi {
    HMODULE  module = nullptr;
    struct { NV_OF_D3D11_API_FUNCTION_LIST list; void* spare[54]; } table{};   // slack in case a driver fills a longer list
    uint32_t apiVersion = 0;      // version the function table was created for
    uint32_t maxApiVersion = 0;   // reported by the driver (0 = unknown)
    bool     loaded = false;
    bool     attempted = false;
};

NvOfApi g_api;

inline NvOFHandle          Session(void* p) { return static_cast<NvOFHandle>(p); }
inline NvOFGPUBufferHandle Buffer(void* p)  { return static_cast<NvOFGPUBufferHandle>(p); }

NvOfStatus SafeCreateInstance(PfnCreateInstance fn, uint32_t version, NV_OF_D3D11_API_FUNCTION_LIST* list, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)fn(version, list); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeMaxApiVersion(PfnMaxApiVersion fn, uint32_t* version, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)fn(version); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeCreateSession(ID3D11Device* dev, ID3D11DeviceContext* ctx, void** session, unsigned long* seh) noexcept {
    *seh = 0;
    *session = nullptr;
    VDC_SEH_TRY {
        NvOFHandle h = nullptr;
        const NvOfStatus st = (NvOfStatus)g_api.table.list.nvCreateOpticalFlowD3D11(dev, ctx, &h);
        *session = h;
        return st;
    }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeInit(void* session, const NV_OF_INIT_PARAMS* p, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFInit(Session(session), p); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeFormatCount(void* session, NV_OF_BUFFER_USAGE usage, uint32_t* count, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFGetSurfaceFormatCountD3D11(Session(session), usage, NV_OF_MODE_OPTICALFLOW, count); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeFormats(void* session, NV_OF_BUFFER_USAGE usage, DXGI_FORMAT* formats, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFGetSurfaceFormatD3D11(Session(session), usage, NV_OF_MODE_OPTICALFLOW, formats); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeGetCaps(void* session, NV_OF_CAPS cap, uint32_t* values, uint32_t* count, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFGetCaps(Session(session), cap, values, count); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeRegister(void* session, ID3D11Resource* res, void** handle, unsigned long* seh) noexcept {
    *seh = 0;
    *handle = nullptr;
    VDC_SEH_TRY {
        NvOFGPUBufferHandle h = nullptr;
        const NvOfStatus st = (NvOfStatus)g_api.table.list.nvOFRegisterResourceD3D11(Session(session), res, &h);
        *handle = h;
        return st;
    }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeUnregister(void* handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFUnregisterResourceD3D11(Buffer(handle)); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeExecute(void* session, const NV_OF_EXECUTE_INPUT_PARAMS* in, NV_OF_EXECUTE_OUTPUT_PARAMS* out, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFExecute(Session(session), in, out); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeDestroy(void* session, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFDestroy(Session(session)); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeGetLastError(void* session, char* buf, uint32_t* size, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFGetLastError(Session(session), buf, size); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}

// NV_OF_STATUS names; the number is always printed alongside.
const char* StatusName(NvOfStatus s) {
    switch (s) {
        case NV_OF_SUCCESS: return "SUCCESS";
        case NV_OF_ERR_OF_NOT_AVAILABLE: return "OF_NOT_AVAILABLE";
        case NV_OF_ERR_UNSUPPORTED_DEVICE: return "UNSUPPORTED_DEVICE";
        case NV_OF_ERR_DEVICE_DOES_NOT_EXIST: return "DEVICE_DOES_NOT_EXIST";
        case NV_OF_ERR_INVALID_PTR: return "INVALID_PTR";
        case NV_OF_ERR_INVALID_PARAM: return "INVALID_PARAM";
        case NV_OF_ERR_INVALID_CALL: return "INVALID_CALL";
        case NV_OF_ERR_INVALID_VERSION: return "INVALID_VERSION";
        case NV_OF_ERR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case NV_OF_ERR_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case NV_OF_ERR_UNSUPPORTED_FEATURE: return "UNSUPPORTED_FEATURE";
        case NV_OF_ERR_GENERIC: return "GENERIC";
        case 0x80004005u: return "E_FAIL (unsupported size)";
        case kNvOfException: return "EXCEPTION";
        default: return "UNKNOWN";
    }
}

std::string StatusText(NvOfStatus s, unsigned long seh) {
    if (seh) return StrPrintf("exception 0x%08lx", seh);
    return StrPrintf("status %u %s", s, StatusName(s));
}

bool LoadApi() {
    if (g_api.attempted) return g_api.loaded;
    g_api.attempted = true;
    g_api.module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_api.module) g_api.module = LoadLibraryW(L"nvofapi64.dll");
    if (!g_api.module) { Log::Info("NVOF: nvofapi64.dll not available (%s)", LastErrorText().c_str()); return false; }
    auto createInstance = reinterpret_cast<PfnCreateInstance>(GetProcAddress(g_api.module, "NvOFAPICreateInstanceD3D11"));
    if (!createInstance) { Log::Warn("NVOF: NvOFAPICreateInstanceD3D11 export missing"); return false; }
    unsigned long seh = 0;
    if (auto maxVersion = reinterpret_cast<PfnMaxApiVersion>(GetProcAddress(g_api.module, "NvOFGetMaxSupportedApiVersion"))) {
        uint32_t v = 0;
        if (SafeMaxApiVersion(maxVersion, &v, &seh) == kNvOfSuccess && !seh) g_api.maxApiVersion = v;
    }
    // API 5.0 (single-pass bidirectional flow) when the driver offers it, otherwise the 2.0 layout.
    for (uint32_t version : { kApi50, kApi20 }) {
        if (version == kApi50 && g_api.maxApiVersion && g_api.maxApiVersion < kApi50) continue;
        std::memset(&g_api.table, 0, sizeof(g_api.table));
        const NvOfStatus st = SafeCreateInstance(createInstance, version, &g_api.table.list, &seh);
        if (!seh && st == kNvOfSuccess) { g_api.apiVersion = version; break; }
        Log::Warn("NVOF: CreateInstance(API %u.%u) failed (%s)", version >> 4, version & 0xF, StatusText(st, seh).c_str());
    }
    if (!g_api.apiVersion) return false;
    const NV_OF_D3D11_API_FUNCTION_LIST& api = g_api.table.list;
    if (!api.nvCreateOpticalFlowD3D11 || !api.nvOFInit || !api.nvOFRegisterResourceD3D11 || !api.nvOFUnregisterResourceD3D11 ||
        !api.nvOFExecute || !api.nvOFDestroy) {
        Log::Warn("NVOF: function table incomplete");
        return false;
    }
    g_api.loaded = true;
    if (g_api.maxApiVersion)
        Log::Info("NVOF: nvofapi64.dll loaded (driver supports API %u.%u, using %u.%u)", g_api.maxApiVersion >> 4, g_api.maxApiVersion & 0xF,
                  g_api.apiVersion >> 4, g_api.apiVersion & 0xF);
    else
        Log::Info("NVOF: nvofapi64.dll loaded (using API %u.%u)", g_api.apiVersion >> 4, g_api.apiVersion & 0xF);
    return true;
}

// Formats the driver accepts for one buffer usage; empty when the query is unavailable.
std::vector<DXGI_FORMAT> QueryFormats(void* session, NV_OF_BUFFER_USAGE usage) {
    std::vector<DXGI_FORMAT> out;
    if (!g_api.table.list.nvOFGetSurfaceFormatCountD3D11 || !g_api.table.list.nvOFGetSurfaceFormatD3D11) return out;
    unsigned long seh = 0;
    uint32_t count = 0;
    if (SafeFormatCount(session, usage, &count, &seh) != kNvOfSuccess || seh || count == 0 || count > 32) return out;
    out.assign(count, DXGI_FORMAT_UNKNOWN);
    if (SafeFormats(session, usage, out.data(), &seh) != kNvOfSuccess || seh) out.clear();
    return out;
}

// Capability values: a list (for example the supported grid sizes 1/2/4, one entry each), empty when unavailable.
std::vector<uint32_t> QueryCaps(void* session, NV_OF_CAPS cap) {
    std::vector<uint32_t> out;
    if (!g_api.table.list.nvOFGetCaps) return out;
    unsigned long seh = 0;
    uint32_t count = 0;
    if (SafeGetCaps(session, cap, nullptr, &count, &seh) != kNvOfSuccess || seh || count == 0 || count > 16) return out;
    out.assign(count, 0u);
    if (SafeGetCaps(session, cap, out.data(), &count, &seh) != kNvOfSuccess || seh) out.clear();
    return out;
}

std::string FormatList(const std::vector<DXGI_FORMAT>& formats) {
    std::string s;
    for (DXGI_FORMAT f : formats) { if (!s.empty()) s += ' '; s += std::to_string((int)f); }
    return s.empty() ? "?" : s;
}

std::string ValueList(const std::vector<uint32_t>& values) {
    std::string s;
    for (uint32_t v : values) { if (!s.empty()) s += ' '; s += std::to_string(v); }
    return s.empty() ? "?" : s;
}

bool Contains(const std::vector<DXGI_FORMAT>& formats, DXGI_FORMAT f) {
    return std::find(formats.begin(), formats.end(), f) != formats.end();
}

} // namespace

bool NvOpticalFlow::LibraryAvailable() { return LoadApi(); }

UINT NvOpticalFlow::ApiVersion() { return g_api.apiVersion; }

std::string NvOpticalFlow::LastError() const {
    if (!m_session || !g_api.table.list.nvOFGetLastError) return {};
    char buf[512] = {};
    uint32_t size = sizeof(buf) - 1;
    unsigned long seh = 0;
    if (SafeGetLastError(m_session, buf, &size, &seh) != kNvOfSuccess || seh) return {};
    return std::string(buf, strnlen(buf, sizeof(buf) - 1));
}

bool NvOpticalFlow::EnsureDevice(GpuContext& gpu, std::string& error) {
    if (m_dev11 && m_ctx11 && m_fence11 && m_fence12) return true;
    m_fence12.Reset(); m_fence11.Reset(); m_ctx11.Reset(); m_dev11.Reset();

    // A native D3D11 device on the same adapter as the D3D12 device: the optical flow driver interface
    // rejects the D3D11On12 mapping layer (UNSUPPORTED_DEVICE).
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(gpu.Dev().Adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                                   D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (FAILED(hr)) { error = "D3D11CreateDevice (optical flow device): " + FormatHr(hr); return false; }
    if (FAILED(dev.As(&m_dev11)) || FAILED(ctx.As(&m_ctx11))) {
        m_ctx11.Reset(); m_dev11.Reset();
        error = "the optical flow device does not support D3D11.4 fences";
        return false;
    }

    // Shared fence: created on D3D11, opened on D3D12, so the two devices can order their work on the GPU.
    hr = m_dev11->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_fence11));
    if (FAILED(hr)) { error = "CreateFence (shared): " + FormatHr(hr); m_ctx11.Reset(); m_dev11.Reset(); return false; }
    HANDLE handle = nullptr;
    hr = m_fence11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle);
    if (FAILED(hr)) { error = "fence CreateSharedHandle: " + FormatHr(hr); m_fence11.Reset(); m_ctx11.Reset(); m_dev11.Reset(); return false; }
    hr = gpu.Dev().D3D12()->OpenSharedHandle(handle, IID_PPV_ARGS(&m_fence12));
    CloseHandle(handle);
    if (FAILED(hr)) { error = "OpenSharedHandle (fence): " + FormatHr(hr); m_fence11.Reset(); m_ctx11.Reset(); m_dev11.Reset(); return false; }
    m_fenceValue = 0;
    Log::Info("NVOF: private D3D11 device ready (feature level %d.%d, shared fence)", (int)(got >> 12) & 0xF, (int)(got >> 8) & 0xF);
    return true;
}

bool NvOpticalFlow::CreateSharedTexture(GpuContext& gpu, UINT w, UINT h, DXGI_FORMAT fmt, ComPtr<ID3D11Texture2D>& tex11,
                                        ComPtr<ID3D12Resource>& res12, const char* what, std::string& error) {
    res12.Reset();
    tex11.Reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    HRESULT hr = m_dev11->CreateTexture2D(&td, nullptr, &tex11);
    if (FAILED(hr)) { error = StrPrintf("shared %s texture (format %d): %s", what, (int)fmt, FormatHr(hr).c_str()); return false; }
    ComPtr<IDXGIResource1> dxgi;
    HANDLE handle = nullptr;
    hr = tex11.As(&dxgi);
    if (SUCCEEDED(hr)) hr = dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle);
    if (FAILED(hr)) { error = StrPrintf("shared %s texture handle: %s", what, FormatHr(hr).c_str()); tex11.Reset(); return false; }
    hr = gpu.Dev().D3D12()->OpenSharedHandle(handle, IID_PPV_ARGS(&res12));
    CloseHandle(handle);
    if (FAILED(hr)) { error = StrPrintf("open shared %s texture on D3D12: %s", what, FormatHr(hr).c_str()); tex11.Reset(); return false; }
    return true;
}

bool NvOpticalFlow::CreateSessionAndRegister(GpuContext& gpu, bool withCost, bool bidirectional, bool verbose, std::string& error) {
    unsigned long seh = 0;
    NvOfStatus st = SafeCreateSession(m_dev11.Get(), m_ctx11.Get(), &m_session, &seh);
    if (seh || st != kNvOfSuccess || !m_session) {
        m_session = nullptr;
        error = StrPrintf("NVOF CreateSession failed (%s)", StatusText(st, seh).c_str());
        return false;
    }

    // Capabilities. nvOFGetCaps returns the supported output grid sizes as a list (1, 2 and/or 4), not a mask.
    const std::vector<uint32_t> grids = QueryCaps(m_session, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES);
    uint32_t gridMask = 0;
    for (uint32_t g : grids) if (g == 1 || g == 2 || g == 4) gridMask |= g;
    if (gridMask && !(gridMask & m_grid)) {
        UINT pick = 0;
        for (UINT g : { m_grid * 2, m_grid * 4, m_grid / 2, m_grid / 4 }) {
            if (g >= 1 && g <= 4 && (gridMask & g)) { pick = g; break; }
        }
        if (pick) { Log::Warn("NVOF: output grid %u not supported (driver offers [%s]), using grid %u", m_grid, ValueList(grids).c_str(), pick); m_grid = pick; }
    }
    m_flowW = (m_width + m_grid - 1) / m_grid;
    m_flowH = (m_height + m_grid - 1) / m_grid;
    if (verbose) {
        const auto wMin = QueryCaps(m_session, NV_OF_CAPS_WIDTH_MIN), hMin = QueryCaps(m_session, NV_OF_CAPS_HEIGHT_MIN);
        const auto wMax = QueryCaps(m_session, NV_OF_CAPS_WIDTH_MAX), hMax = QueryCaps(m_session, NV_OF_CAPS_HEIGHT_MAX);
        if (!wMin.empty() && !hMin.empty() && !wMax.empty() && !hMax.empty())
            Log::Info("NVOF caps: grids [%s], input %ux%u .. %ux%u", ValueList(grids).c_str(), wMin[0], hMin[0], wMax[0], hMax[0]);
        else
            Log::Info("NVOF caps: grids [%s]", ValueList(grids).c_str());

        // Surface formats accepted on this device: logged for diagnostics only. RegisterResource is the authority, so a
        // mismatch here is a warning rather than a failure.
        const std::vector<DXGI_FORMAT> inFmts = QueryFormats(m_session, NV_OF_BUFFER_USAGE_INPUT);
        const std::vector<DXGI_FORMAT> outFmts = QueryFormats(m_session, NV_OF_BUFFER_USAGE_OUTPUT);
        const std::vector<DXGI_FORMAT> costFmts = QueryFormats(m_session, NV_OF_BUFFER_USAGE_COST);
        Log::Info("NVOF formats: input [%s] flow [%s] cost [%s]", FormatList(inFmts).c_str(), FormatList(outFmts).c_str(), FormatList(costFmts).c_str());
        if (!inFmts.empty() && !Contains(inFmts, m_inputFormat))
            Log::Warn("NVOF: input format %d is not in the driver's list (%s)", (int)m_inputFormat, FormatList(inFmts).c_str());
        if (!costFmts.empty() && !Contains(costFmts, DXGI_FORMAT_R8_UINT))
            Log::Warn("NVOF: cost format R8_UINT is not in the driver's list (%s)", FormatList(costFmts).c_str());
    }

    NV_OF_INIT_PARAMS ip{};
    ip.width = m_width;
    ip.height = m_height;
    ip.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(m_grid);
    ip.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;   // external hints are not used
    ip.mode = NV_OF_MODE_OPTICALFLOW;
    ip.perfLevel = static_cast<NV_OF_PERF_LEVEL>(m_perf);
    ip.enableExternalHints = NV_OF_FALSE;
    ip.enableOutputCost = withCost ? NV_OF_TRUE : NV_OF_FALSE;
    ip.hPrivData = nullptr;
    ip.disparityRange = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
    ip.enableRoi = NV_OF_FALSE;
    ip.enableGlobalFlow = NV_OF_FALSE;
    ip.inputBufferFormat = (m_inputFormat == DXGI_FORMAT_R8_UNORM) ? NV_OF_BUFFER_FORMAT_GRAYSCALE8 : NV_OF_BUFFER_FORMAT_ABGR8;
    // API 5.0 computes the backward flow in the same pass (predDirection BOTH); the 2.0 layout needs a second Execute per frame.
    const bool singlePass = bidirectional && g_api.apiVersion >= kApi50;
    m_twoPass = bidirectional && !singlePass;
    ip.predDirection = singlePass ? NV_OF_PRED_DIRECTION_BOTH : NV_OF_PRED_DIRECTION_FORWARD;
    st = SafeInit(m_session, &ip, &seh);
    if ((seh || st != kNvOfSuccess) && singlePass) {
        Log::Warn("NVOF: single-pass bidirectional flow rejected (%s); computing the backward flow in a second pass", StatusText(st, seh).c_str());
        ip.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
        m_twoPass = true;
        st = SafeInit(m_session, &ip, &seh);
    }
    if (seh || st != kNvOfSuccess) {
        error = StrPrintf("NVOF Init failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str());
        return false;
    }

    // Textures registered with the session are plain D3D11 textures; the shared ones carry frames and results across devices.
    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_width; td.Height = m_height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = m_inputFormat;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; ++i) {
        HRESULT hr = m_dev11->CreateTexture2D(&td, nullptr, m_inputs[i].ReleaseAndGetAddressOf());
        if (FAILED(hr)) { error = "NVOF input texture: " + FormatHr(hr); return false; }
        st = SafeRegister(m_session, m_inputs[i].Get(), &m_inputHandles[i], &seh);
        if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(input) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str()); return false; }
    }
    if (!CreateSharedTexture(gpu, m_width, m_height, m_inputFormat, m_sharedInput11, m_sharedInput12, "input", error)) return false;

    D3D11_TEXTURE2D_DESC fd = td;
    fd.Width = m_flowW; fd.Height = m_flowH;
    fd.Format = DXGI_FORMAT_R16G16_SINT;
    HRESULT hr = m_dev11->CreateTexture2D(&fd, nullptr, m_flow.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { error = "NVOF flow texture: " + FormatHr(hr); return false; }
    st = SafeRegister(m_session, m_flow.Get(), &m_flowHandle, &seh);
    if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(flow) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str()); return false; }
    if (!CreateSharedTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R16G16_SINT, m_sharedFlow11, m_sharedFlow12, "flow", error)) return false;

    D3D11_TEXTURE2D_DESC cd = fd;
    cd.Format = DXGI_FORMAT_R8_UINT;
    if (withCost) {
        hr = m_dev11->CreateTexture2D(&cd, nullptr, m_cost.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { error = "NVOF cost texture: " + FormatHr(hr); return false; }
        st = SafeRegister(m_session, m_cost.Get(), &m_costHandle, &seh);
        if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(cost) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str()); return false; }
        if (!CreateSharedTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R8_UINT, m_sharedCost11, m_sharedCost12, "cost", error)) return false;
    }

    if (bidirectional) {
        // Second output pair for the backward flow (previous -> current), used by the forward/backward consistency check.
        hr = m_dev11->CreateTexture2D(&fd, nullptr, m_flowBack.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { error = "NVOF backward flow texture: " + FormatHr(hr); return false; }
        st = SafeRegister(m_session, m_flowBack.Get(), &m_flowBackHandle, &seh);
        if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(backward flow) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str()); return false; }
        if (!CreateSharedTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R16G16_SINT, m_sharedFlowBack11, m_sharedFlowBack12, "backward flow", error)) return false;
        if (withCost) {
            hr = m_dev11->CreateTexture2D(&cd, nullptr, m_costBack.ReleaseAndGetAddressOf());
            if (FAILED(hr)) { error = "NVOF backward cost texture: " + FormatHr(hr); return false; }
            st = SafeRegister(m_session, m_costBack.Get(), &m_costBackHandle, &seh);
            if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(backward cost) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str()); return false; }
            if (!CreateSharedTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R8_UINT, m_sharedCostBack11, m_sharedCostBack12, "backward cost", error)) return false;
        }
    }
    return true;
}

bool NvOpticalFlow::Init(GpuContext& gpu, UINT width, UINT height, DXGI_FORMAT inputFormat, UINT grid, UINT perfLevel, bool bidirectional, std::string& error) {
    ReleaseSession();
    if (!LoadApi()) { error = "nvofapi64.dll unavailable"; return false; }
    if (!width || !height) { error = "invalid size"; return false; }
    if (!EnsureDevice(gpu, error)) return false;
    if (grid != 1 && grid != 2 && grid != 4) grid = 4;
    if (perfLevel != 5 && perfLevel != 10 && perfLevel != 20) perfLevel = 10;
    m_inputFormat = inputFormat;
    m_wantBidirectional = bidirectional;

    auto configure = [&]() {
        m_width = width; m_height = height; m_grid = grid; m_perf = perfLevel;
        m_flowW = (width + grid - 1) / grid;
        m_flowH = (height + grid - 1) / grid;
        m_executeCount = 0;
        m_twoPass = false;
    };
    auto describe = [](bool cost, bool bidir) { return StrPrintf("%s cost%s", cost ? "with" : "without", bidir ? ", bidirectional" : ""); };

    // Attempts in order of preference: the cost map and the backward flow are both optional.
    std::vector<std::pair<bool, bool>> attempts = { { true, bidirectional }, { false, bidirectional } };
    if (bidirectional) { attempts.push_back({ true, false }); attempts.push_back({ false, false }); }
    std::string err;
    for (size_t i = 0; i < attempts.size(); ++i) {
        const bool cost = attempts[i].first, bidir = attempts[i].second;
        if (i) {
            Log::Warn("NVOF: init %s failed (%s), retrying %s", describe(attempts[i - 1].first, attempts[i - 1].second).c_str(), err.c_str(), describe(cost, bidir).c_str());
            ReleaseSession();
        }
        configure();
        if (CreateSessionAndRegister(gpu, cost, bidir, i == 0, err)) {
            Log::Info("NVOF session ready (API %u.%u): %ux%u fmt %d grid %u perf %u -> %ux%u vectors, cost %s, %s", g_api.apiVersion >> 4, g_api.apiVersion & 0xF,
                      width, height, (int)inputFormat, m_grid, perfLevel, m_flowW, m_flowH, HasCost() ? "enabled" : "disabled",
                      Bidirectional() ? (m_twoPass ? "bidirectional (two passes)" : "bidirectional (single pass)") : "forward only");
            return true;
        }
    }
    ReleaseSession();
    error = err;
    return false;
}

void NvOpticalFlow::WaitForD3D11() {
    if (!m_ctx11) return;
    if (m_fence11) {
        const UINT64 v = ++m_fenceValue;
        if (SUCCEEDED(m_ctx11->Signal(m_fence11.Get(), v))) {
            m_ctx11->Flush();
            if (m_fence11->GetCompletedValue() < v) {
                HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (ev) {
                    if (SUCCEEDED(m_fence11->SetEventOnCompletion(v, ev))) WaitForSingleObject(ev, 2000);
                    CloseHandle(ev);
                }
            }
            return;
        }
    }
    m_ctx11->Flush();
    ComPtr<ID3D11Query> query;
    D3D11_QUERY_DESC qd{};
    qd.Query = D3D11_QUERY_EVENT;
    if (m_dev11 && SUCCEEDED(m_dev11->CreateQuery(&qd, &query))) {
        m_ctx11->End(query.Get());
        for (int i = 0; i < 2000; ++i) {
            BOOL done = FALSE;
            if (m_ctx11->GetData(query.Get(), &done, sizeof(done), 0) == S_OK) break;
            Sleep(1);
        }
    }
}

void NvOpticalFlow::ReleaseSession() {
    // The caller idles the D3D12 queue before rebuilding; the optical flow device is drained here.
    WaitForD3D11();
    unsigned long seh = 0;
    for (int i = 0; i < 2; ++i) {
        if (m_inputHandles[i]) { SafeUnregister(m_inputHandles[i], &seh); m_inputHandles[i] = nullptr; }
    }
    if (m_flowHandle) { SafeUnregister(m_flowHandle, &seh); m_flowHandle = nullptr; }
    if (m_costHandle) { SafeUnregister(m_costHandle, &seh); m_costHandle = nullptr; }
    if (m_flowBackHandle) { SafeUnregister(m_flowBackHandle, &seh); m_flowBackHandle = nullptr; }
    if (m_costBackHandle) { SafeUnregister(m_costBackHandle, &seh); m_costBackHandle = nullptr; }
    if (m_session) {
        const NvOfStatus st = SafeDestroy(m_session, &seh);
        if (seh || st != kNvOfSuccess) Log::Warn("NVOF Destroy failed (%s)", StatusText(st, seh).c_str());
        m_session = nullptr;
    }
    // D3D12 views first, then the D3D11 textures that own the memory.
    m_sharedInput12.Reset(); m_sharedFlow12.Reset(); m_sharedCost12.Reset(); m_sharedFlowBack12.Reset(); m_sharedCostBack12.Reset();
    m_sharedInput11.Reset(); m_sharedFlow11.Reset(); m_sharedCost11.Reset(); m_sharedFlowBack11.Reset(); m_sharedCostBack11.Reset();
    for (auto& t : m_inputs) t.Reset();
    m_flow.Reset();
    m_cost.Reset();
    m_flowBack.Reset();
    m_costBack.Reset();
    m_width = m_height = m_flowW = m_flowH = 0;
    m_executeCount = 0;
    m_twoPass = false;
}

void NvOpticalFlow::Shutdown() {
    ReleaseSession();
    m_fence12.Reset();
    m_fence11.Reset();
    if (m_ctx11) { m_ctx11->ClearState(); m_ctx11->Flush(); }
    m_ctx11.Reset();
    m_dev11.Reset();
}

bool NvOpticalFlow::Execute(GpuContext& gpu, bool resetHints, std::string& error) {
    error.clear();
    if (!Ready()) { error = "NVOF session not ready"; return false; }
    const int cur = (int)(m_executeCount & 1);

    // 1. The optical flow device waits (on the GPU) for the D3D12 queue, which has just received the input copy.
    const UINT64 inputReady = ++m_fenceValue;
    HRESULT hr = gpu.Queue()->Signal(m_fence12.Get(), inputReady);
    if (FAILED(hr)) { error = "NVOF fence signal (D3D12): " + FormatHr(hr); return false; }
    hr = m_ctx11->Wait(m_fence11.Get(), inputReady);
    if (FAILED(hr)) { error = "NVOF fence wait (D3D11): " + FormatHr(hr); return false; }

    // 2. Fetch the frame into the registered input and run the engine.
    m_ctx11->CopyResource(m_inputs[cur].Get(), m_sharedInput11.Get());
    bool ok = true;
    const bool primed = m_executeCount > 0;
    if (primed) {
        NV_OF_EXECUTE_INPUT_PARAMS in{};
        in.inputFrame = Buffer(m_inputHandles[cur]);
        in.referenceFrame = Buffer(m_inputHandles[1 - cur]);
        in.externalHints = nullptr;
        // Temporal hints reuse the previous result. With a separate backward pass the alternating directions would poison them.
        in.disableTemporalHints = (resetHints || m_twoPass || m_executeCount < 2) ? NV_OF_TRUE : NV_OF_FALSE;
        NV_OF_EXECUTE_OUTPUT_PARAMS out{};
        out.outputBuffer = Buffer(m_flowHandle);
        out.outputCostBuffer = Buffer(m_costHandle);
        if (!m_twoPass) {
            // API 5.0 single pass: the backward flow (previous -> current) lands in the bwd buffers of the same call.
            out.bwdOutputBuffer = Buffer(m_flowBackHandle);
            out.bwdOutputCostBuffer = Buffer(m_costBackHandle);
        }
        unsigned long seh = 0;
        NvOfStatus st = SafeExecute(m_session, &in, &out, &seh);
        if (seh || st != kNvOfSuccess) {
            ok = false;
            error = StrPrintf("NVOF Execute failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str());
        } else if (m_twoPass && m_flowBackHandle) {
            // Backward pass on the 2.0 layout: previous -> current, used for the forward/backward consistency check.
            NV_OF_EXECUTE_INPUT_PARAMS bin = in;
            bin.inputFrame = Buffer(m_inputHandles[1 - cur]);
            bin.referenceFrame = Buffer(m_inputHandles[cur]);
            NV_OF_EXECUTE_OUTPUT_PARAMS bout{};
            bout.outputBuffer = Buffer(m_flowBackHandle);
            bout.outputCostBuffer = Buffer(m_costBackHandle);
            st = SafeExecute(m_session, &bin, &bout, &seh);
            if (seh || st != kNvOfSuccess) {
                ok = false;
                error = StrPrintf("NVOF Execute (backward) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str());
            }
        }
        if (ok) {
            m_ctx11->CopyResource(m_sharedFlow11.Get(), m_flow.Get());
            if (m_sharedCost11) m_ctx11->CopyResource(m_sharedCost11.Get(), m_cost.Get());
            if (m_sharedFlowBack11) m_ctx11->CopyResource(m_sharedFlowBack11.Get(), m_flowBack.Get());
            if (m_sharedCostBack11) m_ctx11->CopyResource(m_sharedCostBack11.Get(), m_costBack.Get());
        }
    }
    ++m_executeCount;

    // 3. Publish: the D3D12 queue continues once the optical flow device has finished.
    const UINT64 outputReady = ++m_fenceValue;
    hr = m_ctx11->Signal(m_fence11.Get(), outputReady);
    m_ctx11->Flush();
    if (SUCCEEDED(hr)) hr = gpu.Queue()->Wait(m_fence12.Get(), outputReady);
    if (FAILED(hr)) {
        if (ok) error = "NVOF fence publish: " + FormatHr(hr);
        return false;
    }
    return ok && primed;
}

} // namespace vdc
