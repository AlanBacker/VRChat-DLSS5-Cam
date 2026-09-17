// VRChat DLSS5 Cam - the MCP tools: what an assistant may do. Every tool runs on the interface thread between the
// interface's draw and the handling of its events, so it takes the same paths as the widgets: the same event
// flags, the same undo history, the same toasts and the same saved settings.
#include "core/App.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/McpServer.h"
#include "core/Util.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <map>

namespace vdc {

namespace {

enum WaitKind { WaitIdle = 0, WaitLoaded, WaitConverged, WaitDisplay, WaitCapture, WaitVideoRun, WaitBatch, WaitOpen, WaitVideoState };

bool SamePathCi(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t x = a[i], y = b[i];
        if (x == L'/') x = L'\\';
        if (y == L'/') y = L'\\';
        if (towlower(x) != towlower(y)) return false;
    }
    return true;
}

std::string FileName(const std::wstring& path) {
    const size_t k = path.find_last_of(L"\\/");
    return WideToUtf8(k == std::wstring::npos ? path : path.substr(k + 1));
}

std::wstring LowerExt(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    const size_t sep = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && sep > dot)) return std::wstring();
    std::wstring e = path.substr(dot);
    for (wchar_t& c : e) c = (wchar_t)towlower(c);
    return e;
}

// "key=value" lines (Settings::Text and the effect texts) as ordered pairs.
std::vector<std::pair<std::string, std::string>> Pairs(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
    }
    return out;
}

// A setting's text as a typed JSON value (bool, number or string, as the reference says).
Json TypedValue(const McpSettingInfo* info, const std::string& text) {
    if (!info) return Json(text);
    const std::string t = info->type;
    if (t == "bool") return Json(text == "1" || text == "true");
    if (t == "int") return Json((long long)atoll(text.c_str()));
    if (t == "float") return Json(atof(text.c_str()));
    if (t == "enum") {
        const char c = info->range[0];
        if ((c >= '0' && c <= '9') || c == '-') return Json((long long)atoll(text.c_str()));
        return Json(text);
    }
    return Json(text);
}

const char* CompareName(int mode) {
    switch (mode) {
    case CompareOriginal: return "original";
    case CompareWipe: return "wipe";
    case CompareMotion: return "motion vectors";
    case CompareDepth: return "depth";
    default: return "output";
    }
}

const char* ItemStateName(int state) {
    switch (state) {
    case LibraryItem::Queued: return "queued";
    case LibraryItem::Processing: return "processing";
    case LibraryItem::Done: return "done";
    case LibraryItem::Failed: return "failed";
    default: return "idle";
    }
}

Json ItemJson(const LibraryItem& it) {
    Json j = Json::Obj()
        .Set("id", it.id)
        .Set("path", WideToUtf8(it.path))
        .Set("name", it.name)
        .Set("type", it.isVideo ? "video" : "picture")
        .Set("selected", it.selected)
        .Set("state", ItemStateName(it.state));
    if (it.probe == 2) j.Set("error", it.error);
    else if (it.probe == 1) {
        j.Set("width", it.width).Set("height", it.height);
        if (it.isVideo) j.Set("duration", it.duration).Set("fps", it.fps).Set("hasAudio", it.hasAudio);
    } else j.Set("probing", true);
    if (it.isVideo && (it.inSec > 0.0 || it.outSec > 0.0)) j.Set("in", it.inSec).Set("out", it.outSec);
    if (it.state == LibraryItem::Processing) j.Set("progress", it.progress);
    if (!it.outName.empty()) j.Set("output", it.outName);
    if (it.state == LibraryItem::Failed && !it.error.empty()) j.Set("error", it.error);
    if (it.useOwn && it.own) {
        Json own = Json::Obj();
        for (const auto& kv : Pairs(it.own->EffectText())) own.Set(kv.first.c_str(), TypedValue(McpServer::FindSetting(kv.first), kv.second));
        j.Set("own", own);
    }
    if (!it.transform.Identity()) {
        j.Set("transform", Json::Obj().Set("rotate", it.transform.rotate).Set("flipH", it.transform.flipH).Set("flipV", it.transform.flipV)
              .Set("cropped", it.transform.Cropped()));
    }
    return j;
}

void Reply(const std::shared_ptr<McpCall>& call, const Json& j) { call->Json_(j); call->Finish(); }
void ReplyText(const std::shared_ptr<McpCall>& call, const std::string& t) { call->Text(t); call->Finish(); }

// Area-average downscale of RGBA8 pixels.
std::vector<uint8_t> Downscale(const std::vector<uint8_t>& src, UINT sw, UINT sh, UINT dw, UINT dh) {
    std::vector<uint8_t> out((size_t)dw * dh * 4);
    for (UINT y = 0; y < dh; ++y) {
        const UINT y0 = (UINT)((unsigned long long)y * sh / dh), y1 = std::max(y0 + 1, (UINT)((unsigned long long)(y + 1) * sh / dh));
        for (UINT x = 0; x < dw; ++x) {
            const UINT x0 = (UINT)((unsigned long long)x * sw / dw), x1 = std::max(x0 + 1, (UINT)((unsigned long long)(x + 1) * sw / dw));
            unsigned sum[4] = { 0, 0, 0, 0 };
            unsigned n = 0;
            for (UINT yy = y0; yy < y1 && yy < sh; ++yy) {
                const uint8_t* p = src.data() + ((size_t)yy * sw + x0) * 4;
                for (UINT xx = x0; xx < x1 && xx < sw; ++xx, p += 4) { sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3]; ++n; }
            }
            uint8_t* d = out.data() + ((size_t)y * dw + x) * 4;
            if (n == 0) n = 1;
            d[0] = (uint8_t)(sum[0] / n); d[1] = (uint8_t)(sum[1] / n); d[2] = (uint8_t)(sum[2] / n); d[3] = (uint8_t)(sum[3] / n);
        }
    }
    return out;
}

} // namespace

// --- the server's life ------------------------------------------------------------------------

void App::SyncMcp() {
    const bool want = m_settings.mcpEnabled || m_mcpSessionPort > 0;
    const int port = m_mcpSessionPort > 0 ? m_mcpSessionPort : m_settings.mcpPort;
    if (!want) {
        if (m_mcp.Running()) { m_mcp.Stop(); m_mcpRunningPort = 0; }
        return;
    }
    m_mcp.SetReadOnly(m_settings.mcpReadOnly);
    m_mcp.SetToken(m_settings.mcpToken);
    if (m_mcp.Running() && m_mcpRunningPort == port) return;
    const double now = NowSeconds();
    if (m_mcpRunningPort == port && m_mcpRetryTime >= 0.0 && now - m_mcpRetryTime < 10.0) return;   // a failed start is tried again now and then
    m_mcpRetryTime = now;
    m_mcpRunningPort = port;
    m_mcp.Start(port, [this](std::shared_ptr<McpCall> call) {
        { std::lock_guard<std::mutex> lock(m_mcpMutex); m_mcpQueue.push_back(std::move(call)); }
        if (m_hwnd) PostMessageW(m_hwnd, WM_NULL, 0, 0);   // wakes the minimized loop
    });
}

void App::StopMcp() {
    m_mcp.Stop();
    std::deque<std::shared_ptr<McpCall>> queue;
    { std::lock_guard<std::mutex> lock(m_mcpMutex); queue.swap(m_mcpQueue); }
    for (const auto& c : queue) c->Fail("the program is closing");
    for (const McpWaiter& w : m_mcpWaiters) w.call->Fail("the program is closing");
    m_mcpWaiters.clear();
    for (const McpPreview& p : m_mcpPreviews) p.call->Fail("the program is closing");
    m_mcpPreviews.clear();
    if (m_mcpShot.call) { m_mcpShot.call->Fail("the program is closing"); m_mcpShot = McpPreview{}; }
    m_mcpRunningPort = 0;
}

std::string App::McpConfigText() const {
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    return McpServer::ClientConfig(exe);
}

// The interface's picture of the server (sidebar section).
void App::FillMcpInfo(ui::UiFrameInfo& info) const {
    const McpServer::Status st = m_mcp.Get();
    info.mcpRunning = st.running;
    info.mcpError = st.error;
    info.mcpUrl = McpServer::Url(st.running ? st.port : (m_mcpSessionPort > 0 ? m_mcpSessionPort : m_settings.mcpPort));
    info.mcpCalls = st.calls;
    info.mcpLastTool = st.lastTool;
    info.mcpLastAge = st.lastTime >= 0.0 ? NowSeconds() - st.lastTime : -1.0;
    info.mcpSession = m_mcpSessionPort > 0;
    info.mcpConfig = McpConfigText();
}

// The queued calls run now; the waiters are looked at every frame.
void App::DrainMcp(const ui::UiFrameInfo& info, ui::UiEvents& ev) {
    std::deque<std::shared_ptr<McpCall>> queue;
    { std::lock_guard<std::mutex> lock(m_mcpMutex); queue.swap(m_mcpQueue); }
    for (const auto& call : queue) {
        Log::Info("MCP: %s %s", call->name.c_str(), call->args.Dump().c_str());
        McpExecute(call, info, ev);
    }
    McpCheckWaiters();
}

bool App::McpIdle() const {
    return !m_source.batchRunning && !m_libraryBatchRunning && !m_source.videoProcessing && !m_source.videoFinishing &&
           m_capture.Pending() == 0 && m_status.capturesInFlight == 0 && !m_pipeline.CapturePending();
}

// --- what the tools answer with ------------------------------------------------------------------

Json App::McpSourceJson() const {
    const SourceInfo& s = m_source;
    Json j = Json::Obj()
        .Set("mode", m_settings.sourceMode == SourceImage ? "picture" : m_settings.sourceMode == SourceVideo ? "video" : "live")
        .Set("connected", s.connected)
        .Set("hasFrame", s.hasFrame)
        .Set("processingFps", s.processingFps);
    if (m_settings.sourceMode == SourceSpout) {
        j.Set("sender", s.senderName).Set("senderFps", s.senderFps).Set("format", s.format).Set("hdr", s.isHdr)
         .Set("rotate", m_settings.spoutRotate).Set("flipH", m_settings.spoutFlipH).Set("flipV", m_settings.spoutFlipV);
    } else if (m_settings.sourceMode == SourceImage) {
        Json pic = Json::Obj().Set("path", WideToUtf8(s.imagePath)).Set("name", s.imageName).Set("loaded", s.imageLoaded)
            .Set("width", s.imageWidth).Set("height", s.imageHeight).Set("fileWidth", s.imageOrigWidth).Set("fileHeight", s.imageOrigHeight)
            .Set("converging", s.imageConverging);
        j.Set("picture", pic);
    } else {
        Json vid = Json::Obj().Set("path", WideToUtf8(s.videoPath)).Set("name", s.videoName).Set("loaded", s.videoLoaded)
            .Set("codec", s.videoCodec).Set("width", s.videoWidth).Set("height", s.videoHeight).Set("fps", s.videoFps)
            .Set("frames", s.videoFrames).Set("duration", s.videoDurationSeconds).Set("hasAudio", s.videoHasAudio)
            .Set("animation", AnimFormatName((AnimFormat)s.videoAnimation))
            .Set("position", s.videoPosition).Set("playing", s.videoPlaying).Set("seeking", s.videoSeeking)
            .Set("in", s.videoIn).Set("out", s.videoOut).Set("converging", s.imageConverging)
            .Set("processing", s.videoProcessing).Set("finishing", s.videoFinishing);
        if (s.videoProcessing || s.videoFinishing) vid.Set("frame", s.videoFrame).Set("elapsed", s.videoElapsed).Set("output", s.videoOutName);
        j.Set("video", vid);
    }
    return j;
}

Json App::McpStatusJson() const {
    const PipelineStatus& st = m_status;
    const AdapterInfo& ad = m_device.Info();
    int current = 0;
    const std::vector<std::string> labels = m_ui.HistoryLabels(current);
    Json j = Json::Obj()
        .Set("version", APP_VERSION_STRING)
        .Set("edition", APP_EDITION_AMD ? "Radeon" : "GeForce")
        .Set("adapter", Json::Obj().Set("name", WideToUtf8(ad.name)).Set("vramMb", (long long)(ad.dedicatedVideoMemory >> 20))
             .Set("driver", WideToUtf8(ad.nvidiaDriverVersion.empty() ? ad.driverVersion : ad.nvidiaDriverVersion)))
        .Set("source", McpSourceJson())
        .Set("output", Json::Obj().Set("width", st.outWidth).Set("height", st.outHeight).Set("inputWidth", st.srcWidth).Set("inputHeight", st.srcHeight)
             .Set("superResolution", st.upscaleMode == 1 ? st.srQualityName : st.upscaleMode == 2 ? "resampled" : ""))
        .Set("neural", Json::Obj().Set("enabled", m_settings.nrEnabled).Set("captureOnly", m_settings.nrCaptureOnly)
             .Set("runtimeLoaded", st.nrRuntimeLoaded).Set("runtimeVersion", st.nrRuntimeVersion).Set("runtimeIdle", st.nrRuntimeIdle)
             .Set("active", st.nrActive).Set("standby", st.nrStandby).Set("failed", st.nrFailed).Set("error", st.nrError)
             .Set("evaluations", st.nrEvaluations).Set("passWidth", st.nrPassWidth).Set("passHeight", st.nrPassHeight).Set("passCapped", st.nrPassCapped)
             .Set("outputCheck", st.nrOutState == 1 ? "ok" : st.nrOutState == 2 ? "black" : st.nrOutState == 3 ? "unchanged" : st.nrOutState == 4 ? "noise" : "pending")
             .Set("amdPort", st.nrAmdPort).Set("fsrLoaded", st.nrFsrLoaded))
        .Set("guidance", Json::Obj().Set("motion", st.motionModeActive).Set("depth", st.depthModeActive).Set("depthState", st.depthState)
             .Set("depthBackend", st.depthBackend).Set("depthMessage", st.depthMessage).Set("depthPending", st.depthPending)
             .Set("flowLevels", st.flowLevels).Set("sceneCut", st.sceneCut).Set("resets", st.resets))
        .Set("dlaa", Json::Obj().Set("enabled", m_settings.dlaaEnabled).Set("active", st.dlaaActive).Set("failed", st.dlaaFailed).Set("error", st.dlaaError).Set("tooLarge", st.dlaaTooLarge))
        .Set("running", Json::Obj().Set("idle", McpIdle())
             .Set("batch", Json::Obj().Set("running", m_source.batchRunning || m_libraryBatchRunning).Set("index", m_source.batchIndex).Set("count", m_source.batchCount)
                  .Set("done", m_source.batchDone).Set("failed", m_source.batchFailed).Set("item", m_source.batchItemName))
             .Set("videoProcessing", m_source.videoProcessing || m_source.videoFinishing)
             .Set("capturesPending", (long long)(m_capture.Pending() + st.capturesInFlight)))
        .Set("capture", Json::Obj().Set("folder", WideToUtf8(EffectiveCaptureFolder())).Set("last", m_lastCapture).Set("lastOk", m_lastCaptureOk))
        .Set("library", Json::Obj().Set("count", (long long)m_library.size())
             .Set("selected", (long long)std::count_if(m_library.begin(), m_library.end(), [](const LibraryItem& i) { return i.selected; })))
        .Set("history", Json::Obj().Set("entries", (long long)labels.size()).Set("current", current)
             .Set("canUndo", current > 0).Set("canRedo", current + 1 < (int)labels.size()))
        .Set("display", Json::Obj().Set("compareMode", CompareName(m_settings.compareMode)).Set("hasPicture", st.hasDisplay)
             .Set("language", I18n::LanguageName(I18n::Current())).Set("theme", m_settings.theme))
        .Set("window", Json::Obj().Set("minimized", m_minimized).Set("fullscreen", m_fullscreen).Set("headless", m_headless)
             .Set("width", m_settings.windowWidth).Set("height", m_settings.windowHeight)
             .Set("sidebar", m_settings.sidebarVisible).Set("libraryStrip", m_settings.libraryVisible))
        .Set("rates", Json::Obj().Set("uiFps", m_fps).Set("processingFps", m_source.processingFps).Set("frameIntervalMs", st.frameIntervalMs)
             .Set("neuralMs", st.gpuMs[(UINT)GpuTimer::Neural]).Set("processedFrames", st.processedFrames))
        .Set("mcp", Json::Obj().Set("readOnly", m_settings.mcpReadOnly).Set("calls", m_mcp.Get().calls));
    return j;
}

Json App::McpLibraryJson() const {
    Json items = Json::Arr();
    for (const LibraryItem& it : m_library) items.Push(ItemJson(it));
    return Json::Obj().Set("count", (long long)m_library.size()).Set("items", items).Set("running", m_source.batchRunning || m_libraryBatchRunning);
}

// --- the tools --------------------------------------------------------------------------------------

void App::McpAddWaiter(const std::shared_ptr<McpCall>& call, int kind, double timeout, const std::wstring& path, bool video) {
    McpWaiter w;
    w.call = call;
    w.kind = kind;
    w.path = path;
    w.video = video;
    w.deadline = NowSeconds() + std::clamp(timeout, 1.0, 3600.0);
    w.captureBase = m_captureResultsSeen;
    w.loadBase = m_source.loadFailures;
    w.runBase = m_source.videoRuns;
    m_mcpWaiters.push_back(std::move(w));
    McpCheckWaiters();   // may be done already
}

void App::McpCheckWaiters() {
    const double now = NowSeconds();
    for (size_t i = 0; i < m_mcpWaiters.size();) {
        McpWaiter& w = m_mcpWaiters[i];
        const double elapsed = now - w.call->started;
        bool done = false;
        switch (w.kind) {
        case WaitIdle:
            if (elapsed >= 1.0 && McpIdle()) { Reply(w.call, Json::Obj().Set("idle", true).Set("seconds", elapsed).Set("lastCapture", m_lastCapture)); done = true; }
            break;
        case WaitLoaded:
        case WaitOpen: {
            const bool loaded = w.video ? (m_source.videoLoaded && SamePathCi(m_source.videoPath, w.path))
                                        : (m_source.imageLoaded && SamePathCi(m_source.imagePath, w.path));
            if (loaded && (w.kind == WaitLoaded || (m_source.hasFrame && elapsed >= 0.2))) {
                Reply(w.call, Json::Obj().Set("loaded", true).Set("seconds", elapsed).Set("source", McpSourceJson()));
                done = true;
            } else if (m_source.loadFailures != w.loadBase) {
                w.call->Fail("the file could not be loaded: " + FileName(w.path) + " (" + m_source.loadError + ")");
                done = true;
            }
            break;
        }
        case WaitConverged: {
            const bool loaded = m_settings.sourceMode == SourceImage ? m_source.imageLoaded : m_source.videoLoaded;
            if (loaded && m_source.hasFrame && !m_source.imageConverging && elapsed >= 0.5) {
                Reply(w.call, Json::Obj().Set("converged", true).Set("seconds", elapsed).Set("neuralEvaluations", m_status.nrEvaluations));
                done = true;
            }
            break;
        }
        case WaitDisplay:
            if (m_status.hasDisplay && m_source.hasFrame) { Reply(w.call, Json::Obj().Set("display", true).Set("seconds", elapsed)); done = true; }
            break;
        case WaitVideoState:
            // The processing thread publishes its state every quarter second: the answer carries the refreshed one.
            if (elapsed >= 0.35) { Reply(w.call, McpSourceJson()); done = true; }
            break;
        case WaitCapture:
            if (m_captureResultsSeen > w.captureBase) {
                if (m_lastCaptureOk) Reply(w.call, Json::Obj().Set("saved", true).Set("file", m_lastCapture).Set("folder", WideToUtf8(EffectiveCaptureFolder())).Set("seconds", elapsed));
                else w.call->Fail("the capture failed: " + m_lastCapture);
                done = true;
            }
            break;
        case WaitVideoRun:
            if (m_source.videoProcessing || m_source.videoFinishing) w.started = true;
            if (m_source.videoRuns > w.runBase) {   // the run ended: the processing thread published how
                if (m_source.videoLastOk)
                    Reply(w.call, Json::Obj().Set("finished", true).Set("output", m_source.videoLastOut).Set("folder", WideToUtf8(EffectiveCaptureFolder()))
                          .Set("frames", m_source.videoLastFrames).Set("processingSeconds", m_source.videoLastSeconds).Set("seconds", elapsed));
                else w.call->Fail("the video run failed: " + m_source.videoLastError);
                done = true;
            } else if (!w.started && elapsed > 5.0) { w.call->Fail("the video run did not start (get_log tells why)"); done = true; }
            break;
        case WaitBatch:
            if (m_source.batchRunning || m_libraryBatchRunning) w.started = true;
            // The last picture of a batch may still be in the PNG encoder when the batch reports finished: its name arrives with the capture result.
            if (w.started && !m_source.batchRunning && !m_libraryBatchRunning && m_capture.Pending() == 0 && m_status.capturesInFlight == 0 && w.settleTime < 0.0) w.settleTime = now;
            if (w.settleTime >= 0.0 && now - w.settleTime >= 0.1) {
                int doneN = 0, failedN = 0;
                Json results = Json::Arr();
                for (const LibraryItem& it : m_library) {
                    if (it.state == LibraryItem::Done) ++doneN; else if (it.state == LibraryItem::Failed) ++failedN;
                    if (it.state == LibraryItem::Done || it.state == LibraryItem::Failed)
                        results.Push(Json::Obj().Set("id", it.id).Set("name", it.name).Set("state", ItemStateName(it.state)).Set("output", it.outName).Set("error", it.error));
                }
                Reply(w.call, Json::Obj().Set("finished", true).Set("done", doneN).Set("failed", failedN).Set("folder", WideToUtf8(EffectiveCaptureFolder()))
                      .Set("items", results).Set("seconds", elapsed));
                done = true;
            } else if (!w.started && elapsed > 5.0) { w.call->Fail("the run did not start (get_log tells why)"); done = true; }
            break;
        }
        if (!done && now > w.deadline) {
            w.call->Fail(StrPrintf("timeout after %.0f s (the state is in get_status)", elapsed));
            done = true;
        }
        if (done) m_mcpWaiters.erase(m_mcpWaiters.begin() + (ptrdiff_t)i);
        else ++i;
    }
}

void App::McpSetVideoRange(double in, double out) {
    if (out > 0.0 && out <= in) out = 0.0;
    if (in < 0.0) in = 0.0;
    Command c; c.type = Command::VideoSetRange; c.seconds = in; c.seconds2 = out;
    PostCommand(std::move(c));
    m_mcpRangeIn = in; m_mcpRangeOut = out; m_mcpRangeTime = NowSeconds();
    for (LibraryItem& item : m_library)
        if (item.isVideo && SamePathCi(item.path, m_source.videoPath)) { item.inSec = in; item.outSec = out; }
}

void App::McpExecute(const std::shared_ptr<McpCall>& call, const ui::UiFrameInfo& info, ui::UiEvents& ev) {
    const std::string& name = call->name;
    const Json& a = call->args;
    const bool busy = m_source.batchRunning || m_libraryBatchRunning || m_source.videoProcessing || m_source.videoFinishing;

    if (name == "get_status") { Reply(call, McpStatusJson()); return; }

    if (name == "describe_settings") {
        const std::string group = a.Str("group"), key = a.Str("key");
        std::vector<std::string> keys;
        if (const Json* arr = a.Find("keys")) if (arr->type == Json::Array) for (const Json& v : arr->arr) if (v.type == Json::String) keys.push_back(v.str);
        const auto values = Pairs(m_settings.Text());
        Json list = Json::Arr();
        for (const McpSettingInfo& s : McpServer::SettingInfos()) {
            if (!group.empty() && group != s.group) continue;
            if (!key.empty() && key != s.key) continue;
            if (!keys.empty() && std::find(keys.begin(), keys.end(), s.key) == keys.end()) continue;
            Json e = Json::Obj().Set("key", s.key).Set("group", s.group).Set("type", s.type);
            if (s.range[0]) e.Set(strcmp(s.type, "enum") == 0 ? "values" : "range", s.range);
            e.Set("description", s.description);
            for (const auto& kv : values) if (kv.first == s.key) { e.Set("value", std::string(s.key) == "mcpToken" && !kv.second.empty() ? Json("***") : TypedValue(&s, kv.second)); break; }
            list.Push(e);
        }
        if (list.arr.empty()) { call->Fail("no such group or key; groups: source, video, resolution, hdr, neural, blend, guidance, dlaa, display, capture, hotkey, window, updates, mcp"); return; }
        Reply(call, Json::Obj().Set("settings", list));
        return;
    }

    if (name == "get_settings") {
        const Json* keys = a.Find("keys");
        Json out = Json::Obj();
        Json unknown = Json::Arr();
        const auto values = Pairs(m_settings.Text());
        if (keys && keys->type == Json::Array && !keys->arr.empty()) {
            for (const Json& k : keys->arr) {
                if (k.type != Json::String) continue;
                bool found = false;
                for (const auto& kv : values) if (kv.first == k.str) { out.Set(kv.first.c_str(), k.str == "mcpToken" && !kv.second.empty() ? Json("***") : TypedValue(McpServer::FindSetting(kv.first), kv.second)); found = true; break; }
                if (!found) unknown.Push(k.str);
            }
        } else {
            for (const auto& kv : values) out.Set(kv.first.c_str(), kv.first == "mcpToken" && !kv.second.empty() ? Json("***") : TypedValue(McpServer::FindSetting(kv.first), kv.second));
        }
        Json r = Json::Obj().Set("settings", out);
        if (!unknown.arr.empty()) r.Set("unknown", unknown);
        Reply(call, r);
        return;
    }

    if (name == "set_settings") {
        const Json* obj = a.Find("settings");
        if (!obj || obj->type != Json::Object || obj->obj.empty()) { call->Fail("settings must be an object of key: value pairs (describe_settings lists the keys)"); return; }
        std::map<std::string, std::string> before;
        for (const auto& kv : Pairs(m_settings.Text())) before[kv.first] = kv.second;
        const std::string procBefore = m_settings.ProcessingText();
        const Settings old = m_settings;
        Json unknown = Json::Arr(), refused = Json::Arr();
        std::vector<std::string> asked;
        for (const auto& kv : obj->obj) {
            const std::string& k = kv.first;
            if (k == "imagePath" || k == "videoPath") { refused.Push(k + ": use the open tool"); continue; }
            if (k == "settingsVersion") { refused.Push(k); continue; }
            if (k == "mcpEnabled" || k == "mcpPort" || k == "mcpReadOnly" || k == "mcpToken") { refused.Push(k + ": the user sets up the MCP server in the sidebar"); continue; }
            std::string text = kv.second.Text();
            if (kv.second.type == Json::Null || kv.second.type == Json::Array || kv.second.type == Json::Object) { refused.Push(k + ": a scalar value is needed"); continue; }
            if (!m_settings.Apply(k, text)) { unknown.Push(k); continue; }
            asked.push_back(k);
        }
        m_settings.Clamp();
        std::map<std::string, std::string> after;
        for (const auto& kv : Pairs(m_settings.Text())) after[kv.first] = kv.second;
        Json applied = Json::Obj(), unchanged = Json::Arr();
        bool any = false;
        for (const std::string& k : asked) {
            if (before[k] == after[k]) { unchanged.Push(k); continue; }
            any = true;
            applied.Set(k.c_str(), k == "mcpToken" ? Json("***") : TypedValue(McpServer::FindSetting(k), after[k]));
        }
        if (any) {
            ev.settingsChanged = true;
            if (m_settings.ProcessingText() != procBefore) ev.nrChanged = true;
            if (old.dlaaEnabled != m_settings.dlaaEnabled || old.dlaaPreset != m_settings.dlaaPreset) ev.dlaaChanged = true;
            if (old.nrEnabled != m_settings.nrEnabled || old.dlaaEnabled != m_settings.dlaaEnabled) ev.resetHistory = true;
            if (old.senderName != m_settings.senderName) ev.senderChanged = true;
            if (old.hotkeyEnabled != m_settings.hotkeyEnabled || old.hotkeyModifiers != m_settings.hotkeyModifiers || old.hotkeyKey != m_settings.hotkeyKey) ev.hotkeyChanged = true;
            if (old.language != m_settings.language) ev.languageChanged = true;
            if (old.sourceMode != m_settings.sourceMode) ev.sourceModeChanged = true;
            if (old.spoutRotate != m_settings.spoutRotate || old.spoutFlipH != m_settings.spoutFlipH || old.spoutFlipV != m_settings.spoutFlipV) ev.nrChanged = true;
            if (old.sidebarVisible != m_settings.sidebarVisible || old.libraryVisible != m_settings.libraryVisible || old.showLog != m_settings.showLog) { /* the interface reads the settings */ }
        }
        Json r = Json::Obj().Set("applied", applied);
        if (!unchanged.arr.empty()) r.Set("unchanged", unchanged);
        if (!unknown.arr.empty()) r.Set("unknown", unknown);
        if (!refused.arr.empty()) r.Set("refused", refused);
        if (!unchanged.arr.empty()) r.Set("note", "unchanged: the value was already so, or the clamp to its range brought it back");
        Reply(call, r);
        return;
    }

    if (name == "reset_settings") { ev.resetDefaults = true; ReplyText(call, "every setting is back to its default (window placement, language, the opened file and the MCP server settings kept)"); return; }

    if (name == "open") {
        if (busy) { call->Fail("a run is going: wait for it or cancel it first"); return; }
        const std::wstring path = Utf8ToWide(a.Str("path"));
        if (path.empty()) { call->Fail("path is required"); return; }
        if (!FileExists(path)) { call->Fail("no such file: " + WideToUtf8(path)); return; }
        if (DirectoryExists(path)) { call->Fail("a folder: use library add for folders"); return; }
        if (LowerExt(path) == L".dll") { call->Fail("a DLL is not a picture: the neural runtime is chosen with set_settings nrDllPath"); return; }
        if (a.Flag("add_to_library", true)) AddLibraryFiles({ path }, false);
        bool isVideo = false;
        bool known = false;
        for (const LibraryItem& it : m_library) if (SamePathCi(it.path, path)) { isVideo = it.isVideo; known = true; break; }
        if (!known) {
            // Not a library file type (or the library is full): the drop path decides.
            OnFileDropped(path);
            if (m_settings.sourceMode == SourceSpout ||
                !SamePathCi(Utf8ToWide(m_settings.sourceMode == SourceVideo ? m_settings.videoPath : m_settings.imagePath), path)) {
                call->Fail("not a picture or video this program opens: " + FileName(path));
                return;
            }
            isVideo = m_settings.sourceMode == SourceVideo;
        } else if (isVideo) OpenVideoFile(path);
        else OpenImageFile(path);
        if (a.Flag("wait", true)) McpAddWaiter(call, WaitOpen, a.Num("timeout", 30.0), path, isVideo);
        else Reply(call, Json::Obj().Set("opening", WideToUtf8(path)).Set("type", isVideo ? "video" : "picture"));
        return;
    }

    if (name == "open_live") {
        if (busy) { call->Fail("a run is going: wait for it or cancel it first"); return; }
        const std::string sender = a.Str("sender");
        if (a.Has("sender") && sender != m_settings.senderName) { m_settings.senderName = sender; ev.senderChanged = true; ev.settingsChanged = true; }
        if (m_settings.sourceMode != SourceSpout) { m_settings.sourceMode = SourceSpout; ev.sourceModeChanged = true; ev.settingsChanged = true; }
        ev.refreshSenders = true;
        Json senders = Json::Arr();
        for (const std::string& s : m_senders) senders.Push(s);
        Reply(call, Json::Obj().Set("mode", "live").Set("requestedSender", m_settings.senderName).Set("connected", m_source.connected)
              .Set("sender", m_source.senderName).Set("senders", senders).Set("note", "get_status tells when a sender is received"));
        return;
    }

    if (name == "list_senders") {
        ev.refreshSenders = true;
        Json senders = Json::Arr();
        for (const std::string& s : m_senders) senders.Push(s);
        Reply(call, Json::Obj().Set("senders", senders).Set("inUse", m_source.senderName).Set("requested", m_settings.senderName)
              .Set("connected", m_source.connected && m_settings.sourceMode == SourceSpout).Set("live", m_settings.sourceMode == SourceSpout));
        return;
    }

    if (name == "close_media") {
        if (busy) { call->Fail("a run is going: wait for it or cancel it first"); return; }
        if (m_settings.sourceMode == SourceSpout) { call->Fail("the live source is shown, nothing to close"); return; }
        ev.closeMedia = true;
        ReplyText(call, "closed");
        return;
    }

    if (name == "library") {
        const std::string action = a.Str("action");
        auto ids = [&]() {
            std::vector<unsigned> out;
            if (const Json* arr = a.Find("ids")) if (arr->type == Json::Array) for (const Json& v : arr->arr) if (v.type == Json::Number) out.push_back((unsigned)v.num);
            if (a.Has("id")) out.push_back((unsigned)a.Int("id"));
            return out;
        };
        if (action == "list") { Reply(call, McpLibraryJson()); return; }
        if (action == "add") {
            std::vector<std::wstring> paths;
            if (const Json* arr = a.Find("paths")) if (arr->type == Json::Array) for (const Json& v : arr->arr) if (v.type == Json::String) paths.push_back(Utf8ToWide(v.str));
            if (paths.empty()) { call->Fail("paths is required"); return; }
            for (const std::wstring& p : paths) if (!FileExists(p) && !DirectoryExists(p)) { call->Fail("no such file or folder: " + WideToUtf8(p)); return; }
            const size_t before = m_library.size();
            AddLibraryFiles(paths, true);
            Json added = Json::Arr();
            for (size_t i = before; i < m_library.size(); ++i) added.Push(ItemJson(m_library[i]));
            Reply(call, Json::Obj().Set("added", (long long)(m_library.size() - before)).Set("items", added).Set("count", (long long)m_library.size()));
            return;
        }
        if (busy && action != "select") { call->Fail("a run is going: the library is locked until it ends"); return; }
        if (action == "remove") {
            int n = 0;
            for (unsigned id : ids()) if (FindItem(id)) { RemoveLibraryItem(id); ++n; }
            Reply(call, Json::Obj().Set("removed", n).Set("count", (long long)m_library.size()));
            return;
        }
        if (action == "clear") { const size_t n = m_library.size(); ClearLibrary(); Reply(call, Json::Obj().Set("removed", (long long)n)); return; }
        if (action == "select") {
            const std::vector<unsigned> want = ids();
            const bool exclusive = a.Flag("exclusive", true);
            int n = 0;
            for (LibraryItem& it : m_library) {
                const bool sel = std::find(want.begin(), want.end(), it.id) != want.end();
                if (sel) { it.selected = true; ++n; } else if (exclusive) it.selected = false;
            }
            Reply(call, Json::Obj().Set("selected", n));
            return;
        }
        if (action == "show") {
            const unsigned id = (unsigned)a.Int("id");
            const LibraryItem* it = FindItem(id);
            if (!it) { call->Fail("no item with this id"); return; }
            if (it->probe == 2) { call->Fail("the file could not be read: " + it->error); return; }
            PreviewLibraryItem(id);
            McpAddWaiter(call, WaitOpen, a.Num("timeout", 30.0), it->path, it->isVideo);
            return;
        }
        if (action == "set_range") {
            LibraryItem* it = FindItem((unsigned)a.Int("id"));
            if (!it) { call->Fail("no item with this id"); return; }
            if (!it->isVideo) { call->Fail("a range applies to videos"); return; }
            double in = std::max(0.0, a.Num("in", it->inSec)), out = std::max(0.0, a.Num("out", it->outSec));
            if (out > 0.0 && out <= in) { call->Fail("out must be after in (0 = the end)"); return; }
            it->inSec = in; it->outSec = out;
            if (m_settings.sourceMode == SourceVideo && SamePathCi(m_source.videoPath, it->path)) McpSetVideoRange(in, out);
            Reply(call, Json::Obj().Set("id", it->id).Set("in", in).Set("out", out));
            return;
        }
        if (action == "set_own" || action == "clear_own") {
            std::vector<unsigned> want = ids();
            if (want.empty()) { call->Fail("id or ids is required"); return; }
            int n = 0;
            for (unsigned id : want) {
                LibraryItem* it = FindItem(id);
                if (!it) continue;
                if (action == "clear_own") { it->useOwn = false; ++n; continue; }
                if (!it->own) it->own = std::make_shared<Settings>(m_settings);
                if (const Json* s = a.Find("settings")) {
                    if (s->type == Json::Object) for (const auto& kv : s->obj) it->own->Apply(kv.first, kv.second.Text());
                    it->own->Clamp();
                }
                it->useOwn = true;
                ++n;
            }
            ev.itemParamsChanged = true;
            Json items = Json::Arr();
            for (unsigned id : want) if (const LibraryItem* it = FindItem(id)) items.Push(ItemJson(*it));
            Reply(call, Json::Obj().Set("changed", n).Set("items", items));
            return;
        }
        call->Fail("unknown action: " + action);
        return;
    }

    if (name == "process") {
        if (busy) { call->Fail("a run is already going"); return; }
        if (m_library.empty()) { call->Fail("the library is empty: open a file or use library add"); return; }
        const std::string scope = a.Str("scope", "all");
        bool selectedOnly = scope == "selected";
        if (scope == "ids") {
            std::vector<unsigned> want;
            if (const Json* arr = a.Find("ids")) if (arr->type == Json::Array) for (const Json& v : arr->arr) if (v.type == Json::Number) want.push_back((unsigned)v.num);
            if (want.empty()) { call->Fail("ids is required with scope ids"); return; }
            for (LibraryItem& it : m_library) it.selected = std::find(want.begin(), want.end(), it.id) != want.end();
            selectedOnly = true;
        }
        int count = 0;
        for (const LibraryItem& it : m_library) if ((!selectedOnly || it.selected) && it.probe != 2) ++count;
        if (count == 0) { call->Fail(selectedOnly ? "nothing selected" : "nothing to process"); return; }
        if (selectedOnly) ev.libraryProcessSelected = true; else ev.libraryProcessAll = true;
        if (a.Flag("wait", false)) McpAddWaiter(call, WaitBatch, a.Num("timeout", 600.0), std::wstring(), false);
        else Reply(call, Json::Obj().Set("started", count).Set("folder", WideToUtf8(EffectiveCaptureFolder())).Set("note", "wait for=idle or get_status follow the run"));
        return;
    }

    if (name == "capture") {
        if (m_source.batchRunning || m_libraryBatchRunning) { call->Fail("a batch run is going"); return; }
        int kind = WaitCapture;
        if (m_settings.sourceMode == SourceVideo) {
            if (!m_source.videoLoaded) { call->Fail("no video is loaded"); return; }
            if (m_source.videoProcessing || m_source.videoFinishing) { call->Fail("the video is already being processed"); return; }
            kind = WaitVideoRun;
        } else if (m_settings.sourceMode == SourceImage) {
            if (!m_source.imageLoaded) { call->Fail("no picture is loaded"); return; }
        } else if (!m_source.connected) { call->Fail("no live stream is received (list_senders)"); return; }
        ev.captureNow = true;
        if (a.Flag("wait", true)) McpAddWaiter(call, kind, a.Num("timeout", 180.0), std::wstring(), false);
        else Reply(call, Json::Obj().Set("started", true).Set("folder", WideToUtf8(EffectiveCaptureFolder())));
        return;
    }

    if (name == "cancel") {
        bool any = false;
        if (m_source.videoProcessing || m_source.videoFinishing) { ev.cancelVideo = true; any = true; }
        if (m_source.batchRunning || m_libraryBatchRunning) { ev.batchCancel = true; any = true; }
        ReplyText(call, any ? "cancelling" : "nothing is running");
        return;
    }

    if (name == "video") {
        const std::string action = a.Str("action");
        if (action == "info") { Reply(call, McpSourceJson()); return; }
        if (m_settings.sourceMode != SourceVideo || !m_source.videoLoaded) { call->Fail("no video is loaded (open one first)"); return; }
        if (m_source.videoProcessing || m_source.videoFinishing) { call->Fail("the video is being processed: the controls are locked"); return; }
        if (action == "play") { if (!m_source.videoPlaying) ev.videoPlayToggle = true; }
        else if (action == "pause") { if (m_source.videoPlaying) ev.videoPlayToggle = true; }
        else if (action == "toggle") ev.videoPlayToggle = true;
        else if (action == "seek") {
            if (!a.Has("seconds")) { call->Fail("seconds is required"); return; }
            ev.videoSeek = true;
            ev.videoSeekTo = std::clamp(a.Num("seconds"), 0.0, std::max(0.0, m_source.videoDurationSeconds));
        }
        else if (action == "step") ev.videoStep = a.Int("frames", 1);
        else if (action == "set_in" || action == "set_out") {
            double in = m_source.videoIn, out = m_source.videoOut;
            if (m_mcpRangeTime >= 0.0 && NowSeconds() - m_mcpRangeTime < 2.0) { in = m_mcpRangeIn; out = m_mcpRangeOut; }   // one call right after another
            const double at = a.Has("seconds") ? std::max(0.0, a.Num("seconds")) : m_source.videoPosition;
            if (action == "set_in") { in = at; if (out > 0.0 && out <= in) out = 0.0; }
            else { out = at; if (in >= out) in = 0.0; }
            McpSetVideoRange(in, out);
        }
        else if (action == "clear_range") { ev.videoClearRange = true; m_mcpRangeIn = 0.0; m_mcpRangeOut = 0.0; m_mcpRangeTime = NowSeconds(); }
        else { call->Fail("unknown action: " + action); return; }
        McpAddWaiter(call, WaitVideoState, 5.0, std::wstring(), false);
        return;
    }

    if (name == "preview") {
        const std::string view = a.Str("view", "output");
        McpPreview p;
        p.call = call;
        p.view = view == "window" ? 1 : 0;
        p.maxEdge = std::clamp(a.Int("max_edge", 1024), 64, 4096);
        p.saveTo = Utf8ToWide(a.Str("save_to"));
        if (view != "output" && view != "window") { call->Fail("view must be output or window"); return; }
        if (m_minimized && !m_headless) { call->Fail("the window is minimized, nothing is drawn: window restore first"); return; }
        if (p.view == 0 && !info.hasDisplay) { call->Fail("no picture on screen yet (open a file or wait for=display)"); return; }
        m_mcpPreviews.push_back(std::move(p));
        return;
    }

    if (name == "wait") {
        const std::string what = a.Str("for", "idle");
        const double timeout = a.Num("timeout", 60.0);
        if (what == "idle") McpAddWaiter(call, WaitIdle, timeout, std::wstring(), false);
        else if (what == "loaded") {
            if (m_settings.sourceMode == SourceSpout) { Reply(call, Json::Obj().Set("loaded", m_source.connected).Set("live", true).Set("sender", m_source.senderName)); return; }
            const bool video = m_settings.sourceMode == SourceVideo;
            const std::wstring path = Utf8ToWide(video ? m_settings.videoPath : m_settings.imagePath);
            if (path.empty()) { call->Fail("nothing is open"); return; }
            McpAddWaiter(call, WaitLoaded, timeout, path, video);
        }
        else if (what == "converged") {
            if (m_settings.sourceMode == SourceSpout) { Reply(call, Json::Obj().Set("converged", true).Set("live", true).Set("note", "a live stream never settles: each frame is processed as it arrives")); return; }
            if (m_settings.sourceMode == SourceVideo ? !m_source.videoLoaded : !m_source.imageLoaded) { call->Fail("nothing is loaded"); return; }
            McpAddWaiter(call, WaitConverged, timeout, std::wstring(), false);
        }
        else if (what == "display") McpAddWaiter(call, WaitDisplay, timeout, std::wstring(), false);
        else call->Fail("for must be idle, loaded, converged or display");
        return;
    }

    if (name == "undo" || name == "redo") {
        const bool redo = name == "redo";
        const int steps = std::clamp(a.Int("steps", 1), 1, 100);
        int current = 0;
        std::vector<std::string> labels = m_ui.HistoryLabels(current);
        const int can = redo ? (int)labels.size() - 1 - current : current;
        if (can <= 0) { call->Fail(redo ? "nothing to redo" : "nothing to undo"); return; }
        const int n = std::min(steps, can);
        for (int i = 0; i < n; ++i) m_ui.ExternalUndo(m_settings, info, ev, redo);
        labels = m_ui.HistoryLabels(current);
        const std::string label = labels[(size_t)current].empty() ? TR(HistoryInitial) : labels[(size_t)current];
        Reply(call, Json::Obj().Set(redo ? "redone" : "undone", n).Set("current", current).Set("state", label));
        return;
    }

    if (name == "history") {
        int current = 0;
        std::vector<std::string> labels = m_ui.HistoryLabels(current);
        if (a.Has("index")) {
            const int idx = std::clamp(a.Int("index"), 0, (int)labels.size() - 1);
            m_ui.ExternalGoToHistory(m_settings, info, ev, idx);
            labels = m_ui.HistoryLabels(current);
        }
        Json list = Json::Arr();
        for (size_t i = 0; i < labels.size(); ++i)
            list.Push(Json::Obj().Set("index", (long long)i).Set("label", labels[i].empty() ? TR(HistoryInitial) : labels[i]).Set("current", (int)i == current));
        Reply(call, Json::Obj().Set("current", current).Set("entries", list));
        return;
    }

    if (name == "presets") {
        const std::string action = a.Str("action"), pname = a.Str("name");
        auto find = [&]() -> int { for (size_t i = 0; i < m_presets.size(); ++i) if (m_presets[i].name == pname) return (int)i; return -1; };
        if (action == "list") {
            Json list = Json::Arr();
            const std::string current = m_settings.EffectText();
            for (const ui::UserPreset& pr : m_presets) {
                Json values = Json::Obj();
                for (const auto& kv : Pairs(pr.text)) values.Set(kv.first.c_str(), TypedValue(McpServer::FindSetting(kv.first), kv.second));
                list.Push(Json::Obj().Set("name", pr.name).Set("active", pr.text == current).Set("values", values));
            }
            Reply(call, Json::Obj().Set("presets", list));
            return;
        }
        if (pname.empty()) { call->Fail("name is required"); return; }
        if (action == "apply") {
            const int i = find();
            if (i < 0) { call->Fail("no preset named " + pname); return; }
            Settings tmp = m_settings;
            tmp.ApplyText(m_presets[(size_t)i].text);
            m_settings.CopyEffects(tmp);
            m_settings.Clamp();
            ev.nrChanged = true; ev.settingsChanged = true;
            Reply(call, Json::Obj().Set("applied", pname));
            return;
        }
        if (action == "save") { ev.presetSave = true; ev.presetName = pname; Reply(call, Json::Obj().Set("saved", pname).Set("replaced", find() >= 0)); return; }
        if (action == "delete") { const int i = find(); if (i < 0) { call->Fail("no preset named " + pname); return; } ev.presetDelete = i; Reply(call, Json::Obj().Set("deleted", pname)); return; }
        if (action == "rename") {
            const int i = find();
            const std::string nn = a.Str("new_name");
            if (i < 0) { call->Fail("no preset named " + pname); return; }
            if (nn.empty()) { call->Fail("new_name is required"); return; }
            ev.presetRename = i; ev.presetName = nn;
            Reply(call, Json::Obj().Set("renamed", pname).Set("to", nn));
            return;
        }
        call->Fail("unknown action: " + action);
        return;
    }

    if (name == "get_log") {
        const int lines = std::clamp(a.Int("lines", 50), 1, 500);
        const std::string level = a.Str("level"), contains = a.Str("contains");
        const int minLevel = level == "error" ? 2 : level == "warn" ? 1 : 0;
        const std::vector<LogEntry> all = Log::Snapshot();
        std::vector<const LogEntry*> picked;
        for (size_t i = all.size(); i-- > 0 && (int)picked.size() < lines;) {
            const LogEntry& e = all[i];
            if ((int)e.level < minLevel) continue;
            if (!contains.empty() && e.text.find(contains) == std::string::npos) continue;
            picked.push_back(&e);
        }
        std::string text;
        for (size_t i = picked.size(); i-- > 0;) {
            const LogEntry& e = *picked[i];
            text += e.time + (e.level == LogLevel::Error ? " [E] " : e.level == LogLevel::Warn ? " [W] " : " ") + e.text + "\n";
        }
        if (text.empty()) text = "(no matching lines)\n";
        text += "log file: " + WideToUtf8(Log::FilePath());
        ReplyText(call, text);
        return;
    }

    if (name == "window") {
        const std::string action = a.Str("action");
        if (m_headless && action != "sidebar" && action != "library") { call->Fail("the program runs headless: no window"); return; }
        if (action == "show") { if (m_hwnd) { ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW); SetForegroundWindow(m_hwnd); } }
        else if (action == "restore") { if (m_hwnd) ShowWindow(m_hwnd, SW_RESTORE); }
        else if (action == "minimize") { if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE); }
        else if (action == "fullscreen") SetFullscreen(true);
        else if (action == "windowed") SetFullscreen(false);
        else if (action == "resize") {
            const int w = a.Int("width"), h = a.Int("height");
            if (w < 640 || h < 400 || w > 16384 || h > 16384) { call->Fail("width and height are needed (640x400 up to 16384)"); return; }
            if (m_fullscreen) SetFullscreen(false);
            if (m_hwnd) {
                if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                RECT r{ 0, 0, w, h };
                AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(m_hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE));
                SetWindowPos(m_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        else if (action == "sidebar" || action == "library") {
            bool& flag = action == "sidebar" ? m_settings.sidebarVisible : m_settings.libraryVisible;
            const bool want = a.Has("visible") ? a.Flag("visible") : !flag;
            if (want != flag) { flag = want; ev.settingsChanged = true; }
        }
        else { call->Fail("unknown action: " + action); return; }
        Reply(call, Json::Obj().Set("action", action).Set("minimized", m_minimized).Set("fullscreen", m_fullscreen)
              .Set("sidebar", m_settings.sidebarVisible).Set("libraryStrip", m_settings.libraryVisible));
        return;
    }

    if (name == "reload") {
        const std::string what = a.Str("what");
        if (what == "runtime") ev.reloadRuntime = true;
        else if (what == "depth") ev.reloadDepth = true;
        else if (what == "senders") ev.refreshSenders = true;
        else if (what == "history") ev.resetHistory = true;
        else { call->Fail("what must be runtime, depth, senders or history"); return; }
        ReplyText(call, "requested: " + what);
        return;
    }

    call->Fail("unknown tool: " + name);
}

// --- the preview picture ----------------------------------------------------------------------------

// At the render point of the frame: the whole display texture (as the preview shows it) or the back buffer
// (the window) goes into the readback buffer; the next frame's McpFinishPreview reads it.
void App::McpBeginPreview(ID3D12GraphicsCommandList* cmd, const DisplayView& display) {
    if (m_mcpPreviews.empty() || m_mcpShot.call || m_device.ScreenshotPending()) return;   // one readback per frame, the command line's first
    McpPreview p = std::move(m_mcpPreviews.front());
    m_mcpPreviews.erase(m_mcpPreviews.begin());
    if (p.view == 1) {
        if (!m_device.BeginScreenshot(cmd)) { p.call->Fail("the window could not be read back"); return; }
    } else {
        if (!display.valid || !display.resource) { p.call->Fail("no picture on screen yet (open a file or wait for=display)"); return; }
        if (!m_device.BeginReadback(cmd, display.resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, 0, display.width, display.height)) {
            p.call->Fail("the picture could not be read back");
            return;
        }
        p.wide = display.wide;
        p.compareMode = m_settings.compareMode;
        p.wipe = m_settings.wipePosition;
    }
    m_mcpShot = std::move(p);
}

void App::McpFinishPreview() {
    McpPreview p = std::move(m_mcpShot);
    m_mcpShot = McpPreview{};
    std::vector<uint8_t> rgba;
    UINT w = 0, h = 0;
    if (!m_device.ScreenshotPending() || !m_device.FinishScreenshot(m_screenshotFence, rgba, w, h) || !w || !h) {
        p.call->Fail("the readback failed");
        return;
    }
    // The wide display holds the original and the output side by side: the wipe is drawn as the preview draws it.
    if (p.view == 0 && p.wide && w >= 2) {
        const UINT half = w / 2;
        const UINT split = p.compareMode == CompareWipe ? (UINT)std::clamp((int)std::lround(p.wipe * half), 0, (int)half) : 0;
        std::vector<uint8_t> out((size_t)half * h * 4);
        for (UINT y = 0; y < h; ++y) {
            const uint8_t* row = rgba.data() + (size_t)y * w * 4;
            uint8_t* dst = out.data() + (size_t)y * half * 4;
            if (split > 0) std::memcpy(dst, row, (size_t)split * 4);
            if (split < half) std::memcpy(dst + (size_t)split * 4, row + ((size_t)half + split) * 4, (size_t)(half - split) * 4);
        }
        rgba.swap(out);
        w = half;
    }
    UINT dw = w, dh = h;
    const UINT longEdge = std::max(w, h);
    if (longEdge > (UINT)p.maxEdge) {
        const double k = (double)p.maxEdge / longEdge;
        dw = std::max(1u, (UINT)std::lround(w * k));
        dh = std::max(1u, (UINT)std::lround(h * k));
    }
    CaptureJob job;
    job.width = dw; job.height = dh; job.rowPitch = dw * 4; job.keepAlpha = false; job.quiet = true;
    job.pixels = (dw == w && dh == h) ? rgba : Downscale(rgba, w, h, dw, dh);
    std::vector<uint8_t> png;
    std::string error;
    if (!Capture::EncodePngMemory(job, png, error)) { p.call->Fail("PNG encoding failed: " + error); return; }
    Json meta = Json::Obj()
        .Set("view", p.view == 1 ? "window" : "output")
        .Set("width", w).Set("height", h)
        .Set("imageWidth", dw).Set("imageHeight", dh)
        .Set("pngBytes", (long long)png.size());
    if (p.view == 0) meta.Set("compareMode", CompareName(p.compareMode)).Set("source", McpSourceJson());
    if (!p.saveTo.empty()) {
        CaptureJob full;
        full.width = w; full.height = h; full.rowPitch = w * 4; full.keepAlpha = false; full.quiet = true;
        full.path = p.saveTo;
        full.pixels = std::move(rgba);
        m_capture.Enqueue(std::move(full));
        meta.Set("savedTo", WideToUtf8(p.saveTo));
    }
    p.call->Image(png);
    p.call->Text(meta.Dump(2));
    p.call->structured = meta;
    p.call->Finish();
}

// While the window is minimized nothing is drawn: the calls that need no picture still run.
void App::DrainMcpMinimized() {
    bool any;
    { std::lock_guard<std::mutex> lock(m_mcpMutex); any = !m_mcpQueue.empty(); }
    if (!any && m_mcpWaiters.empty() && m_mcpPreviews.empty()) return;
    ui::UiFrameInfo info;
    info.status = &m_status;
    info.library = &m_library;
    info.presets = &m_presets;
    info.sourceMode = m_settings.sourceMode;
    info.imagePath = m_source.imagePath;
    info.videoPath = m_source.videoPath;
    info.imageLoaded = m_source.imageLoaded;
    info.videoLoaded = m_source.videoLoaded;
    info.hasDisplay = false;
    info.fullscreen = m_fullscreen;
    ui::UiEvents ev;
    DrainMcp(info, ev);
    for (const McpPreview& p : m_mcpPreviews) p.call->Fail("the window is minimized, nothing is drawn: window restore first");
    m_mcpPreviews.clear();
    HandleEvents(ev);
}

} // namespace vdc
