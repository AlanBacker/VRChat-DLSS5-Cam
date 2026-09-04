// VRChat DLSS5 Cam - logging to file, debugger and an in-memory ring for the UI.
#pragma once
#include <string>
#include <vector>

namespace vdc {

enum class LogLevel { Info = 0, Warn = 1, Error = 2 };

struct LogEntry {
    LogLevel    level;
    std::string time;   // HH:MM:SS.mmm
    std::string text;
};

namespace Log {
void Init(const std::wstring& filePath);
void Shutdown();
void Write(LogLevel level, const char* fmt, ...);
void Info(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Error(const char* fmt, ...);
void Hr(LogLevel level, const char* what, long hr);          // "<what> failed: 0x.... (text)"
std::vector<LogEntry> Snapshot();                              // copy of recent entries
size_t Count();
unsigned Generation();                                         // increments on every write
const std::wstring& FilePath();
} // namespace Log

} // namespace vdc
