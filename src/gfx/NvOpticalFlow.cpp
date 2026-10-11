// VRChat DLSS5 Cam - NVIDIA Optical Flow provider (nvofapi64.dll, D3D12 interface).
// Interface headers: third_party/nvof (NVIDIA Optical Flow SDK 5.0, MIT). The runtime nvofapi64.dll is part of the NVIDIA driver.
#include "gfx/NvOpticalFlow.h"
#include "core/Log.h"
#include <nvOpticalFlowD3D12.h>
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
constexpr uint32_t kApi50 = 0x50;   // layout of the bundled headers: forward + backward flow in a single pass, fence points on execute

static_assert(NV_OF_API_VERSION == kApi50, "bundled NVOF headers must be API 5.0");
static_assert(sizeof(NV_OF_INIT_PARAMS) == 64, "NV_OF_INIT_PARAMS layout");
static_assert(sizeof(NV_OF_FENCE_POINT) == 16, "NV_OF_FENCE_POINT layout");
static_assert(sizeof(NV_OF_REGISTER_RESOURCE_PARAMS_D3D12) == 48, "NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 layout");
static_assert(sizeof(NV_OF_EXECUTE_INPUT_PARAMS_D3D12) == 72, "NV_OF_EXECUTE_INPUT_PARAMS_D3D12 layout");
static_assert(sizeof(NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12) == 64, "NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 layout");
static_assert(sizeof(NV_OF_D3D12_API_FUNCTION_LIST) == 10 * sizeof(void*), "NV_OF_D3D12_API_FUNCTION_LIST layout");

using PfnCreateInstance = NV_OF_STATUS(NVOFAPI*)(uint32_t apiVer, NV_OF_D3D12_API_FUNCTION_LIST* functionList);
using PfnMaxApiVersion  = NV_OF_STATUS(NVOFAPI*)(uint32_t* version);

struct NvOfApi {
    HMODULE  module = nullptr;
    struct { NV_OF_D3D12_API_FUNCTION_LIST list; void* spare[54]; } table{};   // slack in case a driver fills a longer list
    uint32_t apiVersion = 0;      // version the function table was created for
    uint32_t maxApiVersion = 0;   // reported by the driver (0 = unknown)
    bool     loaded = false;
    bool     attempted = false;
};

NvOfApi g_api;

inline NvOFHandle          Session(void* p) { return static_cast<NvOFHandle>(p); }
inline NvOFGPUBufferHandle Buffer(void* p)  { return static_cast<NvOFGPUBufferHandle>(p); }

NvOfStatus SafeCreateInstance(PfnCreateInstance fn, uint32_t version, NV_OF_D3D12_API_FUNCTION_LIST* list, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)fn(version, list); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeMaxApiVersion(PfnMaxApiVersion fn, uint32_t* version, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)fn(version); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeCreateSession(ID3D12Device* dev, void** session, unsigned long* seh) noexcept {
    *seh = 0;
    *session = nullptr;
    VDC_SEH_TRY {
        NvOFHandle h = nullptr;
        const NvOfStatus st = (NvOfStatus)g_api.table.list.nvCreateOpticalFlowD3D12(dev, &h);
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
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFGetSurfaceFormatCountD3D12(Session(session), usage, NV_OF_MODE_OPTICALFLOW, count); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeFormats(void* session, NV_OF_BUFFER_USAGE usage, DXGI_FORMAT* formats, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFGetSurfaceFormatD3D12(Session(session), usage, NV_OF_MODE_OPTICALFLOW, formats); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeGetCaps(void* session, NV_OF_CAPS cap, uint32_t* values, uint32_t* count, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFGetCaps(Session(session), cap, values, count); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeRegister(void* session, NV_OF_REGISTER_RESOURCE_PARAMS_D3D12* params, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFRegisterResourceD3D12(Session(session), params); }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeUnregister(void* handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY {
        NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 params{};
        params.hOFGpuBuffer = Buffer(handle);
        return (NvOfStatus)g_api.table.list.nvOFUnregisterResourceD3D12(&params);
    }
    VDC_SEH_EXCEPT(*seh) { return kNvOfException; }
}
NvOfStatus SafeExecute(void* session, const NV_OF_EXECUTE_INPUT_PARAMS_D3D12* in, NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12* out, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return (NvOfStatus)g_api.table.list.nvOFExecuteD3D12(Session(session), in, out); }
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
    auto createInstance = reinterpret_cast<PfnCreateInstance>(GetProcAddress(g_api.module, "NvOFAPICreateInstanceD3D12"));
    if (!createInstance) { Log::Warn("NVOF: NvOFAPICreateInstanceD3D12 export missing"); return false; }
    unsigned long seh = 0;
    if (auto maxVersion = reinterpret_cast<PfnMaxApiVersion>(GetProcAddress(g_api.module, "NvOFGetMaxSupportedApiVersion"))) {
        uint32_t v = 0;
        if (SafeMaxApiVersion(maxVersion, &v, &seh) == kNvOfSuccess && !seh) g_api.maxApiVersion = v;
    }
    // The D3D12 execute parameters (fence points) are driven in their API 5.0 layout only; an older driver gets the FSR flow instead.
    if (g_api.maxApiVersion && g_api.maxApiVersion < kApi50) {
        Log::Warn("NVOF: the driver's optical flow API %u.%u is older than 5.0; the FSR optical flow is used instead", g_api.maxApiVersion >> 4, g_api.maxApiVersion & 0xF);
        return false;
    }
    std::memset(&g_api.table, 0, sizeof(g_api.table));
    const NvOfStatus st = SafeCreateInstance(createInstance, kApi50, &g_api.table.list, &seh);
    if (seh || st != kNvOfSuccess) {
        Log::Warn("NVOF: CreateInstance(API 5.0, D3D12) failed (%s)", StatusText(st, seh).c_str());
        return false;
    }
    g_api.apiVersion = kApi50;
    const NV_OF_D3D12_API_FUNCTION_LIST& api = g_api.table.list;
    if (!api.nvCreateOpticalFlowD3D12 || !api.nvOFInit || !api.nvOFRegisterResourceD3D12 || !api.nvOFUnregisterResourceD3D12 ||
        !api.nvOFExecuteD3D12 || !api.nvOFDestroy) {
        Log::Warn("NVOF: function table incomplete");
        return false;
    }
    g_api.loaded = true;
    if (g_api.maxApiVersion)
        Log::Info("NVOF: nvofapi64.dll loaded (driver supports API %u.%u, using %u.%u, D3D12)", g_api.maxApiVersion >> 4, g_api.maxApiVersion & 0xF,
                  g_api.apiVersion >> 4, g_api.apiVersion & 0xF);
    else
        Log::Info("NVOF: nvofapi64.dll loaded (using API %u.%u, D3D12)", g_api.apiVersion >> 4, g_api.apiVersion & 0xF);
    return true;
}

// Formats the driver accepts for one buffer usage; empty when the query is unavailable.
std::vector<DXGI_FORMAT> QueryFormats(void* session, NV_OF_BUFFER_USAGE usage) {
    std::vector<DXGI_FORMAT> out;
    if (!g_api.table.list.nvOFGetSurfaceFormatCountD3D12 || !g_api.table.list.nvOFGetSurfaceFormatD3D12) return out;
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
    if (SafeGetLastError(m_session, buf, &size, &seh) != kNvOfSuccess || seh || !buf[0]) return {};
    return std::string("- ") + buf;
}

bool NvOpticalFlow::EnsureFence(GpuContext& gpu, std::string& error) {
    if (m_fence) return true;
    // Shareable so that the engine, which runs outside the D3D12 queue, can signal and wait on it.
    const HRESULT hr = gpu.Dev().D3D12()->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) { m_fence.Reset(); error = "NVOF fence: " + FormatHr(hr); return false; }
    m_fence->SetName(L"nvofFence");
    m_fenceValue = 0;
    m_engineValue = 0;
    if (!m_fenceEvent) m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return true;
}

bool NvOpticalFlow::CreateTexture(GpuContext& gpu, UINT w, UINT h, DXGI_FORMAT fmt, ComPtr<ID3D12Resource>& res, const wchar_t* name, const char* what, std::string& error) {
    res.Reset();
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    const HRESULT hr = gpu.Dev().D3D12()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&res));
    if (FAILED(hr)) { res.Reset(); error = StrPrintf("NVOF %s texture (format %d): %s", what, (int)fmt, FormatHr(hr).c_str()); return false; }
    res->SetName(name);
    return true;
}

// Registers a texture with the session. The engine may prepare the texture on the GPU: it is told to start after everything the
// fence has been asked for so far and to signal the next value when done; the caller drains the fence before the first frame.
bool NvOpticalFlow::Register(ID3D12Resource* res, void** handle, const char* what, std::string& error) {
    *handle = nullptr;
    NvOFGPUBufferHandle h = nullptr;
    NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 rp{};
    rp.resource = res;
    rp.inputFencePoint.fence = m_fence.Get();
    rp.inputFencePoint.value = m_fenceValue;
    rp.hOFGpuBuffer = &h;
    rp.outputFencePoint.fence = m_fence.Get();
    rp.outputFencePoint.value = m_fenceValue + 1;
    unsigned long seh = 0;
    const NvOfStatus st = SafeRegister(m_session, &rp, &seh);
    if (seh || st != kNvOfSuccess || !h) {
        error = StrPrintf("NVOF RegisterResource(%s) failed (%s) %s", what, StatusText(st, seh).c_str(), LastError().c_str());
        return false;
    }
    *handle = h;
    m_fenceValue = rp.outputFencePoint.value;
    m_engineValue = m_fenceValue;
    return true;
}

// Creates a session and initialises it for the configured size, grid and performance level; caps and formats are logged
// when verbose.
bool NvOpticalFlow::CreateAndInitSession(GpuContext& gpu, bool withCost, bool bidirectional, bool verbose, std::string& error) {
    unsigned long seh = 0;
    NvOfStatus st = SafeCreateSession(gpu.Dev().D3D12(), &m_session, &seh);
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
    // The backward flow comes out of the same pass (predDirection BOTH); a driver that refuses gets a second Execute per frame.
    m_twoPass = false;
    ip.predDirection = bidirectional ? NV_OF_PRED_DIRECTION_BOTH : NV_OF_PRED_DIRECTION_FORWARD;
    st = SafeInit(m_session, &ip, &seh);
    if ((seh || st != kNvOfSuccess) && bidirectional) {
        Log::Warn("NVOF: single-pass bidirectional flow rejected (%s); computing the backward flow in a second pass", StatusText(st, seh).c_str());
        ip.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
        m_twoPass = true;
        st = SafeInit(m_session, &ip, &seh);
    }
    if (seh || st != kNvOfSuccess) {
        error = StrPrintf("NVOF Init failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str());
        return false;
    }
    return true;
}

bool NvOpticalFlow::CreateSessionAndRegister(GpuContext& gpu, bool withCost, bool bidirectional, bool verbose, std::string& error) {
    const UINT requestedGrid = m_grid;
    if (TakeParked(withCost, bidirectional)) {
        m_flowW = (m_width + m_grid - 1) / m_grid;
        m_flowH = (m_height + m_grid - 1) / m_grid;
        if (verbose) Log::Info("NVOF: reusing a parked session (%u left parked)", (unsigned)m_parked.size());
    } else if (!CreateAndInitSession(gpu, withCost, bidirectional, verbose, error)) {
        return false;
    }
    m_key = Parked{ nullptr, m_width, m_height, requestedGrid, m_grid, m_perf, m_inputFormat, withCost, bidirectional, m_twoPass };

    // The textures live on the program's D3D12 device and are registered with the session as they are; the pipeline copies
    // frames into the inputs and reads the results directly.
    for (int i = 0; i < 2; ++i) {
        if (!CreateTexture(gpu, m_width, m_height, m_inputFormat, m_inputs[i], i ? L"nvofInput1" : L"nvofInput0", "input", error)) return false;
        if (!Register(m_inputs[i].Get(), &m_inputHandles[i], "input", error)) return false;
    }
    if (!CreateTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R16G16_SINT, m_flow, L"nvofFlow", "flow", error)) return false;
    if (!Register(m_flow.Get(), &m_flowHandle, "flow", error)) return false;
    if (withCost) {
        if (!CreateTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R8_UINT, m_cost, L"nvofCost", "cost", error)) return false;
        if (!Register(m_cost.Get(), &m_costHandle, "cost", error)) return false;
    }
    if (bidirectional) {
        if (!CreateTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R16G16_SINT, m_flowBack, L"nvofFlowBack", "backward flow", error)) return false;
        if (!Register(m_flowBack.Get(), &m_flowBackHandle, "backward flow", error)) return false;
        if (withCost) {
            if (!CreateTexture(gpu, m_flowW, m_flowH, DXGI_FORMAT_R8_UINT, m_costBack, L"nvofCostBack", "backward cost", error)) return false;
            if (!Register(m_costBack.Get(), &m_costBackHandle, "backward cost", error)) return false;
        }
    }
    // Whatever the engine did at registration is finished before the pipeline touches the textures.
    const ULONGLONG t0 = GetTickCount64();
    WaitForEngine();
    if (verbose) Log::Info("NVOF: registration fence %llu/%llu after %llu ms", (unsigned long long)m_fence->GetCompletedValue(), (unsigned long long)m_engineValue, (unsigned long long)(GetTickCount64() - t0));
    return true;
}

bool NvOpticalFlow::Init(GpuContext& gpu, UINT width, UINT height, DXGI_FORMAT inputFormat, UINT grid, UINT perfLevel, bool bidirectional, std::string& error) {
    ReleaseSession();
    if (!LoadApi()) { error = "nvofapi64.dll unavailable"; return false; }
    if (!width || !height) { error = "invalid size"; return false; }
    if (!EnsureFence(gpu, error)) return false;
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
            Log::Info("NVOF session ready (API %u.%u, D3D12): %ux%u fmt %d grid %u perf %u -> %ux%u vectors, cost %s, %s", g_api.apiVersion >> 4, g_api.apiVersion & 0xF,
                      width, height, (int)inputFormat, m_grid, perfLevel, m_flowW, m_flowH, HasCost() ? "enabled" : "disabled",
                      Bidirectional() ? (m_twoPass ? "bidirectional (two passes)" : "bidirectional (single pass)") : "forward only");
            return true;
        }
    }
    ReleaseSession();
    error = err;
    return false;
}

// Blocks until the engine has signalled everything it was asked to (bounded, in case a failed pass never signals).
void NvOpticalFlow::WaitForEngine() {
    if (!m_fence || m_fence->GetCompletedValue() >= m_engineValue) return;
    if (!m_fenceEvent) m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent && SUCCEEDED(m_fence->SetEventOnCompletion(m_engineValue, m_fenceEvent))) WaitForSingleObject(m_fenceEvent, 2000);
}

// Takes a parked session whose parameters match the ones configured for the next Init(); returns false when there is none.
bool NvOpticalFlow::TakeParked(bool withCost, bool bidirectional) {
    for (size_t i = 0; i < m_parked.size(); ++i) {
        const Parked& p = m_parked[i];
        if (p.width != m_width || p.height != m_height || p.grid != m_grid || p.perf != m_perf || p.format != m_inputFormat ||
            p.cost != withCost || p.bidirectional != bidirectional) continue;
        m_session = p.session;
        m_grid = p.effectiveGrid;
        m_twoPass = p.twoPass;
        m_parked.erase(m_parked.begin() + (ptrdiff_t)i);
        return true;
    }
    return false;
}

// Unregisters the textures and parks the session. The session is not destroyed: after nvOFDestroy the driver reads freed memory
// on the next submission to the graphics queue (on the worker thread after a batch, on the main thread at exit), whatever was
// drained, flushed or kept alive before the call. Unregistering and releasing the textures is fine, so a parked session costs
// only the driver's own state, and the driver reclaims it when the process ends.
void NvOpticalFlow::ReleaseSession() {
    // The caller idles the D3D12 queue before rebuilding; the engine is drained here before its textures go away.
    WaitForEngine();
    unsigned long seh = 0;
    auto unregister = [&](void*& h) { if (h) { SafeUnregister(h, &seh); h = nullptr; } };
    unregister(m_costBackHandle);
    unregister(m_flowBackHandle);
    unregister(m_costHandle);
    unregister(m_flowHandle);
    for (void*& h : m_inputHandles) unregister(h);
    if (m_session) {
        Parked p = m_key;
        p.session = m_session;
        p.twoPass = m_twoPass;
        m_parked.push_back(p);
        m_session = nullptr;
    }
    m_costBack.Reset(); m_flowBack.Reset();
    m_cost.Reset(); m_flow.Reset();
    for (auto& t : m_inputs) t.Reset();
    m_twoPass = false;
    m_executeCount = 0;
}

// Ends the session but keeps the fence: the driver goes on referencing the fence from the graphics queue after the session is
// destroyed, and releasing it here crashed inside the driver on the next frame. It lives until the wrapper is destroyed, and a
// later Init() continues its values.
void NvOpticalFlow::Shutdown() {
    ReleaseSession();
}

NvOpticalFlow::~NvOpticalFlow() {
    ReleaseSession();   // the parked sessions stay with the driver, see ReleaseSession
    m_fence.Reset();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
}

bool NvOpticalFlow::Execute(GpuContext& gpu, bool resetHints, std::string& error) {
    error.clear();
    if (!Ready()) { error = "NVOF session not ready"; return false; }
    const int cur = static_cast<int>(m_executeCount & 1);
    const bool primed = m_executeCount > 0;   // the first call only fills the reference frame
    bool ok = true;

    if (primed) {
        // 1. The engine starts once the queue has passed the copy of the current frame - which also puts it behind the previous
        //    frame's reads of the result textures.
        const UINT64 inputReady = ++m_fenceValue;
        HRESULT hr = gpu.Queue()->Signal(m_fence.Get(), inputReady);
        if (FAILED(hr)) { error = "NVOF fence signal: " + FormatHr(hr); return false; }
        NV_OF_FENCE_POINT waitFor{ m_fence.Get(), inputReady };
        NV_OF_FENCE_POINT done{ m_fence.Get(), m_fenceValue + 1 };

        // 2. Forward flow (current -> previous), with the backward flow in the same pass when the session has it.
        NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{};
        in.inputFrame = Buffer(m_inputHandles[cur]);
        in.referenceFrame = Buffer(m_inputHandles[1 - cur]);
        in.externalHints = nullptr;
        // Temporal hints reuse the previous result: off on a history reset, in the two-pass scheme (the hints would belong to the other
        // direction) and on the first pass, which has no previous result.
        in.disableTemporalHints = (resetHints || m_twoPass || m_executeCount < 2) ? NV_OF_TRUE : NV_OF_FALSE;
        in.hPrivData = nullptr;
        in.numFencePoints = 1;
        in.fencePoint = &waitFor;
        NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{};
        out.outputBuffer = Buffer(m_flowHandle);
        out.outputCostBuffer = Buffer(m_costHandle);
        if (!m_twoPass) {
            out.bwdOutputBuffer = Buffer(m_flowBackHandle);
            out.bwdOutputCostBuffer = Buffer(m_costBackHandle);
        }
        out.fencePoint = &done;
        unsigned long seh = 0;
        NvOfStatus st = SafeExecute(m_session, &in, &out, &seh);
        if (seh || st != kNvOfSuccess) {
            error = StrPrintf("NVOF Execute failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str());
            ok = false;
        } else {
            m_fenceValue = done.value;
            m_engineValue = done.value;
            if (m_twoPass && m_flowBackHandle) {
                // Backward flow (previous -> current) as a second pass, ordered behind the first through its fence point.
                NV_OF_FENCE_POINT waitBack = done;
                NV_OF_FENCE_POINT doneBack{ m_fence.Get(), m_fenceValue + 1 };
                NV_OF_EXECUTE_INPUT_PARAMS_D3D12 inB = in;
                inB.inputFrame = Buffer(m_inputHandles[1 - cur]);
                inB.referenceFrame = Buffer(m_inputHandles[cur]);
                inB.fencePoint = &waitBack;
                NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 outB{};
                outB.outputBuffer = Buffer(m_flowBackHandle);
                outB.outputCostBuffer = Buffer(m_costBackHandle);
                outB.fencePoint = &doneBack;
                st = SafeExecute(m_session, &inB, &outB, &seh);
                if (seh || st != kNvOfSuccess) {
                    error = StrPrintf("NVOF Execute(backward) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str());
                    ok = false;
                } else {
                    m_fenceValue = doneBack.value;
                    m_engineValue = doneBack.value;
                }
            }
        }

        // 3. The queue continues once the engine has signalled, so the next command list reads this frame's vectors.
        if (ok) {
            hr = gpu.Queue()->Wait(m_fence.Get(), m_engineValue);
            if (FAILED(hr)) { error = "NVOF fence wait: " + FormatHr(hr); ok = false; }
        }
    }

    ++m_executeCount;
    return ok && primed;
}

} // namespace vdc
