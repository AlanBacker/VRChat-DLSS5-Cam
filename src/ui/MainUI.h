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

struct UserPreset {
    std::string name;
    std::string text;
};

struct UiFrameInfo {
    const PipelineStatus* status = nullptr;
    const AdapterInfo*    adapter = nullptr;
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
    const std::vector<UserPreset>* presets = nullptr;   // the user's presets, in file order
    size_t                capturePending = 0;
    std::string           lastCapture;
    bool                  lastCaptureOk = true;
    bool                  systemLight = false;      // Windows uses light app colours (theme "System" follows it)
    // Updates (App copies the updater's state; the values follow Updater::State in order).
    int                   updateState = 0;          // UpdateState
    std::string           updateVersion, updateDate, updateNotes, updateError;
    bool                  updatePrerelease = false;
    bool                  updateEdition = false;    // the release found is the other edition of this program
    bool                  updateDowngrade = false;  // the release found is older: the stable channel's way back from a pre-release
    bool                  updateHasAsset = false;   // the release carries the win64 zip
    bool                  updateWritable = true;    // the program folder takes new files
    bool                  updateShow = false;       // open the update popup (set for one frame)
    float                 updateProgress = 0.0f;
    double                updateDownloadedMb = 0.0, updateTotalMb = 0.0;
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
    bool browseRuntime = false;
    bool browseDepthModel = false;
    bool reloadDepth = false;
    bool browseFolder = false;
    bool openCaptureFolder = false;
    bool openLogFile = false;
    bool openSettingsFolder = false;
    bool openProjectPage = false;
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
    bool libraryDeleteSelected = false;   // drop the selected items from the library
    bool itemParamsChanged = false;  // an item's own effect values were edited
    bool cropEditing = false;        // the crop of the shown file is being drawn: show the whole turned picture
    SourceTransform cropPreview;     // its orientation while that lasts
    bool openDocs = false;           // the documentation in the browser (in the interface's language)
    // Presets: saved under a name (a new one, or an existing one to replace), renamed, removed.
    bool        presetSave = false;
    std::string presetName;          // presetSave / presetRename: the name to store under
    int         presetRename = -1;   // index of the preset to rename to presetName
    int         presetDelete = -1;   // index of the preset to remove
};

class MainUI {
public:
    void Draw(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void Toast(const std::string& text, bool error = false);

private:
    struct ToastItem { std::string text; double time; bool error; };

    void DrawTopBar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void DrawSidebar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void DrawPreview(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void DrawPicture(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size);
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
    void BlockAbout(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void EffectControls(Settings& s, UiEvents& ev, bool advanced, bool enabled, const PipelineStatus* st);   // the DLSS 5 effect controls
    void DrawItemParams(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);   // a library item's own values
    void DrawLibraryMenu(Settings& s, const UiFrameInfo& info, UiEvents& ev);      // the context menu of a card
    void DrawPresetRow(Settings& s, UiEvents& ev);                                   // the user's presets of the effect values
    void DrawTransformTools(Settings& s, const UiFrameInfo& info, UiEvents& ev, const ImVec2& origin, const ImVec2& region,
                            const ImVec2& imgPos, const ImVec2& imgSize, bool canvasHovered);   // turn / mirror / crop of the shown file
    void DrawFades(Settings& s, const UiFrameInfo& info, UiEvents& ev);              // the mode, fullscreen and start-up fades
    void RequestFullscreen();                                                        // the switch, behind a short dip to black
    void TrackUndo(const Settings& s, const UiFrameInfo& info);                    // records a settings or library change as an undo step
    void ApplyUndo(Settings& s, const UiFrameInfo& info, UiEvents& ev, bool redo);
    void GoToHistory(Settings& s, const UiFrameInfo& info, UiEvents& ev, int index);   // to an entry of the history list
    void DrawHistory(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& anchor);   // the history popup, under its button
    void DrawUpdatePopup(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
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
    char   m_senderBuf[256] = {};
    bool   m_runtimeEditing = false;
    bool   m_depthModelEditing = false;
    bool   m_folderEditing = false;
    float  m_zoom = 1.0f;          // manual magnification on top of the fit (1 = as fitted, or 1:1), moving toward m_zoomTarget
    float  m_zoomTarget = 1.0f;
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
    float  m_libraryDragH = 0.0f;
    float  m_thumbH = 0.0f;          // thumbnail height of the library cards, from the library's height
    bool   m_updateOpen = false;     // open the update popup on this frame
    bool   m_upscaleWarned = false;  // the notice about the cost of super resolution was shown for the current upscale
    bool   m_wipeDragging = false;
    // Seek bar: while the knob is dragged the bar follows the cursor and seeks are sent a few times per second;
    // after a seek the bar shows the target until the processing thread reports a position near it.
    bool   m_seekDragging = false;
    float  m_libraryFold = 1.0f;     // 0 = the library strip is folded away, 1 = fully shown (animated)
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
};

} // namespace vdc::ui
