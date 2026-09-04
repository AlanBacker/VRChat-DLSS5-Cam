// VRChat DLSS5 Cam - application shell: window, message loop, interface frame loop, processing thread, hotkey, dialogs.
#pragma once
#include <windows.h>
#include "core/Settings.h"
#include "core/Capture.h"
#include "core/SpoutReceiver.h"
#include "core/ImageSource.h"
#include "gfx/Device.h"
#include "gfx/Pipeline.h"
#include "ui/Fonts.h"
#include "ui/MainUI.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

// Two threads: the interface thread owns the window, ImGui and the present queue; the processing thread owns the
// Spout / image source, the pipeline and the processing queue. They exchange small snapshots under one mutex, the
// finished pictures travel through the pipeline's display buffers, so a slow neural pass never stalls the interface.
class App {
public:
    int Run(HINSTANCE hInstance, int nCmdShow);

private:
    // Published by the processing thread for the interface.
    struct SourceInfo {
        int          mode = SourceSpout;
        bool         connected = false;          // Spout sender receiving / image decoded
        bool         hasFrame = false;           // something has been processed into the display
        std::string  senderName;
        double       senderFps = 0.0;
        std::string  format;                     // source texture format label
        bool         isHdr = false;              // floating-point (linear HDR) source texture
        double       processingFps = 0.0;        // processed source frames per second
        std::wstring imagePath;
        std::string  imageName;                  // file name (UTF-8)
        UINT         imageWidth = 0, imageHeight = 0;           // as processed
        UINT         imageOrigWidth = 0, imageOrigHeight = 0;   // as stored in the file
        bool         imageLoaded = false;
        bool         imageConverging = false;    // still-image passes are still running
    };
    struct Notice { std::string text; bool error = false; };
    struct Command {
        enum Type { LoadRuntime, LoadImage, CaptureImage };
        Type         type = LoadRuntime;
        std::wstring path;                       // runtime DLL / image file / capture folder
        bool         announce = false;           // LoadRuntime: toast on success and on a missing file
        bool         keepAlpha = true;           // CaptureImage
        bool         saveOriginal = false;       // CaptureImage
    };
    struct Shared {
        std::mutex               mutex;
        Settings                 settings;       // interface -> processing (copied when the generation changes)
        unsigned                 settingsGeneration = 0;
        std::deque<Command>      commands;       // interface -> processing
        SourceInfo               source;         // processing -> interface
        std::vector<std::string> senders;        // processing -> interface
        unsigned                 sendersGeneration = 0;
        std::deque<Notice>       notices;        // processing -> interface (toasts)
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool Init(HINSTANCE hInstance, int nCmdShow);
    bool CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
    bool InitImGui();
    void Shutdown();

    // Interface thread.
    void Frame();
    void DrainNotices();
    void HandleEvents(ui::UiEvents& ev);
    void CaptureNow();
    void RegisterHotkey();
    void RequestRuntimeLoad(bool announce);
    void OpenImageFile(const std::wstring& path);
    void OnFileDropped(const std::wstring& path);
    void PostCommand(Command&& c);
    void PushSettings();
    void MarkSettingsDirty();
    void SaveSettings();
    void SaveWindowPlacement();
    void ApplyDpi(float scale);
    std::wstring EffectiveRuntimePath() const;
    static std::wstring EffectiveCaptureFolder(const Settings& s);
    std::wstring EffectiveCaptureFolder() const { return EffectiveCaptureFolder(m_settings); }
    void BrowseRuntime();
    void BrowseDepthModel();
    void BrowseFolder();
    void BrowseImage();
    void OpenPath(const std::wstring& path);

    // Processing thread.
    void StartWorker();
    void StopWorker();
    void WakeWorker();
    void WorkerMain();
    void WorkerLoadRuntime(GpuContext& gpu, const std::wstring& path, bool announce);
    void WorkerLoadImage(GpuContext& gpu, const std::wstring& path);
    void PostNotice(const std::string& text, bool error);

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
    bool          m_pendingBrowseDepthModel = false;
    bool          m_pendingBrowseFolder = false;
    bool          m_pendingBrowseImage = false;
    float         m_dpiScale = 1.0f;

    std::wstring  m_exeDir;
    std::wstring  m_appDataDir;
    std::wstring  m_settingsPath;

    Settings      m_settings;
    Device        m_device;
    Pipeline      m_pipeline;
    SpoutReceiver m_spout;                 // processing thread (SetRequestedSender is thread-safe)
    ImageSource   m_image;                 // processing thread
    Capture       m_capture;
    ui::Fonts     m_fonts;
    ui::MainUI    m_ui;

    Shared            m_shared;
    std::thread       m_worker;
    HANDLE            m_wake = nullptr;    // auto-reset event: new commands / settings / requests
    std::atomic<bool> m_workerStop{false};
    std::atomic<bool> m_refreshSenders{false};
    std::atomic<double> m_uiFpsShared{0.0};
    std::atomic<double> m_uiGpuMsShared{0.0};

    // Interface-thread copies of the shared state.
    PipelineStatus m_status;
    SourceInfo     m_source;
    std::vector<std::string> m_senders;
    unsigned       m_sendersSeen = 0;
    double         m_settingsDirtyTime = -1.0;
    double         m_lastFrameTime = 0.0;
    double         m_fps = 0.0;
    double         m_cpuMs = 0.0;
    std::string    m_lastCapture;
    bool           m_lastCaptureOk = true;
};

} // namespace vdc
