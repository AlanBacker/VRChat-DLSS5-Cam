#include "gfx/FsrHost.h"
#include "core/Log.h"
#include "core/Util.h"
#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/dx12/ffx_api_dx12.h>
#include <psapi.h>
#include <algorithm>
#include <cwctype>
#include <vector>

namespace vdc {
namespace {

constexpr wchar_t kDllName[] = L"amd_fidelityfx_dx12.dll";

const char* ReturnText(ffxReturnCode_t rc) {
    switch (rc) {
        case FFX_API_RETURN_OK:                     return "ok";
        case FFX_API_RETURN_ERROR:                  return "error";
        case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "unknown descriptor type";
        case FFX_API_RETURN_ERROR_RUNTIME_ERROR:    return "runtime error";
        case FFX_API_RETURN_NO_PROVIDER:            return "no provider";
        case FFX_API_RETURN_ERROR_MEMORY:           return "out of memory";
        case FFX_API_RETURN_ERROR_PARAMETER:        return "invalid parameter";
        default:                                    return "unknown result";
    }
}

// The runtime's own messages (debug checking is on): warnings about the inputs, errors from the backend.
void FfxMessage(uint32_t type, const wchar_t* message) {
    const std::string text = WideToUtf8(message ? message : L"");
    if (type == FFX_API_MESSAGE_TYPE_ERROR) Log::Error("FSR: %s", text.c_str());
    else Log::Warn("FSR: %s", text.c_str());
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

} // namespace

bool FsrHost::Load(ID3D12Device* device, const std::wstring& exeDir, std::string& error) {
    if (Loaded()) return true;
    m_path = JoinPath(exeDir, kDllName);
    if (!FileExists(m_path)) {
        error = StrPrintf("%s not found next to the executable", WideToUtf8(kDllName).c_str());
        return false;
    }
    HMODULE mod = LoadLibraryExW(m_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        error = StrPrintf("%s could not be loaded: %s", WideToUtf8(kDllName).c_str(), LastErrorText().c_str());
        return false;
    }
    Fn fn;
    fn.create    = (void*)GetProcAddress(mod, "ffxCreateContext");
    fn.destroy   = (void*)GetProcAddress(mod, "ffxDestroyContext");
    fn.configure = (void*)GetProcAddress(mod, "ffxConfigure");
    fn.query     = (void*)GetProcAddress(mod, "ffxQuery");
    fn.dispatch  = (void*)GetProcAddress(mod, "ffxDispatch");
    if (!fn.create || !fn.destroy || !fn.configure || !fn.query || !fn.dispatch) {
        error = StrPrintf("%s does not export the FidelityFX API", WideToUtf8(kDllName).c_str());
        return false;   // the module stays loaded: never freed once mapped
    }
    // The upscaler versions the DLL provides; the first one names the runtime.
    std::string version;
    {
        ffxQueryDescGetVersions q{};
        q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        q.device = device;
        uint64_t count = 0;
        q.outputCount = &count;
        PfnFfxQuery query = (PfnFfxQuery)fn.query;
        if (query(nullptr, &q.header) == FFX_API_RETURN_OK && count > 0 && count < 64) {
            std::vector<uint64_t> ids(count);
            std::vector<const char*> names(count, nullptr);
            q.versionIds = ids.data();
            q.versionNames = names.data();
            if (query(nullptr, &q.header) == FFX_API_RETURN_OK) {
                std::string all;
                for (uint64_t i = 0; i < count; ++i) {
                    if (!names[i]) continue;
                    if (version.empty()) version = names[i];
                    all += (all.empty() ? "" : ", ") + std::string(names[i]);
                }
                Log::Info("FSR host: upscaler versions %s", all.c_str());
            }
        }
    }
    m_fn = fn;
    m_module = mod;
    m_version = version.empty() ? "FSR" : version;
    Log::Info("FSR host: %s loaded from %s", m_version.c_str(), WideToUtf8(m_path).c_str());
    return true;
}

bool FsrHost::Create(ID3D12Device* device, UINT inW, UINT inH, UINT outW, UINT outH, std::string& error) {
    Release();
    if (!Loaded()) { error = "the FSR runtime is not loaded"; return false; }
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = device;
    ffxCreateContextDescUpscale desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.header.pNext = &backend.header;
    // Depth is inverted (1 = near), the runtime meters exposure itself, and the colour is display-referred sRGB.
    desc.flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
    desc.maxRenderSize = { inW, inH };
    desc.maxUpscaleSize = { outW, outH };
    desc.fpMessage = &FfxMessage;
    ffxContext ctx = nullptr;
    const ffxReturnCode_t rc = ((PfnFfxCreateContext)m_fn.create)(&ctx, &desc.header, nullptr);
    if (rc != FFX_API_RETURN_OK || !ctx) {
        error = StrPrintf("ffxCreateContext failed: %s (%u)", ReturnText(rc), (unsigned)rc);
        return false;
    }
    m_ctx = ctx;
    m_inW = inW; m_inH = inH; m_outW = outW; m_outH = outH;
    Log::Info("FSR host: %s context %ux%u -> %ux%u", m_version.c_str(), inW, inH, outW, outH);
    return true;
}

void FsrHost::Release() {
    if (!m_ctx) return;
    ffxContext ctx = (ffxContext)m_ctx;
    ((PfnFfxDestroyContext)m_fn.destroy)(&ctx, nullptr);
    m_ctx = nullptr;
    m_inW = m_inH = m_outW = m_outH = 0;
}

bool FsrHost::Dispatch(ID3D12GraphicsCommandList* cmd, const Inputs& in, std::string& error) {
    if (!m_ctx) { error = "no FSR context"; return false; }
    ffxDispatchDescUpscale d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = cmd;
    d.color = ffxApiGetResourceDX12(in.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.depth = ffxApiGetResourceDX12(in.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors = ffxApiGetResourceDX12(in.mvec, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.output = ffxApiGetResourceDX12(in.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    d.jitterOffset = { 0.0f, 0.0f };             // the source is not jittered
    d.motionVectorScale = { 1.0f, 1.0f };        // already in pixels, current to previous frame
    d.renderSize = { m_inW, m_inH };
    d.upscaleSize = { m_outW, m_outH };
    d.enableSharpening = false;
    d.sharpness = 0.0f;
    d.frameTimeDelta = std::clamp(in.frameTimeMs, 1.0f, 100.0f);
    d.preExposure = 1.0f;
    d.reset = in.reset;
    // A nominal camera: the source is a finished picture, the values only shape the runtime's own reprojection.
    d.cameraNear = 0.1f;
    d.cameraFar = 1000.0f;
    d.cameraFovAngleVertical = 1.0472f;
    d.viewSpaceToMetersFactor = 1.0f;
    d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
    ffxContext ctx = (ffxContext)m_ctx;
    const ffxReturnCode_t rc = ((PfnFfxDispatch)m_fn.dispatch)(&ctx, &d.header);
    if (rc != FFX_API_RETURN_OK) {
        error = StrPrintf("ffxDispatch failed: %s (%u)", ReturnText(rc), (unsigned)rc);
        return false;
    }
    ++m_dispatches;
    return true;
}

std::string FsrHost::PortModule(const std::wstring& exeDir) {
    static const wchar_t* const kProxies[] = { L"version.dll", L"winmm.dll", L"dbghelp.dll", L"wininet.dll", L"winhttp.dll", L"dxgi.dll" };
    std::wstring dir = Lower(exeDir);
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    std::vector<HMODULE> mods(512);
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods.data(), (DWORD)(mods.size() * sizeof(HMODULE)), &needed)) return {};
    const size_t n = std::min<size_t>(needed / sizeof(HMODULE), mods.size());
    for (size_t i = 0; i < n; ++i) {
        wchar_t buf[1024];
        const DWORD len = GetModuleFileNameW(mods[i], buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        if (!len || len >= sizeof(buf) / sizeof(buf[0])) continue;
        const std::wstring full = Lower(std::wstring(buf, len));
        const size_t slash = full.find_last_of(L"\\/");
        const std::wstring name = slash == std::wstring::npos ? full : full.substr(slash + 1);
        const std::wstring folder = slash == std::wstring::npos ? std::wstring() : full.substr(0, slash);
        if (name.find(L"dlssnr") != std::wstring::npos && name != L"nvngx_dlssnr.dll") return WideToUtf8(name);
        if (folder != dir) continue;
        for (const wchar_t* proxy : kProxies)
            if (name == proxy) return WideToUtf8(name);
    }
    return {};
}

// DLSS-NR-on-AMD attaches to a program by hooking the DXGI and Direct3D 12 interfaces. It is loaded as a proxy
// version.dll before the program's own code runs, but installs those hooks from a thread of its own, after making a
// Direct3D 12 device to read the interface tables from. A game spends seconds starting up, so its swap chain is created
// long after the hooks are in place. This program creates its device and swap chain within milliseconds of starting,
// which is before the port has finished: the port then never sees the swap chain being created, treats its frames as
// belonging to another renderer and never initialises its engine. So, when the port is present, the Direct3D start-up
// is held until its log reports the last of its interface hooks (or until a short cap passes, so a change in the
// port's log format cannot hang the program). The port appends to its log run after run, so only what this run has
// written counts: the file must have been written to since this process was created, and the hook line is looked for
// after the last of the port's start lines ("dlssnr_amd v..."); an earlier run's lines would otherwise pass for this
// one's and the hold would end at once, before the hooks are in place. The same hold precedes the start of a second
// instance (App::RunAgainAndWait): its port writes its start line into the same log, and this instance's hook lines
// have to be in the file before that line, or the second instance takes them for its own.
void FsrHost::WaitForPortHooks(const std::wstring& exeDir, const char* next) {
    const std::string port = PortModule(exeDir);
    if (port.empty()) return;
    const std::wstring logPath = exeDir + L"\\dlssnr_on_amd.log";
    FILETIME created{}, exited{}, kernelTime{}, userTime{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernelTime, &userTime);
    const ULONGLONG start = GetTickCount64();
    const ULONGLONG cap = 4000;   // ms; the port needs ~150 ms on an RX 9060 XT
    bool ready = false;
    unsigned long long bytes = 0;
    while (GetTickCount64() - start < cap) {
        HANDLE h = CreateFileW(logPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            const bool thisRun = GetFileInformationByHandle(h, &info) && CompareFileTime(&info.ftLastWriteTime, &created) >= 0;
            std::string text;
            if (thisRun) {
                char buf[4096];
                DWORD n = 0;
                while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) { text.append(buf, n); if (text.size() > (1u << 20)) break; }
            }
            CloseHandle(h);
            bytes = text.size();
            size_t from = text.rfind("\ndlssnr_amd v");   // this run's part of the log (all of it when there is no start line)
            if (from == std::string::npos) from = 0;
            if (thisRun && text.find("hooked IDXGISwapChain1::Present1", from) != std::string::npos) { ready = true; break; }
        }
        Sleep(5);
    }
    const unsigned waited = (unsigned)(GetTickCount64() - start);
    if (ready) Log::Info("DLSS-NR-on-AMD (%s): interface hooks in place after %u ms (its log: %llu bytes); %s", port.c_str(), waited, bytes, next);
    else Log::Warn("DLSS-NR-on-AMD (%s): interface hooks not reported after %u ms (its log: %llu bytes); %s", port.c_str(), waited, bytes, next);
}

} // namespace vdc
