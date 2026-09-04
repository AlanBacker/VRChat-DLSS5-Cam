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
#include <wrl/client.h>
#include <algorithm>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace vdc {

using Microsoft::WRL::ComPtr;

namespace {
constexpr int      kHotkeyId = 1;
constexpr UINT_PTR kSizeTimer = 1;
constexpr const wchar_t* kWindowClass = L"VRChatDLSS5CamWindow";
constexpr const wchar_t* kProjectUrl = L"https://github.com/AlanBacker/VRChat-DLSS5-Cam";

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
            // Nothing to draw: sleep until a message arrives (or 100 ms) and keep settings persisted.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
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
    LoadRuntime(false);
    Log::Info("Startup complete");

    m_lastFrameTime = NowSeconds();
    m_lastTimelapse = m_lastFrameTime;
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
    ii.CommandQueue = m_device.Queue();
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
    if (m_deviceReady) m_device.WaitIdle();
    if (m_imguiReady) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    if (m_hwnd) UnregisterHotKey(m_hwnd, kHotkeyId);
    if (m_deviceReady) {
        m_pipeline.Shutdown(m_device);
        m_spout.Shutdown(m_device);
    }
    m_capture.Shutdown();
    if (m_deviceReady) { m_device.Shutdown(); m_deviceReady = false; }
    if (!m_settingsPath.empty()) m_settings.Save(m_settingsPath);
    Log::Info("Shutdown complete");
    Log::Shutdown();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    UnregisterClassW(kWindowClass, m_hInstance);
    CoUninitialize();
}

// ------------------------------------------------------------------------------------------

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

    m_pipeline.Update(m_device, m_capture);
    CaptureResult cr;
    while (m_capture.PollResult(cr)) {
        const std::wstring name = cr.path.substr(cr.path.find_last_of(L"\\/") + 1);
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

    bool changed = false, fresh = false;
    m_spout.Receive(m_device, changed, fresh);
    if (frameStart - m_sendersTime > 1.0) {
        m_sendersTime = frameStart;
        m_senders = m_spout.EnumerateSenders();
    }

    ID3D12GraphicsCommandList* cmd = m_device.BeginFrame();
    if (!cmd) { m_inFrame = false; return; }
    m_pipeline.Render(m_device, m_spout, m_settings, cmd, fresh, changed);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ui::UiFrameInfo info;
    const PipelineStatus& st = m_pipeline.Status();
    info.status = &st;
    info.adapter = &m_device.Info();
    info.device = &m_device;
    info.fps = m_fps;
    info.cpuMs = m_cpuMs;
    info.senders = &m_senders;
    info.senderName = m_spout.SenderName();
    info.senderFps = m_spout.SenderFps();
    info.sourceConnected = m_spout.Connected();
    switch (m_spout.Format()) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: info.sourceFormat = "RGBA16F"; break;
    case DXGI_FORMAT_R10G10B10A2_UNORM: info.sourceFormat = "RGB10A2"; break;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: info.sourceFormat = "BGRA8"; break;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: info.sourceFormat = "RGBA8"; break;
    default: info.sourceFormat = StrPrintf("DXGI %u", (unsigned)m_spout.Format()); break;
    }
    {
        const DXGI_FORMAT vf = m_spout.ViewFormat();
        info.sourceIsHdr = info.sourceConnected &&
            (vf == DXGI_FORMAT_R16G16B16A16_FLOAT || vf == DXGI_FORMAT_R32G32B32A32_FLOAT || vf == DXGI_FORMAT_R11G11B10_FLOAT);
    }
    info.nrRuntimePath = EffectiveRuntimePath();
    info.nrRuntimeExists = FileExists(info.nrRuntimePath);
    info.captureFolder = EffectiveCaptureFolder();
    info.hotkeyText = HotkeyText(m_settings);
    info.hasDisplay = m_pipeline.HasDisplay();
    info.displayTexture = (ImTextureID)m_pipeline.DisplaySrv().ptr;
    info.displayWidth = m_pipeline.DisplayWidth();
    info.displayHeight = m_pipeline.DisplayHeight();
    info.appVersion = APP_VERSION_STRING;
    info.capturePending = m_capture.Pending() + st.capturesInFlight;
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

    m_device.EndFrame(m_settings.vsync);
    m_pipeline.AfterPresent(m_device);

    const double now = NowSeconds();
    m_cpuMs = m_cpuMs * 0.9 + (now - frameStart) * 1000.0 * 0.1;
    const double dt = std::max(now - m_lastFrameTime, 1e-4);
    m_lastFrameTime = now;
    const double inst = 1.0 / dt;
    m_fps = (m_fps <= 0.0) ? inst : m_fps * 0.92 + inst * 0.08;

    if (m_settings.timelapseSeconds > 0 && now - m_lastTimelapse >= (double)m_settings.timelapseSeconds) {
        m_lastTimelapse = now;
        if (m_spout.HasFrame() && m_pipeline.HasDisplay()) CaptureNow();
    }
    if (m_settingsDirtyTime >= 0.0 && now - m_settingsDirtyTime > 1.0) SaveSettings();
    m_inFrame = false;

    // Modal dialogs run their own message pump; open them only once the frame is fully submitted.
    if (m_pendingBrowseRuntime) { m_pendingBrowseRuntime = false; BrowseRuntime(); }
    if (m_pendingBrowseFolder) { m_pendingBrowseFolder = false; BrowseFolder(); }
}

void App::HandleEvents(ui::UiEvents& ev) {
    if (ev.captureNow) CaptureNow();
    if (ev.browseRuntime) m_pendingBrowseRuntime = true;
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
        LoadRuntime(false);
        m_ui.Toast(TR(SettingsReset));
        ev.settingsChanged = true;
    }
    if (ev.hotkeyChanged) RegisterHotkey();
    if (ev.nrChanged) m_pipeline.MarkNrDirty();
    if (ev.dlaaChanged) m_pipeline.MarkDlaaDirty();
    if (ev.resetHistory) { m_pipeline.RequestReset(); m_ui.Toast(TR(HistoryReset)); }
    if (ev.senderChanged) m_spout.SetRequestedSender(m_settings.senderName);
    if (ev.refreshSenders) m_sendersTime = 0.0;
    if (ev.reloadRuntime) LoadRuntime(true);
    if (ev.settingsChanged) {
        m_settings.Clamp();
        if (m_settingsDirtyTime < 0.0) m_settingsDirtyTime = NowSeconds();
    }
}

void App::CaptureNow() {
    if (!m_spout.HasFrame() || !m_pipeline.HasDisplay()) {
        m_ui.Toast(TR(CaptureNoFrame), true);
        return;
    }
    m_pipeline.RequestCapture(EffectiveCaptureFolder(), m_settings.keepAlpha, m_settings.saveOriginal);
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

void App::LoadRuntime(bool announce) {
    const std::wstring path = EffectiveRuntimePath();
    if (!FileExists(path)) {
        m_pipeline.UnloadNrRuntime(m_device);
        Log::Warn("DLSS 5 runtime not found at %s", WideToUtf8(path).c_str());
        if (announce) m_ui.Toast(TR(RuntimeMissing), true);
        return;
    }
    std::string err;
    if (m_pipeline.LoadNrRuntime(m_device, path, err)) {
        Log::Info("DLSS 5 runtime loaded from %s", WideToUtf8(path).c_str());
        if (announce) m_ui.Toast(TR(RuntimeLoadedToast));
    } else {
        Log::Error("DLSS 5 runtime load failed: %s", err.c_str());
        m_ui.Toast(StrPrintf("%s: %s", TR(RuntimeLoadFailed), err.c_str()), true);
    }
    m_pipeline.MarkNrDirty();
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

std::wstring App::EffectiveCaptureFolder() const {
    if (!m_settings.captureFolder.empty()) return Utf8ToWide(m_settings.captureFolder);
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
        if (m_settingsDirtyTime < 0.0) m_settingsDirtyTime = NowSeconds();
        LoadRuntime(true);
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
        if (m_settingsDirtyTime < 0.0) m_settingsDirtyTime = NowSeconds();
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
