// VRChat DLSS5 Cam - persistent user settings (simple key=value file).
#pragma once
#include <string>

namespace vdc {

// Enumerations shared by settings, pipeline and UI.
enum MotionMode  { MotionZero = 0, MotionCompute = 1, MotionNvOpticalFlow = 2 };
enum DepthMode   { DepthFlat = 0, DepthGradient = 1, DepthZero = 2 };
enum CompareMode { CompareOutput = 0, CompareOriginal = 1, CompareWipe = 2, CompareMotion = 3 };
enum FitMode     { FitWindow = 0, FitOneToOne = 1 };
enum NrRoute     { RouteSignedSnippet = 0, RouteNgxCore = 1 };

struct Settings {
    // General
    int         language = 0;          // 0 = auto, 1 = English, 2 = Chinese, 3 = Japanese, 4 = Korean
    std::string senderName;            // UTF-8, empty = automatic (prefers VRCSender1)

    // Resolution
    bool customResolution = false;
    int  customWidth  = 1920;
    int  customHeight = 1080;
    bool keepAspect   = true;

    // DLSS 5 neural rendering (DLSSNR)
    bool        nrEnabled = true;
    int         nrRoute = RouteSignedSnippet;
    std::string nrDllPath;             // UTF-8, empty = <exe folder>\nvngx_dlssnr.dll
    int         nrPreset = 0;          // 0..3
    int         nrStyle = 0;           // 0 default, 1 natural, 2 cinematic
    float       nrIntensity = 1.0f;    // 0..2
    float       nrGlobalTone = 1.0f;   // 0..2
    float       nrLocalTone = 1.0f;    // 0..2
    float       nrLocalStructure = 1.0f; // 0..2
    float       nrSkinStructure = -1.0f; // -1 = runtime default, else 0..2
    bool        nrAutoMask = false;
    bool        nrUiCorrection = false;
    bool        nrUpscale = false;     // experimental: let DLSSNR upscale to the custom resolution

    // Frame guidance (motion vectors / depth)
    int   motionMode = MotionCompute;
    int   depthMode = DepthFlat;
    int   searchRadius = 7;            // block-matching radius at quarter resolution
    float motionConfidence = 0.35f;    // confidence below which vectors are damped
    int   nvofGrid = 2;                // 1, 2, 4
    int   nvofPerf = 10;               // 5 slow, 10 medium, 20 fast
    bool  autoReset = true;
    float cutThreshold = 0.10f;

    // DLAA pre-pass (DLSS super resolution at native resolution)
    bool dlaaEnabled = false;
    int  dlaaPreset = 11;              // NVSDK_NGX_DLSS_Hint_Render_Preset_K

    // Display
    int   compareMode = CompareOutput;
    float wipePosition = 0.5f;
    bool  checkerboard = true;
    int   fitMode = FitWindow;
    bool  vsync = true;
    bool  showOverlay = true;

    // Capture
    std::string captureFolder;         // UTF-8, empty = Pictures\VRChat DLSS5 Cam
    bool        keepAlpha = true;
    bool        saveOriginal = false;
    bool        hotkeyEnabled = true;
    unsigned    hotkeyModifiers = 0x0002 | 0x0001; // MOD_CONTROL | MOD_ALT
    unsigned    hotkeyKey = 'P';
    int         timelapseSeconds = 0;

    // Window
    int  windowX = -1, windowY = -1, windowWidth = 1520, windowHeight = 940;
    bool windowMaximized = false;
    bool sidebarVisible = true;

    // Misc
    bool showLog = false;
    bool debugLayer = false;

    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;
    void Clamp();
};

} // namespace vdc
