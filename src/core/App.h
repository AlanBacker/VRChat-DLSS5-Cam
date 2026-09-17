// VRChat DLSS5 Cam - application shell: window, message loop, interface frame loop, processing thread, hotkey, dialogs.
#pragma once
#include <windows.h>
#include "core/Settings.h"
#include "core/Splash.h"
#include "core/Updater.h"
#include "core/Capture.h"
#include "core/MediaLibrary.h"
#include "core/SpoutReceiver.h"
#include "core/ImageSource.h"
#include "core/VideoSource.h"
#include "core/AnimatedImage.h"
#include "core/VideoWriter.h"
#include "gfx/Device.h"
#include "gfx/Pipeline.h"
#include "gfx/ThumbnailAtlas.h"
#include "ui/Fonts.h"
#include "ui/MainUI.h"
#include "core/McpServer.h"
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace vdc {

// Options on the command line, for automated runs (see docs/COMMAND_LINE.md).
struct CommandLine {
    bool         headless = false;        // no swap chain: draw offscreen, never show a message box, exit by itself
    UINT         width = 0, height = 0;   // --window WxH
    std::wstring open;                    // --open <picture or video>
    std::vector<std::wstring> add;        // --add <file or folder> (library)
    double       seek = -1.0;             // --seek <seconds>
    bool         play = false;            // --play
    double       in = -1.0, out = -1.0;   // --in / --out <seconds>
    int          language = -1;           // --lang en|zh|ja|ko
    std::vector<std::pair<double, std::wstring>> screenshots;   // --screenshot <seconds> <png>
    bool         process = false;         // --process [folder]: run the opened file / the library, then exit when done
    std::wstring processDir;
    std::wstring splashDump;              // --splash-dump <bmp> (development: a frame of the start-up card)
    double       exitAfter = -1.0;        // --exit-after <seconds>
    bool         update = false;          // --update: install a newer version from the chosen channel if there is one
    int          edition = 0;             // --edition geforce|amd: fetch that edition of this program (1 GeForce, 2 Radeon) and swap to it
    std::vector<std::pair<std::string, std::string>> sets;      // --set key=value (settings)
    std::wstring dataDir;                 // --data-dir <folder>
    bool         afterDeviceLoss = false; // --after-device-loss: started by an instance that lost the graphics device
    double       loseDevice = -1.0;       // --lose-device <seconds> (development: report the device as lost after this long)
    bool         mcp = false;             // --mcp: the stdio bridge for MCP clients (no window; relays to the running program)
    int          mcpPort = 0;             // --mcp-port <port>: run the MCP server on this port for this session (the bridge starts the program so)
    std::string  mcpUrl;                  // --mcp-url <http://host:port/mcp>: the bridge talks to that server (another computer) and starts nothing
    std::string  mcpKey;                  // --mcp-key <key>: the key the bridge presents
    std::string  error;                   // the first unknown option

    static CommandLine Parse();
};

// Two threads: the interface thread owns the window, ImGui and the present queue; the processing thread owns the
// Spout / image / video sources, the pipeline and the processing queue. They exchange small snapshots under one
// mutex, the finished pictures travel through the pipeline's display buffers, so a slow neural pass never stalls
// the interface.
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
        UINT32       videoBitrateKbps = 0;       // average video bitrate of the file (0 = unknown)
        int          videoAnimation = 0;         // AnimFormat of an animated image source (0 = a video file)
        int          videoLoopCount = 0;         // animation: 0 = forever
        bool         videoLossless = false;      // animation: no lossy compression
        bool         videoHasAlpha = false;      // animation: the frames carry transparency
        bool         videoProcessing = false;    // the file is being run through the pipeline
        bool         videoFinishing = false;     // the encoder writes the tail of the file
        UINT64       videoFrame = 0;             // frames delivered to the output
        double       videoElapsed = 0.0;
        std::string  videoOutName;
        double       videoPosition = 0.0;        // preview position (seconds)
        bool         videoPlaying = false;       // the preview plays at the file's frame rate
        bool         videoSeeking = false;       // a seek is being decoded
        double       videoIn = 0.0, videoOut = 0.0;   // processing range (out <= 0: to the end)
        float        videoPreviewLuma = 0.0f;
        bool         batchRunning = false;
        int          batchIndex = 0, batchCount = 0, batchDone = 0, batchFailed = 0;
        unsigned     batchItemId = 0;
        std::string  batchItemName;
        double       costSecPerFrame = 0.0;      // processing time per frame at costPixels pixels (0 = unknown yet)
        double       costPixels = 0.0;
        bool         costMeasured = false;       // from a file run (frames over wall time) rather than the preview passes
        unsigned     loadFailures = 0;           // picture/video loads that failed (the MCP open waiter watches it)
        std::string  loadError;                  // the reason of the last failed load
        unsigned     videoRuns = 0;              // video runs that ended (the MCP capture waiter watches it)
        bool         videoLastOk = false;        // how the last video run ended
        std::string  videoLastOut, videoLastError;
        UINT64       videoLastFrames = 0;
        double       videoLastSeconds = 0.0;
    };
    struct Notice { std::string text; bool error = false; };
    struct BatchItem {
        unsigned     id = 0;
        std::wstring path;
        bool         isVideo = false;
        double       inSec = 0.0, outSec = 0.0;
        std::shared_ptr<Settings> own;           // the item's own effect values (a copy), or null
        bool         ownAll = false;             // own holds the whole settings the item runs with (an MCP job), not only the effects
        SourceTransform transform;               // the item's orientation and crop
    };
    struct BatchEvent {
        unsigned    id = 0;
        int         state = 0;                   // LibraryItem::State
        std::string outName;
        std::string error;
    };
    struct Command {
        enum Type { LoadRuntime, LoadImage, CaptureImage, LoadVideo, ProcessVideo, CancelVideo, BatchStart, BatchCancel,
                    VideoSeek, VideoPlay, VideoPause, VideoStep, VideoSetRange, CloseMedia, SetTransform };
        Type         type = LoadRuntime;
        std::wstring path;                       // runtime DLL / image or video file / capture folder / SetTransform: the file it is for
        SourceTransform transform;               // LoadImage, LoadVideo, SetTransform: orientation and crop of the file
        bool         announce = false;           // LoadRuntime: toast on success and on a missing file
        bool         keepAlpha = true;           // CaptureImage, BatchStart
        bool         saveOriginal = false;       // CaptureImage, BatchStart
        double       seconds = 0.0;              // VideoSeek, VideoSetRange (in)
        double       seconds2 = 0.0;             // VideoSetRange (out), ProcessVideo (unused)
        int          step = 0;                   // VideoStep: frames forward (+) or back (-)
        bool         video = false;              // CloseMedia: the video (else the picture)
        std::vector<BatchItem> items;            // BatchStart
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
        UINT64       total = 0;                  // estimate from the file (or the range)
        double       startTime = 0.0;
        double       fromSec = 0.0, toSec = 0.0; // range
        double       resumeSec = 0.0;            // preview position to return to afterwards
        std::wstring folder;                     // capture folder
        std::wstring stem;
        std::wstring nameTemplate;               // file-name template (Capture::Template)
        std::wstring outPath;                    // MP4 file or PNG folder (once known: with the first frame's size)
        int          codec = 0;
        UINT32       bitrateKbps = 40000;
        bool         matchSource = false;        // the output follows the file: its bitrate scales with the picture
        bool         withAudio = false;
        AnimFormat   animFormat = AnimFormat::None;   // the output is an animated image (GIF, APNG, WebP)
        int          animLoops = 0;              // 0 = forever
        int          animQuality = 90;           // WebP
        bool         animLossless = false;       // WebP
        bool         keepAlpha = false;          // the transparency of the frames goes into the animation
        bool         writerPrepared = false;
        std::map<UINT64, std::pair<LONGLONG, LONGLONG>> times;   // frame index -> (pts, duration)
        std::string  error;
    };
    // The still/playing preview of the opened video (processing thread).
    struct VideoPreview {
        bool   playing = false;                  // wanted: play at the file's frame rate
        bool   running = false;                  // the frame sequence runs for playback
        double inSec = 0.0, outSec = 0.0;        // processing range (outSec <= 0: to the end)
        bool   seekPending = false;
        double seekTo = 0.0;
        int    stepFrames = 0;
        bool   rangeChanged = false;
        double nextFrameWall = 0.0;              // when the next played frame is due
        bool   ended = false;                    // playback reached the end of the range
    };
    // A queue of images and videos processed one after the other (processing thread).
    struct BatchRun {
        std::vector<BatchItem> items;
        size_t       index = 0;
        bool         active = false;
        bool         itemStarted = false;
        bool         itemIsVideo = false;
        int          done = 0, failed = 0;
        bool         cancel = false;
        std::wstring folder;
        bool         keepAlpha = true, saveOriginal = false;
        std::wstring restoreImage, restoreVideo; // the user's own files, reopened afterwards
        SourceTransform restoreImageXform, restoreVideoXform;
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
        std::deque<BatchEvent>   batchEvents;    // processing -> interface (library item states)
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool Init(HINSTANCE hInstance, int nCmdShow);
    bool CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
    bool InitImGui();
    void Shutdown();
    void ApplyCommandLineSettings();
    void RunCommandLineActions();
    void FatalMessage(const std::wstring& text);

    // Interface thread.
    void Frame();
    void DrainNotices();
    void HandleEvents(ui::UiEvents& ev);
    void CaptureNow();
    void RegisterHotkey();
    void RequestRuntimeLoad(bool announce);
    void OpenImageFile(const std::wstring& path);
    void OpenVideoFile(const std::wstring& path);
    void CloseMediaFile();                 // the opened picture or video: processing stops, the preview empties
    void SetFullscreen(bool on);           // borderless window over the monitor, the interface reduced to the picture
    void OnFileDropped(const std::wstring& path);
    void OnFilesDropped(const std::vector<std::wstring>& paths);
    void PostCommand(Command&& c);
    void PushSettings();
    void MarkSettingsDirty();
    void SaveSettings();
    void SaveWindowPlacement();
    void ApplyDpi(float scale);
    // The runtime builds in the order they are tried on this adapter: the bundled ones under runtimes\, the file
    // next to the executable and runtimes\other\ for another vendor. A build that fails is skipped for the rest of
    // the session, the next one takes over and the choice is kept in the settings (CheckRuntimeFallback).
    struct RuntimeCandidate { std::wstring path; const char* build; };   // build: the settings token
    std::vector<RuntimeCandidate> RuntimeCandidates() const;
    std::wstring UserRuntimePath() const;        // the runtime file the user set, when it is there (else empty)
    std::wstring EffectiveRuntimePath() const;
    const char* RuntimeBuildName(const std::wstring& path) const;   // translated name of a candidate, null for another file
    void CheckRuntimeFallback();           // interface thread, after the status snapshot
    void RestartRuntimeChoice();           // forget the failures and the kept build: the next load starts over
    int  EffectiveRoute() const;           // the neural route: a property of the edition (EditionRoute)
    void PollPortSetup();                  // DLSS-NR-on-AMD installer progress (Radeon edition), told once per state
    void RecordPortVersion();              // an installation from before the record: taken as the latest when its installer file is the latest one
    void LogPortState(bool tail);          // its files next to the executable; tail: the last lines of its own log too
    void RelaunchSelf();                   // start the executable again and close this instance
    void RelaunchAfterDeviceLoss();        // after Shutdown: keep the log, start the executable again with --after-device-loss
    int  RunAgainAndWait(const std::string& note);   // start the executable again with the same command line, wait, return its code
    static std::wstring EffectiveCaptureFolder(const Settings& s);
    std::wstring EffectiveCaptureFolder() const { return EffectiveCaptureFolder(m_settings); }
    void BrowseRuntime();
    void BrowseDepthModel();
    void BrowseFolder();
    void BrowseImage();
    void BrowseVideo();
    void BrowseLibraryFiles();
    void BrowseLibraryFolder();
    void OpenPath(const std::wstring& path);

    // Media library (interface thread).
    void AddLibraryFiles(const std::vector<std::wstring>& paths, bool announce);
    void RemoveLibraryItem(unsigned id);
    void RemoveSelectedLibraryItems();
    void RestoreLibrary(const std::vector<ui::LibrarySnapshotItem>& wanted);   // undo/redo of library changes
    void LocateLibraryItem(unsigned id);         // Explorer with the file selected
    // The user's presets: a file of named effect values in the data folder.
    void LoadPresets();
    void SavePresets() const;
    std::wstring DocsUrl() const;                 // the README in the interface's language
    const LibraryItem* OverrideItem() const;
    const LibraryItem* ShownItem() const;
    SourceTransform LibraryTransform(const std::wstring& path, bool video) const;
    void SyncTransform(const ui::UiEvents& ev);
    void ReadSystemTheme();
    void UpdateTitleBar();                 // dark or light title bar to match the interface theme    // the library item whose own effect values apply to the shown file
    void ClearLibrary();
    void PreviewLibraryItem(unsigned id);
    void StartLibraryProcessing(bool selectedOnly);
    void PollScanner();
    void UpdateStoryboard();
    void RequestHoverThumb(double seconds);
    LibraryItem* FindItem(unsigned id);
    void RequestScreenshot(const std::wstring& path);

    // The MCP server (AppMcp.cpp): the assistant's calls run on the interface thread between the interface's draw
    // and the handling of its events, so they take the widgets' paths.
    struct McpWaiter {
        std::shared_ptr<McpCall> call;
        int          kind = 0;               // WaitKind
        std::wstring path;                   // the file waited for
        bool         video = false;
        double       deadline = 0.0;
        unsigned     captureBase = 0;        // m_captureResultsSeen when the wait began
        unsigned     loadBase = 0;           // m_source.loadFailures when the wait began
        unsigned     runBase = 0;            // m_source.videoRuns when the wait began
        bool         started = false;        // the run was seen going
        double       settleTime = -1.0;      // batch: when the run was first seen finished (the last picture may still be encoding)
    };
    struct McpPreview {
        std::shared_ptr<McpCall> call;
        int          view = 0;               // 0 = the output picture (display texture), 1 = the window
        int          maxEdge = 1024;
        std::wstring saveTo;
        bool         wide = false;
        int          compareMode = 0;
        float        wipe = 0.5f;
    };
    void SyncMcp();                                                        // starts or stops the server after the settings
    void StopMcp();                                                        // at shutdown: the server, and every waiting call fails
    void FillMcpInfo(ui::UiFrameInfo& info) const;
    std::string McpConfigText() const;
    void DrainMcp(const ui::UiFrameInfo& info, ui::UiEvents& ev);
    void DrainMcpMinimized();
    void McpExecute(const std::shared_ptr<McpCall>& call, const ui::UiFrameInfo& info, ui::UiEvents& ev);
    void McpAddWaiter(const std::shared_ptr<McpCall>& call, int kind, double timeout, const std::wstring& path, bool video);
    void McpCheckWaiters();
    void McpSetVideoRange(double in, double out);
    bool McpIdle() const;
    void McpBeginPreview(ID3D12GraphicsCommandList* cmd, const DisplayView& display);
    void McpFinishPreview();
    Json McpStatusJson() const;
    Json McpSourceJson() const;
    Json McpLibraryJson() const;

    // Jobs (AppMcpJobs.cpp): files sent by clients (bots), run one after the other whenever the user's own work
    // leaves the program idle. Their batch items and probes carry ids from kMcpJobIdBase up, so the library never
    // sees them.
    static constexpr unsigned kMcpJobIdBase = 0x40000000u;
    struct McpJob {
        enum State { Queued = 0, Running, Done, Failed, Cancelled };
        std::string  id;                     // short and random: the client's handle
        std::string  owner;                  // the key's name
        std::string  label, clientRef;       // the client's own notes, echoed back
        State        state = Queued;
        std::wstring folder;                 // <mcp>\jobs\<id>: the input under in\, the results beside it
        std::wstring inputPath;
        std::string  inputName;
        bool         isVideo = false;
        double       inSec = 0.0, outSec = 0.0;
        std::shared_ptr<Settings> settings;  // the whole settings the job runs with
        std::string  preset;
        Json         overrides;
        long long    submitted = 0, started = 0, finished = 0;   // Unix seconds
        std::vector<std::string> outputs;    // file names in the folder
        std::string  error;
        double       progress = 0.0;
        double       runSeconds = 0.0;       // Done: how long the run took
        int          probe = 0;              // 0 = pending, 1 = read, 2 = failed
        UINT         width = 0, height = 0;
        double       duration = 0.0, fps = 0.0;
        unsigned     itemId = 0;             // the batch item's and the probe's id
        bool         fetching = false;       // the input is being downloaded
        bool         restarted = false;      // was running when the program last closed: queued again
        bool         finishing = false;      // the batch ended; the last picture may still be in the encoder
        bool         cancelAsked = false;
        bool         seen = false;           // the processing thread took the job (its Processing event arrived)
        double       startedMono = 0.0;      // NowSeconds when the batch was posted
    };
    struct McpJobWaiter {
        std::shared_ptr<McpCall> call;
        std::string  jobId;
        double       deadline = 0.0;
        bool         withInline = false;     // jobs result: the picture comes along
        int          maxEdge = 1024;
        double       inlineRetryUntil = 0.0;   // a fresh output may still be held by a scanner: tried again for a moment
    };
    struct McpUpload {
        std::string  id, name, owner;
        std::wstring path;
        uint64_t     bytes = 0;
        long long    created = 0;            // Unix seconds
    };
    struct McpFetchResult { std::string jobId; bool ok = false; std::string error; uint64_t bytes = 0; };
    void McpJobsInit();                                                    // the keys and the jobs from disk
    void McpJobsTick();                                                    // the scheduler, once per frame (also minimized)
    void McpJobsShutdown();
    bool McpExecuteJobs(const std::shared_ptr<McpCall>& call, ui::UiEvents& ev);   // submit, upload, jobs, _upload, _download; false = not one of them
    void McpJobEvent(const BatchEvent& e);
    void McpJobBatchEnded();
    void McpJobProbed(const ScanResult& r);
    void McpJobCaptured(const std::wstring& path, unsigned tag, bool ok, const std::string& error);
    void McpJobStart(McpJob& job);
    void McpJobFinish(McpJob& job, int state, const std::string& error);
    McpJob* McpFindJob(const std::string& id);
    const McpJob* McpFindJob(const std::string& id) const;
    void McpJobOrder(std::vector<const McpJob*>& queued) const;            // the queued jobs in the order they will run
    double McpJobEstimate(const McpJob& job) const;                        // seconds of processing
    void McpQueuePosition(const McpJob& job, int& position, double& etaSeconds) const;
    Json McpJobJson(const McpJob& job, const std::string& host) const;
    Json McpJobsJson(const McpCaller& caller) const;
    Json McpQueueJson() const;                                             // the jobs block of get_status
    Json McpStatusReduced() const;                                         // what the jobs role sees
    void McpSaveJobs();
    void McpLoadJobs();
    void McpSweep();                                                       // old jobs, uploads and part files
    std::wstring McpRoot() const;                                          // where the jobs live
    void McpLoadKeys();
    void McpSaveKeys();
    void McpPushAccess();                                                  // the keys and the switches to the server
    std::string McpAddKey(const std::string& name, int role);              // returns the secret
    bool McpRemoveKey(const std::string& name);
    void McpAllowFirewall();                                               // asks Windows to let the port through (elevation prompt)

    // Processing thread.
    void StartWorker();
    void StopWorker();
    void WakeWorker();
    void WorkerMain();
    void WorkerLoadRuntime(GpuContext& gpu, const std::wstring& path, bool announce);
    void WorkerLoadImage(GpuContext& gpu, const std::wstring& path, bool announce);
    void WorkerLoadVideo(GpuContext& gpu, const std::wstring& path, bool hardwareDecode, bool announce);
    bool WorkerStartVideo(const Settings& settings, VideoRun& run, const std::wstring& folder, double fromSec, double toSec,
                          std::string& error);
    bool WorkerEndVideo(GpuContext& gpu, VideoRun& run, FrameSink& sink, bool completed);
    void WorkerVideoFrame(VideoRun& run, std::vector<uint8_t>&& rgba, UINT w, UINT h, UINT pitch, UINT64 index);
    void WorkerPreviewCommand(const Command& c);
    // Playback / seeking / stepping of the video preview. True when the picture (or the playback state) changed and
    // the still passes should start over; `fresh` is set when a played frame is pending, `reset` when the temporal
    // history should be dropped (a jump).
    bool WorkerPreviewStep(bool& fresh, bool& reset);
    void PostNotice(const std::string& text, bool error);
    void PostBatchEvent(unsigned id, int state, const std::string& outName, const std::string& error);

    HINSTANCE     m_hInstance = nullptr;
    HWND          m_hwnd = nullptr;
    bool          m_quit = false;
    bool          m_minimized = false;
    bool          m_fullscreen = false;
    WINDOWPLACEMENT m_fullscreenPlacement{};   // the window as it was before going fullscreen
    bool          m_sizing = false;
    bool          m_inFrame = false;
    bool          m_deviceReady = false;
    bool          m_imguiReady = false;
    bool          m_systemLight = false;   // Windows app colours are light (read from the registry, refreshed on WM_SETTINGCHANGE)
    int           m_titleDark = -1;        // the title bar colour last handed to DWM (-1: not yet)
    bool          m_deviceLostReported = false;
    bool          m_relaunchAfterLoss = false;   // the device was lost in an interactive session: start again after Shutdown
    double        m_deviceLostTime = 0.0;
    bool          m_fontsDirty = true;
    bool          m_pendingResize = false;
    UINT          m_pendingWidth = 0, m_pendingHeight = 0;
    bool          m_pendingBrowseRuntime = false;
    std::vector<std::wstring> m_runtimeFailed;   // candidates that failed in this session
    std::wstring  m_runtimeRequested;      // the file of the last load request
    double        m_runtimeBlackSince = -1.0;    // when the output check first reported black for that request
    bool          m_runtimeExhausted = false;    // every candidate failed
    bool          m_pendingBrowseDepthModel = false;
    bool          m_pendingBrowseFolder = false;
    bool          m_pendingBrowseImage = false;
    bool          m_pendingBrowseVideo = false;
    bool          m_pendingBrowseLibraryFiles = false;
    bool          m_pendingBrowseLibraryFolder = false;
    float         m_dpiScale = 1.0f;

    std::wstring  m_exeDir;
    std::wstring  m_appDataDir;
    std::wstring  m_settingsPath;
    CommandLine   m_cli;
    bool          m_headless = false;
    int           m_exitCode = 0;
    bool          m_ranAgain = false;      // Init handed the run to a second instance; m_exitCode is its code
    double        m_startTime = 0.0;
    size_t        m_nextScreenshot = 0;
    bool          m_cliActionsDone = false;
    bool          m_cliVideoActionsDone = false;
    bool          m_cliProcessStarted = false;
    double        m_cliProcessStartTime = 0.0;
    unsigned      m_cliCaptureBaseline = 0;
    unsigned      m_captureResultsSeen = 0;
    unsigned      m_batchFailures = 0;
    std::wstring  m_pendingScreenshot;     // path of the screenshot to take with the next frame
    UINT64        m_screenshotFence = 0;
    std::wstring  m_screenshotPath;

    Settings      m_settings;
    Device        m_device;
    Pipeline      m_pipeline;
    SpoutReceiver m_spout;                 // processing thread (SetRequestedSender is thread-safe)
    ImageSource   m_image;                 // processing thread
    VideoSource   m_video;                 // processing thread
    VideoWriter   m_videoWriter;           // processing thread
    AnimWriter    m_animWriter;            // processing thread: GIF / APNG / WebP output
    VideoPreview  m_preview;               // processing thread
    Capture       m_capture;
    ui::Fonts     m_fonts;
    ui::MainUI    m_ui;
    Updater       m_updater;               // looks for and installs a newer version
    bool          m_startCheckDone = false; // the automatic update check of this start ran (it waits for the setup guide)
    PortSetup     m_portSetup;             // Radeon edition: fetches and starts the DLSS-NR-on-AMD installer on request
    PortSetup::Status m_portStatus;        // its state as last polled (the interface reads it)
    unsigned      m_portGenSeen = 0;
    bool          m_portRestartHint = false;   // the installer ran: DLSS-NR-on-AMD loads with the next start
    double        m_portRestartAt = -1.0;      // the automatic restart after its installer (NowSeconds), -1 = none
    bool          m_portWeightsExist = false;  // its weights file lies next to the executable (looked at now and then)
    double        m_portWeightsTime = -1.0;
    Splash        m_splash;                // the start-up card
    bool          m_mainShown = false;     // the main window has been shown (after its first frame)
    int           m_nCmdShow = SW_SHOWNORMAL;
    unsigned      m_frameCount = 0;
    unsigned      m_updateGenSeen = 0;     // the updater state last announced
    ThumbnailAtlas m_atlas;                // interface thread
    LibraryScanner m_scanner;

    Shared            m_shared;
    std::thread       m_worker;
    HANDLE            m_wake = nullptr;    // auto-reset event: new commands / settings / requests
    std::atomic<bool> m_workerStop{false};
    std::atomic<bool> m_refreshSenders{false};
    std::atomic<double> m_uiFpsShared{0.0};
    std::atomic<double> m_uiGpuMsShared{0.0};
    std::atomic<unsigned> m_videoFailures{0};
    std::string       m_workerLastError;   // processing thread: reason of the last failed load
    unsigned          m_workerLoadFailures = 0;   // processing thread: failed picture/video loads
    unsigned          m_workerVideoRuns = 0;      // processing thread: video runs that ended
    bool              m_workerVideoLastOk = false;
    UINT64            m_workerVideoLastFrames = 0;
    double            m_workerVideoLastSeconds = 0.0;
    std::string       m_workerLastOut;     // processing thread: name of the last video output

    // Interface-thread copies of the shared state.
    PipelineStatus m_status;
    SourceInfo     m_source;
    std::vector<ui::UserPreset> m_presets;
    std::wstring   m_presetsPath;
    SourceTransform m_sentTransform;              // orientation last sent for the shown file (SyncTransform)
    std::wstring   m_sentTransformPath;
    bool           m_sentTransformVideo = false;
    std::vector<std::string> m_senders;
    unsigned       m_sendersSeen = 0;
    double         m_settingsDirtyTime = -1.0;
    double         m_lastFrameTime = 0.0;
    double         m_fps = 0.0;
    double         m_cpuMs = 0.0;
    std::string    m_lastCapture;
    bool           m_lastCaptureOk = true;
    double         m_pngSecPerMegapixel = 0.12;   // PNG encoding time, refined from each saved picture

    // Media library and the video storyboard (interface thread).
    std::vector<LibraryItem> m_library;
    unsigned       m_nextItemId = 1;
    unsigned       m_storyGeneration = 0;
    std::wstring   m_storyPath;
    double         m_storyDuration = 0.0;
    std::vector<int>    m_storyCells;
    std::vector<double> m_storyTimes;
    std::vector<bool>   m_storyReady;
    int            m_hoverCell = -1;
    double         m_hoverCellTime = -1.0;   // time of the picture in the hover cell (-1: none)
    double         m_hoverRequested = -1.0;
    double         m_hoverRequestTime = 0.0;
    bool           m_libraryBatchRunning = false;
    std::wstring   m_overridePath;                // file whose own effect values are merged into the pushed settings

    McpServer      m_mcp;
    std::mutex     m_mcpMutex;
    std::deque<std::shared_ptr<McpCall>> m_mcpQueue;   // server threads -> interface thread
    std::vector<McpWaiter>  m_mcpWaiters;
    std::vector<McpPreview> m_mcpPreviews;        // asked, not yet recorded
    McpPreview     m_mcpShot;                     // recorded with the last frame, read back at the next
    int            m_mcpRunningPort = 0;
    int            m_mcpSessionPort = 0;          // --mcp-port: serve for this session whatever the setting says
    double         m_mcpRangeIn = 0.0, m_mcpRangeOut = 0.0, m_mcpRangeTime = -1.0;   // the range just posted (the snapshot follows later)
    double         m_mcpRetryTime = -1.0;
    std::vector<McpKey> m_mcpKeys;
    std::wstring   m_mcpKeysPath;
    bool           m_mcpKeysDirty = false;
    double         m_mcpKeysSaveTime = -1.0;
    std::vector<McpJob> m_mcpJobs;
    std::vector<McpUpload> m_mcpUploads;
    std::vector<McpJobWaiter> m_mcpJobWaiters;
    std::string    m_mcpJobRunning;               // the job whose batch runs (or finishes)
    bool           m_mcpJobsDirty = false;
    double         m_mcpSweepTime = -1.0;
    unsigned       m_mcpNextItemId = kMcpJobIdBase;
    std::map<std::string, long long> m_mcpOwnerLastStart;   // fairness between keys
    double         m_mcpSecPerMp = 0.02;                     // processing seconds per megapixel, learned from finished jobs
    std::mutex     m_mcpFetchMutex;
    std::vector<McpFetchResult> m_mcpFetchResults;           // download threads -> interface thread
    std::atomic<bool> m_mcpFetchCancel{false};
    std::atomic<int>  m_mcpFetchLive{0};
    bool           m_mcpAccessPushed = false;
    std::string    m_mcpAccessSig;              // what SetAccess was last given: pushed again when it changes
    int            m_mcpRunningBind = -1;
};

} // namespace vdc
