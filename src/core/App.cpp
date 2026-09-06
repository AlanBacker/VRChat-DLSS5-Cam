#include "core/App.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Util.h"
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
#include <cmath>
#include <cwctype>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace vdc {

using Microsoft::WRL::ComPtr;

namespace {
constexpr int      kHotkeyId = 1;
constexpr UINT_PTR kSizeTimer = 1;
constexpr const wchar_t* kWindowClass = L"VRChatDLSS5CamWindow";
constexpr const wchar_t* kProjectUrl = L"https://github.com/AlanBacker/VRChat-DLSS5-Cam";
constexpr const wchar_t* kImagePatterns =
    L"*.png;*.jpg;*.jpeg;*.jpe;*.jfif;*.bmp;*.dib;*.tif;*.tiff;*.gif;*.webp;*.heic;*.heif;*.avif;*.jxr;*.wdp;*.hdp;*.ico;*.dds";

// Still images: the neural network is temporal, so a picture is run through it several times until the result
// settles. Opening a picture (or loading the runtime) starts a longer run, a slider change a shorter one.
constexpr int kImageConvergePasses = 32;
constexpr int kImageSettingsPasses = 24;
constexpr double kPerfLogInterval = 15.0;
constexpr UINT WM_COPYGLOBALDATA = 0x0049;

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

void NameCurrentThread(const wchar_t* name) {
    typedef HRESULT(WINAPI * PFN_SetThreadDescription)(HANDLE, PCWSTR);
    if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll"))
        if (auto fn = (PFN_SetThreadDescription)GetProcAddress(k32, "SetThreadDescription")) fn(GetCurrentThread(), name);
}
} // namespace

// ------------------------------------------------------------------------------------------

int App::Run(HINSTANCE hInstance, int nCmdShow) {
    if (!Init(hInstance, nCmdShow)) {
        Shutdown();
        return 1;
    }
    MSG msg{};
    while (!m_quit) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { m_quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_quit) break;
        if (m_minimized) {
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
    return (int)msg.wParam;
}

bool App::Init(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;
    ImGui_ImplWin32_EnableDpiAwareness();
    m_exeDir = GetExeDir();
    m_appDataDir = GetAppDataDir();
    m_settingsPath = JoinPath(m_appDataDir, L"settings.ini");
    Log::Init(JoinPath(m_appDataDir, L"log.txt"));
    Log::Info("VRChat DLSS5 Cam %s starting", APP_VERSION_STRING);
    Log::Info("Executable folder: %s", WideToUtf8(m_exeDir).c_str());

    Log::Info("Settings file: %s", WideToUtf8(m_settingsPath).c_str());
    m_settings.Load(m_settingsPath);
    m_settings.Clamp();
    I18n::SetLanguage(I18n::FromSetting(m_settings.language));
    Log::Info("Language: %s", I18n::LanguageName(I18n::Current()));

    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hrCo)) Log::Hr(LogLevel::Warn, "CoInitializeEx", hrCo);

    Log::Info("Creating main window");
    if (!CreateMainWindow(hInstance, nCmdShow)) return false;
    Log::Info("Main window created (DPI scale %.2f)", m_dpiScale);

    std::wstring err;
    Log::Info("Initialising Direct3D 12");
    if (!m_device.Init(m_hwnd, m_settings.debugLayer, err)) {
        Log::Error("Device init failed: %s", WideToUtf8(err).c_str());
        MessageBoxW(m_hwnd, (Utf8ToWide(TR(InitFailed)) + L"\n\n" + err).c_str(), L"VRChat DLSS5 Cam", MB_ICONERROR | MB_OK);
        return false;
    }
    m_deviceReady = true;
    const AdapterInfo& ai = m_device.Info();
    Log::Info("Adapter: %s (vendor 0x%04X, %llu MB), driver %s", WideToUtf8(ai.name).c_str(), ai.vendorId,
              (unsigned long long)(ai.dedicatedVideoMemory >> 20),
              WideToUtf8(ai.nvidiaDriverVersion.empty() ? ai.driverVersion : ai.nvidiaDriverVersion).c_str());

    Log::Info("Initialising render pipeline");
    if (!m_pipeline.Init(m_device, m_exeDir, m_appDataDir, err)) {
        Log::Error("Pipeline init failed: %s", WideToUtf8(err).c_str());
        MessageBoxW(m_hwnd, (Utf8ToWide(TR(InitFailed)) + L"\n\n" + err).c_str(), L"VRChat DLSS5 Cam", MB_ICONERROR | MB_OK);
        return false;
    }
    Log::Info("Initialising Spout receiver");
    if (!m_spout.Init(m_device)) {
        Log::Error("Spout receiver init failed");
        MessageBoxW(m_hwnd, (Utf8ToWide(TR(InitFailed)) + L"\n\nSpout").c_str(), L"VRChat DLSS5 Cam", MB_ICONERROR | MB_OK);
        return false;
    }
    m_spout.SetRequestedSender(m_settings.senderName);
    if (!m_capture.Init()) Log::Warn("Capture worker failed to start; photo capture is unavailable");

    Log::Info("Initialising UI");
    if (!InitImGui()) return false;
    RegisterHotkey();

    // Accept dropped pictures (also from a non-elevated Explorer when this process runs elevated).
    DragAcceptFiles(m_hwnd, TRUE);
    ChangeWindowMessageFilterEx(m_hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(m_hwnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);

    m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    PushSettings();
    RequestRuntimeLoad(false);
    if (m_settings.sourceMode == SourceImage && !m_settings.imagePath.empty()) {
        const std::wstring path = Utf8ToWide(m_settings.imagePath);
        if (FileExists(path)) {
            Command c; c.type = Command::LoadImage; c.path = path;
            PostCommand(std::move(c));
        } else {
            Log::Warn("Image from the previous session not found: %s", m_settings.imagePath.c_str());
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
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = m_settings.windowWidth, h = m_settings.windowHeight;
    bool restored = false;
    if (m_settings.windowX != -1 || m_settings.windowY != -1) {
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
    m_hwnd = CreateWindowExW(0, kWindowClass, L"VRChat DLSS5 Cam", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, hInstance, this);
    if (!m_hwnd) {
        Log::Error("CreateWindowExW failed: %s", LastErrorText().c_str());
        return false;
    }
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark))))
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));
    m_dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(m_hwnd);
    ShowWindow(m_hwnd, m_settings.windowMaximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(m_hwnd);
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
    StopWorker();
    if (m_deviceReady) { m_device.Ui().WaitIdle(); m_device.Proc().WaitIdle(); }
    if (m_imguiReady) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    if (m_hwnd) UnregisterHotKey(m_hwnd, kHotkeyId);
    if (m_deviceReady) {
        m_pipeline.Shutdown(m_device);
        m_spout.Shutdown(m_device.Proc());
        m_image.Release(m_device.Proc());
    }
    m_capture.Shutdown();
    if (m_deviceReady) { m_device.Shutdown(); m_deviceReady = false; }
    if (!m_settingsPath.empty()) m_settings.Save(m_settingsPath);
    Log::Info("Shutdown complete");
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

void App::PushSettings() {
    {
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        m_shared.settings = m_settings;
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

void App::WorkerLoadImage(GpuContext& gpu, const std::wstring& path) {
    std::string err;
    const std::string name = WideToUtf8(FileNameOf(path));
    if (m_image.Load(gpu, path, err)) {
        PostNotice(StrPrintf("%s: %s (%ux%u)", TR(ImageLoaded), name.c_str(), m_image.OriginalWidth(), m_image.OriginalHeight()), false);
        if (m_image.Width() != m_image.OriginalWidth() || m_image.Height() != m_image.OriginalHeight())
            PostNotice(StrPrintf("%s: %ux%u", TR(ImageDownscaled), m_image.Width(), m_image.Height()), false);
    } else {
        Log::Error("Image load failed for %s: %s", WideToUtf8(path).c_str(), err.c_str());
        PostNotice(StrPrintf("%s: %s (%s)", TR(ImageLoadFailed), name.c_str(), err.c_str()), true);
    }
}

void App::WorkerMain() {
    NameCurrentThread(L"VDC processing");
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);   // WIC decoding
    timeBeginPeriod(1);
    GpuContext& gpu = m_device.Proc();

    Settings settings;
    unsigned settingsGen = 0;
    int      activeMode = -1;
    int      passesLeft = 0;                 // image mode: passes still to run
    bool     imageChanged = false;           // image texture recreated since the last processed frame
    bool     imageCapturePending = false;
    Command  imageCapture;
    double   lastSenderScan = 0.0;
    double   lastTimelapse = NowSeconds();
    double   lastRun = 0.0;
    double   rateNext = 0.0;   // processing rate cap: start of the next processing slot
    double   lastPublish = 0.0;
    double   fpsWindowStart = NowSeconds();
    unsigned fpsWindowFrames = 0;
    double   processingFps = 0.0;

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
                settingsChanged = true;
            }
        }
        const bool imageMode = settings.sourceMode == SourceImage;
        const bool modeChanged = settings.sourceMode != activeMode;
        activeMode = settings.sourceMode;
        bool commandRan = false;
        for (Command& c : commands) {
            commandRan = true;
            switch (c.type) {
            case Command::LoadRuntime:
                WorkerLoadRuntime(gpu, c.path, c.announce);
                passesLeft = std::max(passesLeft, kImageConvergePasses);
                break;
            case Command::LoadImage:
                WorkerLoadImage(gpu, c.path);
                imageChanged = true;
                passesLeft = kImageConvergePasses;
                break;
            case Command::CaptureImage:
                imageCapturePending = true;
                imageCapture = c;
                break;
            }
        }
        if (settingsChanged && !modeChanged) passesLeft = std::max(passesLeft, kImageSettingsPasses);
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
        const bool run = !deviceLost && (fresh || changed || commandRan || settingsChanged || m_pipeline.NeedsFrame() || now - lastRun >= 0.5);
        if (run) {
            const double tBegin = NowSeconds();
            ID3D12GraphicsCommandList* cmd = gpu.BeginFrame();
            const double tRecord = NowSeconds();
            if (imageMode) { m_image.Upload(cmd, gpu); src = m_image.Frame(); imageChanged = false; }
            m_pipeline.Render(gpu, src, settings, cmd, fresh, changed);
            const double tSubmit = NowSeconds();
            const UINT64 fence = gpu.EndFrame();
            m_pipeline.AfterSubmit(gpu, fence);
            const double tUpdate = NowSeconds();
            m_pipeline.Update(gpu, m_capture);
            m_pipeline.PublishStatus(gpu);
            const double tEnd = NowSeconds();
            lastRun = tEnd;
            // A still picture gets one depth estimate, which may land after the passes ran out: converge again with it.
            if (imageMode) {
                const UINT64 inferences = m_pipeline.Status().depthInferences;
                if (inferences != imageDepthSeen) { imageDepthSeen = inferences; passesLeft = std::max(passesLeft, kImageSettingsPasses); }
            }

            const bool processed = fresh && src.Connected() && src.hasFrame;
            if (processed) {
                if (imageMode && passesLeft > 0) --passesLeft;
                ++fpsWindowFrames;
                const PipelineStatus& st = m_pipeline.Status();
                ++perf.frames;
                perf.receive += receiveMs;
                perf.wait += (tRecord - tBegin) * 1000.0;
                perf.record += (tSubmit - tRecord) * 1000.0;
                perf.submit += (tUpdate - tSubmit) * 1000.0;
                perf.update += (tEnd - tUpdate) * 1000.0;
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
        }

        // Source snapshot for the interface.
        if (run || now - lastPublish >= 0.25) {
            lastPublish = now;
            SourceInfo info;
            info.mode = settings.sourceMode;
            info.processingFps = processingFps;
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
                          imageMode ? "image passes" : "processing", perf.frames / (now - perf.logTime), m_spout.SenderFps(),
                          m_uiFpsShared.load(), m_uiGpuMsShared.load(), cpu, perf.receive / n, perf.wait / n, perf.record / n, perf.submit / n, perf.update / n,
                          g(GpuTimer::Frame), g(GpuTimer::Convert), g(GpuTimer::Guidance), g(GpuTimer::OpticalFlow), g(GpuTimer::Dlaa),
                          g(GpuTimer::Neural), g(GpuTimer::Composite), other, perf.depthRuns ? perf.depthMs / perf.depthRuns : 0.0, perf.depthRuns, perf.frames);
            }
            perf.Reset(now);
        }

        if (!run || !fresh) {
            // Idle: poll the live source about every millisecond (new Spout frames are picked up within ~1 ms),
            // more lazily when nothing is connected; commands and settings wake the thread immediately.
            const DWORD ms = deviceLost ? 50 : (!imageMode && m_spout.Connected()) ? 1 : 4;
            WaitForSingleObject(m_wake, ms);
        }
    }

    timeEndPeriod(1);
    if (SUCCEEDED(coHr)) CoUninitialize();
}

// --- interface thread ----------------------------------------------------------------------

void App::DrainNotices() {
    CaptureResult cr;
    while (m_capture.PollResult(cr)) {
        const std::wstring name = FileNameOf(cr.path);
        if (cr.ok) {
            m_lastCapture = WideToUtf8(name);
            m_lastCaptureOk = true;
            m_ui.Toast(StrPrintf("%s: %s (%.1f MB, %.0f ms)", TR(Saved), m_lastCapture.c_str(), cr.bytes / 1048576.0, cr.seconds * 1000.0));
            Log::Info("Saved %s (%llu bytes)", WideToUtf8(cr.path).c_str(), (unsigned long long)cr.bytes);
        } else {
            m_lastCapture = cr.error;
            m_lastCaptureOk = false;
            m_ui.Toast(StrPrintf("%s: %s", TR(CaptureFailed), cr.error.c_str()), true);
            Log::Error("Capture failed for %s: %s", WideToUtf8(cr.path).c_str(), cr.error.c_str());
        }
    }
    std::deque<Notice> notices;
    {
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        notices.swap(m_shared.notices);
    }
    for (const Notice& n : notices) m_ui.Toast(n.text, n.error);
}

void App::Frame() {
    if (m_inFrame || !m_deviceReady || !m_imguiReady) return;
    m_inFrame = true;
    const double frameStart = NowSeconds();

    if (m_device.DeviceRemoved()) {
        if (!m_deviceLostReported) {
            m_deviceLostReported = true;
            Log::Error("Graphics device removed");
            MessageBoxW(m_hwnd, Utf8ToWide(TR(DeviceRemoved)).c_str(), L"VRChat DLSS5 Cam", MB_ICONERROR | MB_OK);
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

    ID3D12GraphicsCommandList* cmd = m_device.BeginFrame();
    if (!cmd) { m_inFrame = false; return; }
    const DisplayView display = m_pipeline.AcquireDisplay(m_device.Ui());
    m_pipeline.StatusSnapshot(m_status);
    {
        std::lock_guard<std::mutex> lock(m_shared.mutex);
        m_source = m_shared.source;
        if (m_shared.sendersGeneration != m_sendersSeen) { m_sendersSeen = m_shared.sendersGeneration; m_senders = m_shared.senders; }
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
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
    info.nrRuntimePath = EffectiveRuntimePath();
    info.nrRuntimeExists = FileExists(info.nrRuntimePath);
    info.captureFolder = EffectiveCaptureFolder();
    info.hotkeyText = HotkeyText(m_settings);
    info.hasDisplay = display.valid;
    info.displayTexture = display.valid ? (ImTextureID)display.srv.ptr : (ImTextureID)0;
    info.displayWidth = display.width;
    info.displayHeight = display.height;
    info.appVersion = APP_VERSION_STRING;
    info.capturePending = m_capture.Pending() + m_status.capturesInFlight;
    info.lastCapture = m_lastCapture;
    info.lastCaptureOk = m_lastCaptureOk;

    ui::UiEvents ev;
    m_ui.Draw(m_settings, info, ev, m_fonts);
    HandleEvents(ev);
    ImGui::Render();

    m_device.TimerBegin(cmd, GpuTimer::Ui);
    ID3D12Resource* backBuffer = m_device.CurrentBackBuffer();
    Device::Barrier(cmd, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_device.CurrentRtv();
    const float clear[4] = { 0.055f, 0.06f, 0.075f, 1.0f };
    cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
    Device::Barrier(cmd, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_device.TimerEnd(cmd, GpuTimer::Ui);

    const UINT64 uiFence = m_device.EndFrame(m_settings.vsync);
    m_pipeline.ReleaseDisplay(uiFence);

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

    // Modal dialogs run their own message pump; open them only once the frame is fully submitted.
    if (m_pendingBrowseRuntime) { m_pendingBrowseRuntime = false; BrowseRuntime(); }
    if (m_pendingBrowseDepthModel) { m_pendingBrowseDepthModel = false; BrowseDepthModel(); }
    if (m_pendingBrowseFolder) { m_pendingBrowseFolder = false; BrowseFolder(); }
    if (m_pendingBrowseImage) { m_pendingBrowseImage = false; BrowseImage(); }

    // Without vsync the interface would otherwise spin at thousands of frames per second and take GPU time away
    // from the processing queue; ~300 fps is plenty for a preview.
    if (!m_settings.vsync) {
        while (NowSeconds() - frameStart < 0.003) Sleep(1);
    }
}

void App::HandleEvents(ui::UiEvents& ev) {
    if (ev.captureNow) CaptureNow();
    if (ev.browseRuntime) m_pendingBrowseRuntime = true;
    if (ev.browseDepthModel) m_pendingBrowseDepthModel = true;
    if (ev.openImage) m_pendingBrowseImage = true;
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
        m_settings = def;
        m_spout.SetRequestedSender(m_settings.senderName);
        m_pipeline.MarkNrDirty();
        m_pipeline.MarkDlaaDirty();
        RegisterHotkey();
        RequestRuntimeLoad(false);
        m_ui.Toast(TR(SettingsReset));
        ev.settingsChanged = true;
    }
    if (ev.sourceModeChanged) {
        Log::Info("Source: %s", m_settings.sourceMode == SourceImage ? "image file" : "Spout");
        ev.settingsChanged = true;
    }
    // The settings snapshot goes to the processing thread before the request flags so that a flag is never
    // consumed together with an older snapshot.
    if (ev.settingsChanged) {
        m_settings.Clamp();
        MarkSettingsDirty();
    }
    if (ev.hotkeyChanged) RegisterHotkey();
    if (ev.nrChanged) { m_pipeline.MarkNrDirty(); WakeWorker(); }
    if (ev.dlaaChanged) { m_pipeline.MarkDlaaDirty(); WakeWorker(); }
    if (ev.resetHistory) { m_pipeline.RequestReset(); WakeWorker(); m_ui.Toast(TR(HistoryReset)); }
    if (ev.senderChanged) { m_spout.SetRequestedSender(m_settings.senderName); WakeWorker(); }
    if (ev.refreshSenders) { m_refreshSenders = true; WakeWorker(); }
    if (ev.reloadRuntime) RequestRuntimeLoad(true);
}

void App::CaptureNow() {
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
    PostCommand(std::move(c));
}

void App::OpenImageFile(const std::wstring& path) {
    if (path.empty()) return;
    Log::Info("Opening image %s", WideToUtf8(path).c_str());
    m_settings.sourceMode = SourceImage;
    m_settings.imagePath = WideToUtf8(path);
    MarkSettingsDirty();
    Command c;
    c.type = Command::LoadImage;
    c.path = path;
    PostCommand(std::move(c));
}

void App::OnFileDropped(const std::wstring& path) {
    if (DirectoryExists(path)) return;
    if (LowerExtension(path) == L"dll") {
        // The runtime itself was dropped: use it.
        m_settings.nrDllPath = WideToUtf8(path);
        MarkSettingsDirty();
        RequestRuntimeLoad(true);
        return;
    }
    OpenImageFile(path);
}

void App::MarkSettingsDirty() {
    if (m_settingsDirtyTime < 0.0) m_settingsDirtyTime = NowSeconds();
    PushSettings();
}

void App::SaveSettings() {
    m_settingsDirtyTime = -1.0;
    if (!m_settings.Save(m_settingsPath)) Log::Warn("Failed to save settings to %s", WideToUtf8(m_settingsPath).c_str());
}

void App::SaveWindowPlacement() {
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (!m_hwnd || !GetWindowPlacement(m_hwnd, &wp)) return;
    m_settings.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    m_settings.windowX = wp.rcNormalPosition.left;
    m_settings.windowY = wp.rcNormalPosition.top;
    m_settings.windowWidth = std::max(400L, wp.rcNormalPosition.right - wp.rcNormalPosition.left);
    m_settings.windowHeight = std::max(300L, wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
}

std::wstring App::EffectiveRuntimePath() const {
    if (!m_settings.nrDllPath.empty()) return Utf8ToWide(m_settings.nrDllPath);
    return JoinPath(m_exeDir, L"nvngx_dlssnr.dll");
}

std::wstring App::EffectiveCaptureFolder(const Settings& s) {
    if (!s.captureFolder.empty()) return Utf8ToWide(s.captureFolder);
    return JoinPath(GetPicturesDir(), L"VRChat DLSS5 Cam");
}

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
    if (!m_settings.imagePath.empty()) {
        // Start in the folder of the current picture.
        const std::wstring prev = Utf8ToWide(m_settings.imagePath);
        const size_t slash = prev.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            ComPtr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(prev.substr(0, slash).c_str(), nullptr, IID_PPV_ARGS(&folder)))) dlg->SetFolder(folder.Get());
        }
    }
    if (FAILED(dlg->Show(m_hwnd))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return;
    PWSTR psz = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
        const std::wstring path = psz;
        CoTaskMemFree(psz);
        OpenImageFile(path);
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
            if (m_deviceReady) {
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
        wchar_t path[MAX_PATH * 4] = {};
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0 && DragQueryFileW(drop, 0, path, (UINT)(sizeof(path) / sizeof(path[0]))) > 0) {
            SetForegroundWindow(hwnd);
            OnFileDropped(path);
        }
        DragFinish(drop);
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
