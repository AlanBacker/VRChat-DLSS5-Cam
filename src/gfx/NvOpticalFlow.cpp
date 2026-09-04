#include "gfx/NvOpticalFlow.h"
#include "core/Log.h"
#include <cstring>

namespace vdc {

namespace {

// --- nvofapi64 D3D11 ABI (adapted from dlss5-bridge, MIT) ---------------------------------------
using NvOfStatus = uint32_t;
constexpr NvOfStatus kNvOfSuccess = 0;
constexpr uint32_t kNvOfApiVersion = 0x20;

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
using PfnCreateSession     = NvOfStatus(__stdcall*)(ID3D11Device*, ID3D11DeviceContext*, void** session);
using PfnInit              = NvOfStatus(__stdcall*)(void* session, const NvOfInitParams*);
using PfnRegisterResource  = NvOfStatus(__stdcall*)(void* session, ID3D11Resource*, void** handle);
using PfnUnregister        = NvOfStatus(__stdcall*)(void* handle);
using PfnExecute           = NvOfStatus(__stdcall*)(void* session, const NvOfExecuteInputParams*, NvOfExecuteOutputParams*);
using PfnDestroy           = NvOfStatus(__stdcall*)(void* session);
using PfnGetLastError      = NvOfStatus(__stdcall*)(void* session, char* buffer, uint32_t* size);

struct NvOfApi {
    HMODULE            module = nullptr;
    void*              table[64] = {};
    PfnCreateSession   createSession = nullptr;
    PfnInit            init = nullptr;
    PfnRegisterResource registerResource = nullptr;
    PfnUnregister      unregister = nullptr;
    PfnExecute         execute = nullptr;
    PfnDestroy         destroy = nullptr;
    PfnGetLastError    getLastError = nullptr;
    bool               loaded = false;
    bool               attempted = false;
};

NvOfApi g_api;

template <typename T> T Slot(void* p) { T fn = nullptr; std::memcpy(&fn, &p, sizeof(fn)); return fn; }

NvOfStatus SafeCreateInstance(PfnCreateInstance fn, void* table[], unsigned long* seh) noexcept {
    *seh = 0;
    VDC_SEH_TRY { return fn(kNvOfApiVersion, table); }
    VDC_SEH_EXCEPT(*seh) { return 0xFFFFFFFFu; }
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
    const NvOfStatus st = SafeCreateInstance(createInstance, g_api.table, &seh);
    if (seh || st != kNvOfSuccess) { Log::Warn("NVOF: CreateInstance failed (status %u, seh 0x%08lx)", st, seh); return false; }
    g_api.createSession    = Slot<PfnCreateSession>(g_api.table[0]);
    g_api.init             = Slot<PfnInit>(g_api.table[1]);
    g_api.registerResource = Slot<PfnRegisterResource>(g_api.table[4]);
    g_api.unregister       = Slot<PfnUnregister>(g_api.table[5]);
    g_api.execute          = Slot<PfnExecute>(g_api.table[6]);
    g_api.destroy          = Slot<PfnDestroy>(g_api.table[7]);
    g_api.getLastError     = Slot<PfnGetLastError>(g_api.table[8]);
    if (!g_api.createSession || !g_api.init || !g_api.registerResource || !g_api.unregister || !g_api.execute || !g_api.destroy) {
        Log::Warn("NVOF: function table incomplete");
        return false;
    }
    g_api.loaded = true;
    Log::Info("NVOF: nvofapi64.dll loaded");
    return true;
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
        case 9: return "NOT_SUPPORTED";
        case 10: return "GENERIC";
        case 0x80004005u: return "E_FAIL (unsupported size)";
        default: return "UNKNOWN";
    }
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

bool NvOpticalFlow::CreateSessionAndRegister(Device& device, bool withCost, std::string& error) {
    unsigned long seh = 0;
    NvOfStatus st = SafeCreateSession(device.D3D11(), device.Context11(), &m_session, &seh);
    if (seh || st != kNvOfSuccess || !m_session) {
        m_session = nullptr;
        error = StrPrintf("NVOF CreateSession failed (%s, seh 0x%08lx)", StatusName(st), seh);
        return false;
    }
    NvOfInitParams ip{};
    ip.width = m_width;
    ip.height = m_height;
    ip.outGridSize = m_grid;
    ip.hintGridSize = m_grid;
    ip.mode = 1;
    ip.perfLevel = m_perf;
    ip.enableExternalHints = 0;
    ip.enableOutputCost = withCost ? 1u : 0u;
    st = SafeInit(m_session, &ip, &seh);
    if (seh || st != kNvOfSuccess) {
        error = StrPrintf("NVOF Init failed (%s, seh 0x%08lx) %s", StatusName(st), seh, LastError().c_str());
        return false;
    }
    ID3D11Device* dev = device.D3D11();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = m_width; td.Height = m_height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = m_inputFormat;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; ++i) {
        HRESULT hr = dev->CreateTexture2D(&td, nullptr, m_inputs[i].ReleaseAndGetAddressOf());
        if (FAILED(hr)) { error = "NVOF input texture: " + FormatHr(hr); return false; }
        st = SafeRegister(m_session, m_inputs[i].Get(), &m_inputHandles[i], &seh);
        if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(input) failed (%s)", StatusName(st)); return false; }
    }
    D3D11_TEXTURE2D_DESC fd = td;
    fd.Width = m_flowW; fd.Height = m_flowH;
    fd.Format = DXGI_FORMAT_R16G16_SINT;
    HRESULT hr = dev->CreateTexture2D(&fd, nullptr, m_flow.ReleaseAndGetAddressOf());
    if (FAILED(hr)) { error = "NVOF flow texture: " + FormatHr(hr); return false; }
    st = SafeRegister(m_session, m_flow.Get(), &m_flowHandle, &seh);
    if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(flow) failed (%s)", StatusName(st)); return false; }
    if (withCost) {
        D3D11_TEXTURE2D_DESC cd = fd;
        cd.Format = DXGI_FORMAT_R8_UINT;
        hr = dev->CreateTexture2D(&cd, nullptr, m_cost.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { error = "NVOF cost texture: " + FormatHr(hr); return false; }
        st = SafeRegister(m_session, m_cost.Get(), &m_costHandle, &seh);
        if (seh || st != kNvOfSuccess) { error = StrPrintf("NVOF RegisterResource(cost) failed (%s)", StatusName(st)); return false; }
    }
    return true;
}

bool NvOpticalFlow::Init(Device& device, UINT width, UINT height, DXGI_FORMAT inputFormat, UINT grid, UINT perfLevel, std::string& error) {
    Shutdown();
    if (!LoadApi()) { error = "nvofapi64.dll unavailable"; return false; }
    if (!width || !height) { error = "invalid size"; return false; }
    if (grid != 1 && grid != 2 && grid != 4) grid = 2;
    if (perfLevel != 5 && perfLevel != 10 && perfLevel != 20) perfLevel = 10;
    m_inputFormat = inputFormat;
    m_width = width; m_height = height; m_grid = grid; m_perf = perfLevel;
    m_flowW = (width + grid - 1) / grid;
    m_flowH = (height + grid - 1) / grid;
    m_executeCount = 0;

    std::string err1;
    if (CreateSessionAndRegister(device, true, err1)) {
        Log::Info("NVOF session ready: %ux%u fmt %d grid %u perf %u (cost enabled)", width, height, (int)inputFormat, grid, perfLevel);
        return true;
    }
    Log::Warn("NVOF: init with cost output failed (%s), retrying without cost", err1.c_str());
    Shutdown();
    m_width = width; m_height = height; m_grid = grid; m_perf = perfLevel;
    m_flowW = (width + grid - 1) / grid;
    m_flowH = (height + grid - 1) / grid;
    if (CreateSessionAndRegister(device, false, error)) {
        Log::Info("NVOF session ready: %ux%u grid %u perf %u (no cost)", width, height, grid, perfLevel);
        return true;
    }
    Shutdown();
    return false;
}

void NvOpticalFlow::Shutdown() {
    unsigned long seh = 0;
    for (int i = 0; i < 2; ++i) {
        if (m_inputHandles[i]) { SafeUnregister(m_inputHandles[i], &seh); m_inputHandles[i] = nullptr; }
        m_inputs[i].Reset();
    }
    if (m_flowHandle) { SafeUnregister(m_flowHandle, &seh); m_flowHandle = nullptr; }
    if (m_costHandle) { SafeUnregister(m_costHandle, &seh); m_costHandle = nullptr; }
    m_flow.Reset();
    m_cost.Reset();
    if (m_session) {
        const NvOfStatus st = SafeDestroy(m_session, &seh);
        if (seh || st != kNvOfSuccess) Log::Warn("NVOF Destroy failed (%s, seh 0x%08lx)", StatusName(st), seh);
        m_session = nullptr;
    }
    m_width = m_height = m_flowW = m_flowH = 0;
}

bool NvOpticalFlow::Execute(int currentIndex, std::string& error) {
    if (!m_session) { error = "NVOF session not ready"; return false; }
    const int cur = currentIndex & 1;
    NvOfExecuteInputParams in{};
    in.inputFrame = m_inputHandles[cur];
    in.referenceFrame = m_inputHandles[1 - cur];
    in.externalHints = nullptr;
    in.disableTemporalHints = 1;
    NvOfExecuteOutputParams out{};
    out.outputBuffer = m_flowHandle;
    out.outputCostBuffer = m_costHandle;
    unsigned long seh = 0;
    const NvOfStatus st = SafeExecute(m_session, &in, &out, &seh);
    ++m_executeCount;
    if (seh || st != kNvOfSuccess) {
        error = StrPrintf("NVOF Execute failed (%s, seh 0x%08lx) %s", StatusName(st), seh, LastError().c_str());
        return false;
    }
    return true;
}

} // namespace vdc
