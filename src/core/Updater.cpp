#include "core/Updater.h"
#include "core/Log.h"
#include "core/Util.h"
#include <winhttp.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace vdc {
namespace {

constexpr const wchar_t* kReleasesUrl = L"https://api.github.com/repos/AlanBacker/VRChat-DLSS5-Cam/releases?per_page=20";
// Each edition updates itself with its own archive.
constexpr const char*    kAssetName   = APP_EDITION_AMD ? "VRChatDLSS5Cam-win64-amd.zip" : "VRChatDLSS5Cam-win64.zip";
constexpr const char*    kOtherAssetName = APP_EDITION_AMD ? "VRChatDLSS5Cam-win64.zip" : "VRChatDLSS5Cam-win64-amd.zip";   // an edition switch
constexpr const wchar_t* kPortReleaseUrl = L"https://api.github.com/repos/danielblnc/DLSS-NR-on-AMD/releases/latest";
constexpr const char*    kPortSetupName  = "dlssnr_on_amd_setup.exe";
constexpr const wchar_t* kExeName     = L"VRChatDLSS5Cam.exe";

std::string WinHttpErrorText(const char* where) {
    const DWORD code = GetLastError();
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   GetModuleHandleW(L"winhttp.dll"), code, 0, (LPWSTR)&buf, 0, nullptr);
    std::string text = n && buf ? WideToUtf8(buf) : std::string();
    if (buf) LocalFree(buf);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.')) text.pop_back();
    if (text.empty()) text = StrPrintf("error %lu", (unsigned long)code);
    return StrPrintf("%s: %s", where, text.c_str());
}

// GET over WinHTTP; the sink gets every chunk with the total announced by the server (0 when unknown).
bool HttpGet(const std::wstring& url, bool json, const std::atomic<bool>& cancel,
             const std::function<bool(const char*, DWORD, unsigned long long)>& sink, std::string& error) {
    wchar_t host[256] = {}, path[4096] = {}, extra[4096] = {};
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { error = "bad URL"; return false; }
    const std::wstring target = std::wstring(path) + extra;

    HINTERNET session = WinHttpOpen(L"VRChatDLSS5Cam (Windows; update check)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) session = WinHttpOpen(L"VRChatDLSS5Cam (Windows; update check)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = WinHttpErrorText("WinHttpOpen"); return false; }
    WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    bool ok = false;
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET req = nullptr;
    if (!conn) { error = WinHttpErrorText("WinHttpConnect"); }
    else {
        req = WinHttpOpenRequest(conn, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!req) error = WinHttpErrorText("WinHttpOpenRequest");
    }
    if (req) {
        const wchar_t* headers = json ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n" : L"Accept: */*\r\n";
        WinHttpAddRequestHeaders(req, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) error = WinHttpErrorText("WinHttpSendRequest");
        else if (!WinHttpReceiveResponse(req, nullptr)) error = WinHttpErrorText("WinHttpReceiveResponse");
        else {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status != 200) {
                error = status == 403 || status == 429 ? StrPrintf("HTTP %lu (the GitHub request limit is reached; try again later)", (unsigned long)status)
                                                       : StrPrintf("HTTP %lu", (unsigned long)status);
            } else {
                DWORD64 length = 0; size = sizeof(length);
                if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64, WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX)) length = 0;
                std::vector<char> buf(64 * 1024);
                ok = true;
                for (;;) {
                    if (cancel) { error = "cancelled"; ok = false; break; }
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail)) { error = WinHttpErrorText("WinHttpQueryDataAvailable"); ok = false; break; }
                    if (avail == 0) break;
                    if (avail > buf.size()) avail = (DWORD)buf.size();
                    DWORD got = 0;
                    if (!WinHttpReadData(req, buf.data(), avail, &got)) { error = WinHttpErrorText("WinHttpReadData"); ok = false; break; }
                    if (got == 0) break;
                    if (!sink(buf.data(), got, length)) { if (error.empty()) error = "write failed"; ok = false; break; }
                }
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

// ---- A small JSON reader: enough for the release list.
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
    const Json* Find(const char* key) const {
        if (type != Object) return nullptr;
        for (const auto& kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string Str(const char* key) const { const Json* j = Find(key); return j && j->type == String ? j->str : std::string(); }
    bool Flag(const char* key) const { const Json* j = Find(key); return j && j->type == Bool && j->b; }
    double Num(const char* key) const { const Json* j = Find(key); return j && j->type == Number ? j->num : 0.0; }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : m_s(text) {}
    bool Parse(Json& out) { Skip(); if (!Value(out, 0)) return false; Skip(); return m_i == m_s.size(); }
private:
    void Skip() { while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t' || m_s[m_i] == '\n' || m_s[m_i] == '\r')) ++m_i; }
    bool Lit(const char* w) { const size_t n = strlen(w); if (m_s.compare(m_i, n, w) != 0) return false; m_i += n; return true; }
    static void PutUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(unsigned& v) {
        if (m_i + 4 > m_s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = m_s[m_i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool Text(std::string& out) {
        if (m_i >= m_s.size() || m_s[m_i] != '"') return false;
        ++m_i;
        while (m_i < m_s.size()) {
            const char c = m_s[m_i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (m_i >= m_s.size()) return false;
            const char e = m_s[m_i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && m_i + 6 <= m_s.size() && m_s[m_i] == '\\' && m_s[m_i + 1] == 'u') {
                    m_i += 2;
                    unsigned lo = 0;
                    if (!Hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                PutUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Value(Json& out, int depth) {
        if (depth > 64 || m_i >= m_s.size()) return false;
        const char c = m_s[m_i];
        if (c == '{') {
            out.type = Json::Object; ++m_i; Skip();
            if (m_i < m_s.size() && m_s[m_i] == '}') { ++m_i; return true; }
            for (;;) {
                Skip();
                std::string key;
                if (!Text(key)) return false;
                Skip();
                if (m_i >= m_s.size() || m_s[m_i] != ':') return false;
                ++m_i; Skip();
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.obj.emplace_back(std::move(key), std::move(v));
                Skip();
                if (m_i >= m_s.size()) return false;
                if (m_s[m_i] == ',') { ++m_i; continue; }
                if (m_s[m_i] == '}') { ++m_i; return true; }
                return false;
            }
        }
        if (c == '[') {
            out.type = Json::Array; ++m_i; Skip();
            if (m_i < m_s.size() && m_s[m_i] == ']') { ++m_i; return true; }
            for (;;) {
                Skip();
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.arr.push_back(std::move(v));
                Skip();
                if (m_i >= m_s.size()) return false;
                if (m_s[m_i] == ',') { ++m_i; continue; }
                if (m_s[m_i] == ']') { ++m_i; return true; }
                return false;
            }
        }
        if (c == '"') { out.type = Json::String; return Text(out.str); }
        if (Lit("true")) { out.type = Json::Bool; out.b = true; return true; }
        if (Lit("false")) { out.type = Json::Bool; out.b = false; return true; }
        if (Lit("null")) { out.type = Json::Null; return true; }
        const size_t start = m_i;
        while (m_i < m_s.size() && (isdigit((unsigned char)m_s[m_i]) || m_s[m_i] == '-' || m_s[m_i] == '+' || m_s[m_i] == '.' || m_s[m_i] == 'e' || m_s[m_i] == 'E')) ++m_i;
        if (m_i == start) return false;
        out.type = Json::Number;
        out.num = atof(m_s.substr(start, m_i - start).c_str());
        return true;
    }
    const std::string& m_s;
    size_t m_i = 0;
};

// "v1.10.2" -> 1, 10, 2. Anything after the numbers (a "-beta") is ignored.
bool ParseVersion(const std::string& text, int out[3]) {
    size_t i = 0;
    while (i < text.size() && (text[i] == 'v' || text[i] == 'V' || text[i] == ' ')) ++i;
    int parts = 0;
    while (parts < 3 && i < text.size() && isdigit((unsigned char)text[i])) {
        int v = 0;
        while (i < text.size() && isdigit((unsigned char)text[i])) v = v * 10 + (text[i++] - '0');
        out[parts++] = v;
        if (i < text.size() && text[i] == '.') ++i; else break;
    }
    for (int k = parts; k < 3; ++k) out[k] = 0;
    return parts >= 2;
}

int CompareVersion(const int a[3], const int b[3]) {
    for (int k = 0; k < 3; ++k) if (a[k] != b[k]) return a[k] < b[k] ? -1 : 1;
    return 0;
}

// The release notes are Markdown; the interface shows them as text, so the markers are taken off.
std::string PlainNotes(const std::string& md) {
    std::string out;
    size_t pos = 0;
    while (pos <= md.size()) {
        size_t eol = md.find('\n', pos);
        if (eol == std::string::npos) eol = md.size();
        std::string line = md.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t k = 0;
        while (k < line.size() && line[k] == '#') ++k;
        if (k > 0) { line = line.substr(k); while (!line.empty() && line.front() == ' ') line.erase(line.begin()); }
        std::string clean;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '`') continue;
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '*') { ++i; continue; }
            clean += line[i];
        }
        if (clean.compare(0, 2, "- ") == 0 || clean.compare(0, 2, "* ") == 0) clean = "\xE2\x80\xA2 " + clean.substr(2);
        out += clean;
        out += '\n';
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

bool RunHidden(const std::wstring& commandLine, DWORD& exitCode) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;   // CreateProcessW may write into the buffer
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, 300000);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveTree(full);
            else { SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(full.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

} // namespace

Updater::~Updater() { Cancel(); Join(); }

void Updater::Join() { if (m_thread.joinable()) m_thread.join(); }

void Updater::Cancel() { m_cancel = true; }

bool Updater::Busy() const { return m_busy; }

Updater::Status Updater::Get() const { std::lock_guard<std::mutex> lock(m_mutex); return m_status; }

void Updater::SetState(State st, const std::string& error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = st;
    m_status.error = error;
    ++m_status.generation;
}

void Updater::Check(const std::string& currentVersion, bool includePrerelease, bool manual, bool otherEdition) {
    if (m_busy) return;
    Join();
    m_cancel = false;
    m_busy = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.manual = manual;
        m_status.download = false;
        m_status.error.clear();
        m_status.state = State::Checking;
        ++m_status.generation;
    }
    m_thread = std::thread([this, currentVersion, includePrerelease, otherEdition]() {
        Release rel;
        rel.edition = otherEdition;   // kept when nothing is found, so the answer is told as the edition's
        bool newer = false;
        std::string error;
        if (RunCheck(currentVersion, includePrerelease, otherEdition, rel, newer, error)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status.release = rel;
            }
            SetState(newer ? State::Available : State::UpToDate);
        } else {
            SetState(State::Failed, error);
        }
        m_busy = false;
    });
}

bool Updater::RunCheck(const std::string& currentVersion, bool includePrerelease, bool otherEdition, Release& out, bool& newer, std::string& error) {
    std::string body;
    if (!HttpGet(kReleasesUrl, true, m_cancel, [&](const char* data, DWORD n, unsigned long long) {
            if (body.size() + n > 8u * 1024u * 1024u) return false;
            body.append(data, n);
            return true;
        }, error)) {
        Log::Warn("Update check failed: %s", error.c_str());
        return false;
    }
    Json root;
    if (!JsonReader(body).Parse(root) || root.type != Json::Array) {
        error = "unexpected answer from GitHub";
        Log::Warn("Update check: %s", error.c_str());
        return false;
    }
    int cur[3] = {};
    ParseVersion(currentVersion, cur);
    const char* asset = otherEdition ? kOtherAssetName : kAssetName;
    int best[3] = { -1, -1, -1 };
    bool found = false;
    for (const Json& r : root.arr) {
        if (r.type != Json::Object || r.Flag("draft")) continue;
        const bool pre = r.Flag("prerelease");
        if (pre && !includePrerelease) continue;
        int v[3] = {};
        if (!ParseVersion(r.Str("tag_name"), v)) continue;
        if (found && CompareVersion(v, best) <= 0) continue;
        std::string assetUrl;
        unsigned long long assetSize = 0;
        if (const Json* assets = r.Find("assets")) {
            if (assets->type == Json::Array) {
                for (const Json& a : assets->arr) {
                    if (a.Str("name") == asset) {
                        assetUrl = a.Str("browser_download_url");
                        assetSize = (unsigned long long)a.Num("size");
                    }
                }
            }
        }
        if (otherEdition && assetUrl.empty()) continue;   // an edition switch needs the archive: a release without it does not count
        found = true;
        for (int k = 0; k < 3; ++k) best[k] = v[k];
        out = Release();
        out.tag = r.Str("tag_name");
        out.version = StrPrintf("%d.%d.%d", v[0], v[1], v[2]);
        out.date = r.Str("published_at").substr(0, 10);
        out.notes = PlainNotes(r.Str("body"));
        out.pageUrl = r.Str("html_url");
        out.prerelease = pre;
        out.assetUrl = assetUrl;
        out.assetSize = assetSize;
        out.assetName = asset;
        out.edition = otherEdition;
    }
    // An edition switch takes this version again in the other edition; an update needs a newer one.
    newer = found && CompareVersion(best, cur) >= (otherEdition ? 0 : 1);
    // The stable channel while a newer pre-release is running: the newest full release is offered as the way back.
    if (found && !newer && !otherEdition && !includePrerelease && CompareVersion(best, cur) < 0) { newer = true; out.downgrade = true; }
    if (found) Log::Info("Update check: newest %s release%s is %s (this is %s)%s", includePrerelease ? "stable or pre-" : "stable",
                         otherEdition ? " of the other edition" : "", out.tag.c_str(), currentVersion.c_str(),
                         newer ? (otherEdition ? ": available" : out.downgrade ? ": older, offered as the way back to the stable channel" : ": newer") : "");
    else Log::Info("Update check: no release%s found on the %s channel", otherEdition ? " of the other edition" : "", includePrerelease ? "pre-release" : "stable");
    return true;
}

void Updater::Download(const std::wstring& exeDir, const std::wstring& stagingDir) {
    if (m_busy) return;
    Join();
    m_cancel = false;
    m_busy = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_status.release.assetUrl.empty()) { m_busy = false; return; }
        m_status.download = true;
        m_status.error.clear();
        m_status.downloadedMb = 0.0;
        m_status.totalMb = m_status.release.assetSize / (1024.0 * 1024.0);
        m_status.state = State::Downloading;
        ++m_status.generation;
    }
    m_thread = std::thread([this, exeDir, stagingDir]() {
        std::string error;
        if (RunDownload(exeDir, stagingDir, error)) SetState(State::Restarting);
        else if (m_cancel) SetState(State::Available);
        else SetState(State::Failed, error);
        m_busy = false;
    });
}

bool Updater::RunDownload(const std::wstring& exeDir, const std::wstring& stagingDir, std::string& error) {
    Release rel;
    { std::lock_guard<std::mutex> lock(m_mutex); rel = m_status.release; }
    // The program folder must take new files; a folder under Program Files usually does not.
    {
        const std::wstring probe = JoinPath(exeDir, L".update-write-test");
        HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            { std::lock_guard<std::mutex> lock(m_mutex); m_status.writable = false; }
            error = "the program folder cannot be written to";
            Log::Warn("Update: %s (%s)", error.c_str(), WideToUtf8(exeDir).c_str());
            return false;
        }
        CloseHandle(h);
    }
    RemoveTree(stagingDir);
    if (!CreateDirectories(stagingDir)) { error = "the update folder could not be created"; return false; }
    const std::wstring zipPath = JoinPath(stagingDir, Utf8ToWide(rel.assetName.empty() ? std::string(kAssetName) : rel.assetName));
    const std::wstring filesDir = JoinPath(stagingDir, L"files");
    Log::Info("Update: downloading %s", rel.assetUrl.c_str());
    FILE* f = nullptr;
    if (_wfopen_s(&f, zipPath.c_str(), L"wb") != 0 || !f) { error = "the download file could not be created"; return false; }
    unsigned long long got = 0;
    const bool ok = HttpGet(Utf8ToWide(rel.assetUrl), false, m_cancel, [&](const char* data, DWORD n, unsigned long long total) {
        if (fwrite(data, 1, n, f) != n) return false;
        got += n;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.downloadedMb = got / (1024.0 * 1024.0);
        if (total) m_status.totalMb = total / (1024.0 * 1024.0);
        return true;
    }, error);
    fclose(f);
    if (!ok) { Log::Warn("Update: download failed: %s", error.c_str()); return false; }
    Log::Info("Update: downloaded %.1f MB", got / (1024.0 * 1024.0));

    SetState(State::Extracting);
    CreateDirectories(filesDir);
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    DWORD code = 1;
    bool extracted = RunHidden(Quote(std::wstring(sys) + L"\\tar.exe") + L" -xf " + Quote(zipPath) + L" -C " + Quote(filesDir), code) && code == 0;
    if (!extracted) {
        Log::Info("Update: tar.exe did not unpack the file (%lu); trying PowerShell", (unsigned long)code);
        std::wstring ps = std::wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        extracted = RunHidden(Quote(ps) + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '" + zipPath +
                              L"' -DestinationPath '" + filesDir + L"' -Force\"", code) && code == 0;
    }
    if (!extracted) { error = "the download could not be unpacked"; Log::Warn("Update: %s", error.c_str()); return false; }
    if (!FileExists(JoinPath(filesDir, kExeName))) { error = "the download did not contain the program"; Log::Warn("Update: %s", error.c_str()); return false; }
    DeleteFileW(zipPath.c_str());

    // The swap runs in a script after this process has gone: wait for the process, copy the files over, start the
    // program again, tidy up.
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeName = exePath;
    const size_t slash = exeName.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exeName = exeName.substr(slash + 1);
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring script = std::wstring(temp) + L"VRChatDLSS5Cam-update-" + std::to_wstring(GetCurrentProcessId()) + L".cmd";
    std::wstring dst = exeDir;
    while (!dst.empty() && (dst.back() == L'\\' || dst.back() == L'/')) dst.pop_back();
    std::wstring text;
    text += L"@echo off\r\nsetlocal\r\n";
    text += L"set \"PID=" + std::to_wstring(GetCurrentProcessId()) + L"\"\r\n";
    text += L"set \"EXENAME=" + exeName + L"\"\r\n";
    text += L"set \"SRC=" + filesDir + L"\"\r\n";
    text += L"set \"DST=" + dst + L"\"\r\n";
    text += L"set \"STAGE=" + stagingDir + L"\"\r\n";
    text += L"set /a N=0\r\n";
    text += L":wait\r\n";
    text += L"tasklist /FI \"PID eq %PID%\" /NH /FO CSV 2>nul | findstr /I /C:\"%EXENAME%\" >nul\r\n";
    text += L"if errorlevel 1 goto go\r\n";
    text += L"set /a N+=1\r\n";
    text += L"if %N% GTR 60 goto go\r\n";
    text += L"ping -n 2 127.0.0.1 >nul\r\n";
    text += L"goto wait\r\n";
    text += L":go\r\n";
    text += L"xcopy \"%SRC%\\*\" \"%DST%\\\" /E /Y /Q /I /H >nul\r\n";
    text += L"start \"\" /D \"%DST%\" \"%DST%\\%EXENAME%\"\r\n";
    text += L"rd /S /Q \"%STAGE%\"\r\n";
    text += L"del \"%~f0\"\r\n";
    FILE* sf = nullptr;
    if (_wfopen_s(&sf, script.c_str(), L"wb") != 0 || !sf) { error = "the update script could not be written"; return false; }
    // The script is ANSI for cmd.exe; paths outside the system code page are handed over through 8.3 names.
    std::string ansi;
    {
        const int n = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::vector<char> buf((size_t)std::max(n, 1));
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, buf.data(), n, nullptr, nullptr);
        ansi = buf.data();
    }
    fwrite(ansi.data(), 1, ansi.size(), sf);
    fclose(sf);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"cmd.exe /C " + Quote(script);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, temp, &si, &pi)) {
        error = "the update script could not be started: " + LastErrorText();
        Log::Warn("Update: %s", error.c_str());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log::Info("Update: %s is unpacked; the program closes and the files are swapped by %s", rel.tag.c_str(), WideToUtf8(script).c_str());
    return true;
}

// ---- DLSS-NR-on-AMD installer ------------------------------------------------------------------------------------

PortSetup::~PortSetup() { Cancel(); Join(); }

void PortSetup::Join() { if (m_thread.joinable()) m_thread.join(); }

void PortSetup::Cancel() { m_cancel = true; }

bool PortSetup::Busy() const { return m_busy; }

PortSetup::Status PortSetup::Get() const { std::lock_guard<std::mutex> lock(m_mutex); return m_status; }

void PortSetup::SetState(State st, const std::string& error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = st;
    m_status.error = error;
    ++m_status.generation;
}

bool PortSetup::RunCheck(std::string& error) {
    std::string body;
    if (!HttpGet(kPortReleaseUrl, true, m_cancel, [&](const char* data, DWORD n, unsigned long long) {
            if (body.size() + n > 4u * 1024u * 1024u) return false;
            body.append(data, n);
            return true;
        }, error)) {
        Log::Warn("DLSS-NR-on-AMD: release lookup failed: %s", error.c_str());
        return false;
    }
    Json root;
    if (!JsonReader(body).Parse(root) || root.type != Json::Object) {
        error = "unexpected answer from GitHub";
        return false;
    }
    Status st;
    st.tag = root.Str("tag_name");
    st.date = root.Str("published_at").substr(0, 10);
    st.pageUrl = root.Str("html_url");
    if (const Json* assets = root.Find("assets")) {
        if (assets->type == Json::Array) {
            for (const Json& a : assets->arr) {
                if (a.Str("name") != kPortSetupName) continue;
                st.assetUrl = a.Str("browser_download_url");
                st.assetSize = (unsigned long long)a.Num("size");
            }
        }
    }
    if (st.tag.empty() || st.assetUrl.empty()) {
        error = StrPrintf("the latest release carries no %s", kPortSetupName);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.tag = st.tag; m_status.date = st.date; m_status.pageUrl = st.pageUrl;
        m_status.assetUrl = st.assetUrl; m_status.assetSize = st.assetSize;
    }
    Log::Info("DLSS-NR-on-AMD: latest release %s (%s), %s %.1f MB", st.tag.c_str(), st.date.c_str(), kPortSetupName, st.assetSize / (1024.0 * 1024.0));
    return true;
}

void PortSetup::Check() {
    if (m_busy) return;
    Join();
    m_cancel = false;
    m_busy = true;
    SetState(State::Checking);
    m_thread = std::thread([this] {
        std::string err;
        if (RunCheck(err)) SetState(State::Ready);
        else SetState(State::Failed, err);
        m_busy = false;
    });
}

void PortSetup::Install(const std::wstring& exeDir) {
    if (m_busy) return;
    Join();
    m_cancel = false;
    m_busy = true;
    m_thread = std::thread([this, exeDir] {
        std::string err;
        bool known;
        { std::lock_guard<std::mutex> lock(m_mutex); known = !m_status.assetUrl.empty(); }
        if (!known) {
            SetState(State::Checking);
            if (!RunCheck(err)) { SetState(State::Failed, err); m_busy = false; return; }
        }
        { std::lock_guard<std::mutex> lock(m_mutex); m_status.downloadedMb = 0.0; m_status.totalMb = m_status.assetSize / (1024.0 * 1024.0); }
        SetState(State::Downloading);
        if (!RunInstall(exeDir, err)) { SetState(State::Failed, err); m_busy = false; return; }
        m_busy = false;
    });
}

bool PortSetup::RunInstall(const std::wstring& exeDir, std::string& error) {
    std::string url;
    { std::lock_guard<std::mutex> lock(m_mutex); url = m_status.assetUrl; }
    const std::wstring path = JoinPath(exeDir, Utf8ToWide(kPortSetupName));
    const std::wstring part = path + L".part";
    Log::Info("DLSS-NR-on-AMD: downloading %s", url.c_str());
    FILE* f = nullptr;
    if (_wfopen_s(&f, part.c_str(), L"wb") != 0 || !f) { error = "the installer could not be written next to the executable"; return false; }
    unsigned long long got = 0;
    const bool ok = HttpGet(Utf8ToWide(url), false, m_cancel, [&](const char* data, DWORD n, unsigned long long total) {
        if (fwrite(data, 1, n, f) != n) return false;
        got += n;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.downloadedMb = got / (1024.0 * 1024.0);
        if (total) m_status.totalMb = total / (1024.0 * 1024.0);
        return true;
    }, error);
    fclose(f);
    if (!ok) { DeleteFileW(part.c_str()); return false; }
    if (!MoveFileExW(part.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        error = "the installer could not be placed: " + LastErrorText();
        DeleteFileW(part.c_str());
        return false;
    }
    { std::lock_guard<std::mutex> lock(m_mutex); m_status.setupPath = path; }

    const std::wstring weights = JoinPath(exeDir, PortSetup::kWeightsFile);
    // A first install answers the same prompts every time - accept the folder, accept the DLL name the setup
    // proposes for our executable (it detects that VRChatDLSS5Cam.exe loads version.dll) - and makes no real choice,
    // so it is run without a window and driven from here. The update / removal menu is a decision and keeps its own
    // window. If the silent run leaves no weights file (a machine the port refuses, or a folder that needs
    // administrator rights), the window opens so the reason is visible.
    if (!FileExists(weights)) {
        SetState(State::Installing);
        std::string out, herr;
        unsigned long code = 1;
        const bool ran = RunSetupHidden(path, exeDir, "\r\n\r\n\r\n\r\n\r\n\r\n", 300000, out, code, herr);
        // Keep the lines that carry words for the log; drop the banner and the progress bar (brackets, hashes, %).
        std::string words;
        std::string line;
        auto flush = [&] {
            if (std::any_of(line.begin(), line.end(), [](unsigned char c) { return std::isalpha(c); }))
                words += line + "\n";
            line.clear();
        };
        for (char c : out) { if (c == '\n' || c == '\r') flush(); else line += c; }
        flush();
        if (!words.empty()) Log::Info("DLSS-NR-on-AMD installer output:\n%s", words.c_str());
        if (ran && code == 0 && FileExists(weights)) {
            { std::lock_guard<std::mutex> lock(m_mutex); m_status.exitCode = code; }
            Log::Info("DLSS-NR-on-AMD: silent install finished (code %lu)", code);
            std::string tuned;
            TuneIni(exeDir, tuned);   // the restart that follows starts with the adjusted file
            SetState(State::Finished);
            return true;
        }
        if (m_cancel) return true;   // the application is closing; leave the window unopened
        const std::string why = ran ? StrPrintf("exit code %lu, no weights file", code) : herr;
        Log::Info("DLSS-NR-on-AMD: the silent install did not complete (%s); opening the installer window", why.c_str());
    }

    // Its own console window, started in the program folder: there it finds the executable and nvngx_dlssnr.dll.
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = path.c_str();
    sei.lpDirectory = exeDir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        error = "the installer could not be started: " + LastErrorText();
        return false;
    }
    Log::Info("DLSS-NR-on-AMD: installer started (%s)", WideToUtf8(path).c_str());
    SetState(State::Launched);
    while (WaitForSingleObject(sei.hProcess, 500) == WAIT_TIMEOUT) {
        if (m_cancel) { CloseHandle(sei.hProcess); return true; }   // the app is closing; the installer goes on by itself
    }
    DWORD code = 0;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    { std::lock_guard<std::mutex> lock(m_mutex); m_status.exitCode = code; }
    Log::Info("DLSS-NR-on-AMD: installer finished with code %lu", (unsigned long)code);
    std::string tuned;
    TuneIni(exeDir, tuned);   // an update may have written the file afresh; the restart that follows uses it
    SetState(State::Finished);
    return true;
}

// The port's settings file, written by its installer with values chosen for a game: the network runs inline, the
// frame's queue waiting for it on the GPU, with a budget of 200 ms after which the frame shows the previous frame's
// result. A game prefers a late frame; a picture or a video frame that is saved must carry its own result, so here
// the budget is raised to a second: the most the port's wait honours (its iteration cap, measured on an RX 9060 XT),
// enough for a 4K frame on that card (0.2 s, the first one 0.4-0.6 s) and under the driver's two-second hang
// detection. The inline mode is kept on, as the asynchronous one hands the result to the next frame. Only the
// values of keys the file already has are changed; every other byte of the file stays as it is.
bool PortSetup::TuneIni(const std::wstring& exeDir, std::string& changes) {
    changes.clear();
    const std::wstring path = JoinPath(exeDir, kIniFile);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string text;
    {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) { text.append(buf, n); if (text.size() > (1u << 20)) break; }
        CloseHandle(h);
    }
    static constexpr unsigned long kInlineWaitMs = 1000;
    std::string out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size(); else ++end;
        std::string line = text.substr(pos, end - pos);
        pos = end;
        std::string body = line, tail;
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) { tail.insert(tail.begin(), body.back()); body.pop_back(); }
        auto key = [&](const char* k) { return body.compare(0, strlen(k), k) == 0; };
        if (key("InlineWaitMs=")) {
            const unsigned long v = strtoul(body.c_str() + strlen("InlineWaitMs="), nullptr, 10);
            if (v < kInlineWaitMs) {
                changes += (changes.empty() ? "" : ", ") + StrPrintf("InlineWaitMs %lu -> %lu", v, kInlineWaitMs);
                line = StrPrintf("InlineWaitMs=%lu", kInlineWaitMs) + tail;
            }
        } else if (key("Inline=")) {
            const std::string v = body.substr(strlen("Inline="));
            if (v != "1") {
                changes += (changes.empty() ? "" : ", ") + ("Inline " + v + " -> 1");
                line = "Inline=1" + tail;
            }
        }
        out += line;
    }
    if (changes.empty()) return false;
    h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        Log::Warn("DLSS-NR-on-AMD: settings file %s could not be written (%s); %s left as they are",
                  WideToUtf8(path).c_str(), LastErrorText().c_str(), changes.c_str());
        changes.clear();
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(h, out.data(), (DWORD)out.size(), &written, nullptr) && written == out.size();
    CloseHandle(h);
    if (!ok) { Log::Warn("DLSS-NR-on-AMD: settings file %s was not written completely", WideToUtf8(path).c_str()); changes.clear(); return false; }
    Log::Info("DLSS-NR-on-AMD: settings file adjusted for saved frames: %s", changes.c_str());
    return true;
}

// Runs dlssnr_on_amd_setup.exe with no window, its standard input fed the answers and its output captured. The
// setup reads one line per prompt and writes a progress bar of many lines, so the output pipe is drained on a
// thread while this one waits for the process. Killed if the timeout passes or the application starts closing.
bool PortSetup::RunSetupHidden(const std::wstring& path, const std::wstring& exeDir, const std::string& answers,
                               unsigned timeoutMs, std::string& captured, unsigned long& exitCode, std::string& error) {
    exitCode = 1;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE inRd = nullptr, inWr = nullptr, outRd = nullptr, outWr = nullptr;
    if (!CreatePipe(&inRd, &inWr, &sa, 0)) { error = "input pipe: " + LastErrorText(); return false; }
    if (!CreatePipe(&outRd, &outWr, &sa, 0)) { error = "output pipe: " + LastErrorText(); CloseHandle(inRd); CloseHandle(inWr); return false; }
    // The ends this process keeps are not inherited by the child.
    SetHandleInformation(inWr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = inRd;
    si.hStdOutput = outWr;
    si.hStdError = outWr;
    std::wstring cmd = L"\"" + path + L"\"";   // no arguments: the folder is the working directory
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(path.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                        nullptr, exeDir.c_str(), &si, &pi);
    CloseHandle(inRd);    // owned by the child now (or unused if it did not start)
    CloseHandle(outWr);
    if (!started) {
        error = "the installer could not be started: " + LastErrorText();
        CloseHandle(inWr);
        CloseHandle(outRd);
        return false;
    }
    if (!answers.empty()) { DWORD wrote = 0; WriteFile(inWr, answers.data(), (DWORD)answers.size(), &wrote, nullptr); }
    CloseHandle(inWr);    // end of input: any further prompt reads end-of-file and takes its default

    std::string buf;
    std::thread reader([&] {
        char chunk[4096];
        DWORD n = 0;
        while (ReadFile(outRd, chunk, sizeof(chunk), &n, nullptr) && n) buf.append(chunk, n);
    });
    unsigned elapsed = 0;
    const DWORD step = 250;
    for (;;) {
        const DWORD w = WaitForSingleObject(pi.hProcess, step);
        if (w == WAIT_OBJECT_0) break;
        elapsed += step;
        if (m_cancel) { TerminateProcess(pi.hProcess, 1); error = "cancelled"; break; }
        if (elapsed >= timeoutMs) { TerminateProcess(pi.hProcess, 1); error = "the installer did not finish in time"; break; }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);   // settle after a possible terminate
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    reader.join();          // the child has gone, so its output end is closed and the reader has ended
    CloseHandle(outRd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    captured = buf;
    exitCode = code;
    return error.empty();
}

} // namespace vdc
