// VRChat DLSS5 Cam - small shared helpers (strings, paths, time, HRESULT text).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vdc {

using Microsoft::WRL::ComPtr;

std::string  WideToUtf8(std::wstring_view s);
std::wstring Utf8ToWide(std::string_view s);
std::string  StrPrintf(const char* fmt, ...);
std::string  FormatHr(HRESULT hr);
std::string  LastErrorText(DWORD err = GetLastError());

std::wstring GetExeDir();                 // folder of the running executable, no trailing slash
std::wstring GetAppDataDir();             // %LOCALAPPDATA%\VRChatDLSS5Cam (created on demand)
std::wstring GetPicturesDir();            // user's Pictures folder
std::wstring GetWindowsFontsDir();        // C:\Windows\Fonts
std::wstring JoinPath(std::wstring_view a, std::wstring_view b);
bool         FileExists(const std::wstring& path);
bool         DirectoryExists(const std::wstring& path);
bool         CreateDirectories(const std::wstring& path);
std::string  GetFileVersionString(const std::wstring& path);   // "310.8.0.0" or ""
uint64_t     GetFileSizeBytes(const std::wstring& path);

double       NowSeconds();                // monotonic seconds
std::wstring TimestampForFileName();      // 2026-09-04_12-34-56.789
std::string  TimestampForLog();           // 12:34:56.789

// Structured exception filter used by the NGX/NVOF wrappers. Stores the code
// and asks the runtime to execute the handler.
long SehFilter(unsigned long code, unsigned long* outCode) noexcept;

#if defined(_MSC_VER)
#define VDC_SEH_TRY __try
#define VDC_SEH_EXCEPT(codeVar) __except (::vdc::SehFilter(GetExceptionCode(), &(codeVar)))
#else
// Non-MSVC compilers (used only for syntax checking) have no SEH.
#define VDC_SEH_TRY if (true)
#define VDC_SEH_EXCEPT(codeVar) else
#endif

} // namespace vdc
