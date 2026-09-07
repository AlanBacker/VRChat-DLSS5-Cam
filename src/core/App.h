// VRChat DLSS5 Cam - application shell: window, message loop, interface frame loop, processing thread, hotkey, dialogs.
#pragma once
#include <windows.h>
#include "core/Settings.h"
#include "core/Capture.h"
#include "core/SpoutReceiver.h"
#include "core/ImageSource.h"
#include "core/VideoSource.h"
#include "core/VideoWriter.h"
#include "gfx/Device.h"
#include "gfx/Pipeline.h"
#include "ui/Fonts.h"
#include "ui/MainUI.h"
#include <atomic>
#include <deque>
#include <map>
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
        bool         imageConverging = false;    // still-image / video-preview passes are still running
        bool         videoLoaded = false;
        std::wstring videoPath;
        std::string  videoName;
        std::string  videoCodec;
        UINT         videoWidth = 0, videoHeight = 0;
        double       videoFps = 0.0;
        UINT64       videoFrames = 0;            // estimate from the file
        double       videoDurationSeconds = 0.0;
        bool         videoHasAudio = false;
        bool         videoHardwareDecode = false;
        bool         videoProcessing = false;    // the file is being run through the pipeline
        bool         videoFinishing = false;     // the encoder writes the tail of the file
        UINT64       videoFrame = 0;             // frames delivered to the output
        double       videoElapsed = 0.0;
        std::string  videoOutName;
        bool         batchRunning = false;
        int          batchIndex = 0, batchCount = 0, batchDone = 0, batchFailed = 0;
        std::string  batchItemName;
    };
    struct Notice { std::string text; bool error = false; };
    struct Command {
        enum Type { LoadRuntime, LoadImage, CaptureImage, LoadVideo, ProcessVideo, CancelVideo, BatchStart, BatchCancel };
        Type         type = LoadRuntime;
        std::wstring path;                       // runtime DLL / image or video file / capture folder
        bool         announce = false;           // LoadRuntime: toast on success and on a missing file
        bool         keepAlpha = true;           // CaptureImage, BatchStart
        bool         saveOriginal = false;       // CaptureImage, BatchStart
        std::vector<std::wstring> paths;         // BatchStart
    };
    // A video file being run through the pipeline (processing thread).
    struct VideoRun {
        bool         active = false;
        bool         cancel = false;
        bool         pngSequence = false;
        bool         frameHeld = false;          // the last frame produced no output yet: it is run again
        int          heldRetries = 0;
        UINT64       handed = 0;                 // frames handed to the pipeline
        UINT64       delivered = 0;              // frames that reached the output
        UINT64       total = 0;                  // estimate from the file
        double       startTime = 0.0;
        std::wstring folder;                     // capture folder
        std::wstring stem;
        std::wstring outPath;                    // MP4 file or PNG folder (once known)
        int          codec = 0;
        UINT32       bitrateKbps = 40000;
        bool         withAudio = false;
        bool         writerPrepared = false;
        std::map<UINT64, std::pair<LONGLONG, LONGLONG>> times;   // frame index -> (pts, duration)
        std::string  error;
    };
    // A queue of images and videos processed one after the other (processing thread).
    struct BatchRun {
        std::vector<std::wstring> files;
        size_t       index = 0;
        bool         active = false;
        bool         itemStarted = false;
        bool         itemIsVideo = false;
        int          done = 0, failed = 0;
        bool         cancel = false;
        std::wstring folder;
        bool         keepAlpha = true, saveOriginal = false;
        std::wstring restoreImage, restoreVideo; // the user's own files, reopened afterwards
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
    void OpenVideoFile(const std::wstring& path);
    void OnFileDropped(const std::wstring& path);
    void AddBatchFiles(const std::vector<std::wstring>& paths);
    void StartBatch();
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
    void BrowseVideo();
    void BrowseBatchFiles();
    void BrowseBatchFolder();
    void OpenPath(const std::wstring& path);

    // Processing thread.
    void StartWorker();
    void StopWorker();
    void WakeWorker();
    void WorkerMain();
    void WorkerLoadRuntime(GpuContext& gpu, const std::wstring& path, bool announce);
    void WorkerLoadImage(GpuContext& gpu, const std::wstring& path, bool announce);
    void WorkerLoadVideo(GpuContext& gpu, const std::wstring& path, bool hardwareDecode, bool announce);
    bool WorkerStartVideo(const Settings& settings, VideoRun& run, const std::wstring& folder, std::string& error);
    bool WorkerEndVideo(GpuContext& gpu, VideoRun& run, FrameSink& sink, bool completed);
    void WorkerVideoFrame(VideoRun& run, std::vector<uint8_t>&& rgba, UINT w, UINT h, UINT pitch, UINT64 index);
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
    bool          m_pendingBrowseVideo = false;
    bool          m_pendingBrowseBatchFiles = false;
    bool          m_pendingBrowseBatchFolder = false;
    float         m_dpiScale = 1.0f;

    std::wstring  m_exeDir;
    std::wstring  m_appDataDir;
    std::wstring  m_settingsPath;

    Settings      m_settings;
    Device        m_device;
    Pipeline      m_pipeline;
    SpoutReceiver m_spout;                 // processing thread (SetRequestedSender is thread-safe)
    ImageSource   m_image;                 // processing thread
    VideoSource   m_video;                 // processing thread
    VideoWriter   m_videoWriter;           // processing thread
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
    std::vector<std::wstring> m_batchFiles;   // interface thread: the batch queue as shown
    std::vector<std::string>  m_batchNames;
};

} // namespace vdc
