// VRChat DLSS5 Cam - per-frame processing graph: convert -> guidance (motion/depth) -> DLAA -> DLSSNR -> composite -> capture.
#pragma once
#include "gfx/Device.h"
#include "gfx/Shaders.h"
#include "gfx/NvOpticalFlow.h"
#include "gfx/DepthEstimator.h"
#include "ngx/NgxCore.h"
#include "ngx/DlssnrFeature.h"
#include "ngx/DlaaFeature.h"
#include "core/Settings.h"
#include "core/SourceFrame.h"
#include "core/Capture.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace vdc {

struct PipelineStatus {
    bool        sourceConnected = false;
    UINT        srcWidth = 0, srcHeight = 0;     // source picture (Spout sender or still image)
    UINT        inWidth = 0, inHeight = 0;       // neural renderer input
    UINT        outWidth = 0, outHeight = 0;     // output / capture / display
    bool        ngxInitialized = false;
    bool        dlssAvailable = false;
    std::string ngxStatus;
    bool        nrRuntimeLoaded = false;
    std::string nrRuntimeVersion;
    std::wstring nrRuntimePath;
    bool        nrActive = false;
    bool        nrFailed = false;
    std::string nrError;
    UINT64      nrEvaluations = 0;
    bool        nrUpscaling = false;
    bool        dlaaActive = false;
    bool        dlaaFailed = false;
    std::string dlaaError;
    int         motionModeActive = MotionZero;   // what actually ran on the last processed frame
    int         depthModeActive = DepthZero;     // depth actually fed to DLSSNR on the last processed frame
    bool        nvofAvailable = false;
    bool        nvofReady = false;
    bool        nvofBidirectional = false;
    bool        nvofSinglePass = false;          // forward + backward flow from one engine pass (driver API 5.0)
    UINT        nvofGrid = 0;                    // spacing of the hardware vectors in source pixels
    std::string nvofError;
    int         depthState = 0;                  // DepthEstimatorState
    std::string depthMessage;                    // failure reason or backend name
    std::string depthBackend;
    UINT        depthInferW = 0, depthInferH = 0;
    double      depthInferMs = 0.0;              // last network run
    double      depthWarmupMs = 0.0;
    double      depthAgeMs = 0.0;                // time since the last network result was applied
    UINT64      depthInferences = 0;
    std::wstring depthModelPath;                 // effective model path
    bool        depthModelExists = false;
    float       statAvgCost = 0.0f, statMaxCost = 0.0f, statAvgMotion = 0.0f;
    bool        sceneCut = false;
    UINT64      resets = 0;
    UINT64      processedFrames = 0;
    double      frameIntervalMs = 0.0;
    size_t      capturesInFlight = 0;
    double      gpuMs[(UINT)GpuTimer::Count] = {};   // processing-queue timers (Frame/Ui slots come from the UI context)
    bool        hasDisplay = false;
};

// What the UI thread draws this frame.
struct DisplayView {
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    UINT width = 0, height = 0;
    bool valid = false;
};

class Pipeline {
public:
    static constexpr UINT kDisplayBuffers = 3;

    // Main thread, before the processing thread starts / after it stopped.
    bool Init(Device& device, const std::wstring& exeDir, const std::wstring& appDataDir, std::wstring& error);
    void Shutdown(Device& device);

    // Processing thread -------------------------------------------------------
    bool LoadNrRuntime(GpuContext& gpu, const std::wstring& dllPath, std::string& error);
    void UnloadNrRuntime(GpuContext& gpu);
    // Records this frame's GPU work into cmd (the processing context's list). fresh = a new source frame arrived,
    // sourceChanged = the source texture was recreated.
    void Render(GpuContext& gpu, const SourceFrame& src, const Settings& s, ID3D12GraphicsCommandList* cmd, bool fresh, bool sourceChanged);
    void AfterSubmit(GpuContext& gpu, UINT64 fenceValue);   // right after GpuContext::EndFrame: publishes the display, tags readbacks
    void Update(GpuContext& gpu, Capture& capture);         // completes readbacks, feeds the PNG worker and the depth network
    void PublishStatus(GpuContext& gpu);                    // copies the status (plus GPU timers) for StatusSnapshot
    const PipelineStatus& Status() const { return m_status; }
    bool NeedsFrame() const;                                // pending requests that want a frame even without new input
    bool HasDisplay() const { return m_hasDisplay; }
    NgxCore& Ngx() { return m_ngx; }

    // Any thread -----------------------------------------------------------------
    void RequestReset() { m_resetReq = true; }
    void RestartDepthEstimator() { m_depthRestartReq = true; }
    void MarkNrDirty() { m_nrDirtyReq = true; }
    void MarkDlaaDirty() { m_dlaaDirtyReq = true; }
    // baseName: empty = timestamped VRChat_DLSS5_... name; else "<baseName>_DLSS5_<w>x<h>.png" (still images).
    void RequestCapture(const std::wstring& folder, bool keepAlpha, bool saveOriginal, const std::wstring& baseName);
    bool CapturePending() const;
    void StatusSnapshot(PipelineStatus& out) const;

    // UI thread ---------------------------------------------------------------------
    // AcquireDisplay picks the newest completed composite (call after Device::BeginFrame); ReleaseDisplay records the
    // fence value of the UI frame that sampled it (call after Device::EndFrame).
    DisplayView AcquireDisplay(GpuContext& ui);
    void        ReleaseDisplay(UINT64 uiFenceValue);

private:
    struct Tex {
        ComPtr<ID3D12Resource>      res;
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};
        D3D12_CPU_DESCRIPTOR_HANDLE uav{};
        D3D12_RESOURCE_STATES       state = D3D12_RESOURCE_STATE_COMMON;
        UINT                        w = 0, h = 0;
        DXGI_FORMAT                 fmt = DXGI_FORMAT_UNKNOWN;
        bool Valid() const { return res != nullptr; }
    };
    struct Readback {
        ComPtr<ID3D12Resource> buffer;
        UINT         w = 0, h = 0, pitch = 0;
        UINT64       fence = 0;
        bool         inUse = false;
        bool         keepAlpha = false;
        std::wstring path;
    };
    struct Config {
        UINT srcW = 0, srcH = 0; DXGI_FORMAT srcFmt = DXGI_FORMAT_UNKNOWN;
        UINT inW = 0, inH = 0, outW = 0, outH = 0;
        bool nvof = false; UINT nvofGrid = 2, nvofPerf = 10; bool nvofBidir = true;
        bool depthEst = false; UINT depthLongSide = 336; std::wstring depthModel;
        bool operator==(const Config& o) const {
            return srcW == o.srcW && srcH == o.srcH && srcFmt == o.srcFmt && inW == o.inW && inH == o.inH &&
                   outW == o.outW && outH == o.outH && nvof == o.nvof && nvofGrid == o.nvofGrid && nvofPerf == o.nvofPerf &&
                   nvofBidir == o.nvofBidir && depthEst == o.depthEst && depthLongSide == o.depthLongSide && depthModel == o.depthModel;
        }
    };

    bool CreateTex(GpuContext& gpu, Tex& t, UINT w, UINT h, DXGI_FORMAT fmt, bool uav, const wchar_t* name);
    void ReleaseTex(GpuContext& gpu, Tex& t);
    bool WrapShared(GpuContext& gpu, Tex& t, ID3D12Resource* res, UINT w, UINT h, DXGI_FORMAT fmt, const wchar_t* name);
    void Transition(ID3D12GraphicsCommandList* cmd, Tex& t, D3D12_RESOURCE_STATES state);
    bool Rebuild(GpuContext& gpu, const Config& cfg);
    void ReleaseResources(GpuContext& gpu);
    void ReleaseFeatures(GpuContext& gpu);
    void ReleaseDepthResources(GpuContext& gpu);
    bool CreateDepthResources(GpuContext& gpu, const Config& cfg);
    bool CreateDisplayBuffers(GpuContext& gpu, UINT w, UINT h);
    void RetireDisplayBuffers(GpuContext& gpu);
    Config ComputeConfig(const SourceFrame& src, const Settings& s) const;
    std::wstring DepthModelPath(const Settings& s) const;

    void RunConvert(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const SourceFrame& src, const Settings& s, bool writeNvof);
    void RunGuidance(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, int motionMode, bool haveHistory);
    bool RunOpticalFlow(GpuContext& gpu, ID3D12GraphicsCommandList*& cmd, bool resetHints);
    void RunDensify(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, int mode);
    void RunStats(GpuContext& gpu, ID3D12GraphicsCommandList* cmd);
    void ReadStats(GpuContext& gpu);
    void RunDepthCapture(GpuContext& gpu, ID3D12GraphicsCommandList*& cmd);      // colour -> network input -> CPU readback
    bool RunDepthApply(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, bool reset);   // network output -> m_depth (temporally filtered)
    bool RunDlaa(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, bool reset);
    bool RunNeural(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, Tex& input, bool reset);
    void RunComposite(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, Tex& processed, bool bypass);
    void EnqueueReadback(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, Tex& src, const std::wstring& path, bool keepAlpha);

    Shaders        m_shaders;
    NgxCore        m_ngx;
    DlssnrFeature  m_nr;
    DlaaFeature    m_dlaa;
    NvOpticalFlow  m_nvof;
    DepthEstimator m_depthEst;
    std::wstring   m_exeDir, m_appDataDir;

    Config m_cfg;
    bool   m_built = false;
    UINT   m_srcW = 0, m_srcH = 0, m_inW = 0, m_inH = 0, m_outW = 0, m_outH = 0;
    UINT   m_gridW[3] = {}, m_gridH[3] = {};
    UINT   m_lumaW[3] = {}, m_lumaH[3] = {};

    Tex m_color8;               // RGBA8 input colour (sRGB encoded)
    Tex m_luma[2][3];           // R8 luma pyramid, ping-pong by frame parity
    Tex m_nvofIn;               // BGRA8 optical-flow input written by the convert pass (R8 luma is the fallback)
    Tex m_bm[3], m_bc[3];       // block motion / cost per level
    Tex m_bmMed[2];             // median-filtered level-0 motion, ping-pong
    Tex m_flow, m_cost;         // NVOF results (D3D12 views of the shared textures)
    Tex m_flowBack, m_costBack; // NVOF backward pass (previous -> current)
    Tex m_mv, m_conf, m_depth;  // dense guidance
    Tex m_depthHist[2];         // temporally filtered depth, ping-pong by frame parity
    Tex m_dlaaOut, m_nrOut;
    Tex m_final;
    ComPtr<ID3D12Resource>      m_statsBuf;
    D3D12_CPU_DESCRIPTOR_HANDLE m_statsUav{};
    ComPtr<ID3D12Resource>      m_statsReadback[GpuContext::kFramesInFlight];
    UINT64                      m_statsFence[GpuContext::kFramesInFlight] = {};
    bool                        m_statsPending[GpuContext::kFramesInFlight] = {};

    // Preview hand-off: the composite pass writes one of three display buffers on the processing queue, the UI thread
    // samples the newest completed one from the present queue. SRVs live in the present heap.
    Tex            m_displayBuf[kDisplayBuffers];
    DescriptorPair m_displaySrv[kDisplayBuffers];
    int            m_displayTarget = -1;            // buffer written by the frame being recorded
    struct DisplayShared {
        int    published = -1;                      // newest finished buffer (-1: none)
        UINT64 publishedFence = 0;                  // processing fence that completes it
        UINT   generation = 0;                      // bumped whenever the buffers are recreated
        UINT   width = 0, height = 0;
        int    uiUsing = -1;                        // buffer the UI thread currently samples
        UINT   uiGeneration = 0;
        UINT64 uiRelease[kDisplayBuffers] = {};     // present-queue fence after which a buffer is free again
    };
    mutable std::mutex m_displayMutex;
    DisplayShared      m_disp;

    Tex                     m_nvofShared;       // shared input texture owned by the optical flow device (D3D12 view)
    DXGI_FORMAT             m_nvofFmt = DXGI_FORMAT_UNKNOWN;
    bool                    m_nvofReady = false;
    std::string             m_nvofError;

    // Depth estimation: GPU pre-pass -> readback -> worker thread (ONNX Runtime) -> upload -> GPU post-pass.
    ComPtr<ID3D12Resource>      m_depthInBuf;                 // planar float network input (UAV)
    D3D12_CPU_DESCRIPTOR_HANDLE m_depthInUav{};
    D3D12_RESOURCE_STATES       m_depthInState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource>      m_depthInReadback;
    UINT64                      m_depthInFence = 0;
    bool                        m_depthInPending = false;
    ComPtr<ID3D12Resource>      m_depthRawBuf;                // network output (SRV)
    D3D12_CPU_DESCRIPTOR_HANDLE m_depthRawSrv{};
    D3D12_RESOURCE_STATES       m_depthRawState = D3D12_RESOURCE_STATE_COPY_DEST;
    ComPtr<ID3D12Resource>      m_depthUpload[GpuContext::kFramesInFlight];
    UINT   m_depthInferW = 0, m_depthInferH = 0;              // network resolution
    UINT   m_depthRawW = 0, m_depthRawH = 0;                  // size of the uploaded network output
    bool   m_depthHaveRaw = false;
    bool   m_depthHistValid = false;
    bool   m_depthRestart = false;
    bool   m_depthModelExists = false;
    int    m_depthFramesSinceCapture = 1000;
    float  m_depthP02 = 0.0f, m_depthInvRange = 1.0f;         // smoothed normalisation range
    double m_depthLastResultTime = 0.0;
    double m_depthInferMs = 0.0;

    int    m_cur = 0;                 // ping-pong index
    bool   m_haveHistory = false;
    bool   m_hasDisplay = false;
    bool   m_resetRequested = true;
    std::atomic<bool> m_resetReq{false};
    std::atomic<bool> m_depthRestartReq{false};
    std::atomic<bool> m_nrDirtyReq{false};
    std::atomic<bool> m_dlaaDirtyReq{false};
    bool   m_nrDirty = false;
    bool   m_nrFailed = false;
    std::string m_nrError;
    bool   m_nrCreatedUseCore = false;
    int    m_nrCreatedPreset = -1;
    bool   m_dlaaFailed = false;
    std::string m_dlaaError;
    int    m_dlaaCreatedPreset = -1;
    double m_lastFreshTime = 0.0;
    double m_frameIntervalMs = 16.7;
    float  m_statAvgCost = 0.0f, m_statMaxCost = 0.0f, m_statAvgMotion = 0.0f;
    float  m_costEma = 0.0f;          // running average of the matching cost (scene-cut reference)
    bool   m_lastWasBlockMode = false;

    mutable std::mutex m_captureMutex;
    bool         m_captureRequested = false;
    std::wstring m_captureFolder;
    std::wstring m_captureBase;
    bool         m_captureKeepAlpha = true;
    bool         m_captureOriginal = false;
    std::vector<Readback> m_readbacks;

    PipelineStatus     m_status;          // processing thread's working copy
    mutable std::mutex m_statusMutex;
    PipelineStatus     m_statusShared;    // copy handed to the UI
};

} // namespace vdc
