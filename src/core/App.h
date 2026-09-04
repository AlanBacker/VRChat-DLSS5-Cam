// VRChat DLSS5 Cam - application shell: window, message loop, frame loop, hotkey, dialogs.
#pragma once
#include <windows.h>
#include "core/Settings.h"
#include "core/Capture.h"
#include "core/SpoutReceiver.h"
#include "gfx/Device.h"
#include "gfx/Pipeline.h"
#include "ui/Fonts.h"
#include "ui/MainUI.h"
#include <string>
#include <vector>

namespace vdc {

class App {
public:
    int Run(HINSTANCE hInstance, int nCmdShow);

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool Init(HINSTANCE hInstance, int nCmdShow);
    bool CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
    bool InitImGui();
    void Shutdown();
    void Frame();
    void HandleEvents(ui::UiEvents& ev);
    void CaptureNow();
    void RegisterHotkey();
    void LoadRuntime(bool announce);
    void SaveSettings();
    void SaveWindowPlacement();
    void ApplyDpi(float scale);
    std::wstring EffectiveRuntimePath() const;
    std::wstring EffectiveCaptureFolder() const;
    void BrowseRuntime();
    void BrowseFolder();
    void OpenPath(const std::wstring& path);

    HINSTANCE     m_hInstance = nullptr;
    HWND          m_hwnd = nullptr;
    bool          m_quit = false;
    bool          m_minimized = false;
    bool          m_sizing = false;
    bool          m_inFrame = false;
    bool          m_deviceReady = false;
    bool          m_imguiReady = false;
    bool          m_deviceLostReported = false;
    bool          m_fontsDirty = true;
    bool          m_pendingResize = false;
    UINT          m_pendingWidth = 0, m_pendingHeight = 0;
    bool          m_pendingBrowseRuntime = false;
    bool          m_pendingBrowseFolder = false;
    float         m_dpiScale = 1.0f;

    std::wstring  m_exeDir;
    std::wstring  m_appDataDir;
    std::wstring  m_settingsPath;

    Settings      m_settings;
    Device        m_device;
    Pipeline      m_pipeline;
    SpoutReceiver m_spout;
    Capture       m_capture;
    ui::Fonts     m_fonts;
    ui::MainUI    m_ui;

    std::vector<std::string> m_senders;
    double        m_sendersTime = 0.0;
    double        m_settingsDirtyTime = -1.0;
    double        m_lastFrameTime = 0.0;
    double        m_fps = 0.0;
    double        m_cpuMs = 0.0;
    double        m_lastTimelapse = 0.0;
    std::string   m_lastCapture;
    bool          m_lastCaptureOk = true;
};

} // namespace vdc
