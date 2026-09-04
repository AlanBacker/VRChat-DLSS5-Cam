#include "core/Util.h"
#include <shlobj.h>
#include <knownfolders.h>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <ctime>
#pragma comment(lib, "version.lib")

namespace vdc {

std::string WideToUtf8(std::wstring_view s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string StrPrintf(const char* fmt, ...) {
    char stackBuf[1024];
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int n = vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
    va_end(args);
    std::string out;
    if (n < 0) {
        out.clear();
    } else if ((size_t)n < sizeof(stackBuf)) {
        out.assign(stackBuf, (size_t)n);
    } else {
        out.resize((size_t)n + 1);
        vsnprintf(out.data(), out.size(), fmt, copy);
        out.resize((size_t)n);
    }
    va_end(copy);
    return out;
}

std::string FormatHr(HRESULT hr) {
    char* msg = nullptr;
    const DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, (DWORD)hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&msg, 0, nullptr);
    std::string text = StrPrintf("0x%08X", (unsigned)hr);
    if (len && msg) {
        std::string m(msg, len);
        while (!m.empty() && (m.back() == '\r' || m.back() == '\n' || m.back() == ' ')) m.pop_back();
        text += " (" + m + ")";
    }
    if (msg) LocalFree(msg);
    return text;
}

std::string LastErrorText(DWORD err) {
    return FormatHr(HRESULT_FROM_WIN32(err));
}

std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring path(buf, n);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(0, slash);
}

static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

std::wstring GetAppDataDir() {
    std::wstring base = KnownFolder(FOLDERID_LocalAppData);
    if (base.empty()) base = GetExeDir();
    std::wstring dir = JoinPath(base, L"VRChatDLSS5Cam");
    CreateDirectories(dir);
    return dir;
}

std::wstring GetPicturesDir() {
    std::wstring p = KnownFolder(FOLDERID_Pictures);
    if (p.empty()) p = GetExeDir();
    return p;
}

std::wstring GetWindowsFontsDir() {
    std::wstring p = KnownFolder(FOLDERID_Fonts);
    if (p.empty()) p = L"C:\\Windows\\Fonts";
    return p;
}

std::wstring JoinPath(std::wstring_view a, std::wstring_view b) {
    std::wstring out(a);
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') out += L'\\';
    out += b;
    return out;
}

bool FileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool CreateDirectories(const std::wstring& path) {
    if (path.empty()) return false;
    if (DirectoryExists(path)) return true;
    std::wstring cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            if (!cur.empty() && cur.back() != L':' && !DirectoryExists(cur)) {
                if (!CreateDirectoryW(cur.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
            }
            if (i < path.size()) cur += path[i];
        } else {
            cur += path[i];
        }
    }
    return DirectoryExists(path);
}

std::string GetFileVersionString(const std::wstring& path) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size) return {};
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", (LPVOID*)&info, &len) || !info) return {};
    return StrPrintf("%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                     HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
}

uint64_t GetFileSizeBytes(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

double NowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

std::wstring TimestampForFileName() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04u-%02u-%02u_%02u-%02u-%02u.%03u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

std::string TimestampForLog() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return StrPrintf("%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

long SehFilter(unsigned long code, unsigned long* outCode) noexcept {
    if (outCode) *outCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace vdc
