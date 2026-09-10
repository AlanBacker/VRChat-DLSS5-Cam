#include "core/App.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Util.h"
#include "gfx/FsrHost.h"
#include "ui/Theme.h"
#include "../../resources/resource.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <timeapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cwctype>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace vdc {

using Microsoft::WRL::ComPtr;

namespace {
std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}
constexpr int      kHotkeyId = 1;
constexpr UINT_PTR kSizeTimer = 1;
constexpr const wchar_t* kWindowClass = L"VRChatDLSS5CamWindow";
constexpr const wchar_t* kProjectUrl = L"https://github.com/AlanBacker/VRChat-DLSS5-Cam";
constexpr const wchar_t* kImagePatterns =
    L"*.png;*.jpg;*.jpeg;*.jpe;*.jfif;*.bmp;*.dib;*.tif;*.tiff;*.gif;*.webp;*.heic;*.heif;*.avif;*.jxr;*.wdp;*.hdp;*.ico;*.dds";
constexpr const wchar_t* kVideoPatterns =
    L"*.mp4;*.m4v;*.mov;*.mkv;*.webm;*.avi;*.wmv;*.mpg;*.mpeg;*.ts;*.m2ts;*.mts;*.3gp;*.3g2;*.flv;*.asf";
constexpr int kVideoHeldRetries = 10;   // passes a video frame gets to produce output before it is skipped

// Still images: the neural network is temporal, so a picture is run through it several times until the result
// settles. Opening a picture (or loading the runtime) starts a longer run, a slider change a shorter one.
constexpr int kImageConvergePasses = 32;
constexpr int kImageSettingsPasses = 24;
constexpr double kPerfLogInterval = 15.0;
constexpr UINT WM_COPYGLOBALDATA = 0x0049;

// Media library: thumbnails live in one atlas together with the seek-bar pictures of the opened video.
constexpr int    kStorySlots = 40;          // seek-bar pictures over the length of the video
constexpr int    kLibraryMax = 200;         // items (atlas cells left after the seek bar)
constexpr double kHoverDebounce = 0.04;     // seconds between frame requests while the cursor moves on the seek bar

std::string HotkeyText(const Settings& s) {
    if (!s.hotkeyEnabled) return "-";
    std::string t;
    if (s.hotkeyModifiers & MOD_CONTROL) t += "Ctrl+";
    if (s.hotkeyModifiers & MOD_ALT) t += "Alt+";
    if (s.hotkeyModifiers & MOD_SHIFT) t += "Shift+";
    if (s.hotkeyModifiers & MOD_WIN) t += "Win+";
    const unsigned vk = s.hotkeyKey;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) t += (char)vk;
    else if (vk >= VK_F1 && vk <= VK_F12) t += StrPrintf("F%u", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) t += StrPrintf("Num%u", vk - VK_NUMPAD0);
    else {
        switch (vk) {
        case VK_SPACE: t += "Space"; break; case VK_INSERT: t += "Insert"; break; case VK_SNAPSHOT: t += "PrintScreen"; break;
        case VK_PAUSE: t += "Pause"; break; case VK_HOME: t += "Home"; break; case VK_END: t += "End"; break;
        case VK_PRIOR: t += "PageUp"; break; case VK_NEXT: t += "PageDown"; break;
        default: t += StrPrintf("VK%02X", vk); break;
        }
    }
    return t;
}

std::string FormatLabel(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "RGBA16F";
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return "RGBA32F";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "RG11B10F";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "RGB10A2";
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "BGRA8";
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_TYPELESS: return "BGRX8";
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "RGBA8";
    default: return StrPrintf("DXGI %u", (unsigned)f);
    }
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring LowerExtension(const std::wstring& path) {
    const std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = name.substr(dot + 1);
    for (wchar_t& c : ext) c = (wchar_t)std::towlower(c);
    return ext;
}

// Same file, ignoring case and the slash direction.
bool SamePath(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t x = a[i], y = b[i];
        if (x == L'/') x = L'\\';
        if (y == L'/') y = L'\\';
        if (std::towlower(x) != std::towlower(y)) return false;
    }
    return true;
}

// Files of a folder (not its subfolders), sorted by name.
std::vector<std::wstring> ListFolderFiles(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(JoinPath(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) continue;
        out.push_back(JoinPath(dir, fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return out;
}

void NameCurrentThread(const wchar_t* name) {
    typedef HRESULT(WINAPI * PFN_SetThreadDescription)(HANDLE, PCWSTR);
    if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll"))
        if (auto fn = (PFN_SetThreadDescription)GetProcAddress(k32, "SetThreadDescription")) fn(GetCurrentThread(), name);
}

// "90", "1:30", "1:02:03.5" -> seconds; negative when empty.
double ParseSeconds(const wchar_t* s) {
    if (!s || !*s) return -1.0;
    double t = 0.0;
    std::wstring cur;
    for (const wchar_t* p = s;; ++p) {
        if (*p == L':' || *p == 0) {
            t = t * 60.0 + (cur.empty() ? 0.0 : _wtof(cur.c_str()));
            cur.clear();
            if (!*p) break;
        } else {
            cur += *p;
        }
    }
    return t;
}

// "1280x800".
bool ParseSize(const wchar_t* s, UINT& w, UINT& h) {
    if (!s) return false;
    wchar_t* end = nullptr;
    const unsigned long a = wcstoul(s, &end, 10);
    if (!end || (*end != L'x' && *end != L'X')) return false;
    const unsigned long b = wcstoul(end + 1, &end, 10);
    if (a < 320 || b < 240 || a > 16384 || b > 16384) return false;
    w = (UINT)a; h = (UINT)b;
    return true;
}

int LanguageCode(const std::wstring& s) {
    if (s == L"auto") return 0;
    if (s == L"en") return 1;
    if (s == L"zh") return 2;
    if (s == L"ja") return 3;
    if (s == L"ko") return 4;
    return -1;
}
} // namespace

// ------------------------------------------------------------------------------------------

CommandLine CommandLine::Parse() {
    CommandLine cl;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return cl;
    auto next = [&](int& i) -> const wchar_t* { return i + 1 < argc ? argv[++i] : nullptr; };
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--headless") cl.headless = true;
        else if (a == L"--window") { const wchar_t* v = next(i); if (!ParseSize(v, cl.width, cl.height) && cl.error.empty()) cl.error = "--window expects WIDTHxHEIGHT"; }
        else if (a == L"--open") { if (const wchar_t* v = next(i)) cl.open = v; }
        else if (a == L"--add") { if (const wchar_t* v = next(i)) cl.add.push_back(v); }
        else if (a == L"--seek") cl.seek = ParseSeconds(next(i));
        else if (a == L"--play") cl.play = true;
        else if (a == L"--in") cl.in = ParseSeconds(next(i));
        else if (a == L"--out") cl.out = ParseSeconds(next(i));
        else if (a == L"--lang") { const wchar_t* v = next(i); cl.language = v ? LanguageCode(v) : -1; if (cl.language < 0 && cl.error.empty()) cl.error = "--lang expects en, zh, ja, ko or auto"; }
        else if (a == L"--screenshot") {
            const double t = ParseSeconds(next(i));
            const wchar_t* path = next(i);
            if (t >= 0.0 && path) cl.screenshots.emplace_back(t, path);
            else if (cl.error.empty()) cl.error = "--screenshot expects <seconds> <file.png>";
        }
        else if (a == L"--process") {
            cl.process = true;
            if (i + 1 < argc && argv[i + 1][0] != L'-') cl.processDir = argv[++i];
        }
        else if (a == L"--exit-after") cl.exitAfter = ParseSeconds(next(i));
        else if (a == L"--update") cl.update = true;
        else if (a == L"--edition") {
            const wchar_t* v = next(i);
            std::wstring e = v ? v : L"";
            for (wchar_t& c : e) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c + 32);
            if (e == L"amd" || e == L"radeon") cl.edition = 2;
            else if (e == L"geforce" || e == L"nvidia") cl.edition = 1;
            else if (cl.error.empty()) cl.error = "--edition expects geforce or amd";
        }
        else if (a == L"--splash-dump") { if (const wchar_t* v = next(i)) cl.splashDump = v; }
        else if (a == L"--set") {
            const wchar_t* v = next(i);
            const std::string kv = v ? WideToUtf8(v) : std::string();
            const size_t eq = kv.find('=');
            if (eq != std::string::npos && eq > 0) cl.sets.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
            else if (cl.error.empty()) cl.error = "--set expects key=value";
        }
        else if (a == L"--data-dir") { if (const wchar_t* v = next(i)) cl.dataDir = v; }
        else if (!a.empty() && a[0] != L'-' && cl.open.empty() && FileExists(a)) cl.open = a;   // "Open with"
        else if (cl.error.empty()) cl.error = "unknown option " + WideToUtf8(a);
    }
    LocalFree(argv);
    return cl;
}

// ------------------------------------------------------------------------------------------

int App::Run(HINSTANCE hInstance, int nCmdShow) {
    if (!Init(hInstance, nCmdShow)) {
        if (m_ranAgain) return m_exitCode;   // nothing of this instance's to shut down: no window, device or settings
        Shutdown();
        return m_exitCode != 0 ? m_exitCode : 1;
    }
    MSG msg{};
    while (!m_quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_quit = true;
                if (m_exitCode == 0) m_exitCode = (int)msg.wParam;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_quit) break;
        if (m_minimized && !m_headless) {
            // Nothing to draw; processing carries on in its own thread. Sleep until a message arrives (or 100 ms),
            // keep settings persisted and keep capture results flowing into the log.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            DrainNotices();
            if (m_settingsDirtyTime >= 0.0 && NowSeconds() - m_settingsDirtyTime > 1.0) SaveSettings();
            continue;
        }
        Frame();
    }
    Shutdown();
    return m_exitCode;
}

void App::FatalMessage(const std::wstring& text) {
    Log::Error("%s", WideToUtf8(text).c_str());
    if (m_headless) return;
    MessageBoxW(m_hwnd, text.c_str(), L"VRChat DLSS5 Cam", MB_ICONERROR | MB_OK);
}

void App::ApplyCommandLineSettings() {
    for (const auto& kv : m_cli.sets) {
        if (m_settings.Apply(kv.first, kv.second)) Log::Info("Command line: %s=%s", kv.first.c_str(), kv.second.c_str());
        else Log::Warn("Command line: unknown setting %s", kv.first.c_str());
    }
    if (m_cli.language >= 0) m_settings.language = m_cli.language;
    if (!m_cli.processDir.empty()) m_settings.captureFolder = WideToUtf8(m_cli.processDir);
    if (m_headless) { m_settings.vsync = false; m_settings.windowMaximized = false; }
    m_settings.Clamp();
}

bool App::Init(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;
    m_cli = CommandLine::Parse();
    m_headless = m_cli.headless;
    m_startTime = NowSeconds();
    ImGui_ImplWin32_EnableDpiAwareness();
    m_exeDir = GetExeDir();
    m_appDataDir = GetAppDataDir();
    m_settingsPath = JoinPath(m_appDataDir, L"settings.ini");
    m_presetsPath = JoinPath(m_appDataDir, L"presets.txt");
    Log::Init(JoinPath(m_appDataDir, L"log.txt"));
    Log::Info("VRChat DLSS5 Cam %s%s starting%s", APP_VERSION_STRING, APP_EDITION_AMD ? " Radeon edition" : "", m_headless ? " (headless)" : "");
    Log::Info("Executable folder: %s", WideToUtf8(m_exeDir).c_str());
    Log::Info("Command line: %s", WideToUtf8(GetCommandLineW()).c_str());
    if (!m_cli.error.empty()) Log::Warn("Command line: %s", m_cli.error.c_str());

#if APP_EDITION_AMD
    // DLSS-NR-on-AMD read its settings file when it loaded, before this code ran. If the file still carries the
    // values its installer chose for a game (PortSetup::TuneIni), it is adjusted now and the run handed to a second
    // instance started with the same command line, which finds the file adjusted; this one waits for it and
    // returns its exit code, so a script that started the program sees one run. A second instance never starts
    // another: it is marked by an environment variable, and the file no longer changes.
    {
        wchar_t adjusted[512] = {};
        if (GetEnvironmentVariableW(L"VDC_PORT_INI_ADJUSTED", adjusted, 512)) {
            Log::Info("DLSS-NR-on-AMD: settings file adjusted for saved frames by the first start (%s)", WideToUtf8(adjusted).c_str());
        } else {
            std::string changes;
            if (PortSetup::TuneIni(m_exeDir, changes)) {
                Log::Info("DLSS-NR-on-AMD: starting the program again so the adjusted settings file applies");
                // This instance's port writes its hook lines into the log the second instance reads at its own start;
                // they must be in the file before that instance's start line (see FsrHost::WaitForPortHooks).
                FsrHost::WaitForPortHooks(m_exeDir, "starting the program again now");
                Log::Shutdown();   // the second instance takes the log file over
                m_ranAgain = true;
                m_exitCode = RunAgainAndWait(changes);
                return false;
            }
        }
    }
#endif

    Log::Info("Settings file: %s", WideToUtf8(m_settingsPath).c_str());
    m_settings.Load(m_settingsPath);
    m_settings.Clamp();
    ApplyCommandLineSettings();
    I18n::SetLanguage(I18n::FromSetting(m_settings.language));
    Log::Info("Language: %s", I18n::LanguageName(I18n::Current()));
    // The start-up card covers the time until the main window has its first frame.
    ReadSystemTheme();
    if (!m_cli.splashDump.empty()) m_splash.SetDumpPath(m_cli.splashDump);
    if (!m_headless) m_splash.Show(hInstance, m_settings.theme == 2 || (m_settings.theme == 0 && m_systemLight), APP_VERSION_STRING, APP_PRERELEASE != 0, TR(SplashStarting));
    LoadPresets();

    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hrCo)) Log::Hr(LogLevel::Warn, "CoInitializeEx", hrCo);

    Log::Info("Creating main window");
    m_splash.SetStatus(TR(SplashWindow));
    if (!CreateMainWindow(hInstance, nCmdShow)) return false;
    Log::Info("Main window created (DPI scale %.2f)", m_dpiScale);

    std::wstring err;
    Log::Info("Initialising Direct3D 12");
    m_splash.SetStatus(TR(SplashGpu));
    RECT client{};
    GetClientRect(m_hwnd, &client);
    if (m_headless && m_cli.width > 0 && m_cli.height > 0) { client = RECT{ 0, 0, (LONG)m_cli.width, (LONG)m_cli.height }; }
    if (!m_device.Init(m_hwnd, m_settings.debugLayer, err, m_headless, (UINT)std::max(1L, client.right - client.left),
                       (UINT)std::max(1L, client.bottom - client.top))) {
        FatalMessage(Utf8ToWide(TR(InitFailed)) + L"\n\n" + err);
        return false;
    }
    m_deviceReady = true;
    const AdapterInfo& ai = m_device.Info();
    Log::Info("Adapter: %s (vendor 0x%04X, %llu MB), driver %s", WideToUtf8(ai.name).c_str(), ai.vendorId,
              (unsigned long long)(ai.dedicatedVideoMemory >> 20),
              WideToUtf8(ai.nvidiaDriverVersion.empty() ? ai.driverVersion : ai.nvidiaDriverVersion).c_str());

    Log::Info("Initialising render pipeline");
    m_splash.SetStatus(TR(SplashPipeline));
    if (!m_pipeline.Init(m_device, m_exeDir, m_appDataDir, err)) {
        FatalMessage(Utf8ToWide(TR(InitFailed)) + L"\n\n" + err);
        return false;
    }
    Log::Info("Initialising Spout receiver");
    if (!m_spout.Init(m_device)) {
        FatalMessage(Utf8ToWide(TR(InitFailed)) + L"\n\nSpout");
        return false;
    }
    m_spout.SetRequestedSender(m_settings.senderName);
    if (!m_capture.Init()) Log::Warn("Capture worker failed to start; photo capture is unavailable");

    Log::Info("Initialising UI");
    m_splash.SetStatus(TR(SplashUi));
    if (!InitImGui()) return false;
    if (!m_headless) RegisterHotkey();

    // Accept dropped pictures (also from a non-elevated Explorer when this process runs elevated).
    DragAcceptFiles(m_hwnd, TRUE);
    ChangeWindowMessageFilterEx(m_hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);

    std::string aerr;
    if (!m_atlas.Init(m_device, aerr)) Log::Warn("Thumbnail atlas unavailable: %s", aerr.c_str());
    m_scanner.Start(m_device.Adapter());

    m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    PushSettings();
    // On the FSR host route the NVIDIA runtime stays unloaded; the pipeline loads the FSR runtime itself.
    if (EffectiveRoute() != RouteFsrHost) RequestRuntimeLoad(false);
#if APP_EDITION_AMD
    if (!m_headless && !m_cli.process && m_settings.updateCheck) m_portSetup.Check();
#endif
    if (EffectiveRoute() == RouteFsrHost) LogPortState(false);
    // --edition: the other edition of this program is fetched and swapped in like an update, without a click.
    const bool otherEdition = m_cli.edition == (APP_EDITION_AMD ? 1 : 2);
    if (m_cli.edition && !otherEdition) Log::Info("--edition: this is the %s edition already", APP_EDITION_AMD ? "Radeon" : "GeForce");
    if (!m_headless && otherEdition) m_updater.Check(APP_VERSION_STRING, true, true, true);
    else if (!m_headless && (m_cli.update || (!m_cli.process && m_settings.updateCheck))) m_updater.Check(APP_VERSION_STRING, m_settings.updateChannel == 1, false);
    // The previous session's file comes back only when the user asked for that.
    if (m_cli.open.empty() && m_settings.reopenLast) {
        if (m_settings.sourceMode == SourceImage && !m_settings.imagePath.empty()) {
            const std::wstring path = Utf8ToWide(m_settings.imagePath);
            if (FileExists(path)) {
                Command c; c.type = Command::LoadImage; c.path = path;
                PostCommand(std::move(c));
            } else {
                Log::Warn("Image from the previous session not found: %s", m_settings.imagePath.c_str());
            }
        }
        if (m_settings.sourceMode == SourceVideo && !m_settings.videoPath.empty()) {
            const std::wstring path = Utf8ToWide(m_settings.videoPath);
            if (FileExists(path)) {
                Command c; c.type = Command::LoadVideo; c.path = path;
                PostCommand(std::move(c));
            } else {
                Log::Warn("Video from the previous session not found: %s", m_settings.videoPath.c_str());
            }
        }
    }
    StartWorker();
    Log::Info("Startup complete");

    m_lastFrameTime = NowSeconds();
    return true;
}

// GetDpiForSystem only exists on Windows 10 1607+, so resolve it at run time
// instead of importing it statically (a missing import kills the process in the
// loader before any of our code runs).
static float SystemDpiScale() {
    typedef UINT(WINAPI * PFN_GetDpiForSystem)(void);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto fn = (PFN_GetDpiForSystem)GetProcAddress(user32, "GetDpiForSystem")) {
            const UINT dpi = fn();
            if (dpi > 0) return (float)dpi / 96.0f;
        }
    }
    float scale = 1.0f;
    if (HDC dc = GetDC(nullptr)) {
        scale = (float)GetDeviceCaps(dc, LOGPIXELSX) / 96.0f;
        ReleaseDC(nullptr, dc);
    }
    return scale > 0.0f ? scale : 1.0f;
}

bool App::CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &App::WndProcThunk;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(14, 15, 19));
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) {
        Log::Error("RegisterClassExW failed: %s", LastErrorText().c_str());
        return false;
    }

    // Restore the previous geometry when it is still on a monitor; otherwise centre on the primary work area.
    // --window WxH asks for that client size instead.
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = m_settings.windowWidth, h = m_settings.windowHeight;
    bool restored = false;
    if (m_cli.width > 0 && m_cli.height > 0) {
        RECT rc{ 0, 0, (LONG)m_cli.width, (LONG)m_cli.height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
        x = 40; y = 40;
        restored = true;
    } else if (!m_headless && (m_settings.windowX != -1 || m_settings.windowY != -1)) {
        RECT rc{ m_settings.windowX, m_settings.windowY, m_settings.windowX + w, m_settings.windowY + h };
        if (MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) { x = rc.left; y = rc.top; restored = true; }
    }
    if (!restored) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const float scale = SystemDpiScale();
        w = std::min((int)(w * scale), (int)(work.right - work.left));
        h = std::min((int)(h * scale), (int)(work.bottom - work.top));
        x = work.left + ((work.right - work.left) - w) / 2;
        y = work.top + ((work.bottom - work.top) - h) / 2;
    }
    m_hwnd = CreateWindowExW(0, kWindowClass, APP_EDITION_AMD ? L"VRChat DLSS5 Cam (Radeon)" : L"VRChat DLSS5 Cam", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, hInstance, this);
    if (!m_hwnd) {
        Log::Error("CreateWindowExW failed: %s", LastErrorText().c_str());
        return false;
    }
    ReadSystemTheme();
    UpdateTitleBar();
    m_dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(m_hwnd);
    if (m_headless) {
        // The window only exists for messages; its client size is the size of the offscreen frames.
        ShowWindow(m_hwnd, SW_HIDE);
    } else {
        m_nCmdShow = nCmdShow;   // shown once its first frame is drawn (App::Frame), while the start-up card still covers the wait
    }
    return true;
}

bool App::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
#ifdef NDEBUG
    // Keep ImGui's error recovery, but never surface "programmer error" tooltips or
    // ID-conflict highlights to end users; they go to the debug log instead.
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableTooltip = false;
    io.ConfigDebugHighlightIdConflicts = false;
#endif
    ApplyDpi(m_dpiScale);

    if (!ImGui_ImplWin32_Init(m_hwnd)) { Log::Error("ImGui_ImplWin32_Init failed"); return false; }
    ImGui_ImplDX12_InitInfo ii;
    ii.Device = m_device.D3D12();
    ii.CommandQueue = m_device.Ui().Queue();           // ImGui renders on the present queue
    ii.NumFramesInFlight = (int)Device::kFramesInFlight;
    ii.RTVFormat = Device::kBackBufferFormat;
    ii.DSVFormat = DXGI_FORMAT_UNKNOWN;
    ii.UserData = &m_device;
    ii.SrvDescriptorHeap = m_device.SrvHeap();
    ii.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        Device* dev = static_cast<Device*>(info->UserData);
        const DescriptorPair d = dev->AllocStatic();
        *cpu = d.cpu;
        *gpu = d.gpu;
    };
    ii.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
        Device* dev = static_cast<Device*>(info->UserData);
        DescriptorPair d;
        d.cpu = cpu;
        d.gpu = gpu;
        dev->FreeStatic(d);
    };
    if (!ImGui_ImplDX12_Init(&ii)) { Log::Error("ImGui_ImplDX12_Init failed"); return false; }
    m_imguiReady = true;
    m_fontsDirty = true;
    return true;
}

void App::ApplyDpi(float scale) {
    m_dpiScale = scale;
    ImGuiStyle& style = ImGui::GetStyle();
    ui::ApplyTheme(style, scale);
    style.FontSizeBase = 16.0f;
}

void App::Shutdown() {
    if (m_deviceReady && EffectiveRoute() == RouteFsrHost) LogPortState(true);
    m_updater.Cancel();
    m_splash.Close();
    StopWorker();
    m_scanner.Stop();
    if (m_deviceReady) { m_device.Ui().WaitIdle(); m_device.Proc().WaitIdle(); }
    if (m_imguiReady) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    if (m_hwnd) UnregisterHotKey(m_hwnd, kHotkeyId);
    if (m_deviceReady) {
        m_atlas.Shutdown();
        m_pipeline.Shutdown(m_device);
        m_spout.Shutdown(m_device.Proc());
        m_image.Release(m_device.Proc());
        m_video.Close(m_device.Proc());
    }
    m_capture.Shutdown();
    mf::Shutdown();
    if (m_deviceReady) { m_device.Shutdown(); m_deviceReady = false; }
    if (!m_settingsPath.empty()) m_settings.Save(m_settingsPath);
    Log::Info("Shutdown complete (exit code %d)", m_exitCode);
    Log::Shutdown();
    if (m_wake) { CloseHandle(m_wake); m_wake = nullptr; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    UnregisterClassW(kWindowClass, m_hInstance);
    CoUninitialize();
}

// --- processing thread ----------------------------------------------------------------------

void App::StartWorker() {
    m_workerStop = false;
    m_worker = std::thread([this] { WorkerMain(); });
}

void App::StopWorker() {
    if (!m_worker.joinable()) return;
    m_workerStop = true;
    WakeWorker();
    m_worker.join();
}

void App::WakeWorker() {
    if (m_wake) SetEvent(m_wake);
}

void App::PostCommand(Command&& c) {
    {
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        m_shared.commands.push_back(std::move(c));
    }
    WakeWorker();
}

void App::PostNotice(const std::string& text, bool error) {
    std::lock_guard<std::mutex> lock(m_shared.mutex);
    m_shared.notices.push_back(Notice{ text, error });
}

void App::PostBatchEvent(unsigned id, int state, const std::string& outName, const std::string& error) {
    std::lock_guard<std::mutex> lock(m_shared.mutex);
    m_shared.batchEvents.push_back(BatchEvent{ id, state, outName, error });
}

void App::PushSettings() {
    {
        Settings effective = m_settings;
        if (const LibraryItem* item = OverrideItem()) effective.CopyEffects(*item->own);
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        m_shared.settings = effective;
        ++m_shared.settingsGeneration;
    }
    WakeWorker();
}

void App::WorkerLoadRuntime(GpuContext& gpu, const std::wstring& path, bool announce) {
    if (!FileExists(path)) {
        m_pipeline.UnloadNrRuntime(gpu);
        Log::Warn("DLSS 5 runtime not found at %s", WideToUtf8(path).c_str());
        if (announce) PostNotice(TR(RuntimeMissing), true);
        m_pipeline.MarkNrDirty();
        return;
    }
    std::string err;
    if (m_pipeline.LoadNrRuntime(gpu, path, err)) {
        Log::Info("DLSS 5 runtime loaded from %s", WideToUtf8(path).c_str());
        if (announce) PostNotice(TR(RuntimeLoadedToast), false);
    } else {
        Log::Error("DLSS 5 runtime load failed: %s", err.c_str());
        PostNotice(StrPrintf("%s: %s", TR(RuntimeLoadFailed), err.c_str()), true);
    }
    m_pipeline.MarkNrDirty();
}

void App::WorkerLoadImage(GpuContext& gpu, const std::wstring& path, bool announce) {
    std::string err;
    const std::string name = WideToUtf8(FileNameOf(path));
    if (m_image.Load(gpu, path, err)) {
        m_workerLastError.clear();
        if (!announce) return;
        PostNotice(StrPrintf("%s: %s (%ux%u)", TR(ImageLoaded), name.c_str(), m_image.OriginalWidth(), m_image.OriginalHeight()), false);
        if (m_image.Width() != m_image.OriginalWidth() || m_image.Height() != m_image.OriginalHeight())
            PostNotice(StrPrintf("%s: %ux%u", TR(ImageDownscaled), m_image.Width(), m_image.Height()), false);
    } else {
        m_workerLastError = err;
        Log::Error("Image load failed for %s: %s", WideToUtf8(path).c_str(), err.c_str());
        PostNotice(StrPrintf("%s: %s (%s)", TR(ImageLoadFailed), name.c_str(), err.c_str()), true);
    }
}

void App::WorkerLoadVideo(GpuContext& gpu, const std::wstring& path, bool hardwareDecode, bool announce) {
    std::string err;
    const std::string name = WideToUtf8(FileNameOf(path));
    // The processing range belongs to the file: it survives a reload of the same one (after a batch), a different
    // file starts without one.
    const bool sameFile = m_video.Loaded() && SamePath(m_video.Path(), path);
    const VideoPreview previous = m_preview;
    if (m_preview.running) m_video.StopSequence();
    m_preview = VideoPreview{};
    if (sameFile) { m_preview.inSec = previous.inSec; m_preview.outSec = previous.outSec; }
    if (m_video.Open(gpu, path, hardwareDecode, err)) {
        m_workerLastError.clear();
        const VideoInfo& vi = m_video.Info();
        if (announce)
            PostNotice(StrPrintf("%s: %s (%ux%u, %.3g fps, %.1f s)", TR(VideoLoaded), name.c_str(), vi.width, vi.height,
                                 vi.fpsDen ? (double)vi.fpsNum / (double)vi.fpsDen : 0.0, vi.durationSeconds), false);
    } else {
        m_workerLastError = err;
        Log::Error("Video open failed for %s: %s", WideToUtf8(path).c_str(), err.c_str());
        PostNotice(StrPrintf("%s: %s (%s)", TR(VideoLoadFailed), name.c_str(), err.c_str()), true);
    }
}

// Starts feeding the opened video (or the range fromSec..toSec of it) through the pipeline. The output file is
// created when the first processed frame arrives (its size is only known then); frames go to an MP4 (VideoWriter)
// or a PNG sequence (Capture).
bool App::WorkerStartVideo(const Settings& settings, VideoRun& run, const std::wstring& folder, double fromSec, double toSec,
                           std::string& error) {
    run = VideoRun{};
    if (!m_video.Loaded()) { error = "no video is open"; return false; }
    const VideoInfo& vi = m_video.Info();
    fromSec = std::max(0.0, fromSec);
    if (toSec <= fromSec) toSec = 0.0;
    if (vi.durationSeconds > 0.0 && fromSec >= vi.durationSeconds) { error = "the range starts after the end of the video"; return false; }
    if (settings.videoMatchSource) {
        // The output follows the file: its codec (HEVC stays HEVC, everything else becomes H.264), its average bitrate
        // (a variable-bitrate source by its average) and, as always, its frame rate.
        run.pngSequence = false;
        run.codec = (vi.codec.find("HEV") != std::string::npos || vi.codec == "H265") ? 1 : 0;
        run.bitrateKbps = vi.videoBitrateKbps > 0 ? std::clamp(vi.videoBitrateKbps, 1000u, 400000u) : 40000u;
        Log::Info("Video: output matched to the source: %s, %u kbit/s, %u/%u fps%s", run.codec ? "HEVC" : "H.264", run.bitrateKbps,
                  vi.fpsNum, vi.fpsDen, vi.videoBitrateKbps ? "" : " (bitrate unknown: 40 Mbit/s)");
    } else {
        run.pngSequence = settings.videoOutput == 2;
        run.codec = settings.videoOutput == 1 ? 1 : 0;
        run.bitrateKbps = (UINT32)std::clamp(settings.videoBitrateMbps, 5, 200) * 1000u;
    }
    run.withAudio = settings.videoKeepAudio && !run.pngSequence && vi.hasAudio;
    run.folder = folder;
    run.stem = m_video.Stem();
    run.resumeSec = m_video.PreviewSeconds();
    if (!CreateDirectories(folder)) { error = "cannot create the capture folder"; return false; }
    if (run.pngSequence) {
        std::wstring dir;
        for (int n = 1; n < 10000; ++n) {
            dir = JoinPath(folder, run.stem + L"_DLSS5" + (n == 1 ? std::wstring() : L"_" + std::to_wstring(n)));
            if (!DirectoryExists(dir) && !FileExists(dir)) break;
        }
        if (!CreateDirectories(dir)) { error = "cannot create the output folder"; return false; }
        run.outPath = dir;
    }
    // Playback of the preview stops; the run has the decoder to itself.
    m_preview.playing = false;
    m_preview.running = false;
    m_preview.ended = false;
    if (!m_video.StartSequence(run.withAudio, fromSec, toSec, error)) return false;
    run.fromSec = fromSec;
    run.toSec = toSec;
    const double fps = vi.fpsDen ? (double)vi.fpsNum / (double)vi.fpsDen : 30.0;
    if (fromSec > 0.0 || toSec > 0.0) {
        const double end = toSec > 0.0 ? std::min(toSec, vi.durationSeconds > 0.0 ? vi.durationSeconds : toSec) : vi.durationSeconds;
        run.total = (UINT64)std::llround(std::max(0.0, end - fromSec) * fps);
    }
    if (run.total == 0) run.total = vi.frameEstimate;
    run.startTime = NowSeconds();
    run.active = true;
    m_pipeline.RequestReset();
    Log::Info("Video: processing %s (%.3f s to %s, %llu frames expected) -> %s", WideToUtf8(m_video.Path()).c_str(), fromSec,
              toSec > 0.0 ? StrPrintf("%.3f s", toSec).c_str() : "the end", (unsigned long long)run.total,
              run.pngSequence ? WideToUtf8(run.outPath).c_str() : run.codec == 1 ? "MP4 (HEVC)" : "MP4 (H.264)");
    return true;
}

// A processed frame from the pipeline (in frame order).
void App::WorkerVideoFrame(VideoRun& run, std::vector<uint8_t>&& rgba, UINT w, UINT h, UINT pitch, UINT64 index) {
    if (!run.active) return;
    LONGLONG pts = 0, duration = 0;
    auto it = run.times.find(index);
    if (it != run.times.end()) { pts = it->second.first; duration = it->second.second; run.times.erase(it); }
    else {
        const VideoInfo& vi = m_video.Info();
        pts = (LONGLONG)((double)index * 10000000.0 * (double)vi.fpsDen / (double)std::max(1u, vi.fpsNum));
    }
    ++run.delivered;
    if (run.pngSequence) {
        while (m_capture.Pending() > 4 && !run.cancel) Sleep(1);   // the PNG encoder is slower than the pipeline
        wchar_t name[64];
        swprintf_s(name, L"_%06llu.png", (unsigned long long)index);
        CaptureJob job;
        job.width = w; job.height = h; job.rowPitch = pitch; job.keepAlpha = false; job.quiet = true;
        job.path = JoinPath(run.outPath, run.stem + name);
        job.pixels = std::move(rgba);
        m_capture.Enqueue(std::move(job));
        return;
    }
    if (!run.writerPrepared) {
        run.writerPrepared = true;
        run.outPath = Capture::MakeVideoFileName(run.folder, run.stem, w, h, L"mp4");
        VideoWriterConfig cfg;
        cfg.path = run.outPath;
        cfg.fpsNum = m_video.Info().fpsNum; cfg.fpsDen = m_video.Info().fpsDen;
        cfg.codec = run.codec;
        cfg.bitrateKbps = run.bitrateKbps;
        cfg.audio = run.withAudio ? m_video.AudioType() : nullptr;
        m_videoWriter.Prepare(cfg);
    }
    m_videoWriter.PushFrame(index, pts, duration, std::move(rgba), pitch, w, h);
}

// Ends a run: frames still on the GPU reach the output, the file is closed (finished, or cut short on a cancel or
// failure) and the result is announced. Not while a command list is being recorded.
bool App::WorkerEndVideo(GpuContext& gpu, VideoRun& run, FrameSink& sink, bool completed) {
    m_video.StopSequence();
    gpu.WaitIdle();
    m_pipeline.Update(gpu, m_capture, &sink);
    m_pipeline.CancelFrameReadback();
    {
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        m_shared.source.videoFinishing = true;
    }
    bool ok = completed;
    std::string error = run.error;
    if (run.pngSequence) {
        while (m_capture.Pending() > 0) Sleep(5);
    } else if (run.writerPrepared) {
        ComPtr<IMFSample> a;
        while (m_video.PopAudio(a)) m_videoWriter.PushAudio(a);
        if (completed) {
            std::string err;
            if (!m_videoWriter.Finish(err)) { ok = false; if (error.empty()) error = err; }
        } else {
            m_videoWriter.Abort();
        }
    } else if (completed) {
        ok = false;
        if (error.empty()) error = "no frame was processed";
    }
    const double seconds = std::max(NowSeconds() - run.startTime, 1e-3);
    const std::string outName = WideToUtf8(FileNameOf(run.outPath));
    m_workerLastOut = outName;
    if (ok) {
        m_workerLastError.clear();
        Log::Info("Video: %llu frames processed in %.1f s (%.1f fps) -> %s", (unsigned long long)run.delivered, seconds,
                  (double)run.delivered / seconds, WideToUtf8(run.outPath).c_str());
        PostNotice(StrPrintf("%s: %s (%llu %s, %.0f s)", TR(VideoSaved), outName.c_str(), (unsigned long long)run.delivered,
                             "frames", seconds), false);
    } else if (run.cancel) {
        m_workerLastError = "cancelled";
        Log::Info("Video: cancelled after %llu frames (%s)", (unsigned long long)run.delivered, WideToUtf8(run.outPath).c_str());
        PostNotice(TR(VideoCancelled), false);
    } else {
        m_workerLastError = error;
        ++m_videoFailures;
        Log::Error("Video: %s (%s)", error.c_str(), WideToUtf8(run.outPath).c_str());
        PostNotice(StrPrintf("%s: %s", TR(VideoFailed), error.c_str()), true);
    }
    run = VideoRun{};
    m_pipeline.RequestReset();
    return ok;
}

void App::WorkerPreviewCommand(const Command& c) {
    if (!m_video.Loaded()) return;
    switch (c.type) {
    case Command::VideoSeek:
        m_preview.seekPending = true;
        m_preview.seekTo = c.seconds;
        m_preview.stepFrames = 0;
        {
            // Visible to the interface while the frame decodes.
            std::lock_guard<std::mutex> lock(m_shared.mutex);
            m_shared.source.videoSeeking = true;
            m_shared.source.videoPosition = c.seconds;
        }
        break;
    case Command::VideoPlay:
        m_preview.playing = true;
        break;
    case Command::VideoPause:
        m_preview.playing = false;
        break;
    case Command::VideoStep:
        m_preview.playing = false;
        m_preview.stepFrames += c.step;
        break;
    case Command::VideoSetRange:
        m_preview.inSec = std::max(0.0, c.seconds);
        m_preview.outSec = c.seconds2 > m_preview.inSec ? c.seconds2 : 0.0;
        m_preview.rangeChanged = true;
        Log::Info("Video: range %.3f s to %s", m_preview.inSec, m_preview.outSec > 0.0 ? StrPrintf("%.3f s", m_preview.outSec).c_str() : "the end");
        break;
    default:
        break;
    }
}

bool App::WorkerPreviewStep(bool& fresh, bool& reset) {
    if (!m_video.Loaded()) { m_preview.playing = false; m_preview.running = false; return false; }
    bool changed = false;
    std::string err;
    const double frame = std::max(m_video.FrameSeconds(), 1e-3);
    const VideoInfo& vi = m_video.Info();
    auto stopPlayback = [&]() {
        if (!m_preview.running) return;
        m_video.StopSequence();
        m_preview.running = false;
        changed = true;   // the still passes converge on the frame that stayed
    };
    // Pause.
    if (!m_preview.playing) stopPlayback();
    // A changed range restarts playback from the current position with the new end.
    if (m_preview.rangeChanged) {
        m_preview.rangeChanged = false;
        if (m_preview.running) stopPlayback();
    }
    // Frame steps become a seek relative to the frame on show.
    if (m_preview.stepFrames != 0) {
        stopPlayback();
        const double base = m_preview.seekPending ? m_preview.seekTo : m_video.PreviewSeconds();
        m_preview.seekTo = base + (double)m_preview.stepFrames * frame;
        m_preview.seekPending = true;
        m_preview.stepFrames = 0;
    }
    if (m_preview.seekPending) {
        m_preview.seekPending = false;
        stopPlayback();
        const double before = m_video.PreviewSeconds();
        const double t0 = NowSeconds();
        if (m_video.SeekPreview(m_preview.seekTo, err)) {
            changed = true;
            reset = std::fabs(m_video.PreviewSeconds() - before) > frame * 1.5;   // a jump: the history is stale
            m_preview.ended = false;
            Log::Info("Video: seek to %.3f s -> frame at %.3f s in %.0f ms", m_preview.seekTo, m_video.PreviewSeconds(), (NowSeconds() - t0) * 1000.0);
        } else {
            Log::Warn("Video: seek to %.3f s failed: %s", m_preview.seekTo, err.c_str());
        }
    }
    // Play: the sequence starts at the frame on show, or at the start of the range after its end was reached.
    if (m_preview.playing && !m_preview.running) {
        const double end = m_preview.outSec > m_preview.inSec ? m_preview.outSec : vi.durationSeconds;
        double from = m_video.PreviewSeconds();
        if (m_preview.ended || (end > 0.0 && from >= end - frame * 0.5)) { from = m_preview.inSec; reset = true; }
        if (m_video.StartSequence(false, from, m_preview.outSec > m_preview.inSec ? m_preview.outSec : 0.0, err)) {
            m_preview.running = true;
            m_preview.ended = false;
            m_preview.nextFrameWall = NowSeconds();
            changed = true;
        } else {
            Log::Warn("Video: playback failed to start: %s", err.c_str());
            PostNotice(StrPrintf("%s: %s", TR(VideoFailed), err.c_str()), true);
            m_preview.playing = false;
        }
    }
    // Playback: one frame per frame interval; when the pipeline falls behind, the clock is moved rather than frames
    // dropped, so every frame is seen.
    if (m_preview.running) {
        const double now = NowSeconds();
        if (now + 0.0005 >= m_preview.nextFrameWall) {
            VideoFrameData t;
            switch (m_video.NextFrame(0.0, t)) {
            case VideoSource::Next::Frame:
                fresh = true;
                changed = true;
                m_preview.nextFrameWall = (now - m_preview.nextFrameWall > frame) ? now + frame : m_preview.nextFrameWall + frame;
                break;
            case VideoSource::Next::Wait:
                break;
            case VideoSource::Next::End:
                m_video.StopSequence();
                m_preview.running = false;
                m_preview.playing = false;
                m_preview.ended = true;
                changed = true;
                break;
            case VideoSource::Next::Error:
                Log::Warn("Video: playback stopped: %s", m_video.SequenceError().c_str());
                m_video.StopSequence();
                m_preview.running = false;
                m_preview.playing = false;
                changed = true;
                break;
            }
        }
    }
    return changed;
}

void App::WorkerMain() {
    NameCurrentThread(L"VDC processing");
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);   // WIC decoding
    timeBeginPeriod(1);
    GpuContext& gpu = m_device.Proc();

    Settings settings;
    unsigned settingsGen = 0;
    std::string processingKey;               // Settings::ProcessingText() of the snapshot the still passes ran with
    int      activeMode = -1;
    int      passesLeft = 0;                 // image mode: passes still to run
    bool     imageChanged = false;           // image texture recreated since the last processed frame
    bool     imageCapturePending = false;
    Command  imageCapture;
    bool     videoChanged = false;           // video texture recreated since the last processed frame
    SourceTransform imageXform, videoXform;  // orientation and crop of the loaded picture / video
    int      userMode = SourceSpout;         // the source mode chosen in the interface (a batch overrides it)
    VideoRun videoRun;
    BatchRun batch;
    struct VideoSink final : FrameSink {
        App* app = nullptr; VideoRun* run = nullptr;
        void OnFrame(std::vector<uint8_t>&& rgba, UINT w, UINT h, UINT pitch, UINT64 index) override {
            app->WorkerVideoFrame(*run, std::move(rgba), w, h, pitch, index);
        }
    } sink;
    sink.app = this; sink.run = &videoRun;
    double   lastSenderScan = 0.0;
    double   lastTimelapse = NowSeconds();
    double   lastRun = 0.0;
    double   rateNext = 0.0;   // processing rate cap: start of the next processing slot
    double   lastPublish = 0.0;
    double   fpsWindowStart = NowSeconds();
    unsigned fpsWindowFrames = 0;
    double   processingFps = 0.0;
    // Processing cost per frame, for the time estimates: gathered over the fps window from the passes that run
    // (preview, still pictures), and replaced by the measured throughput once a file run is under way.
    double   costAccumMs = 0.0;
    unsigned costAccumFrames = 0;
    double   timerCostSec = 0.0, timerCostPixels = 0.0;
    double   runCostSec = 0.0, runCostPixels = 0.0;

    // Perf line: only loop iterations that processed a new source frame count.
    struct Perf {
        double logTime = 0.0;
        unsigned frames = 0;
        double receive = 0.0, wait = 0.0, record = 0.0, submit = 0.0, update = 0.0;
        double gpu[(UINT)GpuTimer::Count] = {};
        double depthMs = 0.0; unsigned depthRuns = 0;
        void Reset(double now) { *this = Perf{}; logTime = now; }
    } perf;
    perf.Reset(NowSeconds());
    UINT64 depthInferencesSeen = 0;
    UINT64 imageDepthSeen = 0;

    auto currentItem = [&]() -> const BatchItem* {
        return batch.active && batch.index < batch.items.size() ? &batch.items[batch.index] : nullptr;
    };

    while (!m_workerStop.load(std::memory_order_acquire)) {
        const double now = NowSeconds();

        // Commands and the settings snapshot from the interface thread.
        std::deque<Command> commands;
        bool settingsChanged = false;
        {
            std::lock_guard<std::mutex> lock(m_shared.mutex);
            commands.swap(m_shared.commands);
            if (m_shared.settingsGeneration != settingsGen) {
                settingsGen = m_shared.settingsGeneration;
                settings = m_shared.settings;
                if (batch.active && batch.itemStarted && batch.index < batch.items.size() && batch.items[batch.index].own)
                    settings.CopyEffects(*batch.items[batch.index].own);
                userMode = settings.sourceMode;
                settingsChanged = true;
            }
        }
        bool commandRan = false;
        for (Command& c : commands) {
            commandRan = true;
            switch (c.type) {
            case Command::LoadRuntime:
                WorkerLoadRuntime(gpu, c.path, c.announce);
                passesLeft = std::max(passesLeft, kImageConvergePasses);
                break;
            case Command::LoadImage:
                if (batch.active) break;
                WorkerLoadImage(gpu, c.path, true);
                imageXform = c.transform;
                imageChanged = true;
                passesLeft = kImageConvergePasses;
                break;
            case Command::SetTransform:
                // For the file that is loaded; one for a file that is not (any more) is stale and dropped.
                if (batch.active) break;
                if (c.video ? (m_video.Loaded() && SamePath(m_video.Path(), c.path)) : (m_image.Loaded() && SamePath(m_image.Path(), c.path))) {
                    SourceTransform& x = c.video ? videoXform : imageXform;
                    if (x != c.transform) {
                        x = c.transform;
                        if (c.video) videoChanged = true; else imageChanged = true;
                        passesLeft = kImageConvergePasses;
                    }
                }
                break;
            case Command::CaptureImage:
                if (batch.active) break;
                imageCapturePending = true;
                imageCapture = c;
                break;
            case Command::LoadVideo:
                if (batch.active || videoRun.active) break;
                WorkerLoadVideo(gpu, c.path, settings.videoHardwareDecode, true);
                videoXform = c.transform;
                videoChanged = true;
                passesLeft = kImageConvergePasses;
                break;
            case Command::ProcessVideo: {
                if (batch.active || videoRun.active) break;
                std::string err;
                if (!WorkerStartVideo(settings, videoRun, c.path, m_preview.inSec, m_preview.outSec, err)) {
                    Log::Error("Video: cannot start: %s", err.c_str());
                    PostNotice(StrPrintf("%s: %s", TR(VideoFailed), err.c_str()), true);
                    ++m_videoFailures;
                }
                break;
            }
            case Command::CancelVideo:
                if (videoRun.active) videoRun.cancel = true;
                break;
            case Command::BatchStart:
                if (batch.active || videoRun.active || c.items.empty()) break;
                batch = BatchRun{};
                batch.items = std::move(c.items);
                batch.folder = c.path;
                batch.keepAlpha = c.keepAlpha;
                batch.saveOriginal = c.saveOriginal;
                batch.restoreImage = m_image.Path();
                batch.restoreVideo = m_video.Path();
                batch.restoreImageXform = imageXform;
                batch.restoreVideoXform = videoXform;
                batch.active = true;
                imageCapturePending = false;
                if (m_preview.running) m_video.StopSequence();
                m_preview.running = false;
                m_preview.playing = false;
                Log::Info("Batch: %zu files -> %s", batch.items.size(), WideToUtf8(batch.folder).c_str());
                break;
            case Command::BatchCancel:
                if (!batch.active) break;
                batch.cancel = true;
                if (videoRun.active) videoRun.cancel = true;
                if (batch.itemStarted && !batch.itemIsVideo) {
                    if (const BatchItem* item = currentItem()) PostBatchEvent(item->id, LibraryItem::Idle, "", "");
                    imageCapturePending = false; batch.itemStarted = false; ++batch.failed; ++batch.index;
                }
                break;
            case Command::VideoSeek:
            case Command::VideoPlay:
            case Command::VideoPause:
            case Command::VideoStep:
            case Command::VideoSetRange:
                if (batch.active || videoRun.active) break;
                WorkerPreviewCommand(c);
                break;
            case Command::CloseMedia:
                if (batch.active || videoRun.active) break;
                if (c.video) {
                    if (m_video.Loaded()) {
                        if (m_preview.running) m_video.StopSequence();
                        m_preview = VideoPreview{};
                        m_video.Close(gpu);
                        videoChanged = true;
                    }
                } else if (m_image.Loaded()) {
                    m_image.Release(gpu);
                    imageChanged = true;
                }
                passesLeft = 0;
                break;
            }
        }

        // Batch: the next file starts once the previous one is finished; the batch ends after the last one.
        bool batchStep = false;
        while (batch.active && !batch.itemStarted && !videoRun.active) {
            batchStep = true;
            if (batch.cancel || batch.index >= batch.items.size()) {
                if (batch.cancel) {
                    Log::Info("Batch: cancelled after %d of %zu files", batch.done, batch.items.size());
                    PostNotice(StrPrintf(TR(BatchCancelled), batch.done, (int)batch.items.size()), false);
                } else {
                    Log::Info("Batch: finished, %d files processed, %d failed", batch.done, batch.failed);
                    PostNotice(StrPrintf(TR(BatchFinished), batch.done, batch.failed), batch.failed > 0);
                }
                const BatchRun ended = std::move(batch);
                batch = BatchRun{};
                imageCapturePending = false;
                // The user's own files come back (or go away, when there were none).
                if (!ended.restoreImage.empty()) { if (!SamePath(m_image.Path(), ended.restoreImage)) WorkerLoadImage(gpu, ended.restoreImage, false); }
                else if (m_image.Loaded()) m_image.Release(gpu);
                if (!ended.restoreVideo.empty()) { if (!SamePath(m_video.Path(), ended.restoreVideo)) WorkerLoadVideo(gpu, ended.restoreVideo, settings.videoHardwareDecode, false); }
                else if (m_video.Loaded()) m_video.Close(gpu);
                imageXform = ended.restoreImageXform;
                videoXform = ended.restoreVideoXform;
                imageChanged = videoChanged = true;
                passesLeft = kImageConvergePasses;
                PostBatchEvent(0, LibraryItem::Idle, "", "");   // the batch is over
                break;
            }
            const BatchItem& item = batch.items[batch.index];
            batch.itemIsVideo = item.isVideo;
            Log::Info("Batch: %zu/%zu %s%s", batch.index + 1, batch.items.size(), WideToUtf8(item.path).c_str(), item.own ? " (own parameters)" : "");
            // The item's own effect values, or the plain settings again after an item that had some.
            {
                std::lock_guard<std::mutex> lock(m_shared.mutex);
                settings = m_shared.settings;
            }
            if (item.own) settings.CopyEffects(*item.own);
            m_pipeline.MarkNrDirty();
            PostBatchEvent(item.id, LibraryItem::Processing, "", "");
            if (item.isVideo) {
                WorkerLoadVideo(gpu, item.path, settings.videoHardwareDecode, false);
                videoXform = item.transform;
                std::string err;
                if (!m_video.Loaded() || !SamePath(m_video.Path(), item.path) ||
                    !WorkerStartVideo(settings, videoRun, batch.folder, item.inSec, item.outSec, err)) {
                    if (!err.empty()) { Log::Error("Video: cannot start: %s", err.c_str()); PostNotice(StrPrintf("%s: %s", TR(VideoFailed), err.c_str()), true); }
                    PostBatchEvent(item.id, LibraryItem::Failed, "", err.empty() ? m_workerLastError : err);
                    ++batch.failed; ++batch.index;
                    continue;
                }
                videoChanged = true;
                passesLeft = 0;
            } else {
                WorkerLoadImage(gpu, item.path, false);
                imageXform = item.transform;
                if (!m_image.Loaded() || !SamePath(m_image.Path(), item.path)) {
                    PostBatchEvent(item.id, LibraryItem::Failed, "", m_workerLastError);
                    ++batch.failed; ++batch.index;
                    continue;
                }
                imageChanged = true;
                passesLeft = kImageConvergePasses;
                imageCapturePending = true;
                imageCapture = Command{};
                imageCapture.type = Command::CaptureImage;
                imageCapture.path = batch.folder;
                imageCapture.keepAlpha = batch.keepAlpha;
                imageCapture.saveOriginal = batch.saveOriginal;
            }
            batch.itemStarted = true;
        }

        // The mode the pipeline runs in: a batch item overrides the interface's choice.
        const int mode = batch.active && batch.itemStarted ? (batch.itemIsVideo ? SourceVideo : SourceImage) : userMode;
        settings.sourceMode = mode;
        const bool imageMode = mode == SourceImage;
        const bool videoMode = mode == SourceVideo;
        const bool modeChanged = mode != activeMode;
        activeMode = mode;
        if (videoRun.active && !videoMode) videoRun.cancel = true;   // switched away while a video was running
        if (!videoMode && m_preview.running) { m_video.StopSequence(); m_preview.running = false; m_preview.playing = false; }
        const bool previewPlaying = videoMode && !videoRun.active && m_preview.running;
        const bool stillMode = imageMode || (videoMode && !videoRun.active && !previewPlaying);   // convergence passes on one picture
        if (settingsChanged && !modeChanged) {
            // Only a change of what the passes read starts the still passes over. A display, blend or interface
            // change (the wipe, the theme, the library strip...) composites the existing result once, without
            // running the picture through the passes again.
            std::string key = settings.ProcessingText(m_pipeline.StrengthsInPass(settings));
            if (key != processingKey) {
                if (stillMode && !processingKey.empty()) {
                    // Which key changed (the first differing line), for the log.
                    size_t a = 0, b = 0; std::string changedKey;
                    while (a < key.size() || b < processingKey.size()) {
                        const size_t ea = key.find('\n', a), eb = processingKey.find('\n', b);
                        const std::string la = key.substr(a, ea == std::string::npos ? std::string::npos : ea - a);
                        const std::string lb = processingKey.substr(b, eb == std::string::npos ? std::string::npos : eb - b);
                        if (la != lb) { changedKey = la.empty() ? lb : la; break; }
                        if (ea == std::string::npos || eb == std::string::npos) break;
                        a = ea + 1; b = eb + 1;
                    }
                    Log::Info("Still passes: %d more (a processing setting changed: %s)", kImageSettingsPasses, changedKey.c_str());
                }
                passesLeft = std::max(passesLeft, kImageSettingsPasses);
            }
            processingKey = std::move(key);
        }
        if (modeChanged) passesLeft = kImageConvergePasses;

        // Source.
        SourceFrame src;
        bool fresh = false, changed = modeChanged;
        double receiveMs = 0.0;
        if (imageMode) {
            src = m_image.Frame();
            if (imageChanged) changed = true;
            fresh = m_image.Loaded() && passesLeft > 0;
            if (imageCapturePending && (!m_image.Loaded() || !src.Connected())) imageCapturePending = false;
            if (imageCapturePending && passesLeft == 0 && src.hasFrame) {
                // Save once the passes have settled: the capture rides on the next processed frame.
                imageCapturePending = false;
                m_pipeline.RequestCapture(imageCapture.path, imageCapture.keepAlpha, imageCapture.saveOriginal, m_image.Stem());
            }
            // A batch item is done once its picture has been written.
            if (batch.active && batch.itemStarted && !batch.itemIsVideo && !imageCapturePending && !m_pipeline.CapturePending() &&
                m_pipeline.Status().capturesInFlight == 0 && m_capture.Pending() == 0) {
                if (const BatchItem* item = currentItem()) PostBatchEvent(item->id, LibraryItem::Done, "", "");
                batch.itemStarted = false; ++batch.done; ++batch.index;
            }
        } else if (videoMode) {
            imageCapturePending = false;
            if (videoChanged) changed = true;
            if (!videoRun.active) {
                bool reset = false;
                if (!batch.active && WorkerPreviewStep(fresh, reset)) {
                    passesLeft = kImageConvergePasses;
                    if (reset) m_pipeline.RequestReset();
                }
                if (!m_preview.running) fresh = m_video.Loaded() && passesLeft > 0;
                src = m_video.Frame(!m_preview.running);
            } else {
                src = m_video.Frame(false);
                // Sound goes to the output as it is decoded, so the decoder never waits on a full audio queue.
                if (!videoRun.pngSequence && m_videoWriter.Running()) { ComPtr<IMFSample> a; while (m_video.PopAudio(a)) m_videoWriter.PushAudio(a); }
                bool ended = false, completed = false;
                if (videoRun.cancel) {
                    ended = true;
                } else if (!videoRun.pngSequence && m_videoWriter.Failed()) {
                    videoRun.error = m_videoWriter.Error(); ended = true;
                } else if (videoRun.frameHeld) {
                    // The last frame produced no output (a feature was created on it): run it through again.
                    if (++videoRun.heldRetries > kVideoHeldRetries) {
                        Log::Warn("Video: frame %llu produced no output after %d passes; skipped", (unsigned long long)(videoRun.handed - 1), kVideoHeldRetries);
                        m_pipeline.CancelFrameReadback();
                        videoRun.frameHeld = false; videoRun.heldRetries = 0;
                    } else {
                        fresh = true;
                    }
                } else {
                    VideoFrameData t;
                    switch (m_video.NextFrame(0.05, t)) {
                    case VideoSource::Next::Frame:
                        videoRun.times[t.index] = { t.pts, t.duration };
                        ++videoRun.handed;
                        videoRun.heldRetries = 0;
                        m_pipeline.RequestFrameReadback(t.index);
                        fresh = true;
                        break;
                    case VideoSource::Next::Wait: break;
                    case VideoSource::Next::End: ended = true; completed = true; break;
                    case VideoSource::Next::Error: videoRun.error = m_video.SequenceError(); ended = true; break;
                    }
                }
                if (ended) {
                    const double resume = videoRun.resumeSec;
                    const bool ok = WorkerEndVideo(gpu, videoRun, sink, completed);
                    if (batch.active && batch.itemStarted && batch.itemIsVideo) {
                        if (const BatchItem* item = currentItem()) PostBatchEvent(item->id, ok ? LibraryItem::Done : LibraryItem::Failed, m_workerLastOut, ok ? "" : m_workerLastError);
                        batch.itemStarted = false; ++batch.index;
                        if (ok) ++batch.done; else ++batch.failed;
                    } else {
                        std::string err;
                        if (m_video.Loaded() && !m_video.SeekPreview(resume, err)) Log::Warn("Video: preview reload failed: %s", err.c_str());
                        passesLeft = kImageConvergePasses;
                    }
                    src = m_video.Frame(true);
                    changed = true;
                    fresh = false;
                }
            }
        } else {
            imageCapturePending = false;
            const double t0 = NowSeconds();
            bool spoutChanged = false;
            m_spout.Receive(gpu, spoutChanged, fresh);
            receiveMs = (NowSeconds() - t0) * 1000.0;
            changed = changed || spoutChanged;
            src = m_spout.Frame();
            // Processing rate cap: source frames that arrive before the next slot are dropped, so the pipeline runs at a
            // steady cadence and the GPU load stays put; the preview keeps the last processed frame meanwhile.
            if (fresh && settings.processRateLimit > 0) {
                const double interval = 1.0 / (double)settings.processRateLimit;
                if (rateNext > 0.0 && now < rateNext - 0.0005) {
                    fresh = false;
                } else {
                    rateNext = (rateNext > 0.0 && now - rateNext < interval) ? rateNext + interval : now + interval;
                }
            } else if (settings.processRateLimit <= 0) {
                rateNext = 0.0;
            }
            // Timelapse (live source only).
            if (settings.timelapseSeconds > 0 && now - lastTimelapse >= (double)settings.timelapseSeconds) {
                lastTimelapse = now;
                if (m_spout.HasFrame() && m_pipeline.HasDisplay())
                    m_pipeline.RequestCapture(EffectiveCaptureFolder(settings), settings.keepAlpha, settings.saveOriginal, L"");
            }
        }

        // Sender list for the interface.
        if (m_refreshSenders.exchange(false) || now - lastSenderScan > 1.0) {
            lastSenderScan = now;
            std::vector<std::string> senders = m_spout.EnumerateSenders();
            std::lock_guard<std::mutex> lock(m_shared.mutex);
            if (senders != m_shared.senders) { m_shared.senders = std::move(senders); ++m_shared.sendersGeneration; }
        }

        const bool deviceLost = m_device.DeviceRemoved();
        // A frame without new input still runs for pending requests, and about twice a second so the status
        // (runtime, NGX, depth estimator) stays current while nothing is connected.
        const bool run = !deviceLost && (fresh || changed || commandRan || settingsChanged || batchStep || m_pipeline.NeedsFrame() ||
                                         now - lastRun >= 0.5);
        if (run) {
            const double tBegin = NowSeconds();
            ID3D12GraphicsCommandList* cmd = gpu.BeginFrame();
            const double tRecord = NowSeconds();
            if (imageMode) { m_image.Upload(cmd, gpu); src = m_image.Frame(); imageChanged = false; }
            else if (videoMode) { m_video.Upload(cmd, gpu); src = m_video.Frame(!videoRun.active && !m_preview.running); videoChanged = false; }
            if (imageMode) src.transform = imageXform;
            else if (videoMode) src.transform = videoXform;
            else src.transform = SourceTransform::Turned(settings.spoutRotate, settings.spoutFlipH, settings.spoutFlipV);
            m_pipeline.Render(gpu, src, settings, cmd, fresh, changed);
            const double tSubmit = NowSeconds();
            const UINT64 fence = gpu.EndFrame();
            m_pipeline.AfterSubmit(gpu, fence);
            const double tUpdate = NowSeconds();
            m_pipeline.Update(gpu, m_capture, videoRun.active ? &sink : nullptr);
            m_pipeline.PublishStatus(gpu);
            const double tEnd = NowSeconds();
            lastRun = tEnd;
            if (videoRun.active) videoRun.frameHeld = m_pipeline.FrameReadbackPending();
            // A still picture gets one depth estimate, which may land after the passes ran out: converge again with it.
            if (stillMode) {
                const UINT64 inferences = m_pipeline.Status().depthInferences;
                if (inferences != imageDepthSeen) {
                    imageDepthSeen = inferences;
                    Log::Info("Still passes: %d more (depth estimate %llu landed, %d were left)", kImageSettingsPasses, (unsigned long long)inferences, passesLeft);
                    passesLeft = std::max(passesLeft, kImageSettingsPasses);
                }
            }

            const bool processed = fresh && src.Connected() && src.hasFrame;
            if (processed) {
                if (stillMode && passesLeft > 0) --passesLeft;
                ++fpsWindowFrames;
                const PipelineStatus& st = m_pipeline.Status();
                ++perf.frames;
                perf.receive += receiveMs;
                perf.wait += (tRecord - tBegin) * 1000.0;
                perf.record += (tSubmit - tRecord) * 1000.0;
                perf.submit += (tUpdate - tSubmit) * 1000.0;
                perf.update += (tEnd - tUpdate) * 1000.0;
                costAccumMs += (tEnd - tBegin) * 1000.0 + receiveMs + st.gpuMs[(UINT)GpuTimer::Frame];
                ++costAccumFrames;
                for (UINT t = 0; t < (UINT)GpuTimer::Count; ++t) perf.gpu[t] += st.gpuMs[t];
                if (st.depthInferences != depthInferencesSeen) {
                    depthInferencesSeen = st.depthInferences;
                    perf.depthMs += st.depthInferMs;
                    ++perf.depthRuns;
                }
            }
        }

        // Processing rate: processed frames per one-second window (falls to zero when input stops).
        if (now - fpsWindowStart >= 1.0) {
            processingFps = fpsWindowFrames / (now - fpsWindowStart);
            fpsWindowStart = now;
            fpsWindowFrames = 0;
            if (costAccumFrames >= 3) {
                const PipelineStatus& st = m_pipeline.Status();
                timerCostSec = costAccumMs / (double)costAccumFrames / 1000.0;
                timerCostPixels = (double)st.srcWidth * (double)st.srcHeight;
            }
            costAccumMs = 0.0; costAccumFrames = 0;
        }

        // Source snapshot for the interface.
        if (run || now - lastPublish >= 0.25) {
            lastPublish = now;
            SourceInfo info;
            info.mode = mode;
            info.processingFps = processingFps;
            {
                const VideoInfo& vi = m_video.Info();
                info.videoLoaded = m_video.Loaded();
                info.videoPath = m_video.Path();
                info.videoName = WideToUtf8(FileNameOf(m_video.Path()));
                info.videoCodec = vi.codec;
                info.videoWidth = vi.width; info.videoHeight = vi.height;
                info.videoFps = vi.fpsDen ? (double)vi.fpsNum / (double)vi.fpsDen : 0.0;
                info.videoFrames = videoRun.active ? videoRun.total : vi.frameEstimate;
                info.videoDurationSeconds = vi.durationSeconds;
                info.videoHasAudio = vi.hasAudio;
                info.videoHardwareDecode = vi.hardwareDecode;
                info.videoBitrateKbps = vi.videoBitrateKbps;
                if (videoRun.active && videoRun.delivered >= 8 && now - videoRun.startTime > 0.5) {
                    runCostSec = (now - videoRun.startTime) / (double)videoRun.delivered;
                    runCostPixels = (double)vi.width * (double)vi.height;
                }
                info.costMeasured = runCostSec > 0.0 && runCostPixels > 0.0;
                info.costSecPerFrame = info.costMeasured ? runCostSec : timerCostSec;
                info.costPixels = info.costMeasured ? runCostPixels : timerCostPixels;
                info.videoProcessing = videoRun.active;
                info.videoFrame = videoRun.delivered;
                info.videoElapsed = videoRun.active ? now - videoRun.startTime : 0.0;
                info.videoOutName = WideToUtf8(FileNameOf(videoRun.outPath));
                info.videoPosition = m_video.PreviewSeconds();
                info.videoPlaying = m_preview.playing || m_preview.running;
                info.videoSeeking = false;
                info.videoIn = m_preview.inSec;
                info.videoOut = m_preview.outSec;
                info.videoPreviewLuma = m_video.PreviewLuma();
                info.batchRunning = batch.active;
                info.batchIndex = (int)batch.index; info.batchCount = (int)batch.items.size();
                info.batchDone = batch.done; info.batchFailed = batch.failed;
                if (const BatchItem* item = currentItem()) { info.batchItemId = item->id; info.batchItemName = WideToUtf8(FileNameOf(item->path)); }
            }
            if (imageMode) {
                info.connected = m_image.Loaded();
                info.hasFrame = m_pipeline.HasDisplay();
                info.imageLoaded = m_image.Loaded();
                info.imageConverging = m_image.Loaded() && passesLeft > 0;
                info.imagePath = m_image.Path();
                info.imageName = WideToUtf8(FileNameOf(m_image.Path()));
                info.imageWidth = m_image.Width(); info.imageHeight = m_image.Height();
                info.imageOrigWidth = m_image.OriginalWidth(); info.imageOrigHeight = m_image.OriginalHeight();
                info.format = m_image.Loaded() ? "BGRA8" : "";
            } else if (videoMode) {
                info.connected = m_video.Loaded();
                info.hasFrame = m_pipeline.HasDisplay();
                info.imageConverging = m_video.Loaded() && !videoRun.active && !m_preview.running && passesLeft > 0;
                info.imageLoaded = m_image.Loaded();
                info.imagePath = m_image.Path();
                info.imageName = WideToUtf8(FileNameOf(m_image.Path()));
                info.format = m_video.Loaded() ? "BGRA8" : "";
            } else {
                info.connected = m_spout.Connected();
                info.hasFrame = m_spout.HasFrame();
                info.senderName = m_spout.SenderName();
                info.senderFps = m_spout.SenderFps();
                info.format = info.connected ? FormatLabel(m_spout.Format()) : "";
                info.isHdr = info.connected && m_spout.Frame().IsHdr();
                info.imageLoaded = m_image.Loaded();
                info.imagePath = m_image.Path();
                info.imageName = WideToUtf8(FileNameOf(m_image.Path()));
                info.imageWidth = m_image.Width(); info.imageHeight = m_image.Height();
                info.imageOrigWidth = m_image.OriginalWidth(); info.imageOrigHeight = m_image.OriginalHeight();
            }
            std::lock_guard<std::mutex> lock(m_shared.mutex);
            m_shared.source = std::move(info);
        }

        // Performance line every 15 s, averaged over the frames that were actually processed.
        if (now - perf.logTime >= kPerfLogInterval) {
            if (perf.frames > 0) {
                const double n = (double)perf.frames;
                auto g = [&](GpuTimer t) { return perf.gpu[(UINT)t] / n; };
                const double cpu = (perf.receive + perf.wait + perf.record + perf.submit + perf.update) / n;
                // "other" is frame time outside every stage: barriers, copies, and the GPU serving another queue
                // (the interface, the depth network) in the middle of the frame.
                const double stages = g(GpuTimer::Convert) + g(GpuTimer::Guidance) + g(GpuTimer::OpticalFlow) + g(GpuTimer::Dlaa) +
                                      g(GpuTimer::Neural) + g(GpuTimer::Composite);
                const double other = std::max(0.0, g(GpuTimer::Frame) - stages);
                Log::Info("Perf: %s %.1f fps (sender %.1f, ui %.0f fps / %.2f ms gpu), cpu %.2f ms/frame (receive %.2f, wait %.2f, record %.2f, submit %.2f, update %.2f), "
                          "gpu %.2f ms (convert %.2f, guidance %.2f, flow %.2f, dlaa %.2f, neural %.2f, composite %.2f, other %.2f), depth net %.1f ms x %u, frames %u",
                          videoRun.active ? "video" : imageMode ? "image passes" : previewPlaying ? "video playback" : videoMode ? "video preview" : "processing",
                          perf.frames / (now - perf.logTime), m_spout.SenderFps(),
                          m_uiFpsShared.load(), m_uiGpuMsShared.load(), cpu, perf.receive / n, perf.wait / n, perf.record / n, perf.submit / n, perf.update / n,
                          g(GpuTimer::Frame), g(GpuTimer::Convert), g(GpuTimer::Guidance), g(GpuTimer::OpticalFlow), g(GpuTimer::Dlaa),
                          g(GpuTimer::Neural), g(GpuTimer::Composite), other, perf.depthRuns ? perf.depthMs / perf.depthRuns : 0.0, perf.depthRuns, perf.frames);
            }
            perf.Reset(now);
        }

        if (!run || !fresh) {
            // Idle: poll the live source about every millisecond (new Spout frames are picked up within ~1 ms),
            // more lazily when nothing is connected; commands and settings wake the thread immediately. Video
            // playback keeps the 1 ms cadence so frames are shown on time.
            const DWORD ms = deviceLost ? 50 : ((!imageMode && !videoMode && m_spout.Connected()) || previewPlaying) ? 1 : 4;
            WaitForSingleObject(m_wake, ms);
        }
    }

    if (videoRun.active) { videoRun.cancel = true; WorkerEndVideo(gpu, videoRun, sink, false); }
    if (m_preview.running) { m_video.StopSequence(); m_preview.running = false; }
    timeEndPeriod(1);
    if (SUCCEEDED(coHr)) CoUninitialize();
}

// --- interface thread -----------------------------------------------------------------------

void App::DrainNotices() {
    CaptureResult cr;
    while (m_capture.PollResult(cr)) {
        const std::wstring name = FileNameOf(cr.path);
        if (cr.ok && cr.width && cr.height && cr.seconds > 0.0)
            m_pngSecPerMegapixel = std::clamp(cr.seconds / ((double)cr.width * (double)cr.height / 1e6), 0.01, 5.0);
        if (cr.ok && cr.quiet) continue;   // a frame of a video sequence, or a screenshot
        if (!cr.quiet) ++m_captureResultsSeen;
        if (cr.ok) {
            m_lastCapture = WideToUtf8(name);
            m_lastCaptureOk = true;
            m_ui.Toast(StrPrintf("%s: %s (%.1f MB, %.0f ms)", TR(Saved), m_lastCapture.c_str(), cr.bytes / 1048576.0, cr.seconds * 1000.0));
            Log::Info("Saved %s (%llu bytes)", WideToUtf8(cr.path).c_str(), (unsigned long long)cr.bytes);
            // The picture of a library item being processed.
            if (LibraryItem* item = FindItem(m_source.batchItemId))
                if (item->state == LibraryItem::Processing && !item->isVideo) item->outName = m_lastCapture;
        } else {
            m_lastCapture = cr.error;
            m_lastCaptureOk = false;
            m_ui.Toast(StrPrintf("%s: %s", TR(CaptureFailed), cr.error.c_str()), true);
            Log::Error("Capture failed for %s: %s", WideToUtf8(cr.path).c_str(), cr.error.c_str());
        }
    }
    std::deque<Notice> notices;
    std::deque<BatchEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        notices.swap(m_shared.notices);
        events.swap(m_shared.batchEvents);
    }
    for (const Notice& n : notices) m_ui.Toast(n.text, n.error);
    for (const BatchEvent& e : events) {
        if (e.id == 0) {
            // The batch is over: whatever is still waiting was not processed.
            m_libraryBatchRunning = false;
            for (LibraryItem& item : m_library)
                if (item.state == LibraryItem::Queued || item.state == LibraryItem::Processing) item.state = LibraryItem::Idle;
            continue;
        }
        LibraryItem* item = FindItem(e.id);
        if (!item) continue;
        item->state = (LibraryItem::State)e.state;
        item->progress = 0.0f;
        if (!e.outName.empty()) item->outName = e.outName;
        item->error = e.error;
        if (item->state == LibraryItem::Failed) ++m_batchFailures;
    }
}

void App::Frame() {
    if (m_inFrame || !m_deviceReady || !m_imguiReady) return;
    m_inFrame = true;
    const double frameStart = NowSeconds();
    // The window appears with its second frame: the first one has been presented behind the start-up card, so no
    // blank window is ever seen.
    if (++m_frameCount == 2 && !m_headless && !m_mainShown) {
        m_mainShown = true;
        ShowWindow(m_hwnd, m_settings.windowMaximized ? SW_SHOWMAXIMIZED : m_nCmdShow);
        UpdateWindow(m_hwnd);
        m_splash.Close();
    }

    // The screenshot recorded with the previous frame.
    if (m_device.ScreenshotPending()) {
        std::vector<uint8_t> rgba;
        UINT w = 0, h = 0;
        if (m_device.FinishScreenshot(m_screenshotFence, rgba, w, h)) {
            CaptureJob job;
            job.width = w; job.height = h; job.rowPitch = w * 4; job.keepAlpha = false; job.quiet = true;
            job.path = m_screenshotPath;
            job.pixels = std::move(rgba);
            m_capture.Enqueue(std::move(job));
            Log::Info("Screenshot: %s (%ux%u)", WideToUtf8(m_screenshotPath).c_str(), w, h);
        } else {
            Log::Warn("Screenshot failed: %s", WideToUtf8(m_screenshotPath).c_str());
        }
    }

    if (m_device.DeviceRemoved()) {
        if (!m_deviceLostReported) {
            m_deviceLostReported = true;
            FatalMessage(Utf8ToWide(TR(DeviceRemoved)));
            m_exitCode = 2;
            PostQuitMessage(2);
        }
        m_inFrame = false;
        return;
    }
    if (m_pendingResize) {
        m_pendingResize = false;
        m_device.Resize(m_pendingWidth, m_pendingHeight);
    }
    if (m_fontsDirty || !m_fonts.Built()) {
        m_fontsDirty = false;
        if (!m_fonts.Build(I18n::Current())) Log::Warn("Font atlas build failed; using the default font");
    }
    DrainNotices();
    PollScanner();
    UpdateStoryboard();

    ID3D12GraphicsCommandList* cmd = m_device.BeginFrame();
    if (!cmd) { m_inFrame = false; return; }
    m_atlas.Upload(cmd, m_device.Ui());
    const DisplayView display = m_pipeline.AcquireDisplay(m_device.Ui());
    m_pipeline.StatusSnapshot(m_status);
    CheckRuntimeFallback();
    {
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        m_source = m_shared.source;
        if (m_shared.sendersGeneration != m_sendersSeen) { m_sendersSeen = m_shared.sendersGeneration; m_senders = m_shared.senders; }
    }
    for (LibraryItem& item : m_library) {
        if (item.state != LibraryItem::Processing) continue;
        item.progress = (item.id == m_source.batchItemId && m_source.videoProcessing && m_source.videoFrames > 0)
                            ? (float)std::min(1.0, (double)m_source.videoFrame / (double)m_source.videoFrames) : 0.0f;
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (m_headless && m_cli.width > 0 && m_cli.height > 0) ImGui::GetIO().DisplaySize = ImVec2((float)m_cli.width, (float)m_cli.height);
    ImGui::NewFrame();

    ui::UiFrameInfo info;
    info.status = &m_status;
    info.adapter = &m_device.Info();
    info.fps = m_fps;
    info.cpuMs = m_cpuMs;
    info.uiGpuMs = m_device.TimerMs(GpuTimer::Ui);
    info.processingFps = m_source.processingFps;
    info.sourceMode = m_settings.sourceMode;
    info.senders = &m_senders;
    info.senderName = m_source.senderName;
    info.senderFps = m_source.senderFps;
    info.sourceFormat = m_source.format;
    info.sourceConnected = m_source.mode == m_settings.sourceMode && m_source.connected;
    info.sourceIsHdr = m_source.isHdr;
    info.imagePath = m_source.imagePath;
    info.imageName = m_source.imageName;
    info.imageWidth = m_source.imageWidth; info.imageHeight = m_source.imageHeight;
    info.imageOrigWidth = m_source.imageOrigWidth; info.imageOrigHeight = m_source.imageOrigHeight;
    info.imageLoaded = m_source.imageLoaded;
    info.imageConverging = m_source.imageConverging;
    info.videoLoaded = m_source.videoLoaded;
    info.videoPath = m_source.videoPath;
    info.videoName = m_source.videoName;
    info.videoCodec = m_source.videoCodec;
    info.videoWidth = m_source.videoWidth; info.videoHeight = m_source.videoHeight;
    info.videoFps = m_source.videoFps;
    info.videoFrames = m_source.videoFrames;
    info.videoDurationSeconds = m_source.videoDurationSeconds;
    info.videoHasAudio = m_source.videoHasAudio;
    info.videoHardwareDecode = m_source.videoHardwareDecode;
    info.videoBitrateKbps = m_source.videoBitrateKbps;
    info.videoProcessing = m_source.videoProcessing;
    info.videoFinishing = m_source.videoFinishing;
    info.videoFrame = m_source.videoFrame;
    info.videoElapsed = m_source.videoElapsed;
    info.videoOutName = m_source.videoOutName;
    info.videoPosition = m_source.videoPosition;
    info.videoPlaying = m_source.videoPlaying;
    info.videoSeeking = m_source.videoSeeking;
    info.videoIn = m_source.videoIn; info.videoOut = m_source.videoOut;
    info.videoPreviewLuma = m_source.videoPreviewLuma;
    info.batchRunning = m_source.batchRunning || m_libraryBatchRunning;
    info.batchIndex = m_source.batchIndex; info.batchCount = m_source.batchCount;
    info.batchDone = m_source.batchDone; info.batchFailed = m_source.batchFailed;
    info.batchItemId = m_source.batchItemId;
    info.batchItemName = m_source.batchItemName;
    info.costSecPerFrame = m_source.costSecPerFrame; info.costPixels = m_source.costPixels; info.costMeasured = m_source.costMeasured;
    info.pngSecPerMegapixel = m_pngSecPerMegapixel;
    info.library = &m_library;
    info.atlas = &m_atlas;
    info.systemLight = m_systemLight;
    info.storyCells = &m_storyCells;
    info.storyTimes = &m_storyTimes;
    info.storyReady = &m_storyReady;
    info.hoverCell = m_hoverCell;
    info.hoverCellTime = m_hoverCellTime;
    info.nrRuntimePath = EffectiveRuntimePath();
    info.nrRuntimeExists = FileExists(info.nrRuntimePath);
    info.nrRuntimeBuild = RuntimeBuildName(m_status.nrRuntimePath);
    info.nrRuntimeExhausted = m_runtimeExhausted;
    info.fsrDllExists = FileExists(JoinPath(m_exeDir, L"amd_fidelityfx_dx12.dll"));
    PollPortSetup();
    info.portSetup = &m_portStatus;
    info.portRestartHint = m_portRestartHint;
    // The automatic restart after its installer: not in the middle of a batch or an update (the hint stays then).
    if (m_portRestartAt >= 0.0) {
        if (m_source.batchRunning || m_libraryBatchRunning || m_updater.Busy()) m_portRestartAt = -1.0;
        else if (NowSeconds() >= m_portRestartAt) { m_portRestartAt = -1.0; RelaunchSelf(); }
    }
    info.portRestartIn = m_portRestartAt >= 0.0 ? (int)std::ceil(m_portRestartAt - NowSeconds()) : -1;
    if (NowSeconds() - m_portWeightsTime > 2.0) { m_portWeightsTime = NowSeconds(); m_portWeightsExist = FileExists(JoinPath(m_exeDir, PortSetup::kWeightsFile)); }
    info.portWeightsExist = m_portWeightsExist;
    info.portConsent = m_settings.portConsent;
    info.portInstalledTag = m_settings.portInstalledTag;
    info.captureFolder = EffectiveCaptureFolder();
    info.hotkeyText = HotkeyText(m_settings);
    info.hasDisplay = display.valid;
    info.fullscreen = m_fullscreen;
    info.displayTexture = display.valid ? (ImTextureID)display.srv.ptr : (ImTextureID)0;
    info.displayWidth = display.width;
    info.displayHeight = display.height;
    info.displayWide = display.wide;
    if (const LibraryItem* shown = ShownItem()) info.shownItem = shown->id;
    info.appVersion = APP_VERSION_STRING;
    info.prerelease = APP_PRERELEASE != 0;
    info.windowShown = m_mainShown;
    info.presets = &m_presets;
    {
        const Updater::Status us = m_updater.Get();
        info.updateState = (int)us.state;
        info.updateVersion = us.release.version;
        info.updateDate = us.release.date;
        info.updateNotes = us.release.notes;
        info.updatePrerelease = us.release.prerelease;
        info.updateEdition = us.release.edition;
        info.updateHasAsset = !us.release.assetUrl.empty();
        info.updateError = us.error;
        info.updateWritable = us.writable;
        info.updateDownloadedMb = us.downloadedMb;
        info.updateTotalMb = us.totalMb;
        info.updateProgress = us.totalMb > 0.0 ? (float)std::min(1.0, us.downloadedMb / us.totalMb) : 0.0f;
        if (us.generation != m_updateGenSeen) {
            m_updateGenSeen = us.generation;
            switch (us.state) {
            case Updater::State::Available:
                info.updateShow = true;
                if (m_cli.update || (us.release.edition && m_cli.edition)) m_updater.Download(m_exeDir, JoinPath(m_appDataDir, L"update"));   // --update / --edition: no click needed
                break;
            case Updater::State::UpToDate:
                if (us.release.edition) m_ui.Toast(StrPrintf(TR(EditionNotFound), APP_EDITION_AMD ? TR(EditionGeforce) : TR(EditionAmd), APP_VERSION_STRING), true);
                else if (us.manual) m_ui.Toast(StrPrintf(TR(UpdateUpToDate), APP_VERSION_STRING));
                break;
            case Updater::State::Failed:
                if (us.download) m_ui.Toast(us.writable ? StrPrintf(TR(UpdateFailed), us.error.c_str()) : std::string(TR(UpdateNotWritable)), true);
                else m_ui.Toast(StrPrintf(TR(UpdateCheckFailed), us.error.c_str()), true);
                break;
            case Updater::State::Restarting: PostMessageW(m_hwnd, WM_CLOSE, 0, 0); break;
            default: break;
            }
        }
    }
    info.capturePending = m_capture.Pending() + m_status.capturesInFlight;
    info.lastCapture = m_lastCapture;
    info.lastCaptureOk = m_lastCaptureOk;

    ui::UiEvents ev;
    m_ui.Draw(m_settings, info, ev, m_fonts);
    HandleEvents(ev);
    UpdateTitleBar();
    ImGui::Render();

    m_device.TimerBegin(cmd, GpuTimer::Ui);
    ID3D12Resource* backBuffer = m_device.CurrentBackBuffer();
    Device::Barrier(cmd, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_device.CurrentRtv();
    const ui::Palette& pal = ui::Colors();
    const float clear[4] = { pal.window.x, pal.window.y, pal.window.z, 1.0f };
    cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
    if (!m_pendingScreenshot.empty()) {
        if (m_device.BeginScreenshot(cmd)) m_screenshotPath = m_pendingScreenshot;
        else Log::Warn("Screenshot could not be recorded: %s", WideToUtf8(m_pendingScreenshot).c_str());
        m_pendingScreenshot.clear();
    }
    Device::Barrier(cmd, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_device.TimerEnd(cmd, GpuTimer::Ui);

    const UINT64 uiFence = m_device.EndFrame(m_settings.vsync);
    m_pipeline.ReleaseDisplay(uiFence);
    if (m_device.ScreenshotPending()) m_screenshotFence = uiFence;

    const double now = NowSeconds();
    m_cpuMs = m_cpuMs * 0.9 + (now - frameStart) * 1000.0 * 0.1;
    const double dt = std::max(now - m_lastFrameTime, 1e-4);
    m_lastFrameTime = now;
    const double inst = 1.0 / dt;
    m_fps = (m_fps <= 0.0) ? inst : m_fps * 0.92 + inst * 0.08;
    m_uiFpsShared.store(m_fps);
    m_uiGpuMsShared.store(info.uiGpuMs);

    if (m_settingsDirtyTime >= 0.0 && now - m_settingsDirtyTime > 1.0) SaveSettings();
    m_inFrame = false;

    RunCommandLineActions();

    // Modal dialogs run their own message pump; open them only once the frame is fully submitted.
    if (m_pendingBrowseRuntime) { m_pendingBrowseRuntime = false; BrowseRuntime(); }
    if (m_pendingBrowseDepthModel) { m_pendingBrowseDepthModel = false; BrowseDepthModel(); }
    if (m_pendingBrowseFolder) { m_pendingBrowseFolder = false; BrowseFolder(); }
    if (m_pendingBrowseImage) { m_pendingBrowseImage = false; BrowseImage(); }
    if (m_pendingBrowseVideo) { m_pendingBrowseVideo = false; BrowseVideo(); }
    if (m_pendingBrowseLibraryFiles) { m_pendingBrowseLibraryFiles = false; BrowseLibraryFiles(); }
    if (m_pendingBrowseLibraryFolder) { m_pendingBrowseLibraryFolder = false; BrowseLibraryFolder(); }

    // Without vsync the interface would otherwise spin at thousands of frames per second and take GPU time away
    // from the processing queue; ~300 fps is plenty for a preview, ~60 for an offscreen run.
    if (!m_settings.vsync || m_headless) {
        const double budget = m_headless ? 0.016 : 0.003;
        while (NowSeconds() - frameStart < budget) Sleep(1);
    }
}

void App::RunCommandLineActions() {
    const double now = NowSeconds();
    const double elapsed = now - m_startTime;
    if (!m_cliActionsDone) {
        m_cliActionsDone = true;
        std::stable_sort(m_cli.screenshots.begin(), m_cli.screenshots.end(),
                         [](const std::pair<double, std::wstring>& a, const std::pair<double, std::wstring>& b) { return a.first < b.first; });
        if (!m_cli.open.empty()) {
            bool isVideo = false;
            if (IsLibraryFile(m_cli.open, isVideo)) {
                if (isVideo) OpenVideoFile(m_cli.open);
                else OpenImageFile(m_cli.open);
                AddLibraryFiles({ m_cli.open }, false);
            } else {
                Log::Warn("Command line: %s is not a picture or a video", WideToUtf8(m_cli.open).c_str());
                m_cli.open.clear();
            }
        }
        if (!m_cli.add.empty()) AddLibraryFiles(m_cli.add, false);
    }
    // The opened file.
    bool openIsVideo = false;
    const bool openWanted = !m_cli.open.empty() && IsLibraryFile(m_cli.open, openIsVideo);
    const bool openLoaded = !openWanted ||
                            (openIsVideo ? (m_source.videoLoaded && SamePath(m_source.videoPath, m_cli.open))
                                         : (m_source.imageLoaded && SamePath(m_source.imagePath, m_cli.open)));
    if (!m_cliVideoActionsDone && (openLoaded || !openIsVideo)) {
        m_cliVideoActionsDone = true;
        if (openWanted && openIsVideo) {
            if (m_cli.in >= 0.0 || m_cli.out >= 0.0) {
                Command c; c.type = Command::VideoSetRange;
                c.seconds = std::max(0.0, m_cli.in);
                c.seconds2 = m_cli.out > c.seconds ? m_cli.out : 0.0;
                for (LibraryItem& item : m_library)
                    if (item.isVideo && SamePath(item.path, m_cli.open)) { item.inSec = c.seconds; item.outSec = c.seconds2; }
                PostCommand(std::move(c));
            }
            if (m_cli.seek >= 0.0) { Command c; c.type = Command::VideoSeek; c.seconds = m_cli.seek; PostCommand(std::move(c)); }
            if (m_cli.play) { Command c; c.type = Command::VideoPlay; PostCommand(std::move(c)); }
        }
    }
    // Screenshots at their times, one at a time.
    if (m_nextScreenshot < m_cli.screenshots.size() && m_pendingScreenshot.empty() && !m_device.ScreenshotPending() &&
        elapsed >= m_cli.screenshots[m_nextScreenshot].first) {
        RequestScreenshot(m_cli.screenshots[m_nextScreenshot].second);
        ++m_nextScreenshot;
    }
    const bool screenshotsFlushed = m_nextScreenshot >= m_cli.screenshots.size() && m_pendingScreenshot.empty() &&
                                    !m_device.ScreenshotPending() && m_capture.Pending() == 0;
    // Processing: the opened file or the library, once everything is loaded.
    bool probesDone = true;
    for (const LibraryItem& item : m_library) if (item.probe == 0) { probesDone = false; break; }
    if (m_cli.process && !m_cliProcessStarted && m_cliVideoActionsDone && elapsed >= 1.0 && probesDone) {
        if (openLoaded) {
            m_cliProcessStarted = true;
            m_cliProcessStartTime = now;
            m_cliCaptureBaseline = m_captureResultsSeen;
            if (!m_library.empty()) {
                Log::Info("Command line: processing the library (%zu files)", m_library.size());
                StartLibraryProcessing(false);
                if (!m_libraryBatchRunning) ++m_batchFailures;   // it did not start (busy, nothing to do)
            } else if (m_settings.sourceMode == SourceSpout) {
                Log::Info("Command line: capturing the live source");
                CaptureNow();
            } else {
                Log::Warn("Command line: nothing to process");
                ++m_batchFailures;
            }
        } else if (elapsed >= 60.0) {
            Log::Error("Command line: %s did not load", WideToUtf8(m_cli.open).c_str());
            m_cliProcessStarted = true;
            m_cliProcessStartTime = now;
            ++m_batchFailures;
        }
    }
    bool processDone = !m_cli.process;
    if (m_cli.process && m_cliProcessStarted && now - m_cliProcessStartTime >= 2.0) {
        const bool running = m_source.batchRunning || m_libraryBatchRunning || m_source.videoProcessing || m_source.videoFinishing ||
                             m_capture.Pending() > 0 || m_status.capturesInFlight > 0 || m_pipeline.CapturePending();
        processDone = !running;
    }
    auto quit = [&](const char* why) {
        if (m_quit) return;
        const bool failed = m_batchFailures > 0 || m_videoFailures.load() > 0;
        if (failed && m_exitCode == 0) m_exitCode = 1;
        Log::Info("Command line: %s, exiting with code %d", why, m_exitCode);
        if (!m_headless) SaveWindowPlacement();
        SaveSettings();
        m_quit = true;
    };
    if (m_cli.exitAfter >= 0.0 && elapsed >= m_cli.exitAfter) {
        if (m_nextScreenshot < m_cli.screenshots.size()) {
            Log::Warn("Command line: %zu screenshot(s) come after --exit-after and were skipped", m_cli.screenshots.size() - m_nextScreenshot);
            m_nextScreenshot = m_cli.screenshots.size();
        }
        if (m_pendingScreenshot.empty() && !m_device.ScreenshotPending() && m_capture.Pending() == 0) quit("--exit-after reached");
        return;
    }
    if ((m_headless || m_cli.process) && screenshotsFlushed && processDone && elapsed >= 1.0) {
        // A headless run without a task still waits for the runtime to load, so the log tells whether it works.
        if (!m_cli.process && m_cli.screenshots.empty() && elapsed < 3.0) return;
        quit(m_cli.process ? "processing finished" : "done");
    }
}

void App::RequestScreenshot(const std::wstring& path) {
    if (path.empty()) return;
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) CreateDirectories(path.substr(0, slash));
    m_pendingScreenshot = path;
}

void App::HandleEvents(ui::UiEvents& ev) {
    if (ev.captureNow) CaptureNow();
    if (ev.browseRuntime) m_pendingBrowseRuntime = true;
    if (ev.browseDepthModel) m_pendingBrowseDepthModel = true;
    if (ev.openImage) m_pendingBrowseImage = true;
    if (ev.openVideo) m_pendingBrowseVideo = true;
    if (ev.cancelVideo) { Command c; c.type = Command::CancelVideo; PostCommand(std::move(c)); }
    if (ev.closeMedia) CloseMediaFile();
    if (ev.fullscreenToggle) SetFullscreen(!m_fullscreen);
    if (ev.libraryRestore) { RestoreLibrary(ev.libraryRestoreItems); ev.itemParamsChanged = true; }
    if (ev.batchCancel) { Command c; c.type = Command::BatchCancel; PostCommand(std::move(c)); }
    // Video controls.
    if (ev.videoSeek) { Command c; c.type = Command::VideoSeek; c.seconds = ev.videoSeekTo; PostCommand(std::move(c)); }
    if (ev.videoPlayToggle) { Command c; c.type = m_source.videoPlaying ? Command::VideoPause : Command::VideoPlay; PostCommand(std::move(c)); }
    if (ev.videoStep != 0) { Command c; c.type = Command::VideoStep; c.step = ev.videoStep; PostCommand(std::move(c)); }
    if (ev.videoSetIn || ev.videoSetOut || ev.videoClearRange) {
        double in = m_source.videoIn, out = m_source.videoOut;
        if (ev.videoClearRange) { in = 0.0; out = 0.0; }
        else if (ev.videoSetIn) { in = m_source.videoPosition; if (out > 0.0 && out <= in) out = 0.0; }
        else { out = m_source.videoPosition; if (in >= out) in = 0.0; }
        Command c; c.type = Command::VideoSetRange; c.seconds = in; c.seconds2 = out;
        PostCommand(std::move(c));
        for (LibraryItem& item : m_library)
            if (item.isVideo && SamePath(item.path, m_source.videoPath)) { item.inSec = in; item.outSec = out; }
    }
    if (ev.videoHover) RequestHoverThumb(ev.videoHoverTime);
    // Media library.
    if (ev.libraryPreview) PreviewLibraryItem(ev.libraryPreview);
    if (ev.libraryRemove) RemoveLibraryItem(ev.libraryRemove);
    if (ev.libraryClear) ClearLibrary();
    if (ev.libraryProcessAll) StartLibraryProcessing(false);
    if (ev.libraryProcessSelected) StartLibraryProcessing(true);
    if (ev.libraryLocate) LocateLibraryItem(ev.libraryLocate);
    if (ev.libraryDeleteSelected) RemoveSelectedLibraryItems();
    // A file with effect values of its own is previewed with them: the pushed settings carry them while it is shown.
    {
        const LibraryItem* item = OverrideItem();
        const std::wstring key = item ? item->path : std::wstring();
        if (key != m_overridePath || ev.itemParamsChanged) {
            m_overridePath = key;
            PushSettings();
            m_pipeline.MarkNrDirty();
            WakeWorker();
        }
    }
    if (ev.libraryAddFiles) m_pendingBrowseLibraryFiles = true;
    if (ev.libraryAddFolder) m_pendingBrowseLibraryFolder = true;
    SyncTransform(ev);

    if (ev.reloadDepth) { m_pipeline.RestartDepthEstimator(); WakeWorker(); }
    if (ev.browseFolder) m_pendingBrowseFolder = true;
    if (ev.openCaptureFolder) {
        const std::wstring folder = EffectiveCaptureFolder();
        CreateDirectories(folder);
        OpenPath(folder);
    }
    if (ev.openLogFile) OpenPath(Log::FilePath());
    if (ev.openSettingsFolder) OpenPath(m_appDataDir);
    if (ev.openProjectPage) OpenPath(kProjectUrl);
    if (ev.openDocs) OpenPath(DocsUrl());
    // Presets.
    if (ev.presetSave) {
        const std::string name = Trim(ev.presetName);
        if (!name.empty()) {
            const std::string text = m_settings.EffectText();
            bool replaced = false;
            for (ui::UserPreset& pr : m_presets) if (pr.name == name) { pr.text = text; replaced = true; }
            if (!replaced) m_presets.push_back({ name, text });
            SavePresets();
            m_ui.Toast(StrPrintf(replaced ? TR(PresetReplaced) : TR(PresetSaved), name.c_str()), false);
        }
    }
    if (ev.presetRename >= 0 && ev.presetRename < (int)m_presets.size()) {
        const std::string name = Trim(ev.presetName);
        if (!name.empty()) { m_presets[(size_t)ev.presetRename].name = name; SavePresets(); }
    }
    if (ev.presetDelete >= 0 && ev.presetDelete < (int)m_presets.size()) {
        m_presets.erase(m_presets.begin() + ev.presetDelete);
        SavePresets();
    }
    if (ev.updateCheckNow) m_updater.Check(APP_VERSION_STRING, m_settings.updateChannel == 1, true);
    if (ev.updateStart) m_updater.Download(m_exeDir, JoinPath(m_appDataDir, L"update"));
    if (ev.updateCancel) m_updater.Cancel();
    if (ev.updateOpenPage) {
        const Updater::Status us = m_updater.Get();
        OpenPath(us.release.pageUrl.empty() ? std::wstring(kProjectUrl) + L"/releases" : Utf8ToWide(us.release.pageUrl));
    }
    if (ev.openLicenses) {
        const std::wstring local = JoinPath(m_exeDir, L"THIRD_PARTY_NOTICES.md");
        OpenPath(FileExists(local) ? local : std::wstring(kProjectUrl) + L"/blob/main/THIRD_PARTY_NOTICES.md");
    }
    if (ev.languageChanged) {
        I18n::SetLanguage(I18n::FromSetting(m_settings.language));
        m_fontsDirty = true;
        Log::Info("Language switched to %s", I18n::LanguageName(I18n::Current()));
    }
    if (ev.resetDefaults) {
        Settings def;
        def.windowX = m_settings.windowX; def.windowY = m_settings.windowY;
        def.windowWidth = m_settings.windowWidth; def.windowHeight = m_settings.windowHeight;
        def.windowMaximized = m_settings.windowMaximized;
        def.language = m_settings.language;
        def.sourceMode = m_settings.sourceMode;
        def.imagePath = m_settings.imagePath;
        def.videoPath = m_settings.videoPath;
        m_settings = def;
        m_spout.SetRequestedSender(m_settings.senderName);
        m_pipeline.MarkNrDirty();
        m_pipeline.MarkDlaaDirty();
        if (!m_headless) RegisterHotkey();
        RestartRuntimeChoice();
        if (EffectiveRoute() != RouteFsrHost) RequestRuntimeLoad(false);
        m_ui.Toast(TR(SettingsReset));
        ev.settingsChanged = true;
    }
    if (ev.sourceModeChanged) {
        Log::Info("Source: %s", m_settings.sourceMode == SourceImage ? "image file" : m_settings.sourceMode == SourceVideo ? "video file" : "Spout");
        ev.settingsChanged = true;
    }
    // The settings snapshot goes to the processing thread before the request flags so that a flag is never
    // consumed together with an older snapshot.
    if (ev.settingsChanged) {
        m_settings.Clamp();
        MarkSettingsDirty();
    }
    if (ev.hotkeyChanged && !m_headless) RegisterHotkey();
    if (ev.nrChanged) {
        m_pipeline.MarkNrDirty();
        // A switch from the FSR host to an NGX route needs the NVIDIA runtime, which that route left unloaded at start.
        if (EffectiveRoute() != RouteFsrHost && m_runtimeRequested.empty() && !m_status.nrRuntimeLoaded && !m_status.nrRuntimeIdle) RequestRuntimeLoad(false);
        WakeWorker();
    }
    if (ev.dlaaChanged) { m_pipeline.MarkDlaaDirty(); WakeWorker(); }
    if (ev.resetHistory) { m_pipeline.RequestReset(); WakeWorker(); m_ui.Toast(TR(HistoryReset)); }
    if (ev.senderChanged) { m_spout.SetRequestedSender(m_settings.senderName); WakeWorker(); }
    if (ev.refreshSenders) { m_refreshSenders = true; WakeWorker(); }
    if (ev.reloadRuntime) {
        if (EffectiveRoute() == RouteFsrHost) { m_pipeline.RetryFsrHost(); m_pipeline.MarkNrDirty(); WakeWorker(); }
        else { RestartRuntimeChoice(); RequestRuntimeLoad(true); }
    }
    if (ev.portInstall) {
        // The first press is the consent to fetching and running that project's installer under its own licence.
        if (!m_settings.portConsent) { m_settings.portConsent = true; MarkSettingsDirty(); }
        m_portRestartHint = false; m_portRestartAt = -1.0;
        m_portSetup.Install(m_exeDir);
    }
    if (ev.portOpenLicense) ShellExecuteW(nullptr, L"open", Utf8ToWide(PortSetup::kLicenseUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (ev.portRestartCancel) m_portRestartAt = -1.0;
    if (ev.editionSwitch) m_updater.Check(APP_VERSION_STRING, true, true, true);
    if (ev.portOpenPage) {
        const std::wstring url = Utf8ToWide(m_portStatus.pageUrl.empty() ? std::string(PortSetup::kPageUrl) : m_portStatus.pageUrl);
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ev.restartApp) RelaunchSelf();
}

void App::CaptureNow() {
    if (m_source.batchRunning || m_libraryBatchRunning) { m_ui.Toast(TR(BatchBusy), true); return; }
    if (m_settings.sourceMode == SourceVideo) {
        if (m_source.videoProcessing) { m_ui.Toast(TR(VideoBusy), true); return; }
        if (!m_source.videoLoaded) { m_ui.Toast(TR(CaptureNoVideo), true); return; }
        Command c;
        c.type = Command::ProcessVideo;
        c.path = EffectiveCaptureFolder();
        PostCommand(std::move(c));
        m_ui.Toast(TR(VideoProcessing));
        return;
    }
    if (m_settings.sourceMode == SourceImage) {
        if (!m_source.imageLoaded) {
            m_ui.Toast(TR(CaptureNoImage), true);
            return;
        }
        Command c;
        c.type = Command::CaptureImage;
        c.path = EffectiveCaptureFolder();
        c.keepAlpha = m_settings.keepAlpha;
        c.saveOriginal = m_settings.saveOriginal;
        PostCommand(std::move(c));
        m_ui.Toast(TR(Capturing));
        return;
    }
    if (!m_source.hasFrame || !m_status.hasDisplay) {
        m_ui.Toast(TR(CaptureNoFrame), true);
        return;
    }
    m_pipeline.RequestCapture(EffectiveCaptureFolder(), m_settings.keepAlpha, m_settings.saveOriginal, L"");
    WakeWorker();
    m_ui.Toast(TR(Capturing));
}

void App::RegisterHotkey() {
    UnregisterHotKey(m_hwnd, kHotkeyId);
    if (!m_settings.hotkeyEnabled) return;
    const UINT mods = (m_settings.hotkeyModifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)) | MOD_NOREPEAT;
    if (!RegisterHotKey(m_hwnd, kHotkeyId, mods, m_settings.hotkeyKey)) {
        Log::Warn("RegisterHotKey(%s) failed: %s", HotkeyText(m_settings).c_str(), LastErrorText().c_str());
        m_ui.Toast(TR(HotkeyFailed), true);
    } else {
        Log::Info("Capture hotkey: %s", HotkeyText(m_settings).c_str());
    }
}

void App::RequestRuntimeLoad(bool announce) {
    Command c;
    c.type = Command::LoadRuntime;
    c.path = EffectiveRuntimePath();
    c.announce = announce;
    m_runtimeRequested = c.path;
    m_runtimeBlackSince = -1.0;
    PostCommand(std::move(c));
}

void App::OpenImageFile(const std::wstring& path) {
    if (path.empty()) return;
    if (m_source.batchRunning || m_libraryBatchRunning) { m_ui.Toast(TR(BatchBusy), true); return; }
    if (m_source.videoProcessing) { m_ui.Toast(TR(VideoBusy), true); return; }
    Log::Info("Opening image %s", WideToUtf8(path).c_str());
    m_settings.sourceMode = SourceImage;
    m_settings.imagePath = WideToUtf8(path);
    MarkSettingsDirty();
    Command c;
    c.type = Command::LoadImage;
    c.path = path;
    c.transform = LibraryTransform(path, false);
    m_sentTransform = c.transform; m_sentTransformPath = path; m_sentTransformVideo = false;
    PostCommand(std::move(c));
}

void App::OpenVideoFile(const std::wstring& path) {
    if (path.empty()) return;
    if (m_source.batchRunning || m_libraryBatchRunning) { m_ui.Toast(TR(BatchBusy), true); return; }
    if (m_source.videoProcessing) { m_ui.Toast(TR(VideoBusy), true); return; }
    Log::Info("Opening video %s", WideToUtf8(path).c_str());
    m_settings.sourceMode = SourceVideo;
    m_settings.videoPath = WideToUtf8(path);
    MarkSettingsDirty();
    Command c;
    c.type = Command::LoadVideo;
    c.path = path;
    c.transform = LibraryTransform(path, true);
    m_sentTransform = c.transform; m_sentTransformPath = path; m_sentTransformVideo = true;
    PostCommand(std::move(c));
}

// presets.txt: "[preset]" opens an entry, "name=" gives its name, the other lines are its effect values.
void App::LoadPresets() {
    m_presets.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, m_presetsPath.c_str(), L"rb") != 0 || !f) return;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    ui::UserPreset* cur = nullptr;
    size_t pos = 0;
    while (pos <= data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        const std::string line = Trim(data.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line == "[preset]") { m_presets.push_back({}); cur = &m_presets.back(); continue; }
        if (!cur) continue;
        if (line.rfind("name=", 0) == 0) cur->name = Trim(line.substr(5));
        else if (line.find('=') != std::string::npos) cur->text += line + "\n";
    }
    // Entries without a name are of no use.
    m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(), [](const ui::UserPreset& pr) { return pr.name.empty(); }), m_presets.end());
    Log::Info("Presets: %zu loaded", m_presets.size());
}

void App::SavePresets() const {
    std::string out = "# VRChat DLSS5 Cam presets\n";
    for (const ui::UserPreset& pr : m_presets) {
        out += "\n[preset]\nname=" + pr.name + "\n" + pr.text;
    }
    FILE* f = nullptr;
    if (_wfopen_s(&f, m_presetsPath.c_str(), L"wb") != 0 || !f) { Log::Warn("Presets: cannot write %s", WideToUtf8(m_presetsPath).c_str()); return; }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
}

std::wstring App::DocsUrl() const {
    switch (I18n::Current()) {
        case Lang::Chinese:  return std::wstring(kProjectUrl) + L"/blob/main/docs/README.zh-CN.md";
        case Lang::Japanese: return std::wstring(kProjectUrl) + L"/blob/main/docs/README.ja.md";
        case Lang::Korean:   return std::wstring(kProjectUrl) + L"/blob/main/docs/README.ko.md";
        default:             return std::wstring(kProjectUrl) + L"/blob/main/README.md";
    }
}

// The orientation and crop a file has in the library (as it comes, for a file that is not in it).
SourceTransform App::LibraryTransform(const std::wstring& path, bool video) const {
    for (const LibraryItem& item : m_library)
        if (item.isVideo == video && SamePath(item.path, path)) return item.transform;
    return SourceTransform{};
}

// The library item shown in the preview (null: none, or a file that is not in the library).
const LibraryItem* App::ShownItem() const {
    const bool video = m_settings.sourceMode == SourceVideo, image = m_settings.sourceMode == SourceImage;
    if (!video && !image) return nullptr;
    const std::wstring& shown = video ? m_source.videoPath : m_source.imagePath;
    if (shown.empty() || (video && !m_source.videoLoaded) || (image && !m_source.imageLoaded)) return nullptr;
    for (const LibraryItem& item : m_library)
        if (item.isVideo == video && SamePath(item.path, shown)) return &item;
    return nullptr;
}

// The shown file's orientation follows its library item (edits, undo, a restore) or, while its crop is being
// drawn, the whole turned picture; the processing thread is told whenever that differs from what it was last sent.
void App::SyncTransform(const ui::UiEvents& ev) {
    const bool video = m_settings.sourceMode == SourceVideo, image = m_settings.sourceMode == SourceImage;
    if (!video && !image) return;
    const std::wstring& shown = video ? m_source.videoPath : m_source.imagePath;
    if (shown.empty() || (video && !m_source.videoLoaded) || (image && !m_source.imageLoaded)) return;
    SourceTransform want = LibraryTransform(shown, video);
    if (ev.cropEditing) { want = ev.cropPreview; want.cropX = want.cropY = 0.0f; want.cropW = want.cropH = 1.0f; }
    if (m_sentTransformVideo == video && SamePath(m_sentTransformPath, shown) && m_sentTransform == want) return;
    m_sentTransform = want; m_sentTransformPath = shown; m_sentTransformVideo = video;
    Command c;
    c.type = Command::SetTransform;
    c.path = shown;
    c.video = video;
    c.transform = want;
    PostCommand(std::move(c));
}

void App::CloseMediaFile() {
    if (m_source.batchRunning || m_libraryBatchRunning) { m_ui.Toast(TR(BatchBusy), true); return; }
    if (m_source.videoProcessing) { m_ui.Toast(TR(VideoBusy), true); return; }
    const bool video = m_settings.sourceMode == SourceVideo;
    if (video ? m_settings.videoPath.empty() : m_settings.imagePath.empty()) return;
    Log::Info("Closing the %s", video ? "video" : "image");
    if (video) m_settings.videoPath.clear(); else m_settings.imagePath.clear();
    MarkSettingsDirty();
    Command c;
    c.type = Command::CloseMedia;
    c.video = video;
    PostCommand(std::move(c));
}

// Fullscreen: a borderless window over the monitor the window is on; the placement it had comes back afterwards
// (and is what gets saved at exit, see SaveWindowPlacement).
void App::SetFullscreen(bool on) {
    if (m_headless || !m_hwnd || on == m_fullscreen) return;
    if (on) {
        m_fullscreenPlacement = WINDOWPLACEMENT{};
        m_fullscreenPlacement.length = sizeof(m_fullscreenPlacement);
        if (!GetWindowPlacement(m_hwnd, &m_fullscreenPlacement)) return;
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        m_fullscreen = true;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, (GetWindowLongPtrW(m_hwnd, GWL_STYLE) & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        m_fullscreen = false;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, (GetWindowLongPtrW(m_hwnd, GWL_STYLE) & ~(LONG_PTR)WS_POPUP) | WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(m_hwnd, &m_fullscreenPlacement);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    Log::Info("Fullscreen %s", on ? "on" : "off");
}

// One dropped file: the runtime DLL is taken as the runtime, a picture or a video opens (and joins the library).
void App::OnFileDropped(const std::wstring& path) {
    if (DirectoryExists(path)) { OnFilesDropped({ path }); return; }
    if (LowerExtension(path) == L"dll") {
        m_settings.nrDllPath = WideToUtf8(path);
        MarkSettingsDirty();
        RequestRuntimeLoad(true);
        return;
    }
    bool isVideo = false;
    if (!IsLibraryFile(path, isVideo)) {
        m_ui.Toast(TR(BatchNoFiles), true);
        return;
    }
    if (isVideo) OpenVideoFile(path);
    else OpenImageFile(path);
    AddLibraryFiles({ path }, false);
}

// Several files or a folder: they join the library; the first one is shown when nothing is open in the current mode.
void App::OnFilesDropped(const std::vector<std::wstring>& paths) {
    if (paths.size() == 1 && !DirectoryExists(paths[0])) { OnFileDropped(paths[0]); return; }
    const size_t before = m_library.size();
    AddLibraryFiles(paths, true);
    if (m_library.size() <= before) return;
    const LibraryItem& first = m_library[before];
    const bool nothingOpen = (m_settings.sourceMode == SourceImage && !m_source.imageLoaded) ||
                             (m_settings.sourceMode == SourceVideo && !m_source.videoLoaded);
    if (nothingOpen) PreviewLibraryItem(first.id);
}

// --- media library --------------------------------------------------------------------------

LibraryItem* App::FindItem(unsigned id) {
    if (id == 0) return nullptr;
    for (LibraryItem& item : m_library) if (item.id == id) return &item;
    return nullptr;
}

void App::AddLibraryFiles(const std::vector<std::wstring>& paths, bool announce) {
    std::vector<std::wstring> files;
    for (const std::wstring& p : paths) {
        if (DirectoryExists(p)) { const std::vector<std::wstring> inside = ListFolderFiles(p); files.insert(files.end(), inside.begin(), inside.end()); }
        else files.push_back(p);
    }
    int added = 0;
    bool full = false;
    for (const std::wstring& f : files) {
        bool isVideo = false;
        if (!IsLibraryFile(f, isVideo)) continue;
        bool dup = false;
        for (const LibraryItem& item : m_library) if (SamePath(item.path, f)) { dup = true; break; }
        if (dup) continue;
        if (m_library.size() >= (size_t)kLibraryMax) { full = true; break; }
        LibraryItem item;
        item.id = m_nextItemId++;
        item.path = f;
        item.name = WideToUtf8(FileNameOf(f));
        item.isVideo = isVideo;
        item.thumbCell = m_atlas.Ready() ? m_atlas.Alloc() : -1;
        m_library.push_back(item);
        m_scanner.Probe(item.id, f, isVideo, item.thumbCell);
        ++added;
    }
    if (added > 0) Log::Info("Library: %d file(s) added, %zu in the library", added, m_library.size());
    if (full) m_ui.Toast(StrPrintf(TR(LibraryFull), kLibraryMax), true);
    if (announce) {
        if (added > 0) m_ui.Toast(StrPrintf(TR(BatchAdded), added));
        else if (!full) m_ui.Toast(TR(BatchNoFiles), true);
    }
}

void App::RemoveLibraryItem(unsigned id) {
    for (size_t i = 0; i < m_library.size(); ++i) {
        LibraryItem& item = m_library[i];
        if (item.id != id) continue;
        if (item.state == LibraryItem::Queued || item.state == LibraryItem::Processing) { m_ui.Toast(TR(BatchBusy), true); return; }
        m_scanner.Forget(id);
        if (item.thumbCell >= 0) m_atlas.Free(item.thumbCell);
        m_library.erase(m_library.begin() + (ptrdiff_t)i);
        return;
    }
}

void App::RemoveSelectedLibraryItems() {
    std::vector<unsigned> ids;
    for (const LibraryItem& item : m_library) if (item.selected) ids.push_back(item.id);
    for (unsigned id : ids) RemoveLibraryItem(id);
}

void App::LocateLibraryItem(unsigned id) {
    const LibraryItem* item = FindItem(id);
    if (!item) return;
    const std::wstring args = L"/select,\"" + item->path + L"\"";
    const HINSTANCE r = ShellExecuteW(m_hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) Log::Warn("Explorer could not be opened for %s (%d)", WideToUtf8(item->path).c_str(), (int)(INT_PTR)r);
}

void App::ReadSystemTheme() {
    DWORD value = 0, size = sizeof(value);
    const LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                    L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    m_systemLight = (st == ERROR_SUCCESS && value != 0);
}

void App::UpdateTitleBar() {
    if (!m_hwnd) return;
    const bool light = m_settings.theme == 2 || (m_settings.theme == 0 && m_systemLight);
    const int dark = light ? 0 : 1;
    if (dark == m_titleDark) return;
    m_titleDark = dark;
    BOOL flag = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &flag, sizeof(flag))))
        DwmSetWindowAttribute(m_hwnd, 19, &flag, sizeof(flag));
}

const LibraryItem* App::OverrideItem() const {
    const bool video = m_settings.sourceMode == SourceVideo, image = m_settings.sourceMode == SourceImage;
    if (!video && !image) return nullptr;
    const std::wstring& shown = video ? m_source.videoPath : m_source.imagePath;
    if (shown.empty() || (video && !m_source.videoLoaded) || (image && !m_source.imageLoaded)) return nullptr;
    for (const LibraryItem& item : m_library)
        if (item.useOwn && item.own && item.isVideo == video && SamePath(item.path, shown)) return &item;
    return nullptr;
}

// Undo/redo of the library: the list is brought to the given one. Files still in the library keep their item
// (thumbnail, probe result, batch state); files that left it come back as new items in their old place.
void App::RestoreLibrary(const std::vector<ui::LibrarySnapshotItem>& wanted) {
    if (m_libraryBatchRunning || m_source.batchRunning) { m_ui.Toast(TR(BatchBusy), true); return; }
    std::vector<LibraryItem> next;
    std::vector<bool> kept(m_library.size(), false);
    for (const ui::LibrarySnapshotItem& w : wanted) {
        size_t found = m_library.size();
        for (size_t i = 0; i < m_library.size(); ++i) {
            if (!kept[i] && SamePath(m_library[i].path, w.path)) { found = i; break; }
        }
        LibraryItem item;
        if (found < m_library.size()) {
            kept[found] = true;
            item = m_library[found];
        } else {
            bool isVideo = false;
            if (!IsLibraryFile(w.path, isVideo)) continue;
            if (next.size() >= (size_t)kLibraryMax) break;
            item.id = m_nextItemId++;
            item.path = w.path;
            item.name = WideToUtf8(FileNameOf(w.path));
            item.isVideo = isVideo;
            item.thumbCell = m_atlas.Ready() ? m_atlas.Alloc() : -1;
            item.inSec = w.inSec; item.outSec = w.outSec;
            m_scanner.Probe(item.id, w.path, isVideo, item.thumbCell);
        }
        item.useOwn = w.useOwn;
        if (w.useOwn && !w.own.empty()) {
            if (!item.own) item.own = std::make_shared<Settings>(m_settings);
            item.own->ApplyText(w.own);
            item.own->Clamp();
        }
        item.transform = w.transform;
        next.push_back(std::move(item));
    }
    for (size_t i = 0; i < m_library.size(); ++i) {
        if (kept[i]) continue;
        m_scanner.Forget(m_library[i].id);
        if (m_library[i].thumbCell >= 0) m_atlas.Free(m_library[i].thumbCell);
    }
    m_library = std::move(next);
    Log::Info("Library: %zu file(s) after undo/redo", m_library.size());
}

void App::ClearLibrary() {
    if (m_libraryBatchRunning || m_source.batchRunning) { m_ui.Toast(TR(BatchBusy), true); return; }
    for (const LibraryItem& item : m_library) {
        m_scanner.Forget(item.id);
        if (item.thumbCell >= 0) m_atlas.Free(item.thumbCell);
    }
    m_library.clear();
}

void App::PreviewLibraryItem(unsigned id) {
    const LibraryItem* item = FindItem(id);
    if (!item) return;
    if (item->isVideo) {
        if (m_settings.sourceMode == SourceVideo && m_source.videoLoaded && SamePath(m_source.videoPath, item->path)) return;
        OpenVideoFile(item->path);
        // The item's range follows the file into the preview (LoadVideo runs first: commands keep their order).
        Command c; c.type = Command::VideoSetRange; c.seconds = item->inSec; c.seconds2 = item->outSec;
        PostCommand(std::move(c));
    } else {
        if (m_settings.sourceMode == SourceImage && m_source.imageLoaded && SamePath(m_source.imagePath, item->path)) return;
        OpenImageFile(item->path);
    }
}

void App::StartLibraryProcessing(bool selectedOnly) {
    if (m_libraryBatchRunning || m_source.batchRunning) { m_ui.Toast(TR(BatchBusy), true); return; }
    if (m_source.videoProcessing) { m_ui.Toast(TR(VideoBusy), true); return; }
    std::vector<BatchItem> items;
    for (const LibraryItem& item : m_library) {
        if (selectedOnly && !item.selected) continue;
        if (item.probe == 2) continue;   // unreadable
        BatchItem b;
        b.id = item.id; b.path = item.path; b.isVideo = item.isVideo; b.inSec = item.inSec; b.outSec = item.outSec;
        b.transform = item.transform;
        if (item.useOwn && item.own) b.own = std::make_shared<Settings>(*item.own);
        items.push_back(b);
    }
    if (items.empty()) { m_ui.Toast(TR(BatchEmpty), true); return; }
    for (const BatchItem& b : items) {
        if (LibraryItem* item = FindItem(b.id)) { item->state = LibraryItem::Queued; item->progress = 0.0f; item->outName.clear(); item->error.clear(); }
    }
    const int count = (int)items.size();
    Command c;
    c.type = Command::BatchStart;
    c.items = std::move(items);
    c.path = EffectiveCaptureFolder();
    c.keepAlpha = m_settings.keepAlpha;
    c.saveOriginal = m_settings.saveOriginal;
    PostCommand(std::move(c));
    m_libraryBatchRunning = true;
    m_ui.Toast(StrPrintf(TR(BatchRunning), 1, count));
}

void App::PollScanner() {
    ScanResult r;
    for (int n = 0; n < 32 && m_scanner.Poll(r); ++n) {
        switch (r.kind) {
        case ScanRequest::Probe:
            if (LibraryItem* item = FindItem(r.id)) {
                item->probe = r.ok ? 1 : 2;
                item->width = r.width; item->height = r.height;
                item->duration = r.duration; item->fps = r.fps; item->hasAudio = r.hasAudio;
                item->error = r.error;
                if (r.ok && item->thumbCell >= 0 && !r.thumb.empty()) m_atlas.Set(item->thumbCell, std::move(r.thumb));
                if (!r.ok) Log::Warn("Library: %s: %s", item->name.c_str(), r.error.c_str());
            }
            break;
        case ScanRequest::Storyboard:
            if (r.id != m_storyGeneration || r.slot < 0 || (size_t)r.slot >= m_storyCells.size()) break;
            if (r.ok && !r.thumb.empty()) {
                m_atlas.Set(m_storyCells[(size_t)r.slot], std::move(r.thumb));
                m_storyTimes[(size_t)r.slot] = r.seconds;
                m_storyReady[(size_t)r.slot] = true;
            }
            break;
        case ScanRequest::Hover:
            if (r.id != m_storyGeneration || m_hoverCell < 0) break;
            if (r.ok && !r.thumb.empty()) {
                m_atlas.Set(m_hoverCell, std::move(r.thumb));
                m_hoverCellTime = r.seconds;
            }
            break;
        }
    }
}

// The seek-bar pictures follow the opened video: evenly spaced frames, decoded coarse to fine.
void App::UpdateStoryboard() {
    if (m_source.batchRunning) return;   // the batch opens its own files
    const std::wstring path = (m_settings.sourceMode == SourceVideo && m_source.videoLoaded) ? m_source.videoPath : std::wstring();
    const double duration = m_source.videoDurationSeconds;
    if (SamePath(path, m_storyPath) && (path.empty() || std::fabs(duration - m_storyDuration) < 1e-6)) return;
    for (int c : m_storyCells) m_atlas.Free(c);
    m_storyCells.clear(); m_storyTimes.clear(); m_storyReady.clear();
    m_scanner.ClearStoryboard();
    ++m_storyGeneration;
    m_hoverCellTime = -1.0;
    m_hoverRequested = -1.0;
    m_storyPath = path;
    m_storyDuration = duration;
    if (path.empty() || !m_atlas.Ready() || duration <= 0.0) return;
    const double frame = m_source.videoFps > 0.0 ? 1.0 / m_source.videoFps : 1.0 / 30.0;
    const int slots = (int)std::clamp(std::ceil(duration / frame), 1.0, (double)kStorySlots);
    for (int i = 0; i < slots; ++i) {
        const int cell = m_atlas.Alloc();
        if (cell < 0) break;
        m_storyCells.push_back(cell);
        m_storyTimes.push_back(duration * ((double)i + 0.5) / (double)slots);
        m_storyReady.push_back(false);
    }
    const int n = (int)m_storyCells.size();
    std::vector<bool> queued((size_t)n, false);
    std::vector<ScanRequest> requests;
    int step = 1;
    while (step * 2 <= n) step *= 2;
    for (; step >= 1; step /= 2) {
        for (int i = 0; i < n; i += step) {
            if (queued[(size_t)i]) continue;
            queued[(size_t)i] = true;
            ScanRequest r;
            r.kind = ScanRequest::Storyboard;
            r.id = m_storyGeneration;
            r.path = path;
            r.isVideo = true;
            r.seconds = m_storyTimes[(size_t)i];
            r.cell = m_storyCells[(size_t)i];
            r.slot = i;
            requests.push_back(std::move(r));
        }
    }
    m_scanner.SetStoryboard(m_storyGeneration, path, std::move(requests));
}

// The exact frame under the cursor on the seek bar (the storyboard picture shows until it arrives).
void App::RequestHoverThumb(double seconds) {
    if (m_storyPath.empty() || !m_atlas.Ready()) return;
    if (m_hoverCell < 0) {
        m_hoverCell = m_atlas.Alloc();
        if (m_hoverCell < 0) return;
    }
    const double frame = m_source.videoFps > 0.0 ? 1.0 / m_source.videoFps : 1.0 / 30.0;
    const double now = NowSeconds();
    if (m_hoverRequested >= 0.0 && std::fabs(seconds - m_hoverRequested) < frame * 0.5) return;
    if (now - m_hoverRequestTime < kHoverDebounce) return;   // the next hover event carries the newest position
    m_hoverRequested = seconds;
    m_hoverRequestTime = now;
    m_scanner.Hover(m_storyGeneration, seconds, m_hoverCell);
}

// --- settings, dialogs ----------------------------------------------------------------------

void App::MarkSettingsDirty() {
    if (m_settingsDirtyTime < 0.0) m_settingsDirtyTime = NowSeconds();
    PushSettings();
}

void App::SaveSettings() {
    m_settingsDirtyTime = -1.0;
    if (!m_settings.Save(m_settingsPath)) Log::Warn("Failed to save settings to %s", WideToUtf8(m_settingsPath).c_str());
}

void App::SaveWindowPlacement() {
    if (m_headless) return;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!m_hwnd) return;
    if (m_fullscreen) wp = m_fullscreenPlacement;   // the window underneath the fullscreen view
    else if (!GetWindowPlacement(m_hwnd, &wp)) return;
    m_settings.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    m_settings.windowX = wp.rcNormalPosition.left;
    m_settings.windowY = wp.rcNormalPosition.top;
    m_settings.windowWidth = std::max(400L, wp.rcNormalPosition.right - wp.rcNormalPosition.left);
    m_settings.windowHeight = std::max(300L, wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
}

// The release archive carries the runtime in two builds under the same file name: runtimes\blackwell\ as shipped
// with games, which only carries code for RTX 50, and runtimes\universal\, adapted for RTX 40/30/20. A build for
// another vendor would be a third file under runtimes\other\, and a file next to the executable is honoured too.
// The order below is the order they are tried in; the generation read from the adapter name decides the first one,
// and a build that took over in an earlier session (Settings::nrRuntimeBuild) moves to the front.
std::vector<App::RuntimeCandidate> App::RuntimeCandidates() const {
    static constexpr wchar_t kName[] = L"nvngx_dlssnr.dll";
    const std::wstring runtimes = JoinPath(m_exeDir, L"runtimes");
    const RuntimeCandidate blackwell{ JoinPath(JoinPath(runtimes, L"blackwell"), kName), "blackwell" };
    const RuntimeCandidate universal{ JoinPath(JoinPath(runtimes, L"universal"), kName), "universal" };
    const RuntimeCandidate other{ JoinPath(JoinPath(runtimes, L"other"), kName), "other" };
    const RuntimeCandidate exe{ JoinPath(m_exeDir, kName), "exe" };
    const AdapterInfo& ai = m_device.Info();
    std::vector<RuntimeCandidate> order;
    if (!ai.IsNvidia())               order = { other, exe };   // NVIDIA's builds do not start elsewhere: no point trying
    else if (ai.RtxGeneration() >= 5) order = { blackwell, exe, universal, other };
    else                              order = { universal, exe, blackwell, other };   // 0: unknown name, older than RTX 50 in all likelihood
    if (!m_settings.nrRuntimeBuild.empty()) {
        const auto it = std::find_if(order.begin(), order.end(), [&](const RuntimeCandidate& c) { return m_settings.nrRuntimeBuild == c.build; });
        if (it != order.end()) std::rotate(order.begin(), it, it + 1);
    }
    return order;
}

std::wstring App::EffectiveRuntimePath() const {
    if (!m_settings.nrDllPath.empty()) return Utf8ToWide(m_settings.nrDllPath);
    const std::vector<RuntimeCandidate> candidates = RuntimeCandidates();
    const std::wstring* last = nullptr;   // the last existing one: with every build failed, its error stays on view
    for (const RuntimeCandidate& c : candidates) {
        if (!FileExists(c.path)) continue;
        if (std::find(m_runtimeFailed.begin(), m_runtimeFailed.end(), c.path) == m_runtimeFailed.end()) return c.path;
        last = &c.path;
    }
    // None there: the first candidate is what the missing-runtime message points at.
    return last ? *last : candidates.front().path;
}

const char* App::RuntimeBuildName(const std::wstring& path) const {
    if (path.empty()) return nullptr;
    for (const RuntimeCandidate& c : RuntimeCandidates()) {
        if (c.path != path) continue;
        if (!std::strcmp(c.build, "blackwell")) return TR(RuntimeBuildBlackwell);
        if (!std::strcmp(c.build, "universal")) return TR(RuntimeBuildUniversal);
        if (!std::strcmp(c.build, "other"))     return TR(RuntimeBuildOther);
        return TR(RuntimeBuildExe);
    }
    return nullptr;
}

int App::EffectiveRoute() const { return EffectiveNrRoute(m_settings.nrRoute, m_device.Info().IsAmd()); }

// The installer runs in its own window; each state change is told once. It leaves its weights file next to the
// executable when it installed: then the release it came from is recorded and the application restarts by itself
// (DLSS-NR-on-AMD attaches at process start), unless work is running.
void App::PollPortSetup() {
    static constexpr double kRestartDelay = 4.0;   // seconds between the installer's end and the restart
    m_portStatus = m_portSetup.Get();
    if (m_portStatus.generation == m_portGenSeen) return;
    m_portGenSeen = m_portStatus.generation;
    switch (m_portStatus.state) {
        case PortSetup::State::Ready: RecordPortVersion(); break;
        case PortSetup::State::Installing: m_ui.Toast(TR(AmdPortInstalling)); break;
        case PortSetup::State::Launched: m_ui.Toast(TR(AmdPortInstallerRunning)); break;
        case PortSetup::State::Finished: {
            m_portWeightsTime = -1.0;   // looked at again on the next frame
            const bool weights = FileExists(JoinPath(m_exeDir, PortSetup::kWeightsFile));
            const std::string tag = weights ? m_portStatus.tag : std::string();   // removed: no release any more
            if (m_settings.portInstalledTag != tag) { m_settings.portInstalledTag = tag; MarkSettingsDirty(); }
            if (weights) { m_portRestartHint = true; m_portRestartAt = NowSeconds() + kRestartDelay; m_ui.Toast(TR(AmdPortInstalled)); }
            else m_ui.Toast(TR(AmdPortNotInstalled), true);
            break;
        }
        case PortSetup::State::Failed: m_ui.Toast(StrPrintf("%s: %s", TR(AmdPortFailed), m_portStatus.error.c_str()), true); break;
        default: break;
    }
}

// An installation made before the record existed (or by hand): when the installer file next to the executable is
// the latest release's one (same size), the installation counts as that release.
void App::RecordPortVersion() {
    if (!m_settings.portInstalledTag.empty() || m_portStatus.tag.empty() || m_portStatus.assetSize == 0) return;
    if (!FileExists(JoinPath(m_exeDir, PortSetup::kWeightsFile))) return;
    if (GetFileSizeBytes(JoinPath(m_exeDir, PortSetup::kSetupFile)) != m_portStatus.assetSize) return;
    m_settings.portInstalledTag = m_portStatus.tag;
    MarkSettingsDirty();
    Log::Info("DLSS-NR-on-AMD: the installer next to the executable is the %s one; the installation is taken as %s", m_portStatus.tag.c_str(), m_portStatus.tag.c_str());
}

// What of DLSS-NR-on-AMD lies next to the executable and, at the end, the last lines of its own log: one log.txt
// then tells the whole story of a report.
void App::LogPortState(bool tail) {
    const wchar_t* names[] = { PortSetup::kWeightsFile, PortSetup::kSetupFile, PortSetup::kLogFile, L"version.dll", L"winmm.dll",
                               L"dbghelp.dll", L"wininet.dll", L"winhttp.dll", L"dxgi.dll" };
    std::string found;
    for (const wchar_t* n : names) {
        const std::wstring p = JoinPath(m_exeDir, n);
        if (!FileExists(p)) continue;
        found += StrPrintf("%s%s (%llu bytes)", found.empty() ? "" : ", ", WideToUtf8(n).c_str(), (unsigned long long)GetFileSizeBytes(p));
    }
    Log::Info("DLSS-NR-on-AMD files next to the executable: %s", found.empty() ? "none" : found.c_str());
    if (!tail) return;
    // Its log may still be open for writing: read with every share mode.
    HANDLE h = CreateFileW(JoinPath(m_exeDir, PortSetup::kLogFile).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    const DWORD take = (DWORD)std::min<LONGLONG>(size.QuadPart, 3000);
    LARGE_INTEGER pos{};
    pos.QuadPart = size.QuadPart - take;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    std::string text((size_t)take, '\0');
    DWORD got = 0;
    if (!ReadFile(h, text.data(), take, &got, nullptr)) got = 0;
    CloseHandle(h);
    text.resize(got);
    if (take < (DWORD)size.QuadPart) {   // from a whole line on
        const size_t nl = text.find('\n');
        text = nl == std::string::npos ? std::string() : text.substr(nl + 1);
    }
    for (char& c : text) if (c == '\r') c = ' ';
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    Log::Info("DLSS-NR-on-AMD log (%s, %lld bytes), last lines:\n%s", WideToUtf8(PortSetup::kLogFile).c_str(), (long long)size.QuadPart, text.c_str());
}

void App::RelaunchSelf() {
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    Log::Info("Restarting %s", WideToUtf8(exe).c_str());
    ShellExecuteW(nullptr, L"open", exe, nullptr, m_exeDir.c_str(), SW_SHOWNORMAL);
    if (m_hwnd) PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
}

int App::RunAgainAndWait(const std::string& note) {
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    std::wstring cmdLine = GetCommandLineW();   // the same arguments; CreateProcess may write into the buffer
    std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end());
    cmd.push_back(L'\0');
    // Inherited by the second instance: marks it as such and carries what was changed for its log.
    SetEnvironmentVariableW(L"VDC_PORT_INI_ADJUSTED", Utf8ToWide(note.empty() ? std::string("1") : note).c_str());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return 1;   // the log is closed; the second instance would have said more
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}

void App::RestartRuntimeChoice() {
    m_runtimeFailed.clear();
    m_runtimeExhausted = false;
    m_runtimeBlackSince = -1.0;
    if (!m_settings.nrRuntimeBuild.empty()) { m_settings.nrRuntimeBuild.clear(); MarkSettingsDirty(); }
}

// A build that does not run on this card fails at load, at feature creation, or silently with a black picture
// (a runtime adapted for another generation has done that). The processing thread reports all three through the
// status; here the failed candidate is set aside, the next one requested, the choice kept for the next start and
// the user told. A file the user selected is never replaced.
void App::CheckRuntimeFallback() {
    static constexpr double kBlackSeconds = 3.0;   // continuous black output before a build counts as failed
    if (m_runtimeExhausted || m_runtimeRequested.empty() || !m_settings.nrDllPath.empty()) return;
    const PipelineStatus& st = m_status;
    if (st.nrRequestedPath != m_runtimeRequested) return;   // the processing thread has not reached the request
    bool failed = false;
    if (!st.nrError.empty() && (st.nrFailed || (!st.nrRuntimeLoaded && !st.nrRuntimeIdle))) failed = true;
    else if (st.nrRuntimeLoaded && st.nrOutState == 2) {
        const double now = NowSeconds();
        if (m_runtimeBlackSince < 0.0) m_runtimeBlackSince = now;
        else if (now - m_runtimeBlackSince >= kBlackSeconds) failed = true;
    } else {
        m_runtimeBlackSince = -1.0;
    }
    if (!failed) return;

    const std::wstring failedPath = m_runtimeRequested;
    const std::string from = RuntimeBuildName(failedPath) ? RuntimeBuildName(failedPath) : WideToUtf8(FileNameOf(failedPath));
    m_runtimeFailed.push_back(failedPath);
    m_runtimeBlackSince = -1.0;
    const RuntimeCandidate* next = nullptr;
    const std::vector<RuntimeCandidate> candidates = RuntimeCandidates();
    for (const RuntimeCandidate& c : candidates)
        if (FileExists(c.path) && std::find(m_runtimeFailed.begin(), m_runtimeFailed.end(), c.path) == m_runtimeFailed.end()) { next = &c; break; }
    if (!next) {
        m_runtimeExhausted = true;
        m_runtimeRequested.clear();
        // The kept build did not help either: the next start begins with the one for the adapter again.
        if (!m_settings.nrRuntimeBuild.empty()) { m_settings.nrRuntimeBuild.clear(); MarkSettingsDirty(); }
        Log::Warn("DLSS 5 runtime: %s failed on this adapter and no other build is left to try", WideToUtf8(failedPath).c_str());
        return;
    }
    Log::Warn("DLSS 5 runtime: %s failed on this adapter; switching to %s", WideToUtf8(failedPath).c_str(), WideToUtf8(next->path).c_str());
    m_ui.Toast(StrPrintf(TR(RuntimeFallbackToast), from.c_str(), RuntimeBuildName(next->path)), true);
    if (m_settings.nrRuntimeBuild != next->build) { m_settings.nrRuntimeBuild = next->build; MarkSettingsDirty(); PushSettings(); }
    RequestRuntimeLoad(false);
}

std::wstring App::EffectiveCaptureFolder(const Settings& s) {
    if (!s.captureFolder.empty()) return Utf8ToWide(s.captureFolder);
    return JoinPath(GetPicturesDir(), L"VRChat DLSS5 Cam");
}

namespace {
// Starts a file dialog in the folder of a previous file.
void StartInFolderOf(IFileOpenDialog* dlg, const std::string& previousUtf8) {
    if (previousUtf8.empty()) return;
    const std::wstring prev = Utf8ToWide(previousUtf8);
    const size_t slash = prev.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHCreateItemFromParsingName(prev.substr(0, slash).c_str(), nullptr, IID_PPV_ARGS(&folder)))) dlg->SetFolder(folder.Get());
}

std::vector<std::wstring> DialogResults(IFileOpenDialog* dlg) {
    std::vector<std::wstring> paths;
    ComPtr<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items)) || !items) return paths;
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        if (FAILED(items->GetItemAt(i, &item)) || !item) continue;
        PWSTR psz = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) { paths.emplace_back(psz); CoTaskMemFree(psz); }
    }
    return paths;
}
} // namespace

void App::BrowseRuntime() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    const COMDLG_FILTERSPEC filters[] = { { L"DLSS 5 runtime (nvngx_dlssnr.dll)", L"nvngx_dlssnr.dll;*.dll" }, { L"All files", L"*.*" } };
    dlg->SetFileTypes(2, filters);
    dlg->SetFileName(L"nvngx_dlssnr.dll");
    dlg->SetTitle(L"nvngx_dlssnr.dll");
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        m_settings.nrDllPath = WideToUtf8(psz);
        CoTaskMemFree(psz);
        MarkSettingsDirty();
        RequestRuntimeLoad(true);
    }
}

void App::BrowseDepthModel() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    const COMDLG_FILTERSPEC filters[] = { { L"ONNX model (*.onnx)", L"*.onnx" }, { L"All files", L"*.*" } };
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(L"Depth model (Depth Anything V2, ONNX)");
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        m_settings.depthModelPath = WideToUtf8(psz);
        CoTaskMemFree(psz);
        MarkSettingsDirty();
    }
}

void App::BrowseFolder() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        m_settings.captureFolder = WideToUtf8(psz);
        CoTaskMemFree(psz);
        MarkSettingsDirty();
    }
}

void App::BrowseImage() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    const std::wstring imagesLabel = Utf8ToWide(TR(ImageFilter));
    const std::wstring title = Utf8ToWide(TR(OpenImage));
    const COMDLG_FILTERSPEC filters[] = { { imagesLabel.c_str(), kImagePatterns }, { L"All files", L"*.*" } };
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(title.c_str());
    StartInFolderOf(dlg.Get(), m_settings.imagePath);
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        const std::wstring path = psz;
        CoTaskMemFree(psz);
        OpenImageFile(path);
        AddLibraryFiles({ path }, false);
    }
}

void App::BrowseVideo() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    const std::wstring videosLabel = Utf8ToWide(TR(VideoFilter));
    const std::wstring title = Utf8ToWide(TR(OpenVideo));
    const COMDLG_FILTERSPEC filters[] = { { videosLabel.c_str(), kVideoPatterns }, { L"All files", L"*.*" } };
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(title.c_str());
    StartInFolderOf(dlg.Get(), m_settings.videoPath);
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        const std::wstring path = psz;
        CoTaskMemFree(psz);
        OpenVideoFile(path);
        AddLibraryFiles({ path }, false);
    }
}

void App::BrowseLibraryFiles() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT);
    const std::wstring mediaLabel = Utf8ToWide(TR(MediaFilter));
    const std::wstring imagesLabel = Utf8ToWide(TR(ImageFilter));
    const std::wstring videosLabel = Utf8ToWide(TR(VideoFilter));
    const std::wstring mediaPatterns = std::wstring(kImagePatterns) + L";" + kVideoPatterns;
    const std::wstring title = Utf8ToWide(TR(AddFiles));
    const COMDLG_FILTERSPEC filters[] = { { mediaLabel.c_str(), mediaPatterns.c_str() }, { imagesLabel.c_str(), kImagePatterns },
                                          { videosLabel.c_str(), kVideoPatterns }, { L"All files", L"*.*" } };
    dlg->SetFileTypes(4, filters);
    dlg->SetTitle(title.c_str());
    StartInFolderOf(dlg.Get(), !m_settings.imagePath.empty() ? m_settings.imagePath : m_settings.videoPath);
    if (FAILED(dlg->Show(m_hwnd))) return;
    const std::vector<std::wstring> paths = DialogResults(dlg.Get());
    if (!paths.empty()) OnFilesDropped(paths);
}

void App::BrowseLibraryFolder() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);
    const std::wstring title = Utf8ToWide(TR(AddFolder));
    dlg->SetTitle(title.c_str());
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        const std::wstring folder = psz;
        CoTaskMemFree(psz);
        OnFilesDropped({ folder });
    }
}

void App::OpenPath(const std::wstring& path) {
    if (path.empty()) return;
    const HINSTANCE r = ShellExecuteW(m_hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) Log::Warn("ShellExecute failed for %s (%d)", WideToUtf8(path).c_str(), (int)(INT_PTR)r);
}

// ------------------------------------------------------------------------------------------

LRESULT CALLBACK App::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (m_imguiReady && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 1;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            m_minimized = true;
        } else {
            m_minimized = false;
            if (m_deviceReady && !(m_headless && m_cli.width > 0)) {
                const UINT w = LOWORD(lParam), h = HIWORD(lParam);
                if (m_inFrame) { m_pendingResize = true; m_pendingWidth = w; m_pendingHeight = h; }
                else m_device.Resize(w, h);
            }
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        m_sizing = true;
        SetTimer(hwnd, kSizeTimer, 16, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        m_sizing = false;
        KillTimer(hwnd, kSizeTimer);
        return 0;
    case WM_TIMER:
        if (wParam == kSizeTimer && m_sizing && !m_minimized) Frame();
        return 0;
    case WM_HOTKEY:
        if ((int)wParam == kHotkeyId) CaptureNow();
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> paths;
        for (UINT i = 0; i < count; ++i) {
            wchar_t path[MAX_PATH * 4] = {};
            if (DragQueryFileW(drop, i, path, (UINT)(sizeof(path) / sizeof(path[0]))) > 0) paths.emplace_back(path);
        }
        DragFinish(drop);
        if (!paths.empty()) {
            SetForegroundWindow(hwnd);
            OnFilesDropped(paths);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (m_imguiReady) {
            ApplyDpi((float)HIWORD(wParam) / 96.0f);
            m_fontsDirty = true;
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 720;
        mmi->ptMinTrackSize.y = 480;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;   // no menu on Alt
        break;
    case WM_SETTINGCHANGE:
        if (lParam && wcscmp((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) ReadSystemTheme();
        break;
    case WM_CLOSE:
        SaveWindowPlacement();
        SaveSettings();
        m_quit = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace vdc
