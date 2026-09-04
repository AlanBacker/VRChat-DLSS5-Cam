// VRChat DLSS5 Cam - entry point with a crash safety net.
//
// A release build must never die silently. Every fatal condition - an
// unhandled structured exception on any thread, a C runtime invalid-parameter
// report, std::terminate, a pure virtual call or an uncaught C++ exception -
// is appended to %LOCALAPPDATA%\VRChatDLSS5Cam\crash.txt and shown in a
// message box before the process exits.
#include "core/App.h"
#include "core/Util.h"
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <string>

#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING "0.0.0"
#endif

namespace {

std::atomic<bool> g_reported{false};
wchar_t           g_crashPath[MAX_PATH * 2] = {};

// Raw Win32 append. Deliberately avoids the CRT streams and the logger's mutex,
// both of which may be in an unknown state when a crash is being reported.
void AppendCrashFile(const std::wstring& text) {
    if (!g_crashPath[0]) return;
    HANDLE h = CreateFileW(g_crashPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const std::string u8 = vdc::WideToUtf8(text) + "\r\n";
    DWORD written = 0;
    WriteFile(h, u8.data(), (DWORD)u8.size(), &written, nullptr);
    CloseHandle(h);
}

std::wstring LocalTimestamp() {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t b[64];
    swprintf_s(b, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

std::wstring OsVersionText() {
    typedef LONG(WINAPI * PFN_RtlGetVersion)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi = {};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (HMODULE nt = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = (PFN_RtlGetVersion)GetProcAddress(nt, "RtlGetVersion")) {
            if (fn(&vi) == 0) {
                wchar_t b[64];
                swprintf_s(b, L"Windows %lu.%lu build %lu", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
                return b;
            }
        }
    }
    return L"Windows (unknown build)";
}

struct MessageBoxJob {
    std::wstring text;
    HANDLE       started = nullptr;
};

DWORD WINAPI MessageBoxThread(LPVOID param) {
    auto* job = static_cast<MessageBoxJob*>(param);
    SetEvent(job->started);
    MessageBoxW(nullptr, job->text.c_str(), L"VRChat DLSS5 Cam",
                MB_ICONERROR | MB_OK | MB_SETFOREGROUND | MB_TOPMOST);
    return 0;
}

// Shows the message box on a helper thread so that the crashing thread does not
// pump window messages (and re-enter the broken frame loop) while the box is
// open. Falls back to a direct call if the thread cannot start, e.g. because
// the loader lock is held.
void ShowFatalMessage(const std::wstring& text) {
    MessageBoxJob job;
    job.text = text;
    job.started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = job.started ? CreateThread(nullptr, 0, MessageBoxThread, &job, 0, nullptr) : nullptr;
    bool shown = false;
    if (thread) {
        if (WaitForSingleObject(job.started, 5000) == WAIT_OBJECT_0) {
            WaitForSingleObject(thread, INFINITE);
            shown = true;
        } else {
            TerminateThread(thread, 0);
        }
        CloseHandle(thread);
    }
    if (job.started) CloseHandle(job.started);
    if (!shown) {
        MessageBoxW(nullptr, text.c_str(), L"VRChat DLSS5 Cam", MB_ICONERROR | MB_OK | MB_SETFOREGROUND | MB_TOPMOST);
    }
}

void ReportFatal(const std::wstring& details) {
    // Only the first report matters; nested failures must not recurse.
    if (g_reported.exchange(true)) return;
    const std::wstring header = L"==== " + LocalTimestamp() + L"  VRChat DLSS5 Cam " +
                                vdc::Utf8ToWide(APP_VERSION_STRING) + L"  " + OsVersionText() + L" ====";
    AppendCrashFile(header + L"\r\n" + details + L"\r\n");
    const std::wstring msg = L"VRChat DLSS5 Cam encountered a fatal error and has to close.\n"
                             L"程序遇到致命错误，必须退出。\n\n" + details +
                             L"\n\nReport / 报告文件:\n" + g_crashPath +
                             L"\n\nPlease attach crash.txt and log.txt from that folder when reporting this.";
    ShowFatalMessage(msg);
}

const wchar_t* ExceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return L"ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:        return L"STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return L"ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return L"INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION:      return L"PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:         return L"IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT:            return L"BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return L"DATATYPE_MISALIGNMENT";
    case 0xE06D7363:                      return L"uncaught C++ exception";
    case 0xC0000409:                      return L"STACK_BUFFER_OVERRUN / FAST_FAIL";
    case 0xC0000374:                      return L"HEAP_CORRUPTION";
    case 0xC0000135:                      return L"DLL_NOT_FOUND";
    case 0xC0000139:                      return L"ENTRYPOINT_NOT_FOUND";
    default:                              return L"exception";
    }
}

std::wstring DescribeException(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return L"Unknown structured exception";
    const EXCEPTION_RECORD& r = *ep->ExceptionRecord;
    wchar_t b[512];
    swprintf_s(b, L"Exception 0x%08lX (%s) at 0x%p", (unsigned long)r.ExceptionCode, ExceptionName(r.ExceptionCode),
               r.ExceptionAddress);
    std::wstring s = b;
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)r.ExceptionAddress, &mod) && mod) {
        wchar_t path[MAX_PATH * 2] = {};
        GetModuleFileNameW(mod, path, (DWORD)(MAX_PATH * 2));
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        swprintf_s(b, L"\r\nModule: %s + 0x%llX", name,
                   (unsigned long long)((uintptr_t)r.ExceptionAddress - (uintptr_t)mod));
        s += b;
    }
    if ((r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        r.NumberParameters >= 2) {
        const wchar_t* kind = r.ExceptionInformation[0] == 0 ? L"Read" : r.ExceptionInformation[0] == 1 ? L"Write" : L"Execute";
        swprintf_s(b, L"\r\n%s of address 0x%p", kind, (void*)r.ExceptionInformation[1]);
        s += b;
    }
    swprintf_s(b, L"\r\nThread: %lu", GetCurrentThreadId());
    s += b;
    return s;
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    ReportFatal(DescribeException(ep));
    return EXCEPTION_EXECUTE_HANDLER;
}

void __cdecl OnInvalidParameter(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line,
                                uintptr_t) {
    std::wstring d = L"The C runtime reported an invalid parameter";
    if (expr && *expr) { d += L": "; d += expr; }
    if (func && *func) { d += L"\r\nFunction: "; d += func; }
    if (file && *file) {
        wchar_t b[32];
        swprintf_s(b, L" line %u", line);
        d += L"\r\nFile: "; d += file; d += b;
    }
    ReportFatal(d);
    TerminateProcess(GetCurrentProcess(), 3);
}

void __cdecl OnTerminate() {
    std::wstring d = L"std::terminate() was called";
    try {
        if (std::exception_ptr ex = std::current_exception()) std::rethrow_exception(ex);
    } catch (const std::exception& e) {
        d += L": "; d += vdc::Utf8ToWide(e.what());
    } catch (...) {
        d += L" (non-standard exception)";
    }
    ReportFatal(d);
    TerminateProcess(GetCurrentProcess(), 3);
}

void __cdecl OnPureCall() {
    ReportFatal(L"Pure virtual function call");
    TerminateProcess(GetCurrentProcess(), 3);
}

void InstallCrashHandlers() {
    const std::wstring path = vdc::JoinPath(vdc::GetAppDataDir(), L"crash.txt");
    wcsncpy_s(g_crashPath, path.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(CrashFilter);
    _set_invalid_parameter_handler(OnInvalidParameter);
    _set_purecall_handler(OnPureCall);
    std::set_terminate(OnTerminate);
}

// All C++ objects live in this frame; the SEH frame below must stay POD-only.
int RunApp(HINSTANCE hInstance, int nCmdShow) {
    try {
        vdc::App app;
        return app.Run(hInstance, nCmdShow);
    } catch (const std::exception& e) {
        ReportFatal(L"Unhandled C++ exception: " + vdc::Utf8ToWide(e.what()));
        return 1;
    } catch (...) {
        ReportFatal(L"Unhandled C++ exception of unknown type");
        return 1;
    }
}

#if defined(_MSC_VER)
// Plain function without objects that need unwinding, so __try/__except is legal.
int RunGuarded(HINSTANCE hInstance, int nCmdShow) {
    __try {
        return RunApp(hInstance, nCmdShow);
    } __except (CrashFilter(GetExceptionInformation())) {
        return 2;
    }
}
#else
int RunGuarded(HINSTANCE hInstance, int nCmdShow) { return RunApp(hInstance, nCmdShow); }
#endif

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    InstallCrashHandlers();
    return RunGuarded(hInstance, nCmdShow);
}
