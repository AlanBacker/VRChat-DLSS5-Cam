#include "gfx/Pipeline.h"
#include "core/Log.h"
#include "core/Util.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vdc {

namespace {
constexpr float kLambda[3] = { 0.0010f, 0.0015f, 0.0020f };   // motion penalty per level
constexpr float kMotionViewScale = 1.0f / 16.0f;
constexpr size_t kMaxReadbacks = 6;

UINT Even(UINT v) { return std::max(16u, v & ~1u); }
UINT AlignPatch(double v) { return std::max(14u, (UINT)std::lround(v / 14.0) * 14u); }   // Depth Anything patch size

constexpr float kDepthHistoryWeight = 0.6f;   // temporal blend towards the reprojected previous depth
constexpr float kDepthRangeSmoothing = 0.3f;  // EMA of the P02/P98 normalisation range
constexpr float kSceneCutRatio = 2.5f;        // cost must exceed this multiple of its running average
constexpr int   kNeuralWarmupFrames = 16;     // capture-only mode: fresh frames the neural pass sees before the capture
constexpr double kNeuralWarmupTimeout = 3.0;  // ... and the longest wait for them (source stalled) in seconds
constexpr UINT  kNeuralCheckGridW = 480, kNeuralCheckGridH = 270;   // sample grid of the neural output check

// The 310.8 runtime build only carries code for RTX 50 (Blackwell): name the likely cause on older cards.
void LogRuntimeGenerationHint(const GpuContext& gpu, const std::string& runtimeVersion) {
    const int gen = gpu.Dev().Info().RtxGeneration();
    if (gen >= 2 && gen <= 4 && (runtimeVersion.empty() || runtimeVersion.rfind("310.8", 0) == 0))
        Log::Warn("DLSSNR: %s is an RTX %d0 series GPU; the 310.8 runtime build only contains code for RTX 50 (Blackwell), so the neural pass cannot start on it",
                  WideToUtf8(gpu.Dev().Info().name).c_str(), gen);
}

bool CreateBuffer(ID3D12Device* dev, D3D12_HEAP_TYPE heap, UINT64 bytes, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                  const wchar_t* name, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = std::max<UINT64>(bytes, 256); bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bd.Flags = flags;
    const HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(out.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) { Log::Hr(LogLevel::Error, WideToUtf8(name).c_str(), hr); return false; }
    out->SetName(name);
    return true;
}
}

// --- resources ----------------------------------------------------------------------------

bool Pipeline::CreateTex(GpuContext& gpu, Tex& t, UINT w, UINT h, DXGI_FORMAT fmt, bool uav, const wchar_t* name) {
    ReleaseTex(gpu, t);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w; desc.Height = h; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = fmt; desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    const HRESULT hr = gpu.Dev().D3D12()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                               nullptr, IID_PPV_ARGS(&t.res));
    if (FAILED(hr)) { Log::Hr(LogLevel::Error, StrPrintf("CreateCommittedResource(%ls)", name).c_str(), hr); return false; }
    t.res->SetName(name);
    t.w = w; t.h = h; t.fmt = fmt; t.state = D3D12_RESOURCE_STATE_COMMON;
    t.srv = gpu.Dev().AllocStaging();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = fmt; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    gpu.Dev().D3D12()->CreateShaderResourceView(t.res.Get(), &sd, t.srv);
    if (uav) {
        t.uav = gpu.Dev().AllocStaging();
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = fmt; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        gpu.Dev().D3D12()->CreateUnorderedAccessView(t.res.Get(), nullptr, &ud, t.uav);
    }
    return true;
}

void Pipeline::ReleaseTex(GpuContext& gpu, Tex& t) {
    if (t.srv.ptr) { gpu.Dev().FreeStaging(t.srv); t.srv = {}; }
    if (t.uav.ptr) { gpu.Dev().FreeStaging(t.uav); t.uav = {}; }
    if (t.res) { gpu.DeferRelease(t.res); t.res.Reset(); }
    t.w = t.h = 0; t.fmt = DXGI_FORMAT_UNKNOWN; t.state = D3D12_RESOURCE_STATE_COMMON;
}

bool Pipeline::WrapShared(GpuContext& gpu, Tex& t, ID3D12Resource* res, UINT w, UINT h, DXGI_FORMAT fmt, const wchar_t* name) {
    ReleaseTex(gpu, t);
    if (!res) return false;
    t.res = res;
    t.res->SetName(name);
    t.w = w; t.h = h; t.fmt = fmt; t.state = D3D12_RESOURCE_STATE_COMMON;
    t.srv = gpu.Dev().AllocStaging();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = fmt; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    gpu.Dev().D3D12()->CreateShaderResourceView(t.res.Get(), &sd, t.srv);
    return true;
}

void Pipeline::Transition(ID3D12GraphicsCommandList* cmd, Tex& t, D3D12_RESOURCE_STATES state) {
    if (!t.res) return;
    if (t.state == state) {
        if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) Device::UavBarrier(cmd, t.res.Get());
        return;
    }
    Device::Barrier(cmd, t.res.Get(), t.state, state);
    t.state = state;
}

void Pipeline::ReleaseFeatures(GpuContext& gpu) {
    if (m_nr.Created() || m_dlaa.Created()) gpu.WaitIdle();
    m_nr.Release(m_ngx);
    m_dlaa.Release(m_ngx);
    m_nrCreatedPreset = -1;
    m_dlaaCreatedPreset = -1;
}

void Pipeline::ReleaseResources(GpuContext& gpu) {
    gpu.WaitIdle();
    ReleaseFeatures(gpu);
    ReleaseTex(gpu, m_nvofShared);
    ReleaseTex(gpu, m_flow); ReleaseTex(gpu, m_cost);
    ReleaseTex(gpu, m_flowBack); ReleaseTex(gpu, m_costBack);
    m_nvof.Shutdown();
    m_nvofReady = false;
    ReleaseDepthResources(gpu);
    ReleaseTex(gpu, m_color8);
    for (auto& set : m_luma) for (auto& t : set) ReleaseTex(gpu, t);
    ReleaseTex(gpu, m_nvofIn);
    for (int i = 0; i < 3; ++i) { ReleaseTex(gpu, m_bm[i]); ReleaseTex(gpu, m_bc[i]); }
    for (auto& t : m_bmMed) ReleaseTex(gpu, t);
    ReleaseTex(gpu, m_mv); ReleaseTex(gpu, m_conf); ReleaseTex(gpu, m_depth);
    ReleaseTex(gpu, m_dlaaOut); ReleaseTex(gpu, m_nrOut); ReleaseTex(gpu, m_nrIn);
    m_nrInExposed = false;
    ReleaseTex(gpu, m_final);
    RetireDisplayBuffers(gpu);
    if (m_statsUav.ptr) { gpu.Dev().FreeStaging(m_statsUav); m_statsUav = {}; }
    if (m_statsBuf) { gpu.DeferRelease(m_statsBuf); m_statsBuf.Reset(); }
    for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i) {
        if (m_statsReadback[i]) { gpu.DeferRelease(m_statsReadback[i]); m_statsReadback[i].Reset(); }
        m_statsPending[i] = false; m_statsFence[i] = 0;
    }
    for (auto& rb : m_readbacks) { if (rb.buffer) gpu.DeferRelease(rb.buffer); }
    m_readbacks.clear();
    m_built = false;
    m_hasDisplay = false;
    m_haveHistory = false;
}

Pipeline::Config Pipeline::ComputeConfig(const SourceFrame& src, const Settings& s) const {
    Config c;
    c.srcW = src.width; c.srcH = src.height; c.srcFmt = src.format;
    if (!c.srcW || !c.srcH) return c;
    c.outW = c.srcW; c.outH = c.srcH;
    if (s.customResolution) {
        c.outW = Even((UINT)s.customWidth);
        c.outH = s.keepAspect ? Even((UINT)std::lround((double)c.outW * c.srcH / c.srcW)) : Even((UINT)s.customHeight);
    }
    const bool upscale = s.nrUpscale && s.customResolution && (c.outW > c.srcW || c.outH > c.srcH);
    c.inW = upscale ? c.srcW : c.outW;
    c.inH = upscale ? c.srcH : c.outH;
    c.nvof = s.motionMode == MotionNvOpticalFlow && !src.stillImage;   // a still picture has no motion
    c.nvofGrid = (UINT)s.nvofGrid;
    c.nvofPerf = (UINT)s.nvofPerf;
    c.nvofBidir = s.nvofBidirectional;
    c.depthEst = s.depthMode == DepthEstimated;
    c.depthLongSide = (UINT)s.depthLongSide;
    c.depthModel = DepthModelPath(s);
    return c;
}

std::wstring Pipeline::DepthModelPath(const Settings& s) const {
    return s.depthModelPath.empty() ? DepthEstimator::DefaultModelPath(m_exeDir) : Utf8ToWide(s.depthModelPath);
}

void Pipeline::ReleaseDepthResources(GpuContext& gpu) {
    for (auto& t : m_depthHist) ReleaseTex(gpu, t);
    if (m_depthInUav.ptr) { gpu.Dev().FreeStaging(m_depthInUav); m_depthInUav = {}; }
    if (m_depthRawSrv.ptr) { gpu.Dev().FreeStaging(m_depthRawSrv); m_depthRawSrv = {}; }
    if (m_depthInBuf) { gpu.DeferRelease(m_depthInBuf); m_depthInBuf.Reset(); }
    if (m_depthInReadback) { gpu.DeferRelease(m_depthInReadback); m_depthInReadback.Reset(); }
    if (m_depthRawBuf) { gpu.DeferRelease(m_depthRawBuf); m_depthRawBuf.Reset(); }
    for (auto& u : m_depthUpload) { if (u) { gpu.DeferRelease(u); u.Reset(); } }
    m_depthInPending = false; m_depthInFence = 0;
    m_depthInState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_depthRawState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_depthHaveRaw = false; m_depthHistValid = false; m_depthStillCaptured = false;
    m_depthRawW = m_depthRawH = 0;
    m_depthFramesSinceCapture = 1000;
}

bool Pipeline::CreateDepthResources(GpuContext& gpu, const Config& cfg) {
    ID3D12Device* dev = gpu.Dev().D3D12();
    const double scale = (double)cfg.depthLongSide / (double)std::max(m_inW, m_inH);
    m_depthInferW = AlignPatch(m_inW * scale);
    m_depthInferH = AlignPatch(m_inH * scale);
    const UINT64 pixels = (UINT64)m_depthInferW * m_depthInferH;
    bool ok = true;
    for (int i = 0; i < 2; ++i) ok &= CreateTex(gpu, m_depthHist[i], m_inW, m_inH, DXGI_FORMAT_R32_FLOAT, true, L"depthHistory");
    ok &= CreateBuffer(dev, D3D12_HEAP_TYPE_DEFAULT, pixels * 3 * sizeof(float), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"depthNetInput", m_depthInBuf);
    ok &= CreateBuffer(dev, D3D12_HEAP_TYPE_READBACK, pixels * 3 * sizeof(float), D3D12_RESOURCE_FLAG_NONE,
                       D3D12_RESOURCE_STATE_COPY_DEST, L"depthNetInputReadback", m_depthInReadback);
    ok &= CreateBuffer(dev, D3D12_HEAP_TYPE_DEFAULT, pixels * sizeof(float), D3D12_RESOURCE_FLAG_NONE,
                       D3D12_RESOURCE_STATE_COPY_DEST, L"depthNetOutput", m_depthRawBuf);
    for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i)
        ok &= CreateBuffer(dev, D3D12_HEAP_TYPE_UPLOAD, pixels * sizeof(float), D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_GENERIC_READ, L"depthNetOutputUpload", m_depthUpload[i]);
    if (!ok) { ReleaseDepthResources(gpu); return false; }
    m_depthInUav = gpu.Dev().AllocStaging();
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = (UINT)(pixels * 3); ud.Buffer.StructureByteStride = 4;
        dev->CreateUnorderedAccessView(m_depthInBuf.Get(), nullptr, &ud, m_depthInUav);
    }
    m_depthRawSrv = gpu.Dev().AllocStaging();
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_UNKNOWN; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.NumElements = (UINT)pixels; sd.Buffer.StructureByteStride = 4;
        dev->CreateShaderResourceView(m_depthRawBuf.Get(), &sd, m_depthRawSrv);
    }
    m_depthInState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    m_depthRawState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_depthModelExists = FileExists(cfg.depthModel);
    // The worker survives resource rebuilds as long as the model and the network resolution stay the same.
    if (m_depthRestart || !m_depthEst.Matches(cfg.depthModel, m_depthInferW, m_depthInferH)) {
        m_depthRestart = false;
        m_depthEst.Start(gpu.Dev(), m_exeDir, cfg.depthModel, m_depthInferW, m_depthInferH);
    }
    return true;
}

bool Pipeline::Rebuild(GpuContext& gpu, const Config& cfg) {
    ReleaseResources(gpu);
    m_cfg = cfg;
    m_srcW = cfg.srcW; m_srcH = cfg.srcH; m_inW = cfg.inW; m_inH = cfg.inH; m_outW = cfg.outW; m_outH = cfg.outH;
    ID3D12Device* dev = gpu.Dev().D3D12();
    bool ok = true;
    ok &= CreateTex(gpu, m_color8, m_inW, m_inH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"color8");
    for (int l = 0; l < 3; ++l) {
        m_lumaW[l] = std::max(1u, (m_inW + (1u << l) - 1) >> l);
        m_lumaH[l] = std::max(1u, (m_inH + (1u << l) - 1) >> l);
        m_gridW[l] = (m_lumaW[l] + 7) / 8;
        m_gridH[l] = (m_lumaH[l] + 7) / 8;
        for (int p = 0; p < 2; ++p) ok &= CreateTex(gpu, m_luma[p][l], m_lumaW[l], m_lumaH[l], DXGI_FORMAT_R8_UNORM, true, L"luma");
        ok &= CreateTex(gpu, m_bm[l], m_gridW[l], m_gridH[l], DXGI_FORMAT_R32G32_FLOAT, true, L"blockMv");
        ok &= CreateTex(gpu, m_bc[l], m_gridW[l], m_gridH[l], DXGI_FORMAT_R32_FLOAT, true, L"blockCost");
    }
    for (int p = 0; p < 2; ++p) ok &= CreateTex(gpu, m_bmMed[p], m_gridW[0], m_gridH[0], DXGI_FORMAT_R32G32_FLOAT, true, L"blockMvMedian");
    ok &= CreateTex(gpu, m_mv, m_inW, m_inH, DXGI_FORMAT_R16G16_FLOAT, true, L"motionVectors");
    ok &= CreateTex(gpu, m_conf, m_inW, m_inH, DXGI_FORMAT_R8_UNORM, true, L"confidence");
    ok &= CreateTex(gpu, m_depth, m_inW, m_inH, DXGI_FORMAT_R32_FLOAT, true, L"depth");
    ok &= CreateTex(gpu, m_dlaaOut, m_inW, m_inH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"dlaaOut");
    ok &= CreateTex(gpu, m_nrOut, m_outW, m_outH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"nrOut");
    ok &= CreateTex(gpu, m_nrIn, m_inW, m_inH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"nrIn");
    ok &= CreateTex(gpu, m_final, m_outW, m_outH, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"final");
    ok &= CreateDisplayBuffers(gpu, m_outW, m_outH);
    if (!ok) { ReleaseResources(gpu); return false; }

    // Stats buffer + readbacks.
    {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = 256; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        HRESULT hr = dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_statsBuf));
        if (FAILED(hr)) { Log::Hr(LogLevel::Error, "stats buffer", hr); ReleaseResources(gpu); return false; }
        m_statsBuf->SetName(L"stats");
        m_statsUav = gpu.Dev().AllocStaging();
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = 8; ud.Buffer.StructureByteStride = 4;
        dev->CreateUnorderedAccessView(m_statsBuf.Get(), nullptr, &ud, m_statsUav);
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        bd.Flags = D3D12_RESOURCE_FLAG_NONE;
        for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i) {
            hr = dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_statsReadback[i]));
            if (FAILED(hr)) { Log::Hr(LogLevel::Error, "stats readback", hr); ReleaseResources(gpu); return false; }
        }
    }

    // Optical flow session on its own native D3D11 device. BGRA8 input written by the convert pass is preferred (the
    // engine's native colour format); the 8-bit luma pyramid level is the fallback. Results arrive through shared textures.
    m_nvofReady = false;
    m_nvofError.clear();
    m_nvofFmt = DXGI_FORMAT_UNKNOWN;
    if (cfg.nvof) {
        std::string err;
        const bool bgraOk = gpu.Dev().TypedUavStoreSupported(DXGI_FORMAT_B8G8R8A8_UNORM);
        if (bgraOk && m_nvof.Init(gpu, m_inW, m_inH, DXGI_FORMAT_B8G8R8A8_UNORM, cfg.nvofGrid, cfg.nvofPerf, cfg.nvofBidir, err)) {
            m_nvofFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
        } else {
            if (bgraOk) Log::Warn("NVOF: BGRA8 input rejected (%s), trying R8 luma", err.c_str());
            if (m_nvof.Init(gpu, m_inW, m_inH, DXGI_FORMAT_R8_UNORM, cfg.nvofGrid, cfg.nvofPerf, cfg.nvofBidir, err)) {
                m_nvofFmt = DXGI_FORMAT_R8_UNORM;
            } else {
                m_nvofError = err;
            }
        }
        if (m_nvofFmt != DXGI_FORMAT_UNKNOWN) {
            bool nvofOk = true;
            if (m_nvofFmt == DXGI_FORMAT_B8G8R8A8_UNORM)
                nvofOk &= CreateTex(gpu, m_nvofIn, m_inW, m_inH, DXGI_FORMAT_B8G8R8A8_UNORM, true, L"nvofIn");
            const UINT fw = m_nvof.FlowWidth(), fh = m_nvof.FlowHeight();
            nvofOk &= WrapShared(gpu, m_nvofShared, m_nvof.Input12(), m_inW, m_inH, m_nvofFmt, L"nvofSharedInput");
            nvofOk &= WrapShared(gpu, m_flow, m_nvof.Flow12(), fw, fh, DXGI_FORMAT_R16G16_SINT, L"nvofFlow");
            if (m_nvof.HasCost()) nvofOk &= WrapShared(gpu, m_cost, m_nvof.Cost12(), fw, fh, DXGI_FORMAT_R8_UINT, L"nvofCost");
            if (m_nvof.Bidirectional()) {
                nvofOk &= WrapShared(gpu, m_flowBack, m_nvof.FlowBack12(), fw, fh, DXGI_FORMAT_R16G16_SINT, L"nvofFlowBack");
                if (m_nvof.HasCost()) nvofOk &= WrapShared(gpu, m_costBack, m_nvof.CostBack12(), fw, fh, DXGI_FORMAT_R8_UINT, L"nvofCostBack");
            }
            if (nvofOk) {
                m_nvofReady = true;
            } else {
                m_nvofError = "failed to create optical flow interop views";
                ReleaseTex(gpu, m_nvofShared);
                ReleaseTex(gpu, m_flow); ReleaseTex(gpu, m_cost);
                ReleaseTex(gpu, m_flowBack); ReleaseTex(gpu, m_costBack);
                m_nvof.Shutdown();
            }
        }
        if (!m_nvofReady) Log::Warn("NVOF unavailable, falling back to block matching: %s", m_nvofError.c_str());
    } else {
        m_nvof.Shutdown();
    }

    // Estimated depth (Depth Anything V2 through ONNX Runtime). Failure is non-fatal: zero depth is used instead.
    if (cfg.depthEst) {
        if (!CreateDepthResources(gpu, cfg)) Log::Warn("Depth estimation resources could not be created; using zero depth");
    } else {
        m_depthEst.Stop();
    }

    m_built = true;
    m_haveHistory = false;
    m_resetRequested = true;
    m_costEma = 0.0f;
    m_cur = 0;
    const std::string nvofInfo = m_nvofReady
        ? StrPrintf(" (NVOF grid %u%s)", m_nvof.Grid(),
                    m_nvof.Bidirectional() ? (m_nvof.SinglePassBidirectional() ? ", bidirectional" : ", bidirectional two-pass") : "")
        : std::string();
    Log::Info("Pipeline resources: source %ux%u, input %ux%u, output %ux%u%s%s", m_srcW, m_srcH, m_inW, m_inH, m_outW, m_outH,
              nvofInfo.c_str(), m_depthInBuf ? StrPrintf(" (depth network %ux%u)", m_depthInferW, m_depthInferH).c_str() : "");
    return true;
}

// --- lifecycle ----------------------------------------------------------------------------

bool Pipeline::Init(Device& device, const std::wstring& exeDir, const std::wstring& appDataDir, std::wstring& error) {
    m_exeDir = exeDir;
    m_appDataDir = appDataDir;
    if (!m_shaders.Init(device, error)) return false;
    m_ngx.Init(device, exeDir, appDataDir);   // failure is non-fatal: the app still works as a viewer
    m_status.nvofAvailable = NvOpticalFlow::LibraryAvailable();
    return true;
}

void Pipeline::Shutdown(Device& device) {
    GpuContext& gpu = device.Proc();
    ReleaseResources(gpu);
    m_depthEst.Stop();
    UnloadNrRuntime(gpu);
    device.Ui().WaitIdle();
    gpu.WaitIdle();
    m_ngx.Shutdown();
    m_shaders.Shutdown();
}

bool Pipeline::LoadNrRuntime(GpuContext& gpu, const std::wstring& dllPath, std::string& error) {
    if (m_nr.RuntimeLoaded() && m_nr.RuntimePath() == dllPath) return true;
    UnloadNrRuntime(gpu);
    m_nrFailed = false;
    m_nrError.clear();
    if (!m_nr.LoadRuntime(gpu.Dev().D3D12(), dllPath, m_exeDir, error)) {
        m_nrError = error;
        Log::Error("DLSSNR runtime: %s", error.c_str());
        return false;
    }
    return true;
}

void Pipeline::UnloadNrRuntime(GpuContext& gpu) {
    if (m_nr.Created()) { gpu.WaitIdle(); m_nr.Release(m_ngx); m_nrCreatedPreset = -1; }
    if (m_nr.RuntimeLoaded()) { gpu.WaitIdle(); m_nr.UnloadRuntime(); }
}

void Pipeline::RequestCapture(const std::wstring& folder, bool keepAlpha, bool saveOriginal, const std::wstring& baseName) {
    std::lock_guard<std::mutex> lock(m_captureMutex);
    m_captureRequested = true;
    m_captureFolder = folder;
    m_captureBase = baseName;
    m_captureKeepAlpha = keepAlpha;
    m_captureOriginal = saveOriginal;
}

bool Pipeline::CapturePending() const {
    std::lock_guard<std::mutex> lock(m_captureMutex);
    return m_captureRequested;
}

bool Pipeline::NeedsFrame() const {
    return m_resetReq.load() || m_depthRestartReq.load() || m_nrDirtyReq.load() || m_dlaaDirtyReq.load() ||
           m_displayRetryReq.load() || CapturePending();
}

void Pipeline::PublishStatus(GpuContext& gpu) {
    for (UINT t = 0; t < (UINT)GpuTimer::Count; ++t) m_status.gpuMs[t] = gpu.TimerMs((GpuTimer)t);
    // The guidance figure covers both halves (pyramid / matching before the optical flow, densify / depth after it).
    m_status.gpuMs[(UINT)GpuTimer::Guidance] += m_status.gpuMs[(UINT)GpuTimer::Densify];
    m_status.hasDisplay = m_hasDisplay;
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_statusShared = m_status;
}

void Pipeline::StatusSnapshot(PipelineStatus& out) const {
    std::lock_guard<std::mutex> lock(m_statusMutex);
    out = m_statusShared;
}

// --- preview hand-off -------------------------------------------------------------------------

bool Pipeline::CreateDisplayBuffers(GpuContext& gpu, UINT w, UINT h) {
    RetireDisplayBuffers(gpu);
    GpuContext& ui = gpu.Dev().Ui();
    ID3D12Device* dev = gpu.Dev().D3D12();
    for (UINT i = 0; i < kDisplayBuffers; ++i) {
        if (!CreateTex(gpu, m_displayBuf[i], w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true, L"display")) return false;
        m_displaySrv[i] = ui.AllocStatic();
        if (!m_displaySrv[i].Valid()) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(m_displayBuf[i].res.Get(), &sd, m_displaySrv[i].cpu);
    }
    std::lock_guard<std::mutex> lock(m_displayMutex);
    m_disp.width = w; m_disp.height = h;
    return true;
}

void Pipeline::RetireDisplayBuffers(GpuContext& gpu) {
    // The UI thread may still have frames in flight that sample these: hand the objects to the present context, which
    // frees them once the fence of its next frame passes. Under the lock so the UI never acquires a retired buffer.
    GpuContext& ui = gpu.Dev().Ui();
    std::lock_guard<std::mutex> lock(m_displayMutex);
    for (UINT i = 0; i < kDisplayBuffers; ++i) {
        Tex& t = m_displayBuf[i];
        if (t.srv.ptr) { gpu.Dev().FreeStaging(t.srv); t.srv = {}; }
        if (t.uav.ptr) { gpu.Dev().FreeStaging(t.uav); t.uav = {}; }
        if (t.res) { ui.DeferRelease(t.res); t.res.Reset(); }
        t.w = t.h = 0; t.fmt = DXGI_FORMAT_UNKNOWN; t.state = D3D12_RESOURCE_STATE_COMMON;
        if (m_displaySrv[i].Valid()) { ui.DeferFreeStatic(m_displaySrv[i]); m_displaySrv[i] = {}; }
        m_disp.uiRelease[i] = 0;
    }
    m_disp.pendingCount = 0;
    m_disp.starved = false;
    m_disp.width = m_disp.height = 0;
    ++m_disp.generation;
    m_displayTarget = -1;
}

DisplayView Pipeline::AcquireDisplay(GpuContext& ui) {
    DisplayView v;
    GpuContext& proc = ui.Dev().Proc();
    std::lock_guard<std::mutex> lock(m_displayMutex);
    if (m_disp.uiUsing >= 0 && m_disp.uiGeneration != m_disp.generation) m_disp.uiUsing = -1;   // buffers were rebuilt
    // Take the newest composite whose processing fence has passed; everything older is superseded and free again.
    int newest = -1;
    for (int i = (int)m_disp.pendingCount - 1; i >= 0; --i) {
        if (proc.IsFenceComplete(m_disp.pending[i].fence)) { newest = i; break; }
    }
    if (newest >= 0) {
        m_disp.uiUsing = m_disp.pending[newest].buffer;
        m_disp.uiGeneration = m_disp.generation;
        const UINT remaining = m_disp.pendingCount - (UINT)newest - 1;
        for (UINT i = 0; i < remaining; ++i) m_disp.pending[i] = m_disp.pending[(UINT)newest + 1 + i];
        m_disp.pendingCount = remaining;
        if (m_disp.starved) { m_disp.starved = false; m_displayRetryReq = true; }   // a buffer is free: composite again
    }
    if (m_disp.uiUsing < 0 || !m_displaySrv[m_disp.uiUsing].Valid()) return v;
    v.srv = m_displaySrv[m_disp.uiUsing].gpu;
    v.width = m_disp.width;
    v.height = m_disp.height;
    v.valid = v.width > 0 && v.height > 0;
    return v;
}

void Pipeline::ReleaseDisplay(UINT64 uiFenceValue) {
    std::lock_guard<std::mutex> lock(m_displayMutex);
    if (m_disp.uiUsing >= 0) m_disp.uiRelease[m_disp.uiUsing] = uiFenceValue;
}

// --- passes -------------------------------------------------------------------------------

void Pipeline::RunConvert(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const SourceFrame& src, const Settings& s, bool writeNvof) {
    gpu.TimerBegin(cmd, GpuTimer::Convert);
    Tex& luma0 = m_luma[m_cur][0];
    Transition(cmd, m_color8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, luma0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (writeNvof) Transition(cmd, m_nvofIn, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchDesc d;
    d.id = ShaderId::Convert;
    d.constants.srcWidth = m_srcW; d.constants.srcHeight = m_srcH;
    d.constants.dstWidth = m_inW; d.constants.dstHeight = m_inH;
    UINT flags = 0;
    if (src.linear) flags |= 1;
    if (m_inW != m_srcW || m_inH != m_srcH) {
        flags |= 2;
        if (m_inW < m_srcW || m_inH < m_srcH) flags |= 16;
    }
    if (writeNvof) flags |= 4;
    const DXGI_FORMAT vf = src.viewFormat;
    if (vf == DXGI_FORMAT_R16G16B16A16_FLOAT || vf == DXGI_FORMAT_R32G32B32A32_FLOAT || vf == DXGI_FORMAT_R11G11B10_FLOAT) {
        // Scene-linear HDR feed: bring it to paper white and roll off highlights before the SDR neural pass.
        flags |= 32;
        d.constants.paramA = 1.0f / std::max(s.hdrPaperWhite, 0.01f);
        d.constants.paramB = std::clamp(s.hdrHighlightCompression, 0.0f, 1.0f);
    }
    d.constants.flags = flags;
    d.srv[0] = src.srv;
    d.uav[0] = m_color8.uav;
    d.uav[1] = luma0.uav;
    d.uav[2] = writeNvof ? m_nvofIn.uav : D3D12_CPU_DESCRIPTOR_HANDLE{};
    d.groupsX = Shaders::Groups(m_inW, 8); d.groupsY = Shaders::Groups(m_inH, 8);
    m_shaders.Dispatch(cmd, gpu, d);
    gpu.TimerEnd(cmd, GpuTimer::Convert);
}

void Pipeline::RunGuidance(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, int motionMode, bool haveHistory) {
    const int cur = m_cur, prev = 1 - m_cur;
    // Luma pyramid.
    for (int l = 1; l < 3; ++l) {
        Transition(cmd, m_luma[cur][l - 1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_luma[cur][l], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchDesc d;
        d.id = ShaderId::Downsample;
        d.constants.srcWidth = m_lumaW[l - 1]; d.constants.srcHeight = m_lumaH[l - 1];
        d.constants.dstWidth = m_lumaW[l]; d.constants.dstHeight = m_lumaH[l];
        d.srv[0] = m_luma[cur][l - 1].srv;
        d.uav[0] = m_luma[cur][l].uav;
        d.groupsX = Shaders::Groups(m_lumaW[l], 8); d.groupsY = Shaders::Groups(m_lumaH[l], 8);
        m_shaders.Dispatch(cmd, gpu, d);
    }
    Transition(cmd, m_luma[cur][2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for (int l = 0; l < 3; ++l) Transition(cmd, m_luma[prev][l], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_bmMed[prev], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // Block matching: level 2 full search is always run (scene-cut statistics), finer levels only in compute mode.
    const int levels = (motionMode == MotionCompute) ? 3 : 1;
    for (int i = 0; i < levels; ++i) {
        const int l = 2 - i;
        Transition(cmd, m_bm[l], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(cmd, m_bc[l], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (l < 2) Transition(cmd, m_bm[l + 1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        DispatchDesc d;
        d.id = ShaderId::BlockMatch;
        d.constants.srcWidth = m_lumaW[l]; d.constants.srcHeight = m_lumaH[l];
        d.constants.level = (UINT)l;
        UINT flags = 0;
        if (l == 2) { flags |= 1; d.constants.intA = (UINT)s.searchRadius; }
        else { flags |= 2; d.constants.intB = (l == 1) ? 2u : 1u; if (l == 0) flags |= 8; }
        if (haveHistory) flags |= 4;
        d.constants.flags = flags;
        d.constants.paramB = kLambda[l];
        d.constants.extra0[0] = (float)m_gridW[0]; d.constants.extra0[1] = (float)m_gridH[0];
        if (l < 2) { d.constants.extra1[0] = (float)m_gridW[l + 1]; d.constants.extra1[1] = (float)m_gridH[l + 1]; }
        d.srv[0] = m_luma[cur][l].srv;
        d.srv[1] = m_luma[prev][l].srv;
        d.srv[2] = (l < 2) ? m_bm[l + 1].srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
        d.srv[3] = m_bmMed[prev].srv;
        d.uav[0] = m_bm[l].uav;
        d.uav[1] = m_bc[l].uav;
        d.groupsX = m_gridW[l]; d.groupsY = m_gridH[l];
        m_shaders.Dispatch(cmd, gpu, d);
    }
    if (motionMode == MotionCompute) {
        Transition(cmd, m_bm[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_bmMed[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        DispatchDesc d;
        d.id = ShaderId::MedianMv;
        d.constants.dstWidth = m_gridW[0]; d.constants.dstHeight = m_gridH[0];
        d.srv[0] = m_bm[0].srv;
        d.uav[0] = m_bmMed[cur].uav;
        d.groupsX = Shaders::Groups(m_gridW[0], 8); d.groupsY = Shaders::Groups(m_gridH[0], 8);
        m_shaders.Dispatch(cmd, gpu, d);
    }
}

bool Pipeline::RunOpticalFlow(GpuContext& gpu, ID3D12GraphicsCommandList*& cmd, bool resetHints) {
    if (!m_nvofReady) return false;
    // Hand the current frame to the optical flow device. The shared textures rest in COMMON whenever the D3D11 side owns them,
    // so every D3D12 access is bracketed by explicit transitions inside one command list.
    Tex& src = (m_nvofFmt == DXGI_FORMAT_R8_UNORM) ? m_luma[m_cur][0] : m_nvofIn;
    Transition(cmd, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(cmd, m_nvofShared, D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(m_nvofShared.res.Get(), src.res.Get());
    Transition(cmd, m_nvofShared, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmd, m_flow, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmd, m_cost, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmd, m_flowBack, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmd, m_costBack, D3D12_RESOURCE_STATE_COMMON);
    gpu.TimerBegin(cmd, GpuTimer::OpticalFlow);
    cmd = gpu.SubmitAndContinue();

    // Runs on the private D3D11 device, ordered behind the submission above and ahead of the next one by the shared fence.
    std::string err;
    const bool ok = m_nvof.Execute(gpu, resetHints, err);
    if (!ok && !err.empty()) {
        m_nvofError = err;
        Log::Warn("NVOF execute failed: %s", err.c_str());
    }
    gpu.TimerEnd(cmd, GpuTimer::OpticalFlow);
    return ok;
}

void Pipeline::RunDensify(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, int mode) {
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_conf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchDesc d;
    d.id = ShaderId::Densify;
    d.constants.dstWidth = m_inW; d.constants.dstHeight = m_inH;
    d.constants.paramA = s.motionConfidence;
    d.constants.intB = (UINT)s.depthMode;
    d.constants.scaleX = 1.0f; d.constants.scaleY = 1.0f;
    if (mode == MotionCompute) {
        Transition(cmd, m_bmMed[m_cur], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_bc[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d.constants.flags = 2;
        d.constants.srcWidth = m_gridW[0]; d.constants.srcHeight = m_gridH[0];
        d.srv[0] = m_bmMed[m_cur].srv;
        d.srv[1] = m_bc[0].srv;
    } else if (mode == MotionNvOpticalFlow) {
        Transition(cmd, m_flow, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_cost, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(cmd, m_flowBack, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d.constants.flags = 4;
        d.constants.intA = m_nvof.Grid();
        d.constants.srcWidth = m_nvof.FlowWidth(); d.constants.srcHeight = m_nvof.FlowHeight();
        d.srv[2] = m_flow.srv;
        d.srv[3] = m_cost.Valid() ? m_cost.srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
        if (m_flowBack.Valid()) { d.constants.flags |= 8; d.srv[4] = m_flowBack.srv; }
    } else {
        d.constants.flags = 1;
    }
    d.uav[0] = m_mv.uav; d.uav[1] = m_conf.uav; d.uav[2] = m_depth.uav;
    d.groupsX = Shaders::Groups(m_inW, 8); d.groupsY = Shaders::Groups(m_inH, 8);
    m_shaders.Dispatch(cmd, gpu, d);
    if (mode == MotionNvOpticalFlow) {
        // Back to COMMON: the optical flow device writes these textures before the next submission.
        Transition(cmd, m_flow, D3D12_RESOURCE_STATE_COMMON);
        Transition(cmd, m_cost, D3D12_RESOURCE_STATE_COMMON);
        Transition(cmd, m_flowBack, D3D12_RESOURCE_STATE_COMMON);
    }
}

void Pipeline::RunStats(GpuContext& gpu, ID3D12GraphicsCommandList* cmd) {
    Transition(cmd, m_bc[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_bm[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Device::UavBarrier(cmd, m_statsBuf.Get());
    DispatchDesc d;
    d.id = ShaderId::Stats;
    d.constants.srcWidth = m_gridW[2]; d.constants.srcHeight = m_gridH[2];
    d.srv[0] = m_bc[2].srv;
    d.srv[1] = m_bm[2].srv;
    d.uav[0] = m_statsUav;
    m_shaders.Dispatch(cmd, gpu, d);
    m_statsGuidanceThisFrame = true;
}

void Pipeline::RunNeuralCheck(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, Tex& input) {
    // Sparse comparison of the neural output with the picture the pass saw. A runtime that reports success but
    // delivers a black or unchanged picture (seen with modified runtime builds) is invisible to every result code;
    // this catches it, and the numbers feed the "Output change" readout.
    Transition(cmd, m_nrOut, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Device::UavBarrier(cmd, m_statsBuf.Get());
    DispatchDesc d;
    d.id = ShaderId::NeuralCheck;
    d.constants.srcWidth = kNeuralCheckGridW; d.constants.srcHeight = kNeuralCheckGridH;
    d.srv[0] = m_nrOut.srv;
    d.srv[1] = input.srv;
    d.uav[0] = m_statsUav;
    m_shaders.Dispatch(cmd, gpu, d);
    m_statsNeuralThisFrame = true;
}

void Pipeline::CopyStats(GpuContext& gpu, ID3D12GraphicsCommandList* cmd) {
    if (!m_statsGuidanceThisFrame && !m_statsNeuralThisFrame) return;
    const UINT slot = gpu.FrameIndex();
    Device::Barrier(cmd, m_statsBuf.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->CopyBufferRegion(m_statsReadback[slot].Get(), 0, m_statsBuf.Get(), 0, 32);
    Device::Barrier(cmd, m_statsBuf.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_statsPending[slot] = true;
    m_statsFence[slot] = 0;
    m_statsHasGuidance[slot] = m_statsGuidanceThisFrame;
    m_statsHasNeural[slot] = m_statsNeuralThisFrame;
    m_statsGuidanceThisFrame = false;
    m_statsNeuralThisFrame = false;
}

void Pipeline::UpdateNeuralCheck(float delta, float outLuma, float inLuma) {
    m_nrOutDelta = delta; m_nrOutLuma = outLuma; m_nrInLuma = inLuma;
    int state = 1;
    if (outLuma < 0.002f && inLuma > 0.02f) state = 2;                  // black picture out of a lit one
    else if (delta < 0.0005f && m_nrMaxStrength > 0.05f) state = 3;     // nothing changed although strengths are set
    if (state == m_nrOutState) return;
    m_nrOutState = state;
    const char* verdict = state == 2 ? "output is black" : state == 3 ? "output equals the input (no effect)" : "ok";
    if (state == 1) Log::Info("DLSSNR output check: mean |output - input| %.4f, output luma %.3f, input luma %.3f: %s", delta, outLuma, inLuma, verdict);
    else Log::Warn("DLSSNR output check: mean |output - input| %.4f, output luma %.3f, input luma %.3f: %s", delta, outLuma, inLuma, verdict);
}

void Pipeline::ReadStats(GpuContext& gpu) {
    // Pick the newest completed readback.
    int best = -1; UINT64 bestFence = 0;
    for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i) {
        if (!m_statsPending[i] || !m_statsFence[i] || !gpu.IsFenceComplete(m_statsFence[i])) continue;
        if (m_statsFence[i] > bestFence) { bestFence = m_statsFence[i]; best = (int)i; }
    }
    if (best < 0) return;
    float v[8] = {};
    D3D12_RANGE range{ 0, 32 };
    void* p = nullptr;
    if (SUCCEEDED(m_statsReadback[best]->Map(0, &range, &p)) && p) {
        std::memcpy(v, p, sizeof(v));
        D3D12_RANGE none{ 0, 0 };
        m_statsReadback[best]->Unmap(0, &none);
        if (m_statsHasGuidance[best]) { m_statAvgCost = v[0]; m_statMaxCost = v[1]; m_statAvgMotion = v[2]; }
        if (m_statsHasNeural[best]) UpdateNeuralCheck(v[4], v[5], v[6]);
    }
    for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i)
        if (m_statsPending[i] && m_statsFence[i] && m_statsFence[i] <= bestFence) m_statsPending[i] = false;
}

void Pipeline::RunDepthCapture(GpuContext& gpu, ID3D12GraphicsCommandList*& cmd) {
    // Downsample + normalise the current colour into the network input buffer and copy it to a readback buffer.
    // Submitted right away (with its own fence) so the CPU can hand it to the worker as early as possible.
    Transition(cmd, m_color8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (m_depthInState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        Device::Barrier(cmd, m_depthInBuf.Get(), m_depthInState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_depthInState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    DispatchDesc d;
    d.id = ShaderId::DepthPre;
    d.constants.srcWidth = m_inW; d.constants.srcHeight = m_inH;
    d.constants.dstWidth = m_depthInferW; d.constants.dstHeight = m_depthInferH;
    d.srv[0] = m_color8.srv;
    d.uav[0] = m_depthInUav;
    d.groupsX = Shaders::Groups(m_depthInferW, 8); d.groupsY = Shaders::Groups(m_depthInferH, 8);
    m_shaders.Dispatch(cmd, gpu, d);
    Device::Barrier(cmd, m_depthInBuf.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_depthInState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    const UINT64 bytes = (UINT64)m_depthInferW * m_depthInferH * 3 * sizeof(float);
    cmd->CopyBufferRegion(m_depthInReadback.Get(), 0, m_depthInBuf.Get(), 0, bytes);
    cmd = gpu.SubmitAndContinue();
    m_depthInFence = gpu.Signal();
    m_depthInPending = true;
    m_depthFramesSinceCapture = 0;
}

bool Pipeline::RunDepthApply(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, bool reset) {
    if (!m_depthRawBuf || !m_depthEst.Ready()) { m_depthHistValid = false; return false; }
    bool newRaw = false;
    DepthResult r;
    if (m_depthEst.TryTakeResult(r)) {
        const UINT64 capacity = (UINT64)m_depthInferW * m_depthInferH;
        if (r.width && r.height && (UINT64)r.width * r.height <= capacity && r.depth.size() >= (size_t)r.width * r.height) {
            const UINT slot = gpu.FrameIndex();
            const size_t bytes = (size_t)r.width * r.height * sizeof(float);
            void* p = nullptr;
            D3D12_RANGE none{ 0, 0 };
            if (SUCCEEDED(m_depthUpload[slot]->Map(0, &none, &p)) && p) {
                std::memcpy(p, r.depth.data(), bytes);
                m_depthUpload[slot]->Unmap(0, nullptr);
                if (m_depthRawState != D3D12_RESOURCE_STATE_COPY_DEST) {
                    Device::Barrier(cmd, m_depthRawBuf.Get(), m_depthRawState, D3D12_RESOURCE_STATE_COPY_DEST);
                    m_depthRawState = D3D12_RESOURCE_STATE_COPY_DEST;
                }
                cmd->CopyBufferRegion(m_depthRawBuf.Get(), 0, m_depthUpload[slot].Get(), 0, bytes);
                m_depthRawW = r.width; m_depthRawH = r.height;
                const float inv = 1.0f / std::max(r.p98 - r.p02, 1e-4f);
                if (!m_depthHaveRaw || reset) { m_depthP02 = r.p02; m_depthInvRange = inv; }
                else { m_depthP02 += (r.p02 - m_depthP02) * kDepthRangeSmoothing; m_depthInvRange += (inv - m_depthInvRange) * kDepthRangeSmoothing; }
                m_depthHaveRaw = true;
                newRaw = true;
                m_depthInferMs = r.inferenceMs;
                m_depthLastResultTime = NowSeconds();
            }
        } else {
            Log::Warn("Depth estimator returned an unexpected %ux%u result", r.width, r.height);
        }
    }
    if (!m_depthHaveRaw) { m_depthHistValid = false; return false; }   // first result still pending: zero depth this frame
    if (m_depthRawState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        Device::Barrier(cmd, m_depthRawBuf.Get(), m_depthRawState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        m_depthRawState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    Tex& histPrev = m_depthHist[1 - m_cur];
    Tex& histCur = m_depthHist[m_cur];
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_conf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, histPrev, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, histCur, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const bool useHistory = m_depthHistValid && !reset;
    DispatchDesc d;
    d.id = ShaderId::DepthPost;
    d.constants.dstWidth = m_inW; d.constants.dstHeight = m_inH;
    d.constants.intA = m_depthRawW; d.constants.intB = m_depthRawH;
    d.constants.flags = ((newRaw || !useHistory) ? 1u : 0u) | (useHistory ? 2u : 0u);
    d.constants.paramA = m_depthP02;
    d.constants.paramB = m_depthInvRange;
    d.constants.paramC = kDepthHistoryWeight;
    d.srv[0] = m_depthRawSrv; d.srv[1] = histPrev.srv; d.srv[2] = m_mv.srv; d.srv[3] = m_conf.srv;
    d.uav[0] = m_depth.uav; d.uav[1] = histCur.uav;
    d.groupsX = Shaders::Groups(m_inW, 8); d.groupsY = Shaders::Groups(m_inH, 8);
    m_shaders.Dispatch(cmd, gpu, d);
    m_depthHistValid = true;
    return true;
}

bool Pipeline::RunDlaa(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, bool reset) {
    if (m_dlaaFailed || !m_ngx.Initialized() || !m_ngx.DlssAvailable()) return false;
    if (!m_dlaa.Created() || m_dlaa.Width() != m_inW || m_dlaa.Height() != m_inH || m_dlaaCreatedPreset != s.dlaaPreset) {
        if (m_dlaa.Created()) { gpu.WaitIdle(); m_dlaa.Release(m_ngx); }
        std::string err;
        if (!m_dlaa.Create(m_ngx, cmd, m_inW, m_inH, s.dlaaPreset, err)) {
            m_dlaaFailed = true; m_dlaaError = err;
            Log::Error("DLAA: %s", err.c_str());
            return false;
        }
        m_dlaaCreatedPreset = s.dlaaPreset;
        reset = true;
    }
    gpu.TimerBegin(cmd, GpuTimer::Dlaa);
    Transition(cmd, m_color8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_dlaaOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::string err;
    const bool ok = m_dlaa.Evaluate(cmd, m_color8.res.Get(), m_mv.res.Get(), m_depth.res.Get(), m_dlaaOut.res.Get(), reset,
                                    (float)m_frameIntervalMs, err);
    gpu.TimerEnd(cmd, GpuTimer::Dlaa);
    if (!ok) {
        m_dlaaFailed = true; m_dlaaError = err;
        Log::Error("DLAA: %s", err.c_str());
    }
    return ok;
}

// The network may look at an exposed copy of its input (a paper-white scale for the neural pass only); the composite
// pass undoes the gain, so the output keeps the original brightness and only the character of the result changes.
Pipeline::Tex& Pipeline::PrepareNeuralInput(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, Tex& base) {
    m_nrInExposed = false;
    if (std::fabs(s.nrInputExposure - 1.0f) < 1e-3f || !m_nrIn.Valid()) return base;
    Transition(cmd, base, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_nrIn, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchDesc d;
    d.id = ShaderId::Expose;
    d.constants.srcWidth = m_inW; d.constants.srcHeight = m_inH;
    d.constants.dstWidth = m_inW; d.constants.dstHeight = m_inH;
    d.constants.paramA = s.nrInputExposure;
    d.srv[0] = base.srv;
    d.uav[0] = m_nrIn.uav;
    d.groupsX = Shaders::Groups(m_inW, 8); d.groupsY = Shaders::Groups(m_inH, 8);
    m_shaders.Dispatch(cmd, gpu, d);
    m_nrInExposed = true;
    return m_nrIn;
}

bool Pipeline::RunNeural(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, Tex& input, bool reset) {
    if (m_nrFailed || !m_ngx.Initialized()) return false;
    const bool useCore = s.nrRoute == RouteNgxCore;
    if (!useCore && !m_nr.RuntimeLoaded()) return false;
    if (!m_nr.Created() || m_nr.InputWidth() != m_inW || m_nr.InputHeight() != m_inH || m_nr.OutputWidth() != m_outW ||
        m_nr.OutputHeight() != m_outH || m_nrCreatedUseCore != useCore || m_nrCreatedPreset != s.nrPreset) {
        if (m_nr.Created()) { gpu.WaitIdle(); m_nr.Release(m_ngx); }
        std::string err;
        if (!m_nr.Create(m_ngx, cmd, m_inW, m_inH, m_outW, m_outH, s.nrPreset, useCore, err)) {
            m_nrFailed = true; m_nrError = err;
            Log::Error("DLSSNR: %s", err.c_str());
            LogRuntimeGenerationHint(gpu, m_nr.RuntimeVersion());
            return false;
        }
        m_nrCreatedUseCore = useCore;
        m_nrCreatedPreset = s.nrPreset;
        m_nrOutState = 0; m_nrOutDelta = -1.0f;   // the output check reports again for the new instance
        reset = true;
    }
    if (m_nrSkipped) { reset = true; m_nrSkipped = false; }   // frames went by without the pass: its history is stale
    gpu.TimerBegin(cmd, GpuTimer::Neural);
    Transition(cmd, input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_nrOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DlssnrFeature::Inputs in;
    in.color = input.res.Get(); in.depth = m_depth.res.Get(); in.mvec = m_mv.res.Get(); in.output = m_nrOut.res.Get();
    in.reset = reset;
    DlssnrFeature::Params p;
    p.preset = s.nrPreset; p.style = s.nrStyle;
    // The runtime accepts strengths up to 1; the part above 1 is applied by the composite pass (see RunComposite).
    p.intensity = std::min(s.nrIntensity, 1.0f); p.globalTone = std::min(s.nrGlobalTone, 1.0f);
    p.localTone = std::min(s.nrLocalTone, 1.0f); p.localStructure = std::min(s.nrLocalStructure, 1.0f);
    p.skinStructure = s.nrSkinStructure < 0.0f ? -1.0f : std::min(s.nrSkinStructure, 1.0f);
    p.autoMask = s.nrAutoMask; p.uiCorrection = s.nrUiCorrection;
    m_nrMaxStrength = std::max({ p.intensity, p.globalTone, p.localTone, p.localStructure, p.skinStructure < 0.0f ? 1.0f : p.skinStructure });
    std::string err;
    const bool ok = m_nr.Evaluate(cmd, in, p, err);
    gpu.TimerEnd(cmd, GpuTimer::Neural);
    if (!ok) {
        m_nrFailed = true; m_nrError = err;
        Log::Error("DLSSNR: %s", err.c_str());
        LogRuntimeGenerationHint(gpu, m_nr.RuntimeVersion());
    }
    m_nrDirty = false;
    return ok;
}

void Pipeline::RunComposite(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, const Settings& s, Tex& processed, Tex* neuralBase,
                            Tex* neuralInput, bool bypass) {
    // Pick a display buffer the UI is not sampling and that no submitted composite still occupies, and make the
    // processing queue wait (GPU side) until the last UI frame that read it has finished. Finished composites that a
    // newer finished one supersedes will never be shown, so their buffers are reclaimed first. With every buffer
    // busy (the UI has not looked for a while) only the capture image is written this frame; the UI asks for a fresh
    // composite as soon as it frees a buffer.
    int target = -1;
    UINT64 uiRelease = 0;
    {
        std::lock_guard<std::mutex> lock(m_displayMutex);
        int newestDone = -1;
        for (int i = (int)m_disp.pendingCount - 1; i >= 0; --i) {
            if (gpu.IsFenceComplete(m_disp.pending[i].fence)) { newestDone = i; break; }
        }
        if (newestDone > 0) {
            const UINT remaining = m_disp.pendingCount - (UINT)newestDone;
            for (UINT i = 0; i < remaining; ++i) m_disp.pending[i] = m_disp.pending[(UINT)newestDone + i];
            m_disp.pendingCount = remaining;
        }
        bool busy[kDisplayBuffers] = {};
        if (m_disp.uiUsing >= 0 && m_disp.uiUsing < (int)kDisplayBuffers) busy[m_disp.uiUsing] = true;
        for (UINT i = 0; i < m_disp.pendingCount; ++i) busy[m_disp.pending[i].buffer] = true;
        for (int i = 0; i < (int)kDisplayBuffers; ++i) {
            if (!busy[i] && m_displayBuf[i].Valid()) { target = i; break; }
        }
        m_disp.starved = target < 0;
        if (target >= 0) uiRelease = m_disp.uiRelease[target];
    }
    if (uiRelease) gpu.WaitOnGpu(gpu.Dev().Ui().Fence(), uiRelease);
    Tex* display = target >= 0 ? &m_displayBuf[target] : nullptr;
    m_displayTarget = target;

    gpu.TimerBegin(cmd, GpuTimer::Composite);
    Transition(cmd, m_color8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, processed, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_mv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_conf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmd, m_final, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (neuralBase) Transition(cmd, *neuralBase, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (neuralInput) Transition(cmd, *neuralInput, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (display) Transition(cmd, *display, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchDesc d;
    d.id = ShaderId::Composite;
    d.constants.srcWidth = m_inW; d.constants.srcHeight = m_inH;
    d.constants.dstWidth = m_outW; d.constants.dstHeight = m_outH;
    UINT flags = 0;
    if (s.keepAlpha) flags |= 1;
    if (s.checkerboard) flags |= 2;
    if (bypass) flags |= 4;
    if (m_inW != m_outW || m_inH != m_outH) flags |= 32;
    if (!bypass && neuralBase && neuralInput &&
        (neuralInput != neuralBase || std::fabs(s.nrToneTransfer - 1.0f) > 1e-3f || std::fabs(s.nrColorStrength - 1.0f) > 1e-3f)) {
        // Output blend: the neural pass's brightness and colour changes reach the output with separate strengths, and
        // the exposure the network saw is undone. All three at their defaults leave the neural result untouched.
        flags |= 64;
        d.constants.paramC = s.nrToneTransfer;
        d.constants.paramD = s.nrColorStrength;
        d.constants.paramE = neuralInput != neuralBase ? 1.0f / std::max(s.nrInputExposure, 0.01f) : 1.0f;
    }
    // Strengths above 1: the runtime stops at 1, so the extra range amplifies the difference between the neural result
    // and the original picture. The highest of the five strengths sets the gain (1..2).
    const float gain = std::max({ s.nrIntensity, s.nrGlobalTone, s.nrLocalTone, s.nrLocalStructure, s.nrSkinStructure, 1.0f });
    if (!bypass && neuralBase && gain > 1.001f) {
        flags |= 128;
        d.constants.paramF = std::min(gain, 2.0f);
    }
    d.constants.flags = flags;
    d.constants.intA = (UINT)s.compareMode;
    d.constants.paramA = s.wipePosition;
    d.constants.paramB = kMotionViewScale;
    d.srv[0] = m_color8.srv; d.srv[1] = processed.srv; d.srv[2] = m_mv.srv; d.srv[3] = m_conf.srv; d.srv[4] = m_depth.srv;
    if (flags & 64) { d.srv[5] = neuralBase->srv; d.srv[6] = neuralInput->srv; }
    d.uav[0] = m_final.uav; d.uav[1] = display ? display->uav : D3D12_CPU_DESCRIPTOR_HANDLE{};   // null view: writes dropped
    d.groupsX = Shaders::Groups(m_outW, 8); d.groupsY = Shaders::Groups(m_outH, 8);
    m_shaders.Dispatch(cmd, gpu, d);
    if (display) Transition(cmd, *display, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    gpu.TimerEnd(cmd, GpuTimer::Composite);
}

void Pipeline::EnqueueReadback(GpuContext& gpu, ID3D12GraphicsCommandList* cmd, Tex& src, const std::wstring& path, bool keepAlpha) {
    const UINT pitch = (src.w * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    Readback* rb = nullptr;
    for (auto& r : m_readbacks) if (!r.inUse && r.w == src.w && r.h == src.h) { rb = &r; break; }
    if (!rb) {
        for (auto& r : m_readbacks) if (!r.inUse) { gpu.DeferRelease(r.buffer); r.buffer.Reset(); rb = &r; break; }
        if (!rb) {
            if (m_readbacks.size() >= kMaxReadbacks) { Log::Warn("Capture skipped: too many captures in flight"); return; }
            m_readbacks.emplace_back();
            rb = &m_readbacks.back();
        }
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (UINT64)pitch * src.h; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr = gpu.Dev().D3D12()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                   nullptr, IID_PPV_ARGS(&rb->buffer));
        if (FAILED(hr)) { Log::Hr(LogLevel::Error, "capture readback buffer", hr); rb->buffer.Reset(); rb->w = rb->h = 0; return; }
        rb->w = src.w; rb->h = src.h; rb->pitch = pitch;
    }
    Transition(cmd, src, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = rb->buffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = src.fmt;
    dst.PlacedFootprint.Footprint.Width = src.w;
    dst.PlacedFootprint.Footprint.Height = src.h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = pitch;
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.pResource = src.res.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.SubresourceIndex = 0;
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &from, nullptr);
    rb->inUse = true; rb->fence = 0; rb->keepAlpha = keepAlpha; rb->path = path;
}

// --- frame --------------------------------------------------------------------------------

void Pipeline::Render(GpuContext& gpu, const SourceFrame& src, const Settings& s, ID3D12GraphicsCommandList* cmd, bool fresh, bool sourceChanged) {
    // Requests posted from the UI thread.
    if (m_resetReq.exchange(false)) m_resetRequested = true;
    if (m_depthRestartReq.exchange(false)) m_depthRestart = true;
    if (m_nrDirtyReq.exchange(false)) { m_nrDirty = true; m_nrFailed = false; m_nrError.clear(); }
    if (m_dlaaDirtyReq.exchange(false)) { m_dlaaFailed = false; m_dlaaError.clear(); }
    m_displayRetryReq.exchange(false);   // this frame composites in any case
    m_displayTarget = -1;

    m_status.ngxInitialized = m_ngx.Initialized();
    m_status.dlssAvailable = m_ngx.DlssAvailable();
    m_status.ngxStatus = m_ngx.Status();
    m_status.nrRuntimeLoaded = m_nr.RuntimeLoaded();
    m_status.nrRuntimeVersion = m_nr.RuntimeVersion();
    m_status.nrRuntimePath = m_nr.RuntimePath();
    m_status.sourceConnected = src.Connected() && src.hasFrame;
    m_status.capturesInFlight = 0;
    for (auto& r : m_readbacks) if (r.inUse) ++m_status.capturesInFlight;

    if (!src.Connected()) {
        if (m_built) ReleaseResources(gpu);
        m_hasDisplay = false;
        m_status.nrActive = false; m_status.dlaaActive = false;
        m_status.srcWidth = m_status.srcHeight = 0;
        return;
    }
    const Config cfg = ComputeConfig(src, s);
    if (!m_built || !(cfg == m_cfg) || sourceChanged) {
        if (!Rebuild(gpu, cfg)) { m_hasDisplay = false; return; }
        fresh = src.hasFrame;
    }
    m_status.srcWidth = m_srcW; m_status.srcHeight = m_srcH;
    m_status.inWidth = m_inW; m_status.inHeight = m_inH;
    m_status.outWidth = m_outW; m_status.outHeight = m_outH;
    m_status.nrUpscaling = (m_inW != m_outW || m_inH != m_outH);
    m_status.nvofReady = m_nvofReady;
    m_status.nvofBidirectional = m_nvofReady && m_nvof.Bidirectional();
    m_status.nvofSinglePass = m_nvofReady && m_nvof.SinglePassBidirectional();
    m_status.nvofGrid = m_nvofReady ? m_nvof.Grid() : 0;
    m_status.nvofError = m_nvofError;
    m_status.depthState = (int)m_depthEst.State();
    m_status.depthMessage = m_depthEst.Message();
    m_status.depthBackend = m_depthEst.Backend();
    m_status.depthInferW = m_depthInferW; m_status.depthInferH = m_depthInferH;
    m_status.depthInferMs = m_depthInferMs;
    m_status.depthWarmupMs = m_depthEst.WarmupMs();
    m_status.depthInferences = m_depthEst.Inferences();
    m_status.depthAgeMs = m_depthLastResultTime > 0.0 ? (NowSeconds() - m_depthLastResultTime) * 1000.0 : 0.0;
    m_status.depthModelPath = m_cfg.depthModel;
    m_status.depthModelExists = m_depthModelExists;
    if (m_depthRestart && m_depthInBuf) {
        m_depthRestart = false;
        m_depthEst.Start(gpu.Dev(), m_exeDir, m_cfg.depthModel, m_depthInferW, m_depthInferH);
        m_depthModelExists = FileExists(m_cfg.depthModel);
        m_depthHaveRaw = false; m_depthHistValid = false; m_depthStillCaptured = false;
    }
    if (!src.hasFrame) { m_hasDisplay = false; return; }

    Tex* processed = &m_color8;
    Tex* neuralBase = nullptr;    // picture the neural pass started from
    Tex* neuralIn = nullptr;      // picture it actually saw (exposed copy or the base itself)
    bool nrOk = false, dlaaOk = false;
    // Capture requests. With "neural pass only for captures" on a live source, a request arms the capture and starts
    // a warm-up burst of fresh frames with the neural pass running, so its temporal history has converged when the
    // picture is saved on the burst's last frame. Otherwise the capture happens on this frame.
    const bool captureOnly = s.nrCaptureOnly && s.nrEnabled && s.sourceMode == SourceSpout;
    bool captureNow = false;
    {
        std::lock_guard<std::mutex> lock(m_captureMutex);
        if (m_captureRequested) {
            m_captureRequested = false;
            if (captureOnly && !m_nrFailed) {
                if (!m_captureArmed) { m_captureArmed = true; m_captureArmedTime = NowSeconds(); m_nrBurst = kNeuralWarmupFrames; }
            } else {
                captureNow = true;
            }
        }
    }
    if (m_captureArmed && (!captureOnly || m_nrFailed || NowSeconds() - m_captureArmedTime > kNeuralWarmupTimeout)) {
        // The mode was switched off, the neural pass failed, or the source stalled: save what there is.
        m_captureArmed = false; m_nrBurst = 0; captureNow = true;
    }
    const bool nrWanted = s.nrEnabled && (!captureOnly || m_nrBurst > 0);
    const bool dlaaWanted = s.dlaaEnabled;

    if (fresh || !m_hasDisplay) {
        const double now = NowSeconds();
        if (m_lastFreshTime > 0.0) m_frameIntervalMs = std::clamp((now - m_lastFreshTime) * 1000.0, 1.0, 100.0);
        m_lastFreshTime = now;

        ReadStats(gpu);
        bool reset = m_resetRequested || !m_haveHistory;
        bool sceneCut = false;
        if (m_haveHistory && m_lastWasBlockMode) {
            // A cut is a sudden jump of the matching cost, not merely a high value: fast camera motion also raises the
            // cost and must not clear the temporal history (DLSS 5 recovers on its own, a reset always pops).
            const float ref = std::max(m_costEma, 0.02f);
            if (s.autoReset && m_statAvgCost > s.cutThreshold && m_statAvgCost > ref * kSceneCutRatio) { sceneCut = true; reset = true; }
            m_costEma = sceneCut ? m_statAvgCost : (m_costEma <= 0.0f ? m_statAvgCost : m_costEma * 0.9f + m_statAvgCost * 0.1f);
        } else {
            m_costEma = 0.0f;
        }
        m_resetRequested = false;
        if (reset) ++m_status.resets;
        m_status.sceneCut = sceneCut;

        int motionMode = src.stillImage ? MotionZero : s.motionMode;
        if (motionMode == MotionNvOpticalFlow && !m_nvofReady) motionMode = MotionCompute;
        const bool nvofBgra = (motionMode == MotionNvOpticalFlow) && m_nvofFmt == DXGI_FORMAT_B8G8R8A8_UNORM;

        RunConvert(gpu, cmd, src, s, nvofBgra);

        // Depth network input: every depthInterval processed frames, as soon as the worker is free.
        const bool depthWanted = s.depthMode == DepthEstimated && m_depthInBuf;
        if (depthWanted) {
            ++m_depthFramesSinceCapture;
            // Live input refreshes the estimate every depthInterval frames. A still picture needs exactly one: the
            // network is deterministic, and repeating it would keep the GPU busy with identical inferences.
            const bool due = src.stillImage ? !m_depthStillCaptured : m_depthFramesSinceCapture >= s.depthInterval;
            if (due && !m_depthInPending && m_depthEst.Idle()) {
                RunDepthCapture(gpu, cmd);
                m_depthStillCaptured = src.stillImage;
            }
        }

        gpu.TimerBegin(cmd, GpuTimer::Guidance);
        int densifyMode = MotionZero;
        if (motionMode != MotionZero) {
            RunGuidance(gpu, cmd, s, motionMode, m_haveHistory && !reset);
            RunStats(gpu, cmd);
            m_lastWasBlockMode = true;
        } else {
            m_lastWasBlockMode = false;
        }
        gpu.TimerEnd(cmd, GpuTimer::Guidance);
        if (m_haveHistory && !reset) {
            if (motionMode == MotionNvOpticalFlow) densifyMode = RunOpticalFlow(gpu, cmd, false) ? MotionNvOpticalFlow : MotionZero;
            else if (motionMode == MotionCompute) densifyMode = MotionCompute;
        } else if (motionMode == MotionNvOpticalFlow) {
            // Prime the optical flow reference frame without using its result.
            RunOpticalFlow(gpu, cmd, true);
        }
        gpu.TimerBegin(cmd, GpuTimer::Densify);
        RunDensify(gpu, cmd, s, densifyMode);
        const bool depthApplied = depthWanted && RunDepthApply(gpu, cmd, reset);
        gpu.TimerEnd(cmd, GpuTimer::Densify);
        m_status.motionModeActive = densifyMode;
        m_status.depthModeActive = depthApplied ? DepthEstimated : (s.depthMode == DepthEstimated ? DepthZero : s.depthMode);

        if (dlaaWanted) {
            dlaaOk = RunDlaa(gpu, cmd, s, reset);
            if (dlaaOk) processed = &m_dlaaOut;
        }
        if (nrWanted) {
            neuralBase = processed;
            neuralIn = &PrepareNeuralInput(gpu, cmd, s, *processed);
            nrOk = RunNeural(gpu, cmd, s, *neuralIn, reset);
            if (nrOk) { processed = &m_nrOut; RunNeuralCheck(gpu, cmd, *neuralIn); }
        } else if (m_nr.Created()) {
            m_nrSkipped = true;   // switch off or between capture bursts: the history is stale when the pass resumes
        }
        CopyStats(gpu, cmd);
        if (captureOnly && m_nrBurst > 0 && nrOk && --m_nrBurst == 0 && m_captureArmed) { m_captureArmed = false; captureNow = true; }
        m_haveHistory = true;
        m_cur = 1 - m_cur;
        ++m_status.processedFrames;
    } else {
        // No new source frame: keep the last results; re-run the neural pass only if its parameters changed.
        dlaaOk = dlaaWanted && !m_dlaaFailed && m_dlaa.Created();
        if (dlaaOk) processed = &m_dlaaOut;
        if (nrWanted && !m_nrFailed && m_nr.Created()) {
            neuralBase = processed;
            if (m_nrDirty) {
                neuralIn = &PrepareNeuralInput(gpu, cmd, s, *processed);
                nrOk = RunNeural(gpu, cmd, s, *neuralIn, false);
            } else {
                nrOk = true;
                neuralIn = m_nrInExposed ? &m_nrIn : processed;
            }
            if (nrOk) processed = &m_nrOut;
        }
    }
    if (!nrWanted) { m_nrDirty = false; }
    // The neural feature stays created while the switch is off: re-creating it stalls the pipeline and, with some
    // runtime builds, the re-created instance silently produces no picture. Size, preset and route changes still
    // re-create it (RunNeural); shutdown releases it.
    if (!dlaaWanted && m_dlaa.Created()) { gpu.WaitIdle(); m_dlaa.Release(m_ngx); m_dlaaCreatedPreset = -1; }

    RunComposite(gpu, cmd, s, *processed, nrOk ? neuralBase : nullptr, nrOk ? neuralIn : nullptr, processed == &m_color8);
    if (m_displayTarget >= 0) m_hasDisplay = true;   // a skipped display write keeps the previous state: the results exist

    if (captureNow) {
        std::wstring captureFolder, captureBase;
        bool captureKeepAlpha = true, captureOriginal = false;
        {
            std::lock_guard<std::mutex> lock(m_captureMutex);
            captureFolder = m_captureFolder; captureBase = m_captureBase;
            captureKeepAlpha = m_captureKeepAlpha; captureOriginal = m_captureOriginal;
        }
        const std::wstring outPath = captureBase.empty()
            ? Capture::MakeFileName(captureFolder, m_outW, m_outH, L"")
            : Capture::MakeImageFileName(captureFolder, captureBase, m_outW, m_outH, L"");
        EnqueueReadback(gpu, cmd, m_final, outPath, captureKeepAlpha);
        if (captureOriginal) {
            const std::wstring origPath = captureBase.empty()
                ? Capture::MakeFileName(captureFolder, m_inW, m_inH, L"_original")
                : Capture::MakeImageFileName(captureFolder, captureBase, m_inW, m_inH, L"_original");
            EnqueueReadback(gpu, cmd, m_color8, origPath, captureKeepAlpha);
        }
    }

    m_status.nrActive = nrOk;
    m_status.nrStandby = captureOnly && !nrOk && !m_nrFailed;
    m_status.nrFailed = m_nrFailed;
    m_status.nrError = m_nrError;
    m_status.nrEvaluations = m_nr.EvaluateCount();
    m_status.nrOutDelta = m_nrOutDelta; m_status.nrOutLuma = m_nrOutLuma; m_status.nrInLuma = m_nrInLuma; m_status.nrOutState = m_nrOutState;
    m_status.dlaaActive = dlaaOk;
    m_status.dlaaFailed = m_dlaaFailed;
    m_status.dlaaError = m_dlaaError;
    m_status.statAvgCost = m_statAvgCost; m_status.statMaxCost = m_statMaxCost; m_status.statAvgMotion = m_statAvgMotion;
    m_status.frameIntervalMs = m_frameIntervalMs;
}

void Pipeline::AfterSubmit(GpuContext& gpu, UINT64 fenceValue) {
    (void)gpu;
    const UINT64 fence = fenceValue;
    for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i)
        if (m_statsPending[i] && m_statsFence[i] == 0) m_statsFence[i] = fence;
    for (auto& r : m_readbacks)
        if (r.inUse && r.fence == 0) r.fence = fence;
    if (m_displayTarget >= 0) {
        std::lock_guard<std::mutex> lock(m_displayMutex);
        if (m_disp.pendingCount < kDisplayBuffers) m_disp.pending[m_disp.pendingCount++] = { m_displayTarget, fence };
        m_displayTarget = -1;
    }
}

void Pipeline::Update(GpuContext& gpu, Capture& capture) {
    if (m_depthInPending && m_depthInFence && gpu.IsFenceComplete(m_depthInFence)) {
        m_depthInPending = false;
        const size_t count = (size_t)m_depthInferW * m_depthInferH * 3;
        D3D12_RANGE range{ 0, count * sizeof(float) };
        void* p = nullptr;
        if (m_depthInReadback && SUCCEEDED(m_depthInReadback->Map(0, &range, &p)) && p) {
            if (!m_depthEst.Submit(static_cast<const float*>(p), count)) {   // worker busy/not ready: retry next frame
                m_depthFramesSinceCapture = 1000;
                m_depthStillCaptured = false;
            }
            D3D12_RANGE none{ 0, 0 };
            m_depthInReadback->Unmap(0, &none);
        }
    }
    for (auto& r : m_readbacks) {
        if (!r.inUse || r.fence == 0 || !gpu.IsFenceComplete(r.fence)) continue;
        CaptureJob job;
        job.width = r.w; job.height = r.h; job.rowPitch = r.pitch; job.keepAlpha = r.keepAlpha; job.path = r.path;
        const size_t bytes = (size_t)r.pitch * r.h;
        D3D12_RANGE range{ 0, bytes };
        void* p = nullptr;
        if (SUCCEEDED(r.buffer->Map(0, &range, &p)) && p) {
            job.pixels.resize(bytes);
            std::memcpy(job.pixels.data(), p, bytes);
            D3D12_RANGE none{ 0, 0 };
            r.buffer->Unmap(0, &none);
            capture.Enqueue(std::move(job));
        } else {
            Log::Error("Capture readback map failed");
        }
        r.inUse = false; r.fence = 0;
    }
}

} // namespace vdc
