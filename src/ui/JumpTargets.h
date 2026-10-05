#pragma once
// The places in the interface that an answer of the Ask AI panel can point at. The page marks a phrase of an answer
// that is one of a target's names (in the interface's language) and sends the target's id when it is clicked; the
// interface then shows the place and rings it (MainUI::JumpTo), and the target's documentation page is one click
// away beside it. Nothing is ever changed by a jump: a control that is not drawn in the current state points at
// what decides it instead (its fallback), a control of another source mode at that mode's switch.
//
// The order counts where two targets share a name: the earlier one takes it. tools/check_jump_targets.py checks the
// table against the documentation (each page and anchor in every language) and against MainUI.cpp (each target has
// its marker there), and that every row of the settings page has a target here.

#include <cstring>

#include "core/I18n.h"

namespace vdc::ui {

enum JumpFlag : unsigned {
    JumpHeader  = 1u << 0,   // a section of the sidebar: its header is ringed, the section opens (marker: the section's id)
    JumpAdv     = 1u << 1,   // shown with the Advanced switch on; with it off, the sidebar's search shows it
    JumpGeForce = 1u << 2,   // not in the Radeon edition
    JumpNoPlace = 1u << 3,   // has no place of its own (a menu entry): goes to its fallback
    JumpTopBar  = 1u << 4,   // in the top bar
    JumpSearch  = 1u << 5,   // in the row above the sidebar's settings (the search field, the Advanced switch)
    JumpLibrary = 1u << 6,   // in the library under the picture
};

// The source modes a target is drawn in (bit = 1 << SourceMode).
enum JumpMode : unsigned { JumpLive = 1u, JumpPicture = 2u, JumpVideo = 4u, JumpFiles = 6u, JumpAnyMode = 7u };

struct JumpLabels {
    Str s[8] = {};
    int n = 0;
    constexpr JumpLabels() = default;
    template <class... T> constexpr JumpLabels(T... a) : s{ a... }, n((int)sizeof...(a)) {}
};

struct JumpTarget {
    const char* id;         // what the page sends
    const char* doc;        // the documentation page (and anchor), under the language's folder of the site
    const char* section;    // the sidebar section it is in, by the section's id ("" outside the sidebar)
    const char* fallback;   // what to point at when it is not drawn ("": its section's header, else the page)
    unsigned    flags;
    unsigned    modes;
    JumpLabels  labels;     // the interface's names for it
    const char* aliases;    // names in every language, '|' between them (key combinations)
};

inline constexpr JumpTarget kJumpTargets[] = {
    // Source
    { "spout-sender",      "settings.html#spout-sender",      "source",    "", 0, JumpLive, { Str::Sender, Str::SenderAuto }, "" },
    { "open-file",         "settings.html#open-file",         "source",    "", 0, JumpFiles, { Str::OpenImage, Str::OpenVideo }, "" },
    { "custom-resolution", "settings.html#custom-resolution", "source",    "", 0, JumpAnyMode, { Str::CustomResolution }, "" },
    { "upscaling",         "settings.html#upscaling",         "source",    "custom-resolution", 0, JumpAnyMode, { Str::UpscaleMethod }, "" },
    { "match-source",      "settings.html#match-source",      "source",    "", 0, JumpVideo, { Str::MatchSource }, "" },
    { "save-as",           "settings.html#save-as",           "source",    "match-source", 0, JumpVideo,
      { Str::VideoOutput, Str::VideoOutputH264, Str::VideoOutputHevc, Str::VideoOutputPng, Str::VideoOutputGif, Str::VideoOutputApng, Str::VideoOutputWebP }, "" },
    { "bitrate",           "settings.html#bitrate",           "source",    "save-as", 0, JumpVideo, { Str::Bitrate }, "" },
    { "keep-audio",        "settings.html#keep-audio",        "source",    "match-source", 0, JumpVideo, { Str::KeepAudio }, "" },
    { "webp-quality",      "settings.html#webp-quality",      "source",    "save-as", 0, JumpVideo, { Str::WebpQuality }, "" },
    { "hardware-decoding", "settings.html#hardware-decoding", "source",    "", JumpAdv, JumpVideo, { Str::HardwareDecode }, "" },
    { "hdr",               "settings.html#hdr",               "source",    "", 0, JumpLive, { Str::PaperWhite, Str::HighlightCompression, Str::HdrSource }, "" },
    // DLSS 5
    { "enable",            "settings.html#enable",            "neural",    "", 0, JumpAnyMode, { Str::NrEnable }, "" },
    { "capture-only",      "settings.html#capture-only",      "neural",    "", JumpAdv, JumpLive, { Str::NrCaptureOnly }, "" },
    { "runtime",           "settings.html#runtime",           "neural",    "", 0, JumpAnyMode, { Str::RuntimePath }, "" },
    { "preset",            "settings.html#preset",            "neural",    "", 0, JumpAnyMode, { Str::Preset, Str::Presets }, "" },
    { "style",             "settings.html#style",             "neural",    "", 0, JumpAnyMode, { Str::Style }, "" },
    { "intensity",         "settings.html#intensity",         "neural",    "", 0, JumpAnyMode, { Str::Intensity }, "" },
    { "global-tone",       "settings.html#global-tone",       "neural",    "", JumpAdv, JumpAnyMode, { Str::GlobalTone }, "" },
    { "local-tone",        "settings.html#local-tone",        "neural",    "", JumpAdv, JumpAnyMode, { Str::LocalTone }, "" },
    { "local-structure",   "settings.html#local-structure",   "neural",    "", JumpAdv, JumpAnyMode, { Str::LocalStructure }, "" },
    { "skin-structure",    "settings.html#skin-structure",    "neural",    "", JumpAdv, JumpAnyMode, { Str::SkinStructure }, "" },
    { "auto-mask",         "settings.html#auto-mask",         "neural",    "", JumpAdv, JumpAnyMode, { Str::AutoMask }, "" },
    { "ui-correction",     "settings.html#ui-correction",     "neural",    "", JumpAdv, JumpAnyMode, { Str::UiCorrection }, "" },
    { "pass-resolution",   "settings.html#pass-resolution",   "neural",    "", JumpAdv, JumpAnyMode, { Str::NrScaleMode }, "" },
    { "output-blend",      "settings.html#input-exposure",    "neural",    "input-exposure", JumpAdv, JumpAnyMode, { Str::OutputBlend }, "" },
    { "input-exposure",    "settings.html#input-exposure",    "neural",    "", JumpAdv, JumpAnyMode, { Str::InputExposure }, "" },
    { "tone-transfer",     "settings.html#tone-transfer",     "neural",    "", JumpAdv, JumpAnyMode, { Str::ToneTransfer }, "" },
    { "colour-strength",   "settings.html#colour-strength",   "neural",    "", JumpAdv, JumpAnyMode, { Str::ColorStrength }, "" },
    { "shadow-strength",   "settings.html#shadow-strength",   "neural",    "", JumpAdv, JumpAnyMode, { Str::ShadowGain }, "" },
    { "highlight-strength","settings.html#highlight-strength","neural",    "", JumpAdv, JumpAnyMode, { Str::HighlightGain }, "" },
    { "reset-history",     "settings.html#reset-history",     "neural",    "", 0, JumpAnyMode, { Str::ResetHistory }, "" },
    { "reset-defaults",    "settings.html#reset-defaults",    "neural",    "", 0, JumpAnyMode, { Str::ResetDefaults }, "" },
    { "readouts",          "settings.html#readouts",          "neural",    "", JumpAdv, JumpAnyMode, { Str::NrPassSize, Str::NrOutputCheck, Str::Frames }, "" },
    // Capture (the button before the section: in Chinese, Japanese and Korean they share the name)
    { "save-button",       "settings.html#save-button",       "save",      "", 0, JumpAnyMode, { Str::Capture, Str::ProcessAndSave, Str::ProcessVideo }, "" },
    { "estimated-time",    "settings.html#estimated-time",    "save",      "", 0, JumpFiles, { Str::Estimate }, "" },
    { "folder",            "settings.html#folder",            "save",      "", 0, JumpAnyMode, { Str::CaptureFolder }, "" },
    { "file-name",         "settings.html#file-name",         "save",      "", 0, JumpAnyMode, { Str::CaptureName, Str::OutputName }, "" },
    { "save-original",     "settings.html#save-original",     "save",      "", 0, JumpAnyMode, { Str::SaveOriginal }, "" },
    { "keep-alpha",        "settings.html#keep-alpha",        "save",      "", JumpAdv, JumpAnyMode, { Str::KeepAlpha }, "" },
    { "hotkey",            "settings.html#hotkey",            "save",      "", 0, JumpLive, { Str::Hotkey }, "" },
    { "timelapse",         "settings.html#timelapse",         "save",      "", 0, JumpLive, { Str::Timelapse }, "" },
    // Frame guidance
    { "motion-vectors",    "settings.html#motion-vectors",    "guidance",  "", JumpAdv, JumpAnyMode, { Str::MotionSource }, "" },
    { "flow-grid",         "settings.html#flow-grid",         "guidance",  "motion-vectors", JumpAdv, JumpAnyMode, { Str::NvofGrid, Str::NvofPerf }, "" },
    { "search-radius",     "settings.html#search-radius",     "guidance",  "motion-vectors", JumpAdv, JumpAnyMode, { Str::SearchRadius }, "" },
    { "bidirectional",     "settings.html#bidirectional",     "guidance",  "motion-vectors", JumpAdv, JumpAnyMode, { Str::NvofBidirectional, Str::FlowBidirectional }, "" },
    { "confidence",        "settings.html#confidence",        "guidance",  "motion-vectors", JumpAdv, JumpAnyMode, { Str::MotionConfidence }, "" },
    { "depth",             "settings.html#depth",             "guidance",  "", JumpAdv, JumpAnyMode, { Str::DepthSource }, "" },
    { "depth-interval",    "settings.html#depth-interval",    "guidance",  "depth", JumpAdv, JumpAnyMode, { Str::DepthInterval }, "" },
    { "depth-resolution",  "settings.html#depth-resolution",  "guidance",  "depth", JumpAdv, JumpAnyMode, { Str::DepthResolution }, "" },
    { "depth-model",       "settings.html#depth-model",       "guidance",  "depth", JumpAdv, JumpAnyMode, { Str::DepthModel }, "" },
    { "auto-reset",        "settings.html#auto-reset",        "guidance",  "", JumpAdv, JumpAnyMode, { Str::AutoReset }, "" },
    { "cut-threshold",     "settings.html#cut-threshold",     "guidance",  "auto-reset", JumpAdv, JumpAnyMode, { Str::CutThreshold }, "" },
    // DLAA
    { "dlaa-enable",       "settings.html#dlaa-enable",       "dlaa",      "", JumpAdv | JumpGeForce, JumpAnyMode, { Str::DlaaEnable }, "" },
    { "dlaa-preset",       "settings.html#dlaa-preset",       "dlaa",      "dlaa-enable", JumpAdv | JumpGeForce, JumpAnyMode, { Str::DlaaPreset }, "" },
    // Display
    { "theme",             "settings.html#theme",             "view",      "", 0, JumpAnyMode, { Str::Theme }, "" },
    // (the Motion vectors and Depth views of Compare share their names with those settings, which take them)
    { "compare",           "settings.html#compare",           "view",      "", 0, JumpAnyMode,
      { Str::Compare, Str::CompareOutput, Str::CompareOriginal, Str::CompareWipe }, "" },
    { "fit",               "settings.html#fit",               "view",      "", 0, JumpAnyMode, { Str::FitWindowLabel, Str::OneToOne }, "" },
    { "zoom",              "settings.html#zoom",              "view",      "", 0, JumpAnyMode, { Str::Zoom }, "" },
    { "checkerboard",      "settings.html#checkerboard",      "view",      "", 0, JumpAnyMode, { Str::Checkerboard }, "" },
    { "show-library",      "settings.html#show-library",      "view",      "", 0, JumpAnyMode, { Str::ShowLibrary }, "" },
    { "overlay",           "settings.html#overlay",           "view",      "", 0, JumpAnyMode, { Str::Overlay }, "" },
    { "show-log",          "settings.html#show-log",          "view",      "", 0, JumpAnyMode, { Str::ShowLog }, "" },
    { "reopen-last",       "settings.html#reopen-last",       "view",      "", 0, JumpAnyMode, { Str::ReopenLast }, "" },
    { "vsync",             "settings.html#vsync",             "view",      "", JumpAdv, JumpAnyMode, { Str::Vsync }, "" },
    { "rate-cap",          "settings.html#rate-cap",          "view",      "", JumpAdv, JumpAnyMode, { Str::RateLimit }, "" },
    // MCP
    { "mcp-run",           "settings.html#mcp-run",           "mcp",       "", 0, JumpAnyMode, { Str::McpEnable }, "" },
    { "mcp-reach",         "settings.html#mcp-reach",         "mcp",       "", 0, JumpAnyMode, { Str::McpReach }, "" },
    { "mcp-port",          "settings.html#mcp-port",          "mcp",       "", 0, JumpAnyMode, { Str::McpPort, Str::McpReadOnly }, "" },
    { "mcp-keys",          "settings.html#mcp-keys",          "mcp",       "", 0, JumpAnyMode, { Str::McpKeys, Str::McpJobs }, "" },
    // Internals
    { "timings",           "settings.html#timings",           "internals", "", JumpAdv, JumpAnyMode, { Str::Timers }, "" },
    // About
    { "update-auto",       "settings.html#update-auto",       "about",     "", 0, JumpAnyMode, { Str::UpdateAuto }, "" },
    { "driver-check",      "settings.html#driver-check",      "about",     "", 0, JumpAnyMode, { Str::DriverCheckAuto }, "" },
    { "update-channel",    "settings.html#update-channel",    "about",     "", 0, JumpAnyMode, { Str::UpdateChannel }, "" },
    { "github-access",     "settings.html#github-access",     "about",     "", 0, JumpAnyMode, { Str::GithubAccess }, "" },
    { "update-now",        "settings.html#update-now",        "about",     "", 0, JumpAnyMode, { Str::UpdateCheckNow }, "" },
    { "logs",              "settings.html#logs",              "about",     "", 0, JumpAnyMode, { Str::OpenLogFile, Str::OpenSettingsFolder }, "" },
    { "report",            "settings.html#report",            "about",     "", 0, JumpAnyMode, { Str::ReportIssue }, "" },
    { "docs",              "settings.html#docs",              "about",     "", 0, JumpAnyMode, { Str::Documentation, Str::ProjectPage }, "" },
    { "setup-guide",       "settings.html#setup-guide",       "about",     "", 0, JumpAnyMode, { Str::GuideTitle }, "" },
    { "notices",           "settings.html#notices",           "about",     "", 0, JumpAnyMode, { Str::Licenses }, "" },
    { "reset-all",         "settings.html#reset-all",         "about",     "", 0, JumpAnyMode, { Str::ResetAllSettings }, "" },
    // The top bar: the source switch (each mode, or "mode": the segments of the modes a control is drawn in),
    // undo, redo and the history
    { "mode-live",         "live.html#receive",               "",          "", JumpTopBar, JumpAnyMode, { Str::TopSpout }, "" },
    { "mode-picture",      "first-picture.html#open",         "",          "", JumpTopBar, JumpAnyMode, { Str::TopImage }, "" },
    { "mode-video",        "videos.html#open",                "",          "", JumpTopBar, JumpAnyMode, { Str::TopVideo }, "" },
    { "mode",              "first-picture.html#open",         "",          "", JumpTopBar, JumpAnyMode, {}, "" },
    { "undo",              "shortcuts.html#undo",             "",          "", JumpTopBar, JumpAnyMode, {}, "Ctrl+Z" },
    { "redo",              "shortcuts.html#redo",             "",          "", JumpTopBar, JumpAnyMode, {}, "Ctrl+Y|Ctrl+Shift+Z" },
    { "history",           "presets.html#undo",               "",          "", JumpTopBar, JumpAnyMode, { Str::SecHistory }, "" },
    // The row above the settings
    { "advanced",          "settings.html",                   "",          "", JumpSearch, JumpAnyMode, { Str::Advanced }, "" },
    { "search",            "shortcuts.html#search",           "",          "", JumpSearch, JumpAnyMode, { Str::SearchHint }, "" },
    // The library
    { "library",           "library.html",                    "",          "", JumpLibrary, JumpAnyMode, { Str::SecLibrary }, "" },
    { "library-add",       "library.html#add",                "",          "library", JumpLibrary, JumpAnyMode, { Str::AddFiles, Str::AddFolder }, "" },
    { "batch",             "library.html#batch",              "",          "library-add", JumpLibrary, JumpAnyMode, { Str::BatchStart }, "" },
    { "process-selected",  "library.html#batch",              "",          "library-add", JumpLibrary, JumpAnyMode, { Str::ProcessSelected }, "" },
    { "own-params",        "library.html#own",                "",          "library", JumpLibrary | JumpNoPlace, JumpAnyMode, { Str::ItemParams }, "" },
    // The sections of the sidebar
    { "source",            "settings.html#source",            "source",    "", JumpHeader, JumpAnyMode, { Str::SecSource }, "" },
    { "dlss5",             "settings.html#dlss5",             "neural",    "", JumpHeader, JumpAnyMode, { Str::SecNeural }, "" },
    { "capture",           "settings.html#capture",           "save",      "", JumpHeader, JumpAnyMode, { Str::SecCapture }, "" },
    { "guidance",          "settings.html#guidance",          "guidance",  "", JumpHeader | JumpAdv, JumpAnyMode, { Str::SecGuidance }, "" },
    { "dlaa",              "settings.html#dlaa",              "dlaa",      "", JumpHeader | JumpAdv | JumpGeForce, JumpAnyMode, { Str::SecDlaa }, "" },
    { "display",           "settings.html#display",           "view",      "", JumpHeader, JumpAnyMode, { Str::SecDisplay }, "" },
    { "mcp",               "settings.html#mcp",               "mcp",       "", JumpHeader, JumpAnyMode, { Str::SecMcp }, "" },
    { "internals",         "settings.html#internals",         "internals", "", JumpHeader | JumpAdv, JumpAnyMode, { Str::SecInternals }, "" },
    { "about",             "settings.html#about",             "about",     "", JumpHeader, JumpAnyMode, { Str::SecAbout }, "" },
};

inline constexpr int kJumpTargetCount = (int)(sizeof(kJumpTargets) / sizeof(kJumpTargets[0]));

// In this edition (the Radeon edition has no DLAA).
inline bool JumpInEdition(const JumpTarget& t) {
#if APP_EDITION_AMD
    return (t.flags & JumpGeForce) == 0;
#else
    (void)t;
    return true;
#endif
}

inline const JumpTarget* FindJumpTarget(const char* id) {
    for (const JumpTarget& t : kJumpTargets)
        if (JumpInEdition(t) && std::strcmp(t.id, id) == 0) return &t;
    return nullptr;
}

} // namespace vdc::ui
