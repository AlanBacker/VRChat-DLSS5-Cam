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
    const Device*         device = nullptr;
    double                fps = 0.0;
    double                cpuMs = 0.0;
    const std::vector<std::string>* senders = nullptr;
    std::string           senderName;
    double                senderFps = 0.0;
    std::string           sourceFormat;
    bool                  sourceConnected = false;
    bool                  sourceIsHdr = false;      // floating-point (linear HDR) Spout texture
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
    void SectionDisplay(Settings& s, const UiFrameInfo& info, UiEvents& ev);
    void SectionAbout(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts);

    std::vector<ToastItem> m_toasts;
    char   m_runtimeBuf[1024] = {};
    char   m_folderBuf[1024] = {};
    char   m_senderBuf[256] = {};
    bool   m_runtimeEditing = false;
    bool   m_folderEditing = false;
    float  m_zoom = 1.0f;
    ImVec2 m_pan = ImVec2(0, 0);
    bool   m_wipeDragging = false;
    unsigned m_logGeneration = 0;
    std::vector<LogEntry> m_logCache;
};

} // namespace vdc::ui
