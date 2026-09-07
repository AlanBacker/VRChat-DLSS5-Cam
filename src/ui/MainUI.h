// VRChat DLSS5 Cam - main window layout (top bar, sidebar, preview, status bar, log, toasts).
#pragma once
#include "core/Settings.h"
#include "gfx/Pipeline.h"
#include "ui/Fonts.h"
#include "core/Log.h"
#include "imgui.h"
#include <string>
#include <vector>

namespace vdc::ui {

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
    std::string           videoName;                // file name of the opened video (UTF-8)
    std::string           videoCodec;
    UINT                  videoWidth = 0, videoHeight = 0;
    double                videoFps = 0.0;
    UINT64                videoFrames = 0;          // estimate from the file
    double                videoDurationSeconds = 0.0;
    bool                  videoHasAudio = false;
    bool                  videoHardwareDecode = false;
    bool                  videoProcessing = false;  // the file is being run through the pipeline
    bool                  videoFinishing = false;
    UINT64                videoFrame = 0;           // frames delivered to the output
    double                videoElapsed = 0.0;
    std::string           videoOutName;
    bool                  batchRunning = false;
    int                   batchIndex = 0, batchCount = 0, batchDone = 0, batchFailed = 0;
    std::string           batchItemName;
    const std::vector<std::string>* batchFiles = nullptr;   // names in the batch queue
    std::wstring          nrRuntimePath;      // effective path
    bool                  nrRuntimeExists = false;
    std::wstring          captureFolder;      // effective folder
    std::string           hotkeyText;
    ImTextureID           displayTexture = 0;
    UINT                  displayWidth = 0, displayHeight = 0;
    bool                  hasDisplay = false;
    std::string           appVersion;
    size_t                capturePending = 0;
    std::string           lastCapture;
    bool                  lastCaptureOk = true;
};

struct UiEvents {
    bool captureNow = false;
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
    bool openImage = false;          // browse for a picture
    bool openVideo = false;          // browse for a video
    bool cancelVideo = false;        // stop the running video
    bool batchAddFiles = false;
    bool batchAddFolder = false;
    bool batchClear = false;
    bool batchStart = false;
    bool batchCancel = false;
    int  batchRemove = -1;           // index of the queued file to drop
    bool sourceModeChanged = false;  // s.sourceMode switched between Spout, image and video
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
    void DrawStatusBar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);
    void DrawLogWindow(Settings& s, UiEvents& ev, const Fonts& fonts);
    void DrawToasts(const Fonts& fonts);

    void SectionSource(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionNeural(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionGuidance(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionDlaa(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionCapture(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionBatch(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionDisplay(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionAbout(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);

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
    float  m_zoom = 1.0f;          // manual magnification on top of the fit (1 = as fitted, or 1:1)
    float  m_baseScale = 1.0f;     // preview pixels per picture pixel at zoom 1, from the last preview draw
    ImVec2 m_pan = ImVec2(0, 0);
    bool   m_wipeDragging = false;
    unsigned m_logGeneration = 0;
    std::vector<LogEntry> m_logCache;
};

} // namespace vdc::ui
