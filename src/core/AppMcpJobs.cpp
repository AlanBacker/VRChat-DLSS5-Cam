// VRChat DLSS5 Cam - MCP jobs: files that clients (bots) send in, run one after the other whenever the user's own
// work leaves the program idle. A client hears at once where its job stands (position, estimate) and polls with
// a bounded wait; nothing blocks for the length of a run. Keys name the clients and give them a role; every job
// belongs to the key that sent it. Everything here runs on the interface thread, like the other MCP tools.
#include "core/App.h"
#include "core/Capture.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/McpServer.h"
#include "core/MediaLibrary.h"
#include "core/Updater.h"
#include "core/Util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <shellapi.h>
#include <shlwapi.h>
#include <wincodec.h>

namespace vdc {

namespace {

struct JobState { enum { Queued = 0, Running, Done, Failed, Cancelled }; };   // App::McpJob::State, reachable here

constexpr double kJobStartGrace = 12.0;        // seconds a posted job may take to be picked up before it is queued again
constexpr long long kUploadKeepSeconds = 2 * 3600;
constexpr long long kPartKeepSeconds = 3600;
constexpr size_t kInlineMax = 16u << 20;       // base64 data in a call: 16 MB

const char* StateName(int s) {
    switch (s) {
    case JobState::Queued: return "queued";
    case JobState::Running: return "running";
    case JobState::Done: return "done";
    case JobState::Failed: return "failed";
    case JobState::Cancelled: return "cancelled";
    }
    return "unknown";
}

int StateFromName(const std::string& s) {
    if (s == "running") return JobState::Running;
    if (s == "done") return JobState::Done;
    if (s == "failed") return JobState::Failed;
    if (s == "cancelled") return JobState::Cancelled;
    return JobState::Queued;
}

long long UnixNow() { return (long long)time(nullptr); }

std::string DurationText(double seconds) {
    if (seconds < 1.0) return "a moment";
    if (seconds < 60.0) return StrPrintf("%.0f s", seconds);
    if (seconds < 3600.0) return StrPrintf("%.0f min", std::ceil(seconds / 60.0));
    return StrPrintf("%.1f h", seconds / 3600.0);
}

// A file name a client sent: the last path segment, no separators, no control characters, never empty.
std::string SafeName(const std::string& given, const char* fallback) {
    std::string name = given;
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::string out;
    for (char c : name) {
        const unsigned char u = (unsigned char)c;
        if (u < 32 || c == '"' || c == '<' || c == '>' || c == '|' || c == ':' || c == '*' || c == '?') continue;
        out += c;
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty() || out == "." || out == "..") out = fallback;
    if (out.size() > 120) out = out.substr(out.size() - 120);
    return out;
}

std::string NameFromUrl(const std::string& url) {
    std::string path = url;
    const size_t q = path.find_first_of("?#");
    if (q != std::string::npos) path.resize(q);
    const size_t slash = path.find_last_of('/');
    std::string name = slash == std::string::npos ? std::string() : path.substr(slash + 1);
    // Percent-encoded names come back readable.
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%' && i + 2 < name.size() && isxdigit((unsigned char)name[i + 1]) && isxdigit((unsigned char)name[i + 2])) {
            out += (char)strtol(name.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else out += name[i];
    }
    return SafeName(out, "download");
}

bool DeleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring path = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(path);
            else { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}

// The files of a job folder (not its in\ subfolder): the results, newest last.
std::vector<std::pair<std::string, uint64_t>> ListOutputs(const std::wstring& dir) {
    std::vector<std::pair<std::string, uint64_t>> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = fd.cFileName;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, L".part") == 0) continue;
        out.emplace_back(WideToUtf8(name), ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

long long FileAgeSeconds(const WIN32_FIND_DATAW& fd) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG a = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
    const ULONGLONG b = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return b > a ? (long long)((b - a) / 10000000ULL) : 0;
}

bool WriteBytes(const std::wstring& path, const uint8_t* data, size_t size) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    while (size > 0 && ok) {
        DWORD w = 0;
        if (!WriteFile(f, data, (DWORD)std::min<size_t>(size, 1 << 20), &w, nullptr) || w == 0) ok = false;
        data += w; size -= w;
    }
    CloseHandle(f);
    return ok;
}

bool ReadText(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    out.resize((size_t)size.QuadPart);
    DWORD n = 0;
    const bool ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &n, nullptr) && n == out.size());
    CloseHandle(f);
    return ok;
}

bool WriteTextAtomic(const std::wstring& path, const std::string& text) {
    const std::wstring tmp = path + L".tmp";
    if (!WriteBytes(tmp, (const uint8_t*)text.data(), text.size())) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// A picture file decoded to RGBA (for an inline result), scaled to fit maxEdge.
bool LoadPictureScaled(const std::wstring& path, int maxEdge, std::vector<uint8_t>& rgba, UINT& w, UINT& h, std::string& error) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = "WIC: " + FormatHr(hr); return false; }
    ComPtr<IWICBitmapDecoder> decoder;
    // Read through a handle that shares everything (a virus scanner may hold a fresh file), then decode from memory.
    std::vector<uint8_t> bytes;
    {
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) { error = "cannot open: " + LastErrorText(); return false; }
        LARGE_INTEGER size{};
        GetFileSizeEx(f, &size);
        if (size.QuadPart <= 0 || size.QuadPart > (1ll << 31)) { CloseHandle(f); error = "empty or oversized file"; return false; }
        bytes.resize((size_t)size.QuadPart);
        size_t got = 0;
        while (got < bytes.size()) {
            DWORD n = 0;
            if (!ReadFile(f, bytes.data() + got, (DWORD)std::min<size_t>(bytes.size() - got, 1 << 20), &n, nullptr) || n == 0) break;
            got += n;
        }
        CloseHandle(f);
        if (got != bytes.size()) { error = "short read"; return false; }
    }
    ComPtr<IStream> stream(SHCreateMemStream(bytes.data(), (UINT)bytes.size()));
    if (!stream) { error = "no memory stream"; return false; }
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) { error = "cannot decode: " + FormatHr(hr); return false; }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) { error = "no frame"; return false; }
    UINT fw = 0, fh = 0;
    frame->GetSize(&fw, &fh);
    if (fw == 0 || fh == 0) { error = "empty picture"; return false; }
    UINT dw = fw, dh = fh;
    if ((int)std::max(fw, fh) > maxEdge) {
        const double k = (double)maxEdge / std::max(fw, fh);
        dw = std::max(1u, (UINT)std::lround(fw * k));
        dh = std::max(1u, (UINT)std::lround(fh * k));
    }
    ComPtr<IWICBitmapSource> source = frame;
    if (dw != fw || dh != fh) {
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame.Get(), dw, dh, WICBitmapInterpolationModeFant))) { error = "scaling failed"; return false; }
        source = scaler;
    }
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) { error = "conversion failed"; return false; }
    rgba.resize((size_t)dw * dh * 4);
    if (FAILED(conv->CopyPixels(nullptr, dw * 4, (UINT)rgba.size(), rgba.data()))) { error = "copy failed"; return false; }
    w = dw; h = dh;
    return true;
}

// Which settings a job may set for itself: the look and the output, never the server's own affairs.
bool OverrideAllowed(const std::string& key) {
    const McpSettingInfo* info = McpServer::FindSetting(key);
    if (!info) return false;
    if (key == "nrDllPath" || key == "nrRuntimeBuild" || key == "depthModelPath" || key == "captureFolder") return false;
    const std::string g = info->group;
    if (g == "video" || g == "resolution" || g == "hdr" || g == "neural" || g == "blend" || g == "guidance" || g == "dlaa") return true;
    return key == "videoMatchSource" || key == "captureName" || key == "outputName" || key == "keepAlpha";
}

} // namespace

// --- where things live ------------------------------------------------------------------------

std::wstring App::McpRoot() const {
    if (!m_settings.mcpJobFolder.empty()) return Utf8ToWide(m_settings.mcpJobFolder);
    return JoinPath(m_appDataDir, L"mcp");
}

App::McpJob* App::McpFindJob(const std::string& id) {
    for (McpJob& j : m_mcpJobs) if (j.id == id) return &j;
    return nullptr;
}

const App::McpJob* App::McpFindJob(const std::string& id) const {
    for (const McpJob& j : m_mcpJobs) if (j.id == id) return &j;
    return nullptr;
}

// --- keys ---------------------------------------------------------------------------------------

void App::McpLoadKeys() {
    m_mcpKeysPath = JoinPath(m_appDataDir, L"mcp-keys.json");
    m_mcpKeys.clear();
    if (FileExists(m_mcpKeysPath) && !McpServer::LoadKeys(m_mcpKeysPath, m_mcpKeys)) Log::Warn("MCP: %s could not be read", WideToUtf8(m_mcpKeysPath).c_str());
    if (!m_mcpKeys.empty()) Log::Info("MCP: %zu keys", m_mcpKeys.size());
}

void App::McpSaveKeys() {
    if (!McpServer::SaveKeys(m_mcpKeysPath, m_mcpKeys)) Log::Warn("MCP: %s could not be written", WideToUtf8(m_mcpKeysPath).c_str());
    m_mcpKeysDirty = false;
    m_mcpKeysSaveTime = NowSeconds();
}

std::string App::McpAddKey(const std::string& nameGiven, int role) {
    std::string name;
    for (char c : nameGiven) if ((unsigned char)c >= 32 && c != '"' && c != '\\') name += c;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (name.empty()) name = StrPrintf("key %zu", m_mcpKeys.size() + 1);
    if (name == "local" || name == "token") return std::string();   // those names mean the keyless user and the legacy token
    for (const McpKey& k : m_mcpKeys) if (k.name == name) return std::string();
    McpKey k;
    k.name = name;
    k.role = std::clamp(role, (int)McpRoleViewer, (int)McpRoleAdmin);
    k.secret = McpServer::NewSecret();
    k.created = UnixNow();
    m_mcpKeys.push_back(k);
    McpSaveKeys();
    McpPushAccess();
    Log::Info("MCP: key '%s' created (%s)", name.c_str(), McpServer::RoleName(k.role));
    return k.secret;
}

bool App::McpRemoveKey(const std::string& name) {
    const size_t before = m_mcpKeys.size();
    m_mcpKeys.erase(std::remove_if(m_mcpKeys.begin(), m_mcpKeys.end(), [&](const McpKey& k) { return k.name == name; }), m_mcpKeys.end());
    if (m_mcpKeys.size() == before) return false;
    McpSaveKeys();
    McpPushAccess();
    Log::Info("MCP: key '%s' removed", name.c_str());
    return true;
}

void App::McpPushAccess() {
    McpServer::Access a;
    a.keys = m_mcpKeys;
    a.legacyToken = m_settings.mcpToken;
    a.localNoKey = m_settings.mcpLocalNoKey;
    a.readOnly = m_settings.mcpReadOnly;
    m_mcp.SetAccess(a);
    m_mcp.SetUploads(JoinPath(McpRoot(), L"uploads"), (uint64_t)m_settings.mcpUploadMaxMb << 20);
    m_mcpAccessPushed = true;
}

void App::McpAllowFirewall() {
    const int port = m_mcpSessionPort > 0 ? m_mcpSessionPort : m_settings.mcpPort;
    // One rule, replaced when the port changes; Windows asks for elevation.
    const std::wstring args = L"advfirewall firewall delete rule name=\"VRChat DLSS5 Cam MCP\" & netsh advfirewall firewall add rule name=\"VRChat DLSS5 Cam MCP\" dir=in action=allow protocol=TCP localport=" + std::to_wstring(port);
    const std::wstring cmd = L"/c netsh " + args;
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = cmd.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) { m_ui.Toast(TR(McpFirewallDenied), true); return; }
    WaitForSingleObject(sei.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (code == 0) { m_ui.Toast(StrPrintf(TR(McpFirewallDoneFmt), port)); Log::Info("MCP: firewall rule added for port %d", port); }
    else m_ui.Toast(TR(McpFirewallDenied), true);
}

// --- the jobs on disk ---------------------------------------------------------------------------

void App::McpSaveJobs() {
    Json list = Json::Arr();
    for (const McpJob& j : m_mcpJobs) {
        if (j.fetching && j.state == McpJob::Queued) continue;   // an input still downloading is lost with the program
        Json outs = Json::Arr();
        for (const std::string& o : j.outputs) outs.Push(o);
        list.Push(Json::Obj()
            .Set("id", j.id).Set("owner", j.owner).Set("label", j.label).Set("clientRef", j.clientRef)
            .Set("state", StateName(j.state)).Set("folder", WideToUtf8(j.folder)).Set("input", WideToUtf8(j.inputPath)).Set("name", j.inputName)
            .Set("video", j.isVideo).Set("in", j.inSec).Set("out", j.outSec).Set("preset", j.preset).Set("overrides", j.overrides)
            .Set("settings", j.settings ? j.settings->Text() : std::string())
            .Set("submitted", j.submitted).Set("started", j.started).Set("finished", j.finished)
            .Set("outputs", outs).Set("error", j.error).Set("runSeconds", j.runSeconds)
            .Set("width", j.width).Set("height", j.height).Set("duration", j.duration).Set("fps", j.fps));
    }
    Json doc = Json::Obj().Set("secPerMp", m_mcpSecPerMp).Set("jobs", list);
    const std::wstring root = McpRoot();
    CreateDirectories(root);
    if (!WriteTextAtomic(JoinPath(root, L"jobs.json"), doc.Dump(2))) Log::Warn("MCP: jobs.json could not be written");
    m_mcpJobsDirty = false;
}

void App::McpLoadJobs() {
    std::string text;
    Json doc;
    if (!ReadText(JoinPath(McpRoot(), L"jobs.json"), text) || !JsonReader::Parse(text, doc)) return;
    if (doc.Num("secPerMp", 0.0) > 0.0) m_mcpSecPerMp = std::clamp(doc.Num("secPerMp"), 0.001, 5.0);
    const Json* list = doc.Find("jobs");
    if (!list) return;
    int requeued = 0;
    for (const Json& e : list->arr) {
        McpJob j;
        j.id = e.Str("id");
        if (j.id.empty()) continue;
        j.owner = e.Str("owner"); j.label = e.Str("label"); j.clientRef = e.Str("clientRef");
        j.state = (McpJob::State)StateFromName(e.Str("state"));
        j.folder = Utf8ToWide(e.Str("folder")); j.inputPath = Utf8ToWide(e.Str("input")); j.inputName = e.Str("name");
        j.isVideo = e.Flag("video"); j.inSec = e.Num("in"); j.outSec = e.Num("out"); j.preset = e.Str("preset");
        if (const Json* o = e.Find("overrides")) j.overrides = *o;
        const std::string st = e.Str("settings");
        if (!st.empty()) { j.settings = std::make_shared<Settings>(m_settings); j.settings->ApplyText(st, true); j.settings->Clamp(); }
        j.submitted = (long long)e.Num("submitted"); j.started = (long long)e.Num("started"); j.finished = (long long)e.Num("finished");
        if (const Json* outs = e.Find("outputs")) for (const Json& o : outs->arr) if (o.type == Json::String) j.outputs.push_back(o.str);
        j.error = e.Str("error"); j.runSeconds = e.Num("runSeconds");
        j.width = (UINT)e.Num("width"); j.height = (UINT)e.Num("height"); j.duration = e.Num("duration"); j.fps = e.Num("fps");
        j.itemId = m_mcpNextItemId++;
        if (j.state == McpJob::Running) { j.state = McpJob::Queued; j.restarted = true; j.started = 0; ++requeued; }
        if (j.state == McpJob::Queued) {
            if (!FileExists(j.inputPath)) { j.state = McpJob::Failed; j.error = "the input file is gone"; j.finished = UnixNow(); }
            else { j.probe = 0; m_scanner.Probe(j.itemId, j.inputPath, j.isVideo, -1); }
        } else j.probe = 1;
        m_mcpJobs.push_back(std::move(j));
    }
    if (!m_mcpJobs.empty()) Log::Info("MCP: %zu jobs from the last session (%d queued again)", m_mcpJobs.size(), requeued);
}

void App::McpJobsInit() {
    McpLoadKeys();
    McpLoadJobs();
    McpPushAccess();
}

void App::McpJobsShutdown() {
    for (const McpJobWaiter& w : m_mcpJobWaiters) w.call->Fail("the program is closing");
    m_mcpJobWaiters.clear();
    m_mcpFetchCancel.store(true);
    for (int i = 0; i < 300 && m_mcpFetchLive.load() > 0; ++i) Sleep(10);
    if (m_mcpKeysDirty) McpSaveKeys();
    if (!m_mcpJobs.empty() || m_mcpJobsDirty) McpSaveJobs();
}

// Old jobs and forgotten uploads go; every minute.
void App::McpSweep() {
    const long long now = UnixNow();
    const long long keep = (long long)m_settings.mcpKeepHours * 3600;
    for (size_t i = 0; i < m_mcpJobs.size();) {
        McpJob& j = m_mcpJobs[i];
        const bool over = j.state == McpJob::Done || j.state == McpJob::Failed || j.state == McpJob::Cancelled;
        if (over && j.finished > 0 && now - j.finished > keep) {
            if (!j.folder.empty() && DirectoryExists(j.folder)) DeleteTree(j.folder);
            Log::Info("MCP: job %s of %s expired", j.id.c_str(), j.owner.c_str());
            m_mcpJobs.erase(m_mcpJobs.begin() + (ptrdiff_t)i);
            m_mcpJobsDirty = true;
        } else ++i;
    }
    for (size_t i = 0; i < m_mcpUploads.size();) {
        const McpUpload& u = m_mcpUploads[i];
        if (now - u.created > kUploadKeepSeconds) {
            const std::wstring dir = u.path.substr(0, u.path.find_last_of(L"\\/"));
            DeleteTree(dir);
            m_mcpUploads.erase(m_mcpUploads.begin() + (ptrdiff_t)i);
        } else ++i;
    }
    // Part files of uploads that never completed, and upload folders no record knows (a previous session's).
    const std::wstring ups = JoinPath(McpRoot(), L"uploads");
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((ups + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring path = ups + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                bool known = false;
                for (const McpUpload& u : m_mcpUploads) if (u.path.rfind(path + L"\\", 0) == 0) { known = true; break; }
                if (!known && FileAgeSeconds(fd) > kUploadKeepSeconds) DeleteTree(path);
            } else if (FileAgeSeconds(fd) > kPartKeepSeconds) DeleteFileW(path.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// --- the queue ------------------------------------------------------------------------------------

// Fairness between keys: the key whose last job started longest ago goes first, then by submission. One bot
// sending ten files does not keep the others waiting for all ten.
void App::McpJobOrder(std::vector<const McpJob*>& queued) const {
    queued.clear();
    for (const McpJob& j : m_mcpJobs) if (j.state == McpJob::Queued) queued.push_back(&j);
    auto lastStart = [&](const std::string& owner) { auto it = m_mcpOwnerLastStart.find(owner); return it == m_mcpOwnerLastStart.end() ? 0LL : it->second; };
    std::stable_sort(queued.begin(), queued.end(), [&](const McpJob* a, const McpJob* b) {
        const long long la = lastStart(a->owner), lb = lastStart(b->owner);
        if (la != lb) return la < lb;
        if (a->submitted != b->submitted) return a->submitted < b->submitted;
        return a->id < b->id;
    });
}

double App::McpJobEstimate(const McpJob& j) const {
    const double mp = (j.width && j.height) ? (double)j.width * (double)j.height / 1e6 : 2.0;
    double perFrame = m_mcpSecPerMp * mp;
    if (m_source.costSecPerFrame > 0.0 && m_source.costPixels > 0.0) perFrame = m_source.costSecPerFrame * (mp * 1e6 / m_source.costPixels);
    perFrame = std::clamp(perFrame, 0.002, 30.0);
    if (j.isVideo) {
        double span = j.duration > 0.0 ? j.duration : 10.0;
        if (j.outSec > j.inSec && j.outSec > 0.0) span = std::min(span, j.outSec - j.inSec);
        else if (j.inSec > 0.0) span = std::max(1.0, span - j.inSec);
        const double frames = std::max(1.0, span * (j.fps > 0.0 ? j.fps : 30.0));
        return 4.0 + frames * perFrame;
    }
    return 4.0 + 14.0 * perFrame + m_pngSecPerMegapixel * mp;
}

// Where a queued job stands: its place and the seconds until it starts (the running job's remainder plus the
// jobs before it). The user's own work is not in the sum: it is unknown and is reported in words.
void App::McpQueuePosition(const McpJob& job, int& position, double& etaSeconds) const {
    position = 0;
    etaSeconds = 0.0;
    if (const McpJob* r = McpFindJob(m_mcpJobRunning)) etaSeconds += McpJobEstimate(*r) * (1.0 - std::clamp(r->progress, 0.0, 0.95));
    std::vector<const McpJob*> order;
    McpJobOrder(order);
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == &job) { position = (int)i + 1; return; }
        etaSeconds += McpJobEstimate(*order[i]);
    }
}

void App::McpJobStart(McpJob& job) {
    Command c;
    c.type = Command::BatchStart;
    BatchItem b;
    b.id = job.itemId;
    b.path = job.inputPath;
    b.isVideo = job.isVideo;
    b.inSec = job.inSec;
    b.outSec = job.outSec;
    b.own = job.settings ? job.settings : std::make_shared<Settings>(m_settings);
    b.ownAll = true;
    c.items.push_back(b);
    c.path = job.folder;
    c.keepAlpha = b.own->keepAlpha;
    c.saveOriginal = false;
    CreateDirectories(job.folder);
    PostCommand(std::move(c));
    job.state = McpJob::Running;
    job.started = UnixNow();
    job.startedMono = NowSeconds();
    job.seen = false;
    job.finishing = false;
    job.progress = 0.0;
    job.error.clear();
    m_mcpJobRunning = job.id;
    m_mcpOwnerLastStart[job.owner] = job.started;
    m_mcpJobsDirty = true;
    Log::Info("MCP: job %s of %s starts: %s%s", job.id.c_str(), job.owner.c_str(), job.inputName.c_str(), job.restarted ? " (again, after a restart)" : "");
}

void App::McpJobFinish(McpJob& job, int state, const std::string& error) {
    job.finished = UnixNow();
    if (job.startedMono > 0.0 && (state == McpJob::Done || state == McpJob::Failed)) job.runSeconds = NowSeconds() - job.startedMono;
    job.finishing = false;
    job.fetching = false;
    job.progress = state == McpJob::Done ? 1.0 : job.progress;
    if (state == McpJob::Done || state == McpJob::Failed) {
        // What the run left in the folder; a picture's capture result may have named it already.
        for (const auto& f : ListOutputs(job.folder))
            if (std::find(job.outputs.begin(), job.outputs.end(), f.first) == job.outputs.end()) job.outputs.push_back(f.first);
    }
    if (state == McpJob::Done && job.outputs.empty()) { state = McpJob::Failed; job.error = error.empty() ? "no output was written (the log tells why)" : error; }
    else job.error = error;
    if (job.cancelAsked && state == McpJob::Failed) { state = McpJob::Cancelled; job.error = "cancelled"; }
    job.state = (McpJob::State)state;
    if (state == McpJob::Done && job.runSeconds > 0.5) {
        // The rate learned: seconds per megapixel and frame, blended so one odd run does not swing it.
        const double mp = (job.width && job.height) ? (double)job.width * (double)job.height / 1e6 : 2.0;
        double frames = 14.0;
        if (job.isVideo) {
            double span = job.duration > 0.0 ? job.duration : 10.0;
            if (job.outSec > job.inSec && job.outSec > 0.0) span = std::min(span, job.outSec - job.inSec);
            frames = std::max(1.0, span * (job.fps > 0.0 ? job.fps : 30.0));
        }
        const double rate = std::clamp((job.runSeconds - 4.0) / (frames * mp), 0.001, 5.0);
        m_mcpSecPerMp = m_mcpSecPerMp * 0.6 + rate * 0.4;
    }
    if (m_mcpJobRunning == job.id) m_mcpJobRunning.clear();
    m_mcpJobsDirty = true;
    Log::Info("MCP: job %s of %s %s%s%s", job.id.c_str(), job.owner.c_str(), StateName(state), job.error.empty() ? "" : ": ", job.error.c_str());
    if (!m_headless) m_ui.Toast(StrPrintf(TR(McpJobEndedFmt), job.owner.c_str(), job.inputName.c_str(), StateName(state)), state == McpJob::Failed);
}

void App::McpJobEvent(const BatchEvent& e) {
    McpJob* job = nullptr;
    for (McpJob& j : m_mcpJobs) if (j.itemId == e.id) { job = &j; break; }
    if (!job || job->state != McpJob::Running) return;
    switch (e.state) {
    case LibraryItem::Processing: job->seen = true; break;
    case LibraryItem::Done:
        if (!e.outName.empty() && std::find(job->outputs.begin(), job->outputs.end(), e.outName) == job->outputs.end()) job->outputs.push_back(e.outName);
        job->finishing = true;   // the last picture may still be in the encoder: the tick settles it
        break;
    case LibraryItem::Failed: McpJobFinish(*job, McpJob::Failed, e.error); break;
    case LibraryItem::Idle: McpJobFinish(*job, McpJob::Cancelled, job->cancelAsked ? "cancelled" : "cancelled by the server's user"); break;
    default: break;
    }
}

void App::McpJobBatchEnded() {
    McpJob* job = McpFindJob(m_mcpJobRunning);
    if (job && job->state == McpJob::Running && !job->finishing) {
        // The batch is over without a word for the item: it never started (the file could not be opened).
        if (!job->seen) McpJobFinish(*job, McpJob::Failed, "the file could not be opened (the log tells why)");
        else job->finishing = true;
    }
}

void App::McpJobProbed(const ScanResult& r) {
    for (McpJob& j : m_mcpJobs) {
        if (j.itemId != r.id) continue;
        j.probe = r.ok ? 1 : 2;
        j.width = r.width; j.height = r.height; j.duration = r.duration; j.fps = r.fps;
        if (!r.ok && j.state == McpJob::Queued) McpJobFinish(j, McpJob::Failed, "the file cannot be read: " + r.error);
        m_mcpJobsDirty = true;
        return;
    }
}

void App::McpJobCaptured(const std::wstring& path, unsigned tag, bool ok, const std::string& error) {
    for (McpJob& j : m_mcpJobs) {
        if (j.itemId != tag) continue;
        const size_t slash = path.find_last_of(L"\\/");
        const std::string name = WideToUtf8(slash == std::wstring::npos ? path : path.substr(slash + 1));
        if (ok) { if (std::find(j.outputs.begin(), j.outputs.end(), name) == j.outputs.end()) j.outputs.push_back(name); }
        else if (j.error.empty()) j.error = "the picture could not be saved: " + error;
        m_mcpJobsDirty = true;
        return;
    }
}

// Once per frame: downloads that ended, the running job's progress and end, the next job, the waiting calls.
void App::McpJobsTick() {
    const double now = NowSeconds();
    static double lastSave = 0.0, lastUse = 0.0;
    std::vector<McpFetchResult> fetched;
    { std::lock_guard<std::mutex> lock(m_mcpFetchMutex); fetched.swap(m_mcpFetchResults); }
    for (const McpFetchResult& f : fetched) {
        McpJob* job = McpFindJob(f.jobId);
        if (!job) continue;
        job->fetching = false;
        if (job->state != McpJob::Queued) { if (FileExists(job->inputPath)) DeleteFileW(job->inputPath.c_str()); continue; }   // cancelled meanwhile
        if (!f.ok) { McpJobFinish(*job, McpJob::Failed, "the download failed: " + f.error); continue; }
        Log::Info("MCP: job %s: %s downloaded (%llu bytes)", job->id.c_str(), job->inputName.c_str(), (unsigned long long)f.bytes);
        m_scanner.Probe(job->itemId, job->inputPath, job->isVideo, -1);
        m_mcpJobsDirty = true;
    }
    if (now - lastUse > 5.0) {
        lastUse = now;
        for (const McpServer::KeyUse& u : m_mcp.TakeKeyUse())
            for (McpKey& k : m_mcpKeys) if (k.name == u.name) { k.lastUsed = std::max(k.lastUsed, u.lastUsed); k.calls += u.calls; m_mcpKeysDirty = true; }
        if (m_mcpKeysDirty && now - m_mcpKeysSaveTime > 60.0) McpSaveKeys();
    }
    if (m_mcpSweepTime < 0.0 || now - m_mcpSweepTime > 60.0) { m_mcpSweepTime = now; McpSweep(); }

    if (McpJob* job = McpFindJob(m_mcpJobRunning)) {
        if (job->state != McpJob::Running) m_mcpJobRunning.clear();
        else if (job->finishing) {
            if (!m_source.batchRunning && m_capture.Pending() == 0 && m_status.capturesInFlight == 0 && !m_pipeline.CapturePending())
                McpJobFinish(*job, McpJob::Done, std::string());
        } else if (!job->seen && now - job->startedMono > kJobStartGrace && !m_source.batchRunning) {
            // The processing thread never took it (the user's own run came first): it waits its turn again.
            Log::Warn("MCP: job %s was not picked up, queued again", job->id.c_str());
            job->state = McpJob::Queued;
            job->started = 0;
            job->startedMono = 0.0;
            m_mcpJobRunning.clear();
            m_mcpJobsDirty = true;
        } else if (job->seen) {
            if (job->isVideo) {
                if (m_source.batchItemId == job->itemId && m_source.videoFrames > 0 && (m_source.videoProcessing || m_source.videoFinishing))
                    job->progress = std::min(0.99, (double)m_source.videoFrame / (double)m_source.videoFrames);
            } else job->progress = 0.5;
        }
    } else if (!m_mcpJobRunning.empty()) m_mcpJobRunning.clear();

    if (m_mcpJobRunning.empty() && m_mcp.Running() && McpIdle() && !m_settings.mcpReadOnly) {
        std::vector<const McpJob*> order;
        McpJobOrder(order);
        for (const McpJob* c : order) {
            if (c->fetching || c->probe != 1) continue;
            McpJobStart(*const_cast<McpJob*>(c));
            break;
        }
    }

    for (size_t i = 0; i < m_mcpJobWaiters.size();) {
        McpJobWaiter& w = m_mcpJobWaiters[i];
        const McpJob* job = McpFindJob(w.jobId);
        bool done = false;
        if (!job) { w.call->Fail("the job " + w.jobId + " is gone"); done = true; }
        else if ((job->state != McpJob::Queued && job->state != McpJob::Running) || now > w.deadline) {
            Json j = McpJobJson(*job, w.call->caller.host);
            if (job->state == McpJob::Done && w.withInline && !job->outputs.empty() && !job->isVideo) {
                std::vector<uint8_t> rgba;
                UINT pw = 0, ph = 0;
                std::string err;
                if (!LoadPictureScaled(JoinPath(job->folder, Utf8ToWide(job->outputs.front())), w.maxEdge, rgba, pw, ph, err)) {
                    if (w.inlineRetryUntil == 0.0) w.inlineRetryUntil = now + 3.0;
                    if (now < w.inlineRetryUntil) { ++i; continue; }
                    Log::Warn("MCP: job %s: no inline picture: %s", job->id.c_str(), err.c_str());
                } else {
                    CaptureJob cj;
                    cj.width = pw; cj.height = ph; cj.rowPitch = pw * 4; cj.keepAlpha = false; cj.quiet = true;
                    cj.pixels = std::move(rgba);
                    std::vector<uint8_t> png;
                    if (Capture::EncodePngMemory(cj, png, err)) { w.call->Image(png); j.Set("inlineWidth", pw).Set("inlineHeight", ph); }
                    else Log::Warn("MCP: job %s: no inline picture: %s", job->id.c_str(), err.c_str());
                }
            }
            w.call->Json_(j);
            w.call->Finish();
            done = true;
        }
        if (done) m_mcpJobWaiters.erase(m_mcpJobWaiters.begin() + (ptrdiff_t)i); else ++i;
    }
    if (m_mcpJobsDirty && now - lastSave > 2.0) { lastSave = now; McpSaveJobs(); }
}

// --- what the tools answer with -------------------------------------------------------------------

Json App::McpJobJson(const McpJob& j, const std::string& host) const {
    Json out = Json::Obj()
        .Set("jobId", j.id).Set("state", StateName(j.state)).Set("owner", j.owner).Set("name", j.inputName)
        .Set("isVideo", j.isVideo).Set("submitted", j.submitted);
    if (!j.label.empty()) out.Set("label", j.label);
    if (!j.clientRef.empty()) out.Set("clientRef", j.clientRef);
    if (!j.preset.empty()) out.Set("preset", j.preset);
    if (j.overrides.type == Json::Object && !j.overrides.obj.empty()) out.Set("settings", j.overrides);
    if (j.width && j.height) out.Set("width", j.width).Set("height", j.height);
    if (j.isVideo && j.duration > 0.0) out.Set("duration", j.duration).Set("fps", j.fps);
    if (j.started > 0) out.Set("started", j.started);
    if (j.finished > 0) out.Set("finished", j.finished).Set("expiresAt", j.finished + (long long)m_settings.mcpKeepHours * 3600);
    const double estimate = McpJobEstimate(j);
    std::string message;
    if (j.state == McpJob::Queued) {
        int position = 0;
        double eta = 0.0;
        McpQueuePosition(j, position, eta);
        out.Set("position", position).Set("ahead", std::max(0, position - 1)).Set("etaSeconds", std::round(eta)).Set("estimateSeconds", std::round(estimate));
        if (j.fetching) message = StrPrintf("Queued at position %d; the input is still downloading", position);
        else if (j.probe == 0) message = StrPrintf("Queued at position %d; the file is being read", position);
        else if (position <= 1 && m_mcpJobRunning.empty()) message = m_mcp.Running() ? "Next to run" : "Queued; the server is not listening right now";
        else message = StrPrintf("Queued at position %d (%d ahead), about %s until it starts", position, std::max(0, position - 1), DurationText(eta).c_str());
        if (m_mcpJobRunning.empty() && !McpIdle()) message += "; the server is busy with its own work first";
        if (j.restarted) message += " (queued again after the server restarted)";
        message += StrPrintf(". The run itself takes about %s.", DurationText(estimate).c_str());
    } else if (j.state == McpJob::Running) {
        const double left = estimate * (1.0 - std::clamp(j.progress, 0.0, 0.95));
        out.Set("progress", j.progress).Set("etaSeconds", std::round(left)).Set("estimateSeconds", std::round(estimate));
        message = j.finishing ? "Finishing: the result is being written." : StrPrintf("Running, %.0f %%, about %s left.", j.progress * 100.0, DurationText(left).c_str());
    } else if (j.state == McpJob::Done) {
        message = StrPrintf("Done in %s: %zu file%s. Download them with the same key.", DurationText(j.runSeconds).c_str(), j.outputs.size(), j.outputs.size() == 1 ? "" : "s");
    } else if (j.state == McpJob::Failed) {
        message = "Failed: " + j.error;
    } else message = "Cancelled" + (j.error.empty() || j.error == "cancelled" ? std::string() : ": " + j.error);
    if (!j.error.empty() && j.state != McpJob::Done) out.Set("error", j.error);
    Json outs = Json::Arr();
    for (const std::string& name : j.outputs) {
        std::string enc;
        for (unsigned char c : name) {
            if (isalnum(c) || c == '.' || c == '-' || c == '_') enc += (char)c;
            else enc += StrPrintf("%%%02X", c);
        }
        outs.Push(Json::Obj().Set("name", name).Set("url", "http://" + host + "/download/" + j.id + "/" + enc)
                  .Set("bytes", (unsigned long long)GetFileSizeBytes(JoinPath(j.folder, Utf8ToWide(name)))));
    }
    out.Set("outputs", outs);
    out.Set("message", message);
    return out;
}

Json App::McpQueueJson() const {
    int queued = 0, finished = 0;
    for (const McpJob& j : m_mcpJobs) { if (j.state == McpJob::Queued) ++queued; else if (j.state != McpJob::Running) ++finished; }
    const McpJob* r = McpFindJob(m_mcpJobRunning);
    Json j = Json::Obj().Set("queued", queued).Set("running", r ? r->id : std::string()).Set("finished", finished)
        .Set("keepHours", m_settings.mcpKeepHours).Set("queueMax", m_settings.mcpQueueMax).Set("perKey", m_settings.mcpQueuePerKey)
        .Set("uploadMaxMb", m_settings.mcpUploadMaxMb).Set("serverBusyWithOwnWork", !McpIdle() && !r);
    if (r) j.Set("runningOwner", r->owner).Set("runningName", r->inputName).Set("runningProgress", r->progress);
    return j;
}

Json App::McpJobsJson(const McpCaller& caller) const {
    Json list = Json::Arr();
    for (const McpJob& j : m_mcpJobs)
        if (caller.role == McpRoleAdmin || j.owner == caller.keyName) list.Push(McpJobJson(j, caller.host));
    Json out = McpQueueJson();
    out.Set("jobs", list);
    return out;
}

// The jobs role sees the machine and the queue, not the user's files or window.
Json App::McpStatusReduced() const {
    const PipelineStatus& st = m_status;
    const AdapterInfo& ad = m_device.Info();
    return Json::Obj()
        .Set("version", APP_VERSION_STRING)
        .Set("edition", APP_EDITION_AMD ? "Radeon" : "GeForce")
        .Set("adapter", Json::Obj().Set("name", WideToUtf8(ad.name)).Set("vramMb", (long long)(ad.dedicatedVideoMemory >> 20)))
        .Set("neural", Json::Obj().Set("enabled", m_settings.nrEnabled).Set("runtimeLoaded", st.nrRuntimeLoaded).Set("runtimeVersion", st.nrRuntimeVersion)
             .Set("active", st.nrActive).Set("failed", st.nrFailed).Set("error", st.nrError))
        .Set("idle", McpIdle())
        .Set("jobs", McpQueueJson());
}

// --- the tools ------------------------------------------------------------------------------------

bool App::McpExecuteJobs(const std::shared_ptr<McpCall>& call, ui::UiEvents& ev) {
    const std::string& name = call->name;
    const Json& a = call->args;
    const McpCaller& caller = call->caller;
    auto reply = [&](const Json& j) { call->Json_(j); call->Finish(); };
    auto uploadReply = [&](const McpUpload& u) {
        reply(Json::Obj().Set("uploadId", u.id).Set("name", u.name).Set("bytes", (unsigned long long)u.bytes).Set("expiresAt", u.created + kUploadKeepSeconds)
              .Set("message", "Uploaded. Pass upload_id to submit within two hours; an unused upload is deleted after that."));
    };
    auto newId = [&]() {
        for (;;) {
            const std::string id = McpServer::NewSecret().substr(4, 10);
            if (!McpFindJob(id)) return id;
        }
    };
    auto storeUpload = [&](const std::string& givenName, const std::string& owner, std::wstring& outDir, std::string& outName) {
        outName = SafeName(givenName, "upload.bin");
        outDir = JoinPath(JoinPath(McpRoot(), L"uploads"), Utf8ToWide(McpServer::NewSecret().substr(4, 10)));
        CreateDirectories(outDir);
        (void)owner;
    };

    if (name == "upload") {
        const std::string data = a.Str("data");
        if (data.empty()) { call->Fail("data (base64) is required; larger files go to POST /upload?name=<file name> with the bytes as the body"); return true; }
        if (data.size() > kInlineMax * 4 / 3 + 4) { call->Fail("data is larger than 16 MB: use POST /upload instead", Json::Obj().Set("reason", "too_large")); return true; }
        std::vector<uint8_t> bytes;
        if (!McpServer::Base64Decode(data, bytes) || bytes.empty()) { call->Fail("data is not valid base64"); return true; }
        if (bytes.size() > ((uint64_t)m_settings.mcpUploadMaxMb << 20)) { call->Fail(StrPrintf("the file is larger than the server allows (%d MB)", m_settings.mcpUploadMaxMb), Json::Obj().Set("reason", "too_large")); return true; }
        McpUpload u;
        std::wstring dir;
        storeUpload(a.Str("name"), caller.keyName, dir, u.name);
        u.path = JoinPath(dir, Utf8ToWide(u.name));
        if (!WriteBytes(u.path, bytes.data(), bytes.size())) { call->Fail("the file could not be written"); return true; }
        u.id = WideToUtf8(dir.substr(dir.find_last_of(L"\\/") + 1));
        u.owner = caller.keyName;
        u.bytes = bytes.size();
        u.created = UnixNow();
        m_mcpUploads.push_back(u);
        uploadReply(u);
        return true;
    }

    if (name == "_upload") {
        const std::wstring part = Utf8ToWide(a.Str("file"));
        if (part.empty() || !FileExists(part)) { call->Fail("no upload data arrived"); return true; }
        McpUpload u;
        std::wstring dir;
        storeUpload(a.Str("name"), caller.keyName, dir, u.name);
        u.path = JoinPath(dir, Utf8ToWide(u.name));
        if (!MoveFileExW(part.c_str(), u.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) { call->Fail("the upload could not be stored: " + LastErrorText()); return true; }
        u.id = WideToUtf8(dir.substr(dir.find_last_of(L"\\/") + 1));
        u.owner = caller.keyName;
        u.bytes = (uint64_t)a.Num("bytes", 0.0);
        if (u.bytes == 0) u.bytes = GetFileSizeBytes(u.path);
        u.created = UnixNow();
        m_mcpUploads.push_back(u);
        Log::Info("MCP: %s uploaded %s (%llu bytes)", caller.keyName.c_str(), u.name.c_str(), (unsigned long long)u.bytes);
        uploadReply(u);
        return true;
    }

    if (name == "submit") {
        // The caps first: a refused job costs the client nothing but a short wait.
        int queuedAll = 0, mine = 0;
        for (const McpJob& j : m_mcpJobs) {
            if (j.state == McpJob::Queued) ++queuedAll;
            if ((j.state == McpJob::Queued || j.state == McpJob::Running) && j.owner == caller.keyName) ++mine;
        }
        if (queuedAll >= m_settings.mcpQueueMax) {
            double eta = 0.0;
            std::vector<const McpJob*> order;
            McpJobOrder(order);
            for (const McpJob* j : order) eta += McpJobEstimate(*j);
            call->Fail(StrPrintf("The queue is full (%d jobs waiting, about %s of work): try again later.", queuedAll, DurationText(eta).c_str()),
                       Json::Obj().Set("reason", "queue_full").Set("queued", queuedAll).Set("etaSeconds", std::round(eta)).Set("retryAfterSeconds", std::round(std::max(30.0, eta * 0.5))));
            return true;
        }
        if (caller.role != McpRoleAdmin && mine >= m_settings.mcpQueuePerKey) {
            call->Fail(StrPrintf("You already have %d jobs waiting or running (the limit per key is %d): wait for one to finish, or cancel one.", mine, m_settings.mcpQueuePerKey),
                       Json::Obj().Set("reason", "too_many_jobs").Set("yours", mine).Set("perKey", m_settings.mcpQueuePerKey).Set("retryAfterSeconds", 30));
            return true;
        }
        const std::string uploadId = a.Str("upload_id"), url = a.Str("url"), data = a.Str("data"), pathGiven = a.Str("path");
        const int sources = (int)!uploadId.empty() + (int)!url.empty() + (int)!data.empty() + (int)!pathGiven.empty();
        if (sources != 1) { call->Fail("exactly one of upload_id, url, data or path names the input"); return true; }
        if (!pathGiven.empty() && caller.role != McpRoleAdmin) { call->Fail("path is for admin keys (a file on the server itself): send the file with upload or url", Json::Obj().Set("reason", "not_allowed")); return true; }
        // The input's name decides whether it is a picture or a video.
        std::string inputName;
        const McpUpload* upload = nullptr;
        if (!uploadId.empty()) {
            for (const McpUpload& u : m_mcpUploads) if (u.id == uploadId && (u.owner == caller.keyName || caller.role == McpRoleAdmin)) { upload = &u; break; }
            if (!upload) { call->Fail("no such upload (an unused upload is deleted after two hours; the program's restart drops them too)"); return true; }
            inputName = a.Str("name").empty() ? upload->name : SafeName(a.Str("name"), upload->name.c_str());
        } else if (!url.empty()) {
            if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) { call->Fail("url must start with http:// or https://"); return true; }
            inputName = a.Str("name").empty() ? NameFromUrl(url) : SafeName(a.Str("name"), "download");
        } else if (!data.empty()) {
            if (a.Str("name").empty()) { call->Fail("name (with the file's extension) is required with data"); return true; }
            inputName = SafeName(a.Str("name"), "picture.png");
        } else {
            const std::wstring p = Utf8ToWide(pathGiven);
            if (!FileExists(p)) { call->Fail("no such file on the server: " + pathGiven); return true; }
            inputName = WideToUtf8(p.substr(p.find_last_of(L"\\/") + 1));
        }
        bool isVideo = false;
        if (!IsLibraryFile(Utf8ToWide(inputName), isVideo)) { call->Fail("unsupported file type: " + inputName + " (pictures: png, jpg, webp, gif, bmp, tif...; videos: mp4, mov, mkv, webm, avi...)"); return true; }
        // The settings the job runs with: the server's current ones, a preset, then the job's own values.
        auto settings = std::make_shared<Settings>(m_settings);
        const std::string preset = a.Str("preset");
        if (!preset.empty()) {
            bool found = false;
            for (const ui::UserPreset& pr : m_presets) if (pr.name == preset) { settings->ApplyText(pr.text); found = true; break; }
            if (!found) { call->Fail("no such preset: " + preset + " (presets list names them)"); return true; }
        }
        Json overrides = Json::Obj();
        if (const Json* o = a.Find("settings")) {
            if (o->type != Json::Object) { call->Fail("settings must be an object of key: value pairs"); return true; }
            Json refused = Json::Arr();
            for (const auto& kv : o->obj) {
                if (!OverrideAllowed(kv.first)) { refused.Push(kv.first); continue; }
                if (settings->Apply(kv.first, kv.second.Text())) overrides.Set(kv.first.c_str(), kv.second);
                else refused.Push(kv.first);
            }
            if (!refused.arr.empty()) { call->Fail("these settings cannot be set for a job: " + refused.Dump() + " (the look, the output and the guidance keys may)"); return true; }
        }
        settings->Clamp();
        McpJob job;
        job.id = newId();
        job.owner = caller.keyName;
        job.label = a.Str("label").substr(0, 200);
        job.clientRef = a.Str("client_ref").substr(0, 200);
        job.isVideo = isVideo;
        job.inSec = std::max(0.0, a.Num("in", 0.0));
        job.outSec = std::max(0.0, a.Num("out", 0.0));
        job.settings = settings;
        job.preset = preset;
        job.overrides = overrides;
        job.submitted = UnixNow();
        job.itemId = m_mcpNextItemId++;
        job.inputName = inputName;
        job.folder = JoinPath(JoinPath(McpRoot(), L"jobs"), Utf8ToWide(job.id));
        const std::wstring inDir = JoinPath(job.folder, L"in");
        if (!CreateDirectories(inDir)) { call->Fail("the job folder could not be created: " + WideToUtf8(job.folder)); return true; }
        job.inputPath = pathGiven.empty() ? JoinPath(inDir, Utf8ToWide(inputName)) : Utf8ToWide(pathGiven);
        if (upload) {
            if (!MoveFileExW(upload->path.c_str(), job.inputPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) { call->Fail("the upload could not be moved: " + LastErrorText()); DeleteTree(job.folder); return true; }
            const std::wstring udir = upload->path.substr(0, upload->path.find_last_of(L"\\/"));
            m_mcpUploads.erase(std::remove_if(m_mcpUploads.begin(), m_mcpUploads.end(), [&](const McpUpload& u) { return u.id == uploadId; }), m_mcpUploads.end());
            DeleteTree(udir);
        } else if (!data.empty()) {
            if (data.size() > kInlineMax * 4 / 3 + 4) { call->Fail("data is larger than 16 MB: upload it first (POST /upload)", Json::Obj().Set("reason", "too_large")); DeleteTree(job.folder); return true; }
            std::vector<uint8_t> bytes;
            if (!McpServer::Base64Decode(data, bytes) || bytes.empty() || !WriteBytes(job.inputPath, bytes.data(), bytes.size())) { call->Fail("data is not valid base64, or could not be written"); DeleteTree(job.folder); return true; }
        } else if (!url.empty()) {
            job.fetching = true;
            const std::string jobId = job.id;
            const std::wstring target = job.inputPath;
            const uint64_t maxBytes = (uint64_t)m_settings.mcpUploadMaxMb << 20;
            m_mcpFetchLive.fetch_add(1);
            std::thread([this, jobId, url, target, maxBytes] {
                McpFetchResult r;
                r.jobId = jobId;
                HANDLE f = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f == INVALID_HANDLE_VALUE) r.error = "the file could not be created";
                else {
                    uint64_t got = 0;
                    std::string error;
                    const bool ok = HttpGet(Utf8ToWide(url), false, m_mcpFetchCancel, [&](const char* p, DWORD n, unsigned long long total) {
                        if (total > maxBytes || got + n > maxBytes) { error = StrPrintf("the file is larger than the server allows (%llu MB)", (unsigned long long)(maxBytes >> 20)); return false; }
                        DWORD w = 0;
                        if (!WriteFile(f, p, n, &w, nullptr) || w != n) { error = "write failed"; return false; }
                        got += n;
                        return true;
                    }, r.error, 30000);
                    CloseHandle(f);
                    if (!error.empty()) r.error = error;
                    r.ok = ok && error.empty() && got > 0;
                    if (r.ok) r.bytes = got; else if (r.error.empty()) r.error = "empty answer";
                    if (!r.ok) DeleteFileW(target.c_str());
                }
                { std::lock_guard<std::mutex> lock(m_mcpFetchMutex); m_mcpFetchResults.push_back(std::move(r)); }
                if (m_hwnd) PostMessageW(m_hwnd, WM_NULL, 0, 0);
                m_mcpFetchLive.fetch_sub(1);
            }).detach();
        }
        if (!job.fetching) m_scanner.Probe(job.itemId, job.inputPath, job.isVideo, -1);
        m_mcpJobs.push_back(job);
        m_mcpJobsDirty = true;
        Log::Info("MCP: job %s from %s: %s%s", job.id.c_str(), job.owner.c_str(), inputName.c_str(), job.fetching ? " (downloading)" : "");
        if (a.Flag("wait", false)) {
            McpJobWaiter w;
            w.call = call; w.jobId = job.id; w.deadline = NowSeconds() + std::clamp(a.Num("timeout", 30.0), 1.0, 60.0);
            m_mcpJobWaiters.push_back(w);
        } else reply(McpJobJson(m_mcpJobs.back(), caller.host));
        return true;
    }

    if (name == "jobs") {
        const std::string action = a.Str("action", "list");
        if (action == "list") { reply(McpJobsJson(caller)); return true; }
        const std::string id = a.Str("id");
        McpJob* job = id.empty() ? nullptr : McpFindJob(id);
        if (job && caller.role != McpRoleAdmin && job->owner != caller.keyName) job = nullptr;   // another client's job: not even its existence
        if (!job) { call->Fail(id.empty() ? "id is required" : "no such job: " + id + " (finished jobs are kept for " + std::to_string(m_settings.mcpKeepHours) + " hours)"); return true; }
        const bool over = job->state != McpJob::Queued && job->state != McpJob::Running;
        if (action == "get" || action == "wait" || action == "result") {
            McpJobWaiter w;
            w.call = call; w.jobId = job->id;
            w.withInline = a.Flag("inline", action == "result");
            w.maxEdge = std::clamp(a.Int("max_edge", 1024), 64, 4096);
            w.deadline = action == "wait" && !over ? NowSeconds() + std::clamp(a.Num("timeout", 30.0), 1.0, 60.0) : 0.0;
            m_mcpJobWaiters.push_back(w);   // answered by the tick: now (deadline passed) or when the job ends
            return true;
        }
        if (action == "cancel") {
            if (over) { reply(McpJobJson(*job, caller.host)); return true; }
            if (job->state == McpJob::Queued) { McpJobFinish(*job, McpJob::Cancelled, "cancelled by " + caller.keyName); reply(McpJobJson(*job, caller.host)); return true; }
            job->cancelAsked = true;
            if (m_mcpJobRunning == job->id) ev.batchCancel = true;
            Json j = McpJobJson(*job, caller.host);
            j.Set("message", "Cancelling; jobs wait tells when it has stopped.");
            reply(j);
            return true;
        }
        if (action == "delete") {
            if (!over) { call->Fail("the job is still " + std::string(StateName(job->state)) + ": cancel it first"); return true; }
            if (!job->folder.empty() && DirectoryExists(job->folder)) DeleteTree(job->folder);
            const std::string gone = job->id;
            m_mcpJobs.erase(std::remove_if(m_mcpJobs.begin(), m_mcpJobs.end(), [&](const McpJob& j) { return j.id == gone; }), m_mcpJobs.end());
            m_mcpJobsDirty = true;
            reply(Json::Obj().Set("deleted", gone));
            return true;
        }
        call->Fail("action must be list, get, wait, result, cancel or delete");
        return true;
    }

    if (name == "_download") {
        const std::string id = a.Str("job"), file = a.Str("file");
        const McpJob* job = McpFindJob(id);
        if (!job) { call->Fail("no such job"); return true; }
        if (caller.role != McpRoleAdmin && job->owner != caller.keyName) { call->Fail("not allowed: the job belongs to another key"); return true; }
        std::string chosen;
        if (file.empty() || std::all_of(file.begin(), file.end(), [](char c) { return isdigit((unsigned char)c); })) {
            const size_t index = file.empty() ? 0 : (size_t)atoi(file.c_str());
            if (index < job->outputs.size()) chosen = job->outputs[index];
        } else {
            for (const std::string& o : job->outputs) if (_stricmp(o.c_str(), file.c_str()) == 0) { chosen = o; break; }
            if (chosen.empty() && file == job->inputName) chosen = std::string();   // the input is not served
        }
        if (chosen.empty()) { call->Fail("no such file in the job (jobs get lists its outputs)"); return true; }
        const std::wstring path = JoinPath(job->folder, Utf8ToWide(chosen));
        if (!FileExists(path)) { call->Fail("the file is gone"); return true; }
        call->Json_(Json::Obj().Set("path", WideToUtf8(path)).Set("name", chosen).Set("type", ""));
        call->Finish();
        return true;
    }
    return false;
}

} // namespace vdc
