#include "gfx/NvOpticalFlow.h"
#include "core/Log.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace vdc {

namespace {

// --- nvofapi64 D3D11 ABI (adapted from dlss5-bridge, MIT) ---------------------------------------
using NvOfStatus = uint32_t;
constexpr NvOfStatus kNvOfSuccess = 0;
constexpr uint32_t kNvOfApiVersion = 0x20;   // public 2.0 layout (48-byte init params, two-frame execute)

// Enumerations from NVIDIA's public nvOpticalFlowCommon.h (SDK 2.0).
constexpr uint32_t kUsageInput = 1, kUsageOutput = 2, kUsageCost = 4;
constexpr uint32_t kModeOpticalFlow = 1;
constexpr uint32_t kCapsOutputGridSizes = 0, kCapsWidthMin = 4, kCapsHeightMin = 5, kCapsWidthMax = 6, kCapsHeightMax = 7;

struct NvOfInitParams {
    uint32_t width;
    uint32_t height;
    uint32_t outGridSize;
    uint32_t hintGridSize;
    uint32_t mode;             // 1 = optical flow
    uint32_t perfLevel;        // 5 slow, 10 medium, 20 fast
    uint32_t enableExternalHints;
    uint32_t enableOutputCost;
    void*    hPrivData;        // @32
    uint32_t disparityRange;
    uint32_t enableRoi;
};
static_assert(sizeof(NvOfInitParams) == 48, "NvOfInitParams layout");

struct NvOfExecuteInputParams {
    void*    inputFrame;       // current frame
    void*    referenceFrame;   // previous frame
    void*    externalHints;
    uint32_t disableTemporalHints;
    uint32_t padding;
    void*    hPrivData;        // @32
    uint32_t padding2;
    uint32_t numRois;          // @44
    void*    roiData;          // @48
};
static_assert(sizeof(NvOfExecuteInputParams) == 56, "NvOfExecuteInputParams layout");

struct NvOfExecuteOutputParams {
    void* outputBuffer;
    void* outputCostBuffer;
    void* hPrivData;
};

using PfnCreateInstance    = NvOfStatus(__stdcall*)(uint32_t apiVersion, void* functionTable[]);
using PfnMaxApiVersion     = NvOfStatus(__stdcall*)(uint32_t* version);
using PfnCreateSession     = NvOfStatus(__stdcall*)(ID3D11Device*, ID3D11DeviceContext*, void** session);
using PfnInit              = NvOfStatus(__stdcall*)(void* session, const NvOfInitParams*);
using PfnFormatCount       = NvOfStatus(__stdcall*)(void* session, uint32_t usage, uint32_t mode, uint32_t* count);
using PfnFormats           = NvOfStatus(__stdcall*)(void* session, uint32_t usage, uint32_t mode, DXGI_FORMAT* formats);
using PfnRegisterResource  = NvOfStatus(__stdcall*)(void* session, ID3D11Resource*, void** handle);
using PfnUnregister        = NvOfStatus(__stdcall*)(void* handle);
using PfnExecute           = NvOfStatus(__stdcall*)(void* session, const NvOfExecuteInputParams*, NvOfExecuteOutputParams*);
using PfnDestroy           = NvOfStatus(__stdcall*)(void* session);
using PfnGetLastError      = NvOfStatus(__stdcall*)(void* session, char* buffer, uint32_t* size);
using PfnGetCaps           = NvOfStatus(__stdcall*)(void* session, uint32_t cap, uint32_t* values, uint32_t* count);

struct NvOfApi {
    HMODULE             module = nullptr;
    void*               table[64] = {};
    PfnCreateSession    createSession = nullptr;
    PfnInit             init = nullptr;
    PfnFormatCount      formatCount = nullptr;
    PfnFormats          formats = nullptr;
    PfnRegisterResource registerResource = nullptr;
    PfnUnregister       unregister = nullptr;
    PfnExecute          execute = nullptr;
    PfnDestroy          destroy = nullptr;
    PfnGetLastError     getLastError = nullptr;
    PfnGetCaps          getCaps = nullptr;
    uint32_t            maxApiVersion = 0;
    bool                loaded = false;
    bool                attempted = false;
};

NvOfApi g_api;

template <typename T> T Slot(void* p) { T fn = nullptr; std::memcpy(&fn, &p, sizeof(fn)); return fn; }

NvOfStatus SafeCreateInstance(PfnCreateInstance fn, void* table[], unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(kNvOfApiVersion, table); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeMaxApiVersion(PfnMaxApiVersion fn, uint32_t* version, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(version); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeCreateSession(ID3D11Device* dev, ID3D11DeviceContext* ctx, void** session, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.createSession(dev, ctx, session); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeInit(void* session, const NvOfInitParams* p, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.init(session, p); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeFormatCount(void* session, uint32_t usage, uint32_t* count, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.formatCount(session, usage, kModeOpticalFlow, count); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeFormats(void* session, uint32_t usage, DXGI_FORMAT* formats, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.formats(session, usage, kModeOpticalFlow, formats); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeGetCaps(void* session, uint32_t cap, uint32_t* values, uint32_t* count, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.getCaps(session, cap, values, count); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeRegister(void* session, ID3D11Resource* res, void** handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.registerResource(session, res, handle); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeUnregister(void* handle, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.unregister(handle); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeExecute(void* session, const NvOfExecuteInputParams* in, NvOfExecuteOutputParams* out, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.execute(session, in, out); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeDestroy(void* session, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.destroy(session); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}
NvOfStatus SafeGetLastError(void* session, char* buf, uint32_t* size, unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return g_api.getLastError(session, buf, size); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
}

// NV_OF_STATUS names as declared in NVIDIA's public header (SDK 2.0). The number is always printed alongside.
const char* StatusName(NvOfStatus s) {
    switch (s) {
        case 0: return "SUCCESS";
        case 1: return "OF_NOT_AVAILABLE";
        case 2: return "UNSUPPORTED_DEVICE";
        case 3: return "DEVICE_DOES_NOT_EXIST";
        case 4: return "INVALID_PTR";
        case 5: return "INVALID_PARAM";
        case 6: return "INVALID_CALL";
        case 7: return "INVALID_VERSION";
        case 8: return "OUT_OF_MEMORY";
        case 9: return "NOT_INITIALIZED";
        case 10: return "UNSUPPORTED_FEATURE";
        case 11: return "GENERIC";
        case 0x80004005u: return "E_FAIL (unsupported size)";
        case 0xFFFFFFFFu: return "EXCEPTION";
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
    const NvOfStatus st = SafeCreateInstance(createInstance, g_api.table, &seh);
    if (seh || st != kNvOfSuccess) { Log::Warn("NVOF: CreateInstance failed (%s)", StatusText(st, seh).c_str()); return false; }
    g_api.createSession    = Slot<PfnCreateSession>(g_api.table[0]);
    g_api.init             = Slot<PfnInit>(g_api.table[1]);
    g_api.formatCount      = Slot<PfnFormatCount>(g_api.table[2]);
    g_api.formats          = Slot<PfnFormats>(g_api.table[3]);
    g_api.registerResource = Slot<PfnRegisterResource>(g_api.table[4]);
    g_api.unregister       = Slot<PfnUnregister>(g_api.table[5]);
    g_api.execute          = Slot<PfnExecute>(g_api.table[6]);
    g_api.destroy          = Slot<PfnDestroy>(g_api.table[7]);
    g_api.getLastError     = Slot<PfnGetLastError>(g_api.table[8]);
    g_api.getCaps          = Slot<PfnGetCaps>(g_api.table[9]);
    if (!g_api.createSession || !g_api.init || !g_api.registerResource || !g_api.unregister || !g_api.execute || !g_api.destroy) {
        Log::Warn("NVOF: function table incomplete");
        return false;
    }
    g_api.loaded = true;
    if (g_api.maxApiVersion)
        Log::Info("NVOF: nvofapi64.dll loaded (driver supports API %u.%u, using 2.0)", g_api.maxApiVersion >> 4, g_api.maxApiVersion & 0xF);
    else
        Log::Info("NVOF: nvofapi64.dll loaded");
    return true;
}

// Formats the driver accepts for one buffer usage; empty when the query is unavailable.
std::vector<DXGI_FORMAT> QueryFormats(void* session, uint32_t usage) {
    std::vector<DXGI_FORMAT> out;
    if (!g_api.formatCount || !g_api.formats) return out;
    unsigned long seh = 0;
    uint32_t count = 0;
    if (SafeFormatCount(session, usage, &count, &seh) != kNvOfSuccess || seh || count == 0 || count > 32) return out;
    out.assign(count, DXGI_FORMAT_UNKNOWN);
    if (SafeFormats(session, usage, out.data(), &seh) != kNvOfSuccess || seh) out.clear();
    return out;
}

std::vector<uint32_t> QueryCaps(void* session, uint32_t cap) {
    std::vector<uint32_t> out;
    if (!g_api.getCaps) return out;
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

bool Contains(const std::vector<DXGI_FORMAT>& formats, DXGI_FORMAT f) {
    return std::find(formats.begin(), formats.end(), f) != formats.end();
}

} // namespace

bool NvOpticalFlow::LibraryAvailable() { return LoadApi(); }

std::string NvOpticalFlow::LastError() const {
    if (!m_session || !g_api.getLastError) return {};
    char buf[512] = {};
    uint32_t size = sizeof(buf) - 1;
    unsigned long seh = 0;
    if (SafeGetLastError(m_session, buf, &size, &seh) != kNvOfSuccess || seh) return {};
    return std::string(buf, strnlen(buf, sizeof(buf) - 1));
}

bool NvOpticalFlow::EnsureDevice(Device& device, std::string& error) {
    if (m_dev11 && m_ctx11 && m_fence11 && m_fence12) return true;
    m_fence12.Reset(); m_fence11.Reset(); m_ctx11.Reset(); m_dev11.Reset();

    // A native D3D11 device on the same adapter as the D3D12 device: the optical flow driver interface
    // rejects the D3D11On12 mapping layer (UNSUPPORTED_DEVICE).
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(device.Adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
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
    hr = device.D3D12()->OpenSharedHandle(handle, IID_PPV_ARGS(&m_fence12));
    CloseHandle(handle);
    if (FAILED(hr)) { error = "OpenSharedHandle (fence): " + FormatHr(hr); m_fence11.Reset(); m_ctx11.Reset(); m_dev11.Reset(); return false; }
    m_fenceValue = 0;
    Log::Info("NVOF: private D3D11 device ready (feature level %d.%d, shared fence)", (int)(got >> 12) & 0xF, (int)(got >> 8) & 0xF);
    return true;
}

bool NvOpticalFlow::CreateSharedTexture(Device& device, UINT w, UINT h, DXGI_FORMAT fmt, ComPtr<ID3D11Texture2D>& tex11,
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
    hr = device.D3D12()->OpenSharedHandle(handle, IID_PPV_ARGS(&res12));
    CloseHandle(handle);
    if (FAILED(hr)) { error = StrPrintf("open shared %s texture on D3D12: %s", what, FormatHr(hr).c_str()); tex11.Reset(); return false; }
    return true;
}

bool NvOpticalFlow::CreateSessionAndRegister(Device& device, bool withCost, std::string& error) {
    unsigned long seh = 0;
    NvOfStatus st = SafeCreateSession(m_dev11.Get(), m_ctx11.Get(), &m_session, &seh);
    if (seh || st != kNvOfSuccess || !m_session) {
        m_session = nullptr;
        error = StrPrintf("NVOF CreateSession failed (%s)", StatusText(st, seh).c_str());
        return false;
    }

    // Capabilities: the output grid must be one the engine supports (Turing only offers 4, later GPUs 1/2/4).
    const std::vector<uint32_t> grids = QueryCaps(m_session, kCapsOutputGridSizes);
    const uint32_t gridMask = grids.empty() ? 0u : grids[0];
    if (gridMask && !(gridMask & m_grid)) {
        UINT pick = 0;
        for (UINT g : { m_grid * 2, m_grid * 4, m_grid / 2, m_grid / 4 }) {
            if (g >= 1 && g <= 4 && (gridMask & g)) { pick = g; break; }
        }
        if (pick) { Log::Warn("NVOF: output grid %u not supported (mask 0x%x), using grid %u", m_grid, gridMask, pick); m_grid = pick; }
    }
    m_flowW = (m_width + m_grid - 1) / m_grid;
    m_flowH = (m_height + m_grid - 1) / m_grid;
    if (withCost) {
        const auto wMin = QueryCaps(m_session, kCapsWidthMin), hMin = QueryCaps(m_session, kCapsHeightMin);
        const auto wMax = QueryCaps(m_session, kCapsWidthMax), hMax = QueryCaps(m_session, kCapsHeightMax);
        if (!wMin.empty() && !hMin.empty() && !wMax.empty() && !hMax.empty())
            Log::Info("NVOF caps: grids 0x%x, input %ux%u .. %ux%u", gridMask, wMin[0], hMin[0], wMax[0], hMax[0]);
    }

    // Surface formats accepted on this device: logged for diagnostics only. RegisterResource is the authority, so a
    // mismatch here is a warning rather than a failure.
    if (withCost) {
        const std::vector<DXGI_FORMAT> inFmts = QueryFormats(m_session, kUsageInput);
        const std::vector<DXGI_FORMAT> outFmts = QueryFormats(m_session, kUsageOutput);
        const std::vector<DXGI_FORMAT> costFmts = QueryFormats(m_session, kUsageCost);
        Log::Info("NVOF formats: input [%s] flow [%s] cost [%s]", FormatList(inFmts).c_str(), FormatList(outFmts).c_str(), FormatList(costFmts).c_str());
        if (!inFmts.empty() && !Contains(inFmts, m_inputFormat))
            Log::Warn("NVOF: input format %d is not in the driver's list (%s)", (int)m_inputFormat, FormatList(inFmts).c_str());
        if (!costFmts.empty() && !Contains(costFmts, DXGI_FORMAT_R8_UINT))
            Log::Warn("NVOF: cost format R8_UINT is not in the driver's list (%s)", FormatList(costFmts).c_str());
    }

    NvOfInitParams ip{};
    ip.width = m_width;
    ip.height = m_height;
    ip.outGridSize = m_grid;
    ip.hintGridSize = m_grid;
    ip.mode = kModeOpticalFlow;
    ip.perfLevel = m_perf;
    ip.enableExternalHints = 0;
    ip.enableOutputCost = withCost ? 1u : 0u;
    st = SafeInit(m_session, &ip, &seh);
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
    if (!CreateSharedTexture(device, m_width, m_height, m_inputFormat, m_sharedInput11, m_sharedInput12, "input", error)) return false;

    D3D11_TEXTURE2D_DESC fd = td;
    fd.Width = m_flowW; fd.Height = m_flowH;
    fd.Format = DXGI_FORMAT_R16G16_SINT;
    HRESULT hr = m_dev11->CreateTexture2D(&fd, nullptr, m_flow.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { error = "NVOF flow texture: " + FormatHr(hr); return false; }
    st = SafeRegister(m_session, m_flow.Get(), &m_flowHandle, &seh);
    if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(flow) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str()); return false; }
    if (!CreateSharedTexture(device, m_flowW, m_flowH, DXGI_FORMAT_R16G16_SINT, m_sharedFlow11, m_sharedFlow12, "flow", error)) return false;

    D3D11_TEXTURE2D_DESC cd = fd;
    cd.Format = DXGI_FORMAT_R8_UINT;
    if (withCost) {
        hr = m_dev11->CreateTexture2D(&cd, nullptr, m_cost.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { error = "NVOF cost texture: " + FormatHr(hr); return false; }
        st = SafeRegister(m_session, m_cost.Get(), &m_costHandle, &seh);
        if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(cost) failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str()); return false; }
        if (!CreateSharedTexture(device, m_flowW, m_flowH, DXGI_FORMAT_R8_UINT, m_sharedCost11, m_sharedCost12, "cost", error)) return false;
    }

    if (m_wantBidirectional) {
        // Second output pair for the backward pass (previous -> current). Optional: the session keeps working without it.
        std::string berr;
        bool ok = SUCCEEDED(hr = m_dev11->CreateTexture2D(&fd, nullptr, m_flowBack.ReleaseAndGetAddressOf()));
        if (!ok) berr = "flow texture: " + FormatHr(hr);
        if (ok) {
            st = SafeRegister(m_session, m_flowBack.Get(), &m_flowBackHandle, &seh);
            ok = !seh && st == kNvOfSuccess;
            if (!ok) berr = StrPrintf("RegisterResource(flow) %s", StatusText(st, seh).c_str());
        }
        if (ok) ok = CreateSharedTexture(device, m_flowW, m_flowH, DXGI_FORMAT_R16G16_SINT, m_sharedFlowBack11, m_sharedFlowBack12, "backward flow", berr);
        if (ok && withCost) {
            ok = SUCCEEDED(hr = m_dev11->CreateTexture2D(&cd, nullptr, m_costBack.ReleaseAndGetAddressOf()));
            if (!ok) berr = "cost texture: " + FormatHr(hr);
            if (ok) {
                st = SafeRegister(m_session, m_costBack.Get(), &m_costBackHandle, &seh);
                ok = !seh && st == kNvOfSuccess;
                if (!ok) berr = StrPrintf("RegisterResource(cost) %s", StatusText(st, seh).c_str());
            }
            if (ok) ok = CreateSharedTexture(device, m_flowW, m_flowH, DXGI_FORMAT_R8_UINT, m_sharedCostBack11, m_sharedCostBack12, "backward cost", berr);
        }
        if (!ok) {
            Log::Warn("NVOF: backward pass unavailable (%s); using forward flow only", berr.c_str());
            if (m_flowBackHandle) { SafeUnregister(m_flowBackHandle, &seh); m_flowBackHandle = nullptr; }
            if (m_costBackHandle) { SafeUnregister(m_costBackHandle, &seh); m_costBackHandle = nullptr; }
            m_sharedFlowBack12.Reset(); m_sharedCostBack12.Reset();
            m_sharedFlowBack11.Reset(); m_sharedCostBack11.Reset();
            m_flowBack.Reset();
            m_costBack.Reset();
        }
    }
    return true;
}

bool NvOpticalFlow::Init(Device& device, UINT width, UINT height, DXGI_FORMAT inputFormat, UINT grid, UINT perfLevel, bool bidirectional, std::string& error) {
    ReleaseSession();
    if (!LoadApi()) { error = "nvofapi64.dll unavailable"; return false; }
    if (!width || !height) { error = "invalid size"; return false; }
    if (!EnsureDevice(device, error)) return false;
    if (grid != 1 && grid != 2 && grid != 4) grid = 2;
    if (perfLevel != 5 && perfLevel != 10 && perfLevel != 20) perfLevel = 10;
    m_inputFormat = inputFormat;
    m_wantBidirectional = bidirectional;

    auto configure = [&]() {
        m_width = width; m_height = height; m_grid = grid; m_perf = perfLevel;
        m_flowW = (width + grid - 1) / grid;
        m_flowH = (height + grid - 1) / grid;
        m_executeCount = 0;
    };
    configure();
    std::string err1;
    if (CreateSessionAndRegister(device, true, err1)) {
        Log::Info("NVOF session ready: %ux%u fmt %d grid %u perf %u -> %ux%u vectors (cost enabled%s)", width, height, (int)inputFormat, m_grid, perfLevel,
                  m_flowW, m_flowH, Bidirectional() ? ", bidirectional" : "");
        return true;
    }
    Log::Warn("NVOF: init with cost output failed (%s), retrying without cost", err1.c_str());
    ReleaseSession();
    configure();
    if (CreateSessionAndRegister(device, false, error)) {
        Log::Info("NVOF session ready: %ux%u fmt %d grid %u perf %u -> %ux%u vectors (no cost%s)", width, height, (int)inputFormat, m_grid, perfLevel,
                  m_flowW, m_flowH, Bidirectional() ? ", bidirectional" : "");
        return true;
    }
    ReleaseSession();
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
}

void NvOpticalFlow::Shutdown() {
    ReleaseSession();
    m_fence12.Reset();
    m_fence11.Reset();
    if (m_ctx11) { m_ctx11->ClearState(); m_ctx11->Flush(); }
    m_ctx11.Reset();
    m_dev11.Reset();
}

bool NvOpticalFlow::Execute(Device& device, bool resetHints, std::string& error) {
    error.clear();
    if (!Ready()) { error = "NVOF session not ready"; return false; }
    const int cur = (int)(m_executeCount & 1);

    // 1. The optical flow device waits (on the GPU) for the D3D12 queue, which has just received the input copy.
    const UINT64 inputReady = ++m_fenceValue;
    HRESULT hr = device.Queue()->Signal(m_fence12.Get(), inputReady);
    if (FAILED(hr)) { error = "NVOF fence signal (D3D12): " + FormatHr(hr); return false; }
    hr = m_ctx11->Wait(m_fence11.Get(), inputReady);
    if (FAILED(hr)) { error = "NVOF fence wait (D3D11): " + FormatHr(hr); return false; }

    // 2. Fetch the frame into the registered input and run the engine.
    m_ctx11->CopyResource(m_inputs[cur].Get(), m_sharedInput11.Get());
    bool ok = true;
    const bool primed = m_executeCount > 0;
    if (primed) {
        NvOfExecuteInputParams in{};
        in.inputFrame = m_inputHandles[cur];
        in.referenceFrame = m_inputHandles[1 - cur];
        in.externalHints = nullptr;
        // Temporal hints reuse the previous result; they only make sense for a continuous, single-direction sequence.
        in.disableTemporalHints = (resetHints || m_flowBackHandle || m_executeCount < 2) ? 1u : 0u;
        NvOfExecuteOutputParams out{};
        out.outputBuffer = m_flowHandle;
        out.outputCostBuffer = m_costHandle;
        unsigned long seh = 0;
        NvOfStatus st = SafeExecute(m_session, &in, &out, &seh);
        if (seh || st != kNvOfSuccess) {
            ok = false;
            error = StrPrintf("NVOF Execute failed (%s) %s", StatusText(st, seh).c_str(), LastError().c_str());
        } else if (m_flowBackHandle) {
            // Backward pass: previous -> current, used for the forward/backward consistency check.
            NvOfExecuteInputParams bin = in;
            bin.inputFrame = m_inputHandles[1 - cur];
            bin.referenceFrame = m_inputHandles[cur];
            NvOfExecuteOutputParams bout{};
            bout.outputBuffer = m_flowBackHandle;
            bout.outputCostBuffer = m_costBackHandle;
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
    if (SUCCEEDED(hr)) hr = device.Queue()->Wait(m_fence12.Get(), outputReady);
    if (FAILED(hr)) {
        if (ok) error = "NVOF fence publish: " + FormatHr(hr);
        return false;
    }
    return ok && primed;
}

} // namespace vdc
