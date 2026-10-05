// VRChat DLSS5 Cam - main window layout: top bar, preview with the video controls and the media library, sidebar,
// status bar, log window, toasts.
#pragma once
#include "core/Settings.h"
#include "core/MediaLibrary.h"
#include "core/Updater.h"
#include "gfx/Pipeline.h"
#include "gfx/ThumbnailAtlas.h"
#include "ui/Fonts.h"
#include "core/Log.h"
#include "imgui.h"
#include <string>
#include <vector>

namespace vdc::ui {

enum class Icon;
struct JumpTarget;

struct McpKeyView {
    std::string name;
    int         role = 1;        // McpRole
    double      lastAge = -1.0;  // seconds since its last call, -1 = never
    long long   calls = 0;
};

struct UserPreset {
    std::string name;
    std::string text;
};

struct UiFrameInfo {
    const PipelineStatus* status = nullptr;
    const AdapterInfo*    adapter = nullptr;
    bool                  driverKnown = false;      // the graphics device is up and its driver version was read
    bool                  driverOutdated = false;   // NVIDIA driver older than the oldest known to run the DLSS 5 runtime
    std::string           driverInstalled;          // "536.99" (NVIDIA numbering)
    std::string           driverRequired;           // "616.56"
    bool                  driverDialog = false;     // driverOutdated and this is a session with somebody to tell (not a --process run)
    std::string           driverDownloadUrl;        // the page "Download driver" opens (NVIDIA, in the interface's language)
    double                fps = 0.0;               // interface thread
    double                cpuMs = 0.0;             // interface thread, per frame
    double                uiGpuMs = 0.0;           // present-queue time of the last interface frame
    double                processingFps = 0.0;     // processed source frames per second (processing thread)
    int                   sourceMode = SourceSpout;
    const std::vector<std::string>* senders = nullptr;
    std::string           senderName;
    double                senderFps = 0.0;
    std::string           sourceFormat;
    bool                  sourceConnected = false;  // sender receiving / image decoded
    bool                  sourceIsHdr = false;      // floating-point (linear HDR) Spout texture
    std::wstring          imagePath;
    std::string           imageName;                // file name of the opened picture (UTF-8)
    UINT                  imageWidth = 0, imageHeight = 0;           // as processed
    UINT                  imageOrigWidth = 0, imageOrigHeight = 0;   // as stored in the file
    bool                  imageLoaded = false;
    bool                  imageConverging = false;  // still-image / video-preview passes still running
    bool                  videoLoaded = false;
    std::wstring          videoPath;
    std::string           videoName;                // file name of the opened video (UTF-8)
    std::string           videoCodec;
    UINT                  videoWidth = 0, videoHeight = 0;
    double                videoFps = 0.0;
    UINT64                videoFrames = 0;          // estimate from the file (or the range while processing)
    double                videoDurationSeconds = 0.0;
    bool                  videoHasAudio = false;
    bool                  videoHardwareDecode = false;
    UINT32                videoBitrateKbps = 0;     // average video bitrate of the file (0 = unknown)
    int                   videoAnimation = 0;       // AnimFormat of an animated image source (0 = a video file)
    int                   videoLoopCount = 0;       // animation: 0 = forever
    bool                  videoLossless = false;    // animation: no lossy compression
    bool                  videoHasAlpha = false;    // animation: the frames carry transparency
    bool                  videoProcessing = false;  // the file is being run through the pipeline
    bool                  videoFinishing = false;
    UINT64                videoFrame = 0;           // frames delivered to the output
    double                videoElapsed = 0.0;
    std::string           videoOutName;
    double                videoPosition = 0.0;      // preview position (seconds)
    bool                  videoPlaying = false;
    bool                  videoSeeking = false;
    double                videoIn = 0.0, videoOut = 0.0;   // processing range (out <= 0: to the end)
    float                 videoPreviewLuma = 0.0f;
    bool                  batchRunning = false;
    int                   batchIndex = 0, batchCount = 0, batchDone = 0, batchFailed = 0;
    unsigned              batchItemId = 0;
    std::string           batchItemName;
    double                costSecPerFrame = 0.0;    // processing time per frame at costPixels pixels (0 = unknown yet)
    double                costPixels = 0.0;
    bool                  costMeasured = false;     // from a file run rather than the preview passes
    double                pngSecPerMegapixel = 0.12;
    std::vector<LibraryItem>* library = nullptr;    // the interface toggles `selected`
    const ThumbnailAtlas* atlas = nullptr;          // thumbnails of the library and the seek bar
    const std::vector<int>*    storyCells = nullptr;   // seek-bar pictures of the opened video
    const std::vector<double>* storyTimes = nullptr;
    const std::vector<bool>*   storyReady = nullptr;
    int                   hoverCell = -1;           // exact frame under the cursor (-1: none)
    double                hoverCellTime = -1.0;
    std::wstring          nrRuntimePath;      // effective path
    bool                  nrRuntimeExists = false;
    bool                  nrSetPathMissing = false;   // a runtime path is set but its file is not there (a bundled build serves)
    const char*           nrRuntimeBuild = nullptr;   // translated name of the loaded build (bundled, or the file next to the executable)
    bool                  nrRuntimeExhausted = false; // every runtime build failed on this adapter
    bool                  fsrDllExists = false;       // amd_fidelityfx_dx12.dll next to the executable (FSR host route)
    const PortSetup::Status* portSetup = nullptr;     // the DLSS-NR-on-AMD installer (Radeon edition)
    bool                  portRestartHint = false;    // the installer ran: DLSS-NR-on-AMD loads with the next start
    bool                  portWeightsExist = false;   // its weights file lies next to the executable (installed)
    bool                  portConsent = false;        // the user agreed to fetching and running its installer
    int                   portRestartIn = -1;         // seconds until the automatic restart after its installer, -1 = none
    std::string           portInstalledTag;           // the release its installer came from, empty = unknown
    std::wstring          captureFolder;      // effective folder
    std::string           hotkeyText;
    ImTextureID           displayTexture = 0;
    UINT                  displayWidth = 0, displayHeight = 0;
    bool                  hasDisplay = false;
    bool                  displayWide = false;      // the display holds the original and the output side by side (wipe)
    unsigned              shownItem = 0;            // the library item in the preview (0: none)
    bool                  fullscreen = false;       // the window covers the screen: only the picture is drawn
    bool                  windowShown = false;      // the main window is on screen (the start-up card has closed)
    std::string           appVersion;
    bool                  prerelease = false;       // the build is marked as a pre-release
    bool                  previousAbnormal = false;  // the session before this one ended without an orderly shutdown
    bool                  previousCrashed = false;   // ... and left a record in crash.txt
    std::string           previousDetails;           // that record's first lines
    // The MCP server (the clients' way in): its state for the sidebar section.
    bool                  mcpRunning = false;
    bool                  mcpSession = false;         // started for this session by --mcp-port or a bridge (the switch does not stop it)
    std::string           mcpError;                   // why it is not running
    std::string           mcpUrl;
    unsigned              mcpCalls = 0;
    std::string           mcpLastTool;
    double                mcpLastAge = -1.0;          // seconds since the last call, -1 = none
    std::string           mcpConfig;                  // the JSON block for an MCP client's configuration file
    bool                  mcpNetwork = false;         // reachable from the local network (mcpBind = 1)
    std::vector<std::string> mcpAddresses;            // the URLs other computers reach it at
    std::vector<McpKeyView>  mcpKeys;                 // the access keys, in file order
    int                   mcpJobsQueued = 0, mcpJobsRunning = 0, mcpJobsDone = 0;
    std::string           mcpJob;                     // "owner: file" of the AI job running now, empty when none
    const std::vector<UserPreset>* presets = nullptr;   // the user's presets, in file order
    size_t                capturePending = 0;
    std::string           lastCapture;
    bool                  lastCaptureOk = true;
    std::string           lastSaved;                // file name of the last picture or video saved (or that failed)
    bool                  lastSavedOk = true;
    bool                  lastSavedKnown = false;
    bool                  systemLight = false;      // Windows uses light app colours (theme "System" follows it)
    // Updates (App copies the updater's state; the values follow Updater::State in order).
    int                   updateState = 0;          // UpdateState
    std::string           updateVersion, updateDate, updateNotes, updateError;
    bool                  updateNotesEnglish = false;   // the interface is not in English and the release has no notes in its language
    bool                  updatePrerelease = false;
    bool                  updateEdition = false;    // the release found is the other edition of this program
    bool                  updateDowngrade = false;  // the release found is older: the stable channel's way back from a pre-release
    bool                  updateHasAsset = false;   // the release carries the win64 zip
    bool                  updateWritable = true;    // the program folder takes new files
    bool                  updateShow = false;       // open the update popup (set for one frame)
    bool                  updaterBusy = false;      // a check, a download or a measurement runs
    // GitHub access (App copies the updater's measurement of the mirror sites).
    struct MirrorRow { std::string url, error; double seconds = 0.0; bool ok = false; };
    std::vector<MirrorRow> mirrors;                 // fastest first; empty until measured
    bool                  mirrorProbing = false;
    std::string           mirrorInUse;              // the site the last check or download went through; empty = GitHub itself
    bool                  mirrorPrompt = false;     // open the "no site answered" popup (set for one frame)
    float                 updateProgress = 0.0f;
    double                updateDownloadedMb = 0.0, updateTotalMb = 0.0;
    // The Ask AI panel (App's AskPanel): docked at the window's right edge while open.
    bool                  askOpen = false;          // the panel is open (or opening); false while it slides away
    bool                  askWebShown = false;      // the page is on screen over the panel's card
    bool                  askPressed = false;       // the page took the keyboard since the last frame (a click in it)
    int                   askState = 0;             // 0 nothing yet, 1 loading, 2 ready, 3 failed
};

enum UpdateState { UpIdle, UpChecking, UpUpToDate, UpAvailable, UpDownloading, UpExtracting, UpRestarting, UpFailed };

// What the undo history keeps of a library item (MainUI::TrackUndo): the file, its own effect values, its range.
struct LibrarySnapshotItem {
    std::wstring path;
    bool         useOwn = false;
    std::string  own;               // the item's own effect values as text (empty: none)
    double       inSec = 0.0, outSec = 0.0;
    SourceTransform transform;      // orientation and crop
};

// A user preset: a name and the effect values it holds (Settings::ParameterText() lines of the effect fields).

struct UiEvents {
    bool captureNow = false;
    bool closeMedia = false;         // close the opened picture or video
    bool fullscreenToggle = false;   // enter or leave the fullscreen view (F11, Esc, the corner button)
    bool libraryRestore = false;     // undo/redo: bring the library to libraryRestoreItems
    std::vector<LibrarySnapshotItem> libraryRestoreItems;
    bool updateCheckNow = false;     // look for a new version now
    bool updateStart = false;        // download and install the version found
    bool updateOpenPage = false;     // the release page in the browser
    bool updateCancel = false;       // stop a running download
    bool mirrorProbe = false;        // measure the built-in mirror sites now
    bool openBooth = false;          // the BOOTH page in the browser
    bool guideClosed = false;        // the setup guide was closed (finished or skipped)
    bool browseRuntime = false;
    bool browseDepthModel = false;
    bool reloadDepth = false;
    bool browseFolder = false;
    bool openCaptureFolder = false;
    bool openLogFile = false;
    bool openSettingsFolder = false;
    bool openProjectPage = false;
    bool openIssueReport = false;    // the issue form on GitHub, prefilled (openIssueCrash: with the crash record)
    bool openIssueCrash = false;
    bool openDriverDownload = false; // NVIDIA's driver download page in the browser
    bool openLicenses = false;
    bool languageChanged = false;
    bool settingsChanged = false;
    bool hotkeyChanged = false;
    bool nrChanged = false;
    bool dlaaChanged = false;
    bool resetHistory = false;
    bool senderChanged = false;
    bool refreshSenders = false;
    bool resetDefaults = false;
    bool reloadRuntime = false;
    bool portInstall = false;        // download and start the DLSS-NR-on-AMD installer
    bool portOpenPage = false;       // open its release page
    bool portOpenLicense = false;    // open its licence in the browser
    bool portRestartCancel = false;  // keep running: no automatic restart after its installer
    bool editionSwitch = false;      // fetch the other edition of this program (GeForce <-> Radeon) and swap to it
    bool restartApp = false;
    bool openImage = false;          // browse for a picture
    bool openVideo = false;          // browse for a video
    bool cancelVideo = false;        // stop the running video
    bool batchCancel = false;
    bool sourceModeChanged = false;  // s.sourceMode switched between Spout, image and video
    // Video controls
    bool   videoSeek = false;
    double videoSeekTo = 0.0;
    bool   videoPlayToggle = false;
    int    videoStep = 0;            // frames forward (+) or back (-)
    bool   videoSetIn = false;
    bool   videoSetOut = false;
    bool   videoClearRange = false;
    bool   videoHover = false;       // the cursor is on the seek bar at videoHoverTime
    double videoHoverTime = 0.0;
    // Media library
    unsigned libraryPreview = 0;     // item to show in the preview
    unsigned libraryRemove = 0;      // item to drop
    bool libraryClear = false;
    bool libraryProcessAll = false;
    bool libraryProcessSelected = false;
    bool libraryAddFiles = false;
    bool libraryAddFolder = false;
    unsigned libraryLocate = 0;      // show this item's file in Explorer
    bool revealLastSaved = false;    // show the last saved file in Explorer
    bool libraryDeleteSelected = false;   // drop the selected items from the library
    bool itemParamsChanged = false;  // an item's own effect values were edited
    bool cropEditing = false;        // the crop of the shown file is being drawn: show the whole turned picture
    SourceTransform cropPreview;     // its orientation while that lasts
    bool openDocs = false;           // the documentation in the browser (in the interface's language)
    bool askToggle = false;          // the Ask AI button: open or close the panel
    bool askRetry = false;           // Try again in the panel after a failed start
    bool askDocs = false;            // Open documentation beside it
    std::string jumpDocs;            // a place an answer pointed at could not be shown: its documentation page (under the language's folder)
    bool mcpOpenPage = false;       // the server's information page in the browser
    bool mcpOpenDocs = false;        // the documentation site's MCP page in the browser
    bool mcpOpenJobs = false;        // the jobs folder in Explorer
    bool mcpFirewall = false;        // let the port through the Windows firewall (asks for elevation)
    bool mcpKeyAdd = false, mcpKeyRemove = false, mcpKeyCopy = false;   // on the key named mcpKeyName
    std::string mcpKeyName;
    int         mcpKeyRole = 1;
    // Presets: saved under a name (a new one, or an existing one to replace), renamed, removed.
    bool        presetSave = false;
    std::string presetName;          // presetSave / presetRename: the name to store under
    int         presetRename = -1;   // index of the preset to rename to presetName
    int         presetDelete = -1;   // index of the preset to remove
};

class MainUI {
public:
    void Draw(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void Toast(const std::string& text, bool error = false, bool success = false);
    // A client (App's MCP tools) shares the undo history: an undo or redo like Ctrl+Z / Ctrl+Y, a jump to an
    // entry of the history list, and the list itself (oldest first, the current entry's index).
    void ExternalUndo(Settings& s, const UiFrameInfo& info, UiEvents& ev, bool redo) { ApplyUndo(s, info, ev, redo); }
    void ExternalGoToHistory(Settings& s, const UiFrameInfo& info, UiEvents& ev, int index) { GoToHistory(s, info, ev, index); }
    std::vector<std::string> HistoryLabels(int& current) const;
    bool WantsFrames() const;   // something moves or waits: keep drawing at full rate
    // Where the Ask AI page goes (window client pixels): true once the panel has landed and nothing covers it.
    bool AskPanelRect(ImVec2& min, ImVec2& max) const;
    struct FloatRect { ImVec2 min, max; float rounding; };
    void FloatingRects(std::vector<FloatRect>& out) const;   // this frame's tooltips and popups (for the page's holes)
    void AskButtonRect(ImVec2& min, ImVec2& max) const { min = m_askBtnMin; max = m_askBtnMax; }   // the top bar's Ask AI button
    bool FullscreenSwitching() const { return m_fsPending || m_fsRise; }   // the dip to black and the rise after it
    void ToggleFullscreen() { RequestFullscreen(); }   // F11 pressed in the Ask AI page
    void TestGuide(int page) { m_guideOpen = true; m_guideStartPage = page; }   // test runs: the setup guide at that page (0-based)
    void TestSpotlight() { m_spotAt = ImGui::GetTime() + 0.3; m_spotUntil = m_spotAt + 1.4; }   // test runs: the guide's pointers flash
    // A place an answer of the Ask AI panel pointed at (an id of JumpTargets.h): shown and ringed on the next frame.
    void JumpTo(const std::string& id) { m_jumpAsk = id; }

private:
    struct ToastItem { std::string text; double time; bool error; bool success; };
    struct LockLayout {              // the lock banner's layout (LayOutLockBanner)
        float cancelW = 0.0f;        // the Cancel button's width (0: none)
        bool  buttonBelow = false;   // the button sits under the text: the two don't fit side by side
        float wrapW = 0.0f;          // the text's wrap width (0: one line)
        float textH = 0.0f;          // the height of the text's lines, padded like a frame
        float height = 0.0f;         // the banner's height
    };

    void DrawTopBar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void DrawSidebar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    float DrawSidebarSearch(Settings& s, UiEvents& ev, float rowW);   // the search field and the Advanced switch, above the scrolled settings
    static LockLayout LayOutLockBanner(const std::string& text, bool cancellable, float width);   // in a banner that wide
    void DrawLockBanner(const UiFrameInfo& info, UiEvents& ev, const LockLayout& lock, float t, float space, float height, float width, float barW);   // over the locked settings
    void DrawPreview(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void DrawPicture(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size);
    void DrawWelcome(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& origin, const ImVec2& region);   // nothing open yet
    void DrawTransport(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size);
    void DrawFullscreen(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);   // the picture alone
    void FullscreenButton(const UiFrameInfo& info, UiEvents& ev, const ImVec2& origin, const ImVec2& region);
    void VideoKeys(const UiFrameInfo& info, UiEvents& ev);                                        // keyboard control of a video
    void DrawLibrary(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size);
    void DrawStatusBar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void DrawLogWindow(Settings& s, UiEvents& ev, const Fonts& fonts);
    void DrawToasts(const Fonts& fonts);

    // Sidebar blocks.
    void BlockSource(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void BlockNeural(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void BlockSave(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void BlockView(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void BlockGuidance(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void BlockDlaa(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void BlockNgxRuntime(Settings& s, const UiFrameInfo& info, UiEvents& ev);   // the NVIDIA runtime rows of the DLSS 5 section
    void BlockFsrHost(Settings& s, const UiFrameInfo& info, UiEvents& ev);      // the FSR host rows (Radeon)
    void PortActions(const UiFrameInfo& info, UiEvents& ev, bool portLoaded, bool card);   // DLSS-NR-on-AMD: installer state, restart, buttons
    void BlockInternals(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void BlockMcp(Settings& s, const UiFrameInfo& info, UiEvents& ev);         // the MCP section
    void BlockAbout(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void EffectControls(Settings& s, UiEvents& ev, float advanced, bool enabled, const PipelineStatus* st);   // the DLSS 5 effect controls (advanced: how far the expert ones show, 0..1)
    void DrawItemParams(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);   // a library item's own values
    void DrawLibraryMenu(Settings& s, const UiFrameInfo& info, UiEvents& ev);      // the context menu of a card
    void DrawPresetRow(Settings& s, UiEvents& ev);                                   // the user's presets of the effect values
    void DrawTransformTools(Settings& s, const UiFrameInfo& info, UiEvents& ev, const ImVec2& origin, const ImVec2& region,
                            const ImVec2& imgPos, const ImVec2& imgSize, bool canvasHovered);   // turn / mirror / crop of the shown file
    void ModeFadeContent(int fromVtx);
    void DrawFades(Settings& s, const UiFrameInfo& info, UiEvents& ev);              // the mode, fullscreen and start-up fades
    void RequestFullscreen();                                                        // the switch, behind a short dip to black
    void TrackUndo(const Settings& s, const UiFrameInfo& info, bool force = false);   // records a settings or library change as an undo step
    void ApplyUndo(Settings& s, const UiFrameInfo& info, UiEvents& ev, bool redo);
    void GoToHistory(Settings& s, const UiFrameInfo& info, UiEvents& ev, int index);   // to an entry of the history list
    void DrawHistory(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& anchor);   // the history popup, under its button
    void DrawUpdatePopup(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void MirrorControls(Settings& s, const UiFrameInfo& info, UiEvents& ev, float width, bool why);   // the GitHub access choice
    void DrawMirrorPopup(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);   // no mirror site answered
    void DrawSetupGuide(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);    // the first-start guide
    void DrawCrashNotice(const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);                 // the previous session ended badly
    void DrawDriverNotice(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);  // the NVIDIA driver is too old for DLSS 5
    void CloseGuide(Settings& s, UiEvents& ev, bool point);                                          // point = light up the places the guide named
    void Spotlight(bool foreground);                                                                 // a pulsing ring around the last item, after the guide
    // Jumps (JumpTo): the place is shown (the sidebar comes out, its section opens, the sidebar glides to it) and ringed.
    // The markers say where each place is drawn: JumpBegin/JumpEnd around a row of the sidebar, JumpItem after a
    // single control there, JumpRect anywhere with its rectangle, Section for the sidebar's section headers.
    void JumpFrame(Settings& s, UiEvents& ev);                                                       // at the top of the frame: takes up a jump, decides its next step
    void JumpAim(Settings& s, UiEvents& ev, const JumpTarget* t);
    void JumpFallback(Settings& s, UiEvents& ev);
    bool JumpSearchFor(const JumpTarget* t);
    void JumpRestoreQuery();                                                                         // the search field as the user left it, if a jump's query is still there
    void JumpGlide();                                                                                // in the sidebar, before its content
    bool Section(const char* label, const char* code, bool defaultOpen, Icon icon);
    void JumpBegin(const char* id);
    void JumpEnd();
    void JumpItem(const char* id);
    void JumpRect(const char* id, const ImVec2& min, const ImVec2& max, bool inside = false);
    bool JumpWants(const char* id) const;
    void JumpSeen(const ImVec2& min, const ImVec2& max, bool sidebar, bool inside);
    static std::vector<LibrarySnapshotItem> LibrarySnapshot(const UiFrameInfo& info);
    static bool SameLibrary(const std::vector<LibrarySnapshotItem>& a, const std::vector<LibrarySnapshotItem>& b);
    void ResetView(bool animate);                                                  // back to the fitted, centred picture
    std::string EstimateText(const Settings& s, const UiFrameInfo& info) const;    // "≈ 2 min 30 s" for the current file
    double BatchRemaining(const UiFrameInfo& info) const;                          // seconds left in the batch (-1: unknown)

    // Performance readouts are refreshed a few times per second rather than every frame, so the digits stay legible;
    // every place that shows them uses fixed-width text so a changing figure never moves the controls around it.
    struct ShownPerf {
        double gpuMs[(UINT)GpuTimer::Count] = {};
        double fps = 0.0, cpuMs = 0.0, uiGpuMs = 0.0, processingFps = 0.0, senderFps = 0.0, depthMs = 0.0;
        float  statAvgCost = 0.0f, statMaxCost = 0.0f, statAvgMotion = 0.0f;
        float  nrOutDelta = -1.0f;
        unsigned long long processedFrames = 0, resets = 0;
    };
    void UpdateShown(const UiFrameInfo& info);
    ShownPerf    m_shown;
    double       m_shownTime = -1.0;
    const Fonts* m_fonts = nullptr;

    std::vector<ToastItem> m_toasts;
    char   m_runtimeBuf[1024] = {};
    char   m_depthModelBuf[1024] = {};
    char   m_folderBuf[1024] = {};
    char   m_nameBuf[512] = {};      // the file-name template (of the mode's kind)
    char   m_senderBuf[256] = {};
    bool   m_runtimeEditing = false;
    bool   m_depthModelEditing = false;
    bool   m_folderEditing = false;
    char   m_mcpKeyBuf[64] = {};      // the name of the key being added
    int    m_mcpKeyRole = 1;
    char   m_mcpJobFolderBuf[1024] = {};
    bool   m_mcpJobFolderEditing = false;
    bool   m_nameEditing = false;
    float  m_zoom = 1.0f;          // manual magnification on top of the fit (1 = as fitted, or 1:1), moving toward m_zoomTarget
    float  m_zoomTarget = 1.0f;
    float  m_zoomFloor = 0.1f;     // lowest magnification relative to the picture's pixels: kZoomMin, or the fitted view when that is smaller
    ImVec2 m_zoomAnchor = ImVec2(0, 0);   // the screen point kept in place while the zoom moves
    bool   m_panHome = false;      // the pan glides back to the centre
    float  m_baseScale = 1.0f;     // preview pixels per picture pixel at zoom 1, from the last preview draw
    ImVec2 m_pan = ImVec2(0, 0);
    float  m_themeLight = -1.0f;   // the theme blend in use (-1: not set yet)
    // Undo: a snapshot of the adjustable values and of the library is taken whenever they change while no control
    // is held; Ctrl+Z / Ctrl+Y and the top-bar buttons move through them.
    struct UndoStep { std::string params; std::vector<LibrarySnapshotItem> library; std::string label; };   // label: the change that led here
    static std::string StepLabel(const UndoStep& from, const UndoStep& to);      // a short name for the change between two states
    std::vector<UndoStep> m_undo, m_redo;
    UndoStep m_undoBase;
    double m_fullscreenMouseTime = 0.0;   // when the mouse last moved in the fullscreen view
    float  m_fullscreenControls = 1.0f;   // how far the fullscreen controls are shown (they fade after a rest)
    bool   m_undoInit = false;
    bool   m_undoHold = false;       // skip one TrackUndo: the library restore of an undo lands after this frame
    float  m_sidebarDragW = 0.0f;
    float  m_askDragW = 0.0f;        // the Ask AI panel's width when its edge drag began
    int    m_askDragFrames = 0;      // that drag's frames, those slower than a 60 Hz frame, and its longest one
    int    m_askDragSlow = 0;        // (logged at the end)
    float  m_askDragLongest = 0.0f;
    float  m_libraryDragH = 0.0f;
    float  m_thumbH = 0.0f;          // thumbnail height of the library cards, from the library's height
    bool   m_updateOpen = false;     // open the update popup on this frame
    // The setup guide, and the places it points at once it closes.
    bool   m_guideOpen = false;      // open the guide on this frame
    int    m_guideStartPage = 0;     // at this page (test runs only open it elsewhere than the first)
    bool   m_guideShowing = false;   // the guide is up
    bool   m_crashNoticeDone = false;  // the previous session's end was reported (or there was nothing to report)
    bool   m_driverNoticeDone = false;   // the driver check had its one chance this session
    bool   m_driverOpened = false;       // the driver notice was opened this session (nothing of it runs otherwise)
    bool   m_driverShowing = false;      // the driver notice is up
    bool   m_driverDontShow = false;     // its "Don't show again" box
    double m_driverShownAt = -1.0;       // the first frame it was visible: its motion runs from there
    bool   m_guideAutoDone = false;  // the first-start opening was decided
    int    m_guidePage = 0;
    double m_guidePageTime = -1.0;   // when the page changed (its content fades in)
    bool   m_updateDeferred = false; // an update popup waits for the window to come up and for the guide to close
    bool   m_mirrorOpen = false;     // open the mirror-sites popup on this frame
    bool   m_mirrorDeferred = false; // the mirror-sites popup waits for the window to come up
    int    m_mirrorBlockState = -1;  // the mirror controls' mode-specific part: what it showed last (its content fades in on a change)
    double m_mirrorBlockTime = -1.0; // when that changed
    bool   m_openAbout = false;      // open the About section (once, in the sidebar)
    double m_scrollToAbout = -1.0;   // the sidebar glides to the About section until this time (its fold opens over a few frames)
    float  m_aboutY = -1.0f;         // the About header's place in the sidebar (content coordinates)
    float  m_advShown = 0.0f;        // how far the Advanced switch's controls show (0..1, moving while it is flipped)
    float  m_adv = 0.0f;             // the same in the sidebar, where a search shows them all
    float  m_sidebarBarW = 0.0f;     // the settings' scrollbar width on the last frame: the banner and the search row end where the cards do
    std::string m_lockText;          // the lock banner's text, kept while the banner fades out
    double m_spotAt = -1.0;          // the spotlight rings flash once, from this time ...
    double m_spotUntil = -1.0;       // ... until this one
    // Jumps.
    struct JumpMark { bool on = false; float x = 0.0f, y = 0.0f; };
    std::string m_jumpAsk;           // asked for (an id), taken up at the top of the next frame
    const JumpTarget* m_jumpReq = nullptr;   // what was asked for
    const JumpTarget* m_jumpCur = nullptr;   // what is pointed at: that, what stands for it, or the source switch
    const char* m_jumpCurId = "";   // its marker's id (a section header's: the section's code)
    bool   m_jumpIsHeader = false;   // it is a section header of the sidebar
    int    m_jumpPhase = 0;          // 0 nothing, 1 looking for it (and gliding to it), 2 ringing it
    int    m_jumpHops = 0;           // fallbacks taken
    int    m_jumpFrames = 0;         // frames since it was aimed at
    double m_jumpAimAt = -1.0;       // when it was aimed at
    double m_jumpOpenedAt = -1.0;    // when something opened to show it (the sidebar, a section)
    double m_jumpRingAt = -1.0;      // when the ring came up
    double m_jumpCut = -1.0;         // when an input ended it early (the ring fades)
    double m_jumpGlideAt = -1.0;     // when the sidebar began to glide to it (-1: it did not have to)
    int    m_jumpSeenFrame = -100;   // the last frame its marker was drawn
    bool   m_jumpInSidebar = false;  // that marker is in the sidebar's scrolled settings
    float  m_jumpY0 = 0.0f, m_jumpY1 = 0.0f;   // its place there (content coordinates)
    float  m_jumpDist = 0.0f;        // how far the glide still has to go
    unsigned m_jumpModeMask = 0;     // the source modes the asked-for place is drawn in ("mode" rings their segments)
    std::string m_jumpOpenSection;   // the sidebar section to open (its code)
    bool   m_jumpSearched = false;   // the search was used to show it
    bool   m_jumpQuerySet = false;   // the search field holds what a jump put there ...
    std::string m_jumpQuery, m_jumpUserQuery;   // ... this, and what it held before
    ImGuiID m_searchId = 0;          // the search field
    bool   m_markSidebar = false;    // the sidebar's settings are being drawn (EffectControls also draws a library item's own values)
    JumpMark m_jumpStack[8];
    int    m_jumpDepth = 0;
    bool   m_upscaleWarned = false;  // the notice about the cost of super resolution was shown for the current upscale
    bool   m_wipeDragging = false;
    // Seek bar: while the knob is dragged the bar follows the cursor and seeks are sent a few times per second;
    // after a seek the bar shows the target until the processing thread reports a position near it.
    bool   m_seekDragging = false;
    float  m_libraryFold = 1.0f;     // 0 = the library strip is folded away, 1 = fully shown (animated)
    float  m_libraryFill = 1.0f;     // 0 = the library is empty and only its header shows, 1 = it has files (animated)
    std::vector<unsigned> m_paramsItems;   // the library items whose own parameters are open in a window (first = shown)
    unsigned m_ctxItem = 0;          // the item under the context menu
    unsigned m_lastClicked = 0;      // anchor of a shift-click range
    bool   m_libDrag = false;        // a selection drag runs in the library strip
    bool   m_libDragMoved = false;
    ImVec2 m_libDragStart;           // in strip content coordinates
    std::vector<unsigned> m_libDragKeep;   // items selected when the drag began (kept with Ctrl)
    double m_seekDragTime = 0.0;
    double m_seekSentTime = 0.0;     // when the last seek was sent
    double m_seekTarget = -1.0;      // -1: none pending
    unsigned m_logGeneration = 0;
    std::vector<LogEntry> m_logCache;
    // The library bar (click folds, drag resizes).
    // Fades: the source switch waits behind a dip toward the preview's background, the fullscreen switch behind a
    // dip to black, and the window comes up from its own background after the start-up card.
    int    m_lastMode = -1;
    int    m_modePending = -1;
    double m_modeFadeStart = -1.0;
    float  m_modeFade = 0.0f;
    bool   m_fsPending = false, m_fsSent = false, m_fsTarget = false, m_fsRise = false;
    double m_fsFadeStart = -1.0;
    int    m_fsFrames = 0;
    double m_startFade = -1.0;
    ImVec2 m_previewMin, m_previewMax;   // where the preview fade is painted
    float  m_welcomeH = 0.0f;        // height of the welcome's content (centred in the preview from the next frame)
    double m_undoCheckTime = -1.0;   // when TrackUndo last compared the state
    double m_loadingSince = -1.0;    // since when an open source has had no picture (-1: not waiting)
    float  m_toastBottom = 0.0f;     // the notices stack up from here (just above the status bar or the video controls)
    // The Ask AI panel: its card at the window's right edge, under the page.
    float  m_askT = 0.0f;            // how far it has slid in (0..1, eased)
    ImVec2 m_askMin, m_askMax;       // the card's rectangle this frame
    ImVec2 m_askBtnMin, m_askBtnMax; // the top bar's Ask AI button this frame
    bool   m_askLanded = false;      // fully in, and its own content (the arc, a failure) faded out: the page may show
    double m_askLoadingSince = -1.0; // the page began to load (the arc waits a moment before it shows)
    float  m_askCut = 0.0f;          // the main viewport ends here while the panel is in (0: it is not)
    bool   m_askTrimmed = false;     // the viewport is cut at m_askCut now
    bool   m_floatTrimmed = false;   // it was cut when a tooltip, dropdown or menu widened it (SetFloatingArea)
    float  m_vpW = 0.0f, m_vpWorkW = 0.0f;   // its widths before the cut
    void   AskTrim(bool on);         // the main viewport ends at the panel's left edge (on) or at the window's (off)
    void   DrawAskCard(const UiFrameInfo& info, UiEvents& ev);   // the card, and what it shows while the page is not there
    void   DrawAskEdge(Settings& s, const UiFrameInfo& info, UiEvents& ev, float gap, float minW, float maxW);   // its resize line
    // Presets.
    const std::vector<UserPreset>* m_presetList = nullptr;
    char   m_presetBuf[96] = {};
    int    m_presetEdit = -1;        // the preset whose name is being typed in the list
    bool   m_presetFocus = false;
    // The sidebar search.
    char   m_searchBuf[128] = {};
    // Cropping of the shown library file: the rectangle is adjusted on a copy and applied at the end.
    bool   m_cropEditing = false;
    unsigned m_cropItem = 0;
    SourceTransform m_cropWork, m_cropDragBase;
    int    m_cropHandle = -1;        // 0-7 the handles (clockwise from the top left), 8 the whole rectangle
    ImVec2 m_cropDragStart;
    // The turn/mirror/crop tool row being moved by its grip: the mouse keeps this offset from the row's centre.
    bool   m_toolRowDragging = false;
    ImVec2 m_toolRowDragOffset;
    ImVec2 m_toolRowMin, m_toolRowMax;   // where the row was drawn last frame (empty when it was not): the overlay stays above it
};

} // namespace vdc::ui
