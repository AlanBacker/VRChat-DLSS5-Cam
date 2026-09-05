// VRChat DLSS5 Cam - persistent user settings (simple key=value file).
#pragma once
#include <string>

namespace vdc {

// Enumerations shared by settings, pipeline and UI.
enum MotionMode  { MotionZero = 0, MotionCompute = 1, MotionNvOpticalFlow = 2 };
enum DepthMode   { DepthFlat = 0, DepthGradient = 1, DepthZero = 2, DepthEstimated = 3 };
enum CompareMode { CompareOutput = 0, CompareOriginal = 1, CompareWipe = 2, CompareMotion = 3, CompareDepth = 4 };
enum FitMode     { FitWindow = 0, FitOneToOne = 1 };
enum NrRoute     { RouteSignedSnippet = 0, RouteNgxCore = 1 };
enum SourceMode  { SourceSpout = 0, SourceImage = 1 };

struct Settings {
    // General
    int         language = 0;          // 0 = auto, 1 = English, 2 = Chinese, 3 = Japanese, 4 = Korean
    std::string senderName;            // UTF-8, empty = automatic (prefers VRCSender1)
    int         sourceMode = SourceSpout;   // Spout stream or a still image file
    std::string imagePath;             // UTF-8, the picture opened in image mode (reopened at startup)

    // Resolution
    bool customResolution = false;
    int  customWidth  = 1920;
    int  customHeight = 1080;
    bool keepAspect   = true;

    // HDR source (only used when the Spout texture is a floating-point / linear HDR format)
    float hdrPaperWhite = 1.0f;           // scene value mapped to display white, 0.1..8
    float hdrHighlightCompression = 1.0f; // 0 = hard clip above white, 1 = full soft roll-off

    // DLSS 5 neural rendering (DLSSNR)
    bool        nrEnabled = true;
    int         nrRoute = RouteSignedSnippet;
    std::string nrDllPath;             // UTF-8, empty = <exe folder>\nvngx_dlssnr.dll
    int         nrPreset = 0;          // 0..3
    int         nrStyle = 0;           // 0 default, 1 natural, 2 cinematic
    // Strengths are 0..2. Up to 1 goes to the runtime, which stops there; above 1 the composite pass amplifies the
    // difference between the neural result and the original picture, with the highest of the five as the gain.
    float       nrIntensity = 1.0f;
    float       nrGlobalTone = 1.0f;
    float       nrLocalTone = 1.0f;
    float       nrLocalStructure = 1.0f;
    float       nrSkinStructure = -1.0f; // -1 = runtime default, else 0..2
    bool        nrAutoMask = false;
    bool        nrUiCorrection = false;
    bool        nrUpscale = false;     // experimental: let DLSSNR upscale to the custom resolution
    // Output blend (composite pass)
    float       nrInputExposure = 1.0f; // 0.25..4: gain on the picture the network sees (paper-white scale), undone afterwards
    float       nrToneTransfer = 1.0f;  // 0..2: share of the neural pass's brightness change that reaches the output
    float       nrColorStrength = 1.0f; // 0..2: share of the neural pass's colour change that reaches the output

    // Frame guidance (motion vectors / depth)
    int   motionMode = MotionNvOpticalFlow;   // falls back to block matching when the hardware engine is unavailable
    int   depthMode = DepthEstimated;         // falls back to zero depth when the estimator is unavailable
    int   searchRadius = 7;            // block-matching radius at quarter resolution
    float motionConfidence = 0.35f;    // confidence below which vectors are damped
    int   nvofGrid = 4;                // 4, 2, 1 source pixels between hardware vectors (4 = fastest)
    int   nvofPerf = 10;               // 5 slow, 10 medium, 20 fast
    bool  nvofBidirectional = true;    // forward/backward consistency check
    int   depthInterval = 4;           // run the depth network every N processed frames (1..10)
    int   depthLongSide = 336;         // network resolution (long side): 252, 336, 420, 518
    std::string depthModelPath;        // UTF-8, empty = <exe folder>\models\depth_anything_v2_small_fp16.onnx
    bool  autoReset = false;           // reset the temporal history on detected scene cuts (DLSS 5 recovers by itself)
    float cutThreshold = 0.10f;
    int   settingsVersion = 3;         // bumped when defaults change; older files are migrated in Load()

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
