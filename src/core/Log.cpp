#include "core/Log.h"
#include "core/Util.h"
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <share.h>

namespace vdc::Log {

namespace {
std::mutex             g_mutex;
FILE*                  g_file = nullptr;
std::wstring           g_path;
std::deque<LogEntry>   g_ring;
size_t                 g_count = 0;
unsigned               g_generation = 0;
constexpr size_t       kRingMax = 600;

void WriteV(LogLevel level, const char* fmt, va_list args) {
    char buf[4096];
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    std::string text = n < 0 ? std::string(fmt) : std::string(buf, (size_t)std::min<int>(n, (int)sizeof(buf) - 1));
    const char* tag = level == LogLevel::Error ? "ERROR" : level == LogLevel::Warn ? "WARN " : "INFO ";
    const std::string time = TimestampForLog();

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fprintf(g_file, "[%s] %s %s\r\n", time.c_str(), tag, text.c_str());
        fflush(g_file);
    }
    const std::string dbg = "[VRChatDLSS5Cam] " + std::string(tag) + " " + text + "\n";
    OutputDebugStringA(dbg.c_str());
    g_ring.push_back(LogEntry{level, time, std::move(text)});
    if (g_ring.size() > kRingMax) g_ring.pop_front();
    ++g_count;
    ++g_generation;
}
} // namespace

// The logs of the earlier sessions stay next to the current one: log.txt becomes log-1.txt, log-1.txt becomes
// log-2.txt and so on up to log-5.txt, which is dropped. Someone looks for the log after a restart, when the session
// it concerns is over; a log wiped at every start was gone by then. A file another process still holds open stays
// where it is (the move fails) and is continued.
void Rotate(const std::wstring& filePath) {
    const size_t slash = filePath.find_last_of(L"\\/");
    const size_t dot = filePath.find_last_of(L'.');
    const bool hasExt = dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
    const std::wstring stem = hasExt ? filePath.substr(0, dot) : filePath;
    const std::wstring ext = hasExt ? filePath.substr(dot) : std::wstring();
    auto kept = [&](int i) { return stem + L"-" + std::to_wstring(i) + ext; };
    DeleteFileW(kept(kKeptLogs).c_str());
    for (int i = kKeptLogs - 1; i >= 1; --i) MoveFileExW(kept(i).c_str(), kept(i + 1).c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(filePath.c_str(), kept(1).c_str(), MOVEFILE_REPLACE_EXISTING);
}

void Init(const std::wstring& filePath, bool append) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = filePath;
    if (g_file) fclose(g_file);
    if (!append) Rotate(filePath);
    // Binary mode on purpose: opening with "ccs=UTF-8" switches the stream to
    // the CRT's Unicode text mode, where narrow fprintf() is an invalid
    // parameter and the release runtime terminates the process silently.
    g_file = _wfsopen(filePath.c_str(), append ? L"ab" : L"wb", _SH_DENYNO);
    if (g_file) {
        bool fresh = !append;
        if (append) { fseek(g_file, 0, SEEK_END); fresh = ftell(g_file) == 0; }
        if (fresh) {
            static const unsigned char kBom[3] = { 0xEF, 0xBB, 0xBF };
            fwrite(kBom, 1, sizeof(kBom), g_file);
        }
        fflush(g_file);
    }
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) fclose(g_file);
    g_file = nullptr;
}

void Write(LogLevel level, const char* fmt, ...) { va_list a; va_start(a, fmt); WriteV(level, fmt, a); va_end(a); }
void Info(const char* fmt, ...)  { va_list a; va_start(a, fmt); WriteV(LogLevel::Info, fmt, a);  va_end(a); }
void Warn(const char* fmt, ...)  { va_list a; va_start(a, fmt); WriteV(LogLevel::Warn, fmt, a);  va_end(a); }
void Error(const char* fmt, ...) { va_list a; va_start(a, fmt); WriteV(LogLevel::Error, fmt, a); va_end(a); }

void Hr(LogLevel level, const char* what, long hr) {
    Write(level, "%s failed: %s", what, FormatHr((HRESULT)hr).c_str());
}

std::vector<LogEntry> Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::vector<LogEntry>(g_ring.begin(), g_ring.end());
}

size_t Count() { std::lock_guard<std::mutex> lock(g_mutex); return g_count; }
unsigned Generation() { std::lock_guard<std::mutex> lock(g_mutex); return g_generation; }
const std::wstring& FilePath() { return g_path; }

} // namespace vdc::Log
