// VRChat DLSS5 Cam - Depth Anything V2 inference through the ONNX Runtime C API (DirectML execution provider).
#include "gfx/DepthEstimator.h"
#include "core/Util.h"
#include "core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <chrono>
#include <thread>

// ONNX Runtime headers come from the NuGet package fetched at configure time (cmake/FetchOnnxRuntime.cmake);
// the DLL itself is loaded dynamically so the app still starts when it is missing.
#include <dml_provider_factory.h>   // includes DirectML.h and onnxruntime_c_api.h
#include <onnxruntime_c_api.h>

namespace vdc {
namespace {

// IID of IDMLDevice (DirectML.h declares it with __uuidof, spelled out here so the value does not depend on compiler support).
const GUID kIidDmlDevice = { 0x6dbd6437, 0x96fd, 0x423f, { 0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f } };
using PfnDmlCreateDevice = HRESULT(WINAPI*)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);
using PfnOrtGetApiBase = const OrtApiBase*(ORT_API_CALL*)();

std::mutex    g_loadMutex;
HMODULE       g_ortModule = nullptr;    // kept loaded for the lifetime of the process
HMODULE       g_dmlModule = nullptr;
const OrtApi* g_ort = nullptr;
OrtEnv*       g_env = nullptr;

void ORT_API_CALL OrtLogCallback(void*, OrtLoggingLevel severity, const char* category, const char*, const char*, const char* message) {
    if (severity >= ORT_LOGGING_LEVEL_ERROR) Log::Warn("ONNX Runtime [%s]: %s", category ? category : "", message ? message : "");
    else if (severity == ORT_LOGGING_LEVEL_WARNING) Log::Info("ONNX Runtime [%s]: %s", category ? category : "", message ? message : "");
}

// Loads onnxruntime.dll from the executable folder (LOAD_WITH_ALTERED_SEARCH_PATH makes its own imports resolve there too).
bool LoadOrt(const std::wstring& exeDir, std::string& error) {
    std::lock_guard<std::mutex> lock(g_loadMutex);
    if (g_ort && g_env) return true;
    if (!g_ortModule) {
        const std::wstring path = DepthEstimator::RuntimePath(exeDir);
        g_ortModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_ortModule) {
            error = StrPrintf("onnxruntime.dll could not be loaded from %s (%s)", WideToUtf8(path).c_str(), LastErrorText().c_str());
            return false;
        }
    }
    if (!g_ort) {
        const auto getBase = reinterpret_cast<PfnOrtGetApiBase>(GetProcAddress(g_ortModule, "OrtGetApiBase"));
        const OrtApiBase* base = getBase ? getBase() : nullptr;
        g_ort = base ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (!g_ort) {
            error = StrPrintf("onnxruntime.dll does not provide ORT API version %u (runtime version %s)", (unsigned)ORT_API_VERSION,
                              base && base->GetVersionString ? base->GetVersionString() : "?");
            return false;
        }
        Log::Info("ONNX Runtime %s loaded", base->GetVersionString ? base->GetVersionString() : "?");
    }
    if (!g_env) {
        OrtStatus* st = g_ort->CreateEnvWithCustomLogger(&OrtLogCallback, nullptr, ORT_LOGGING_LEVEL_WARNING, "VRChatDLSS5Cam", &g_env);
        if (st) {
            error = std::string("OrtCreateEnv: ") + g_ort->GetErrorMessage(st);
            g_ort->ReleaseStatus(st);
            g_env = nullptr;
            return false;
        }
    }
    return true;
}

PfnDmlCreateDevice LoadDml(const std::wstring& exeDir) {
    std::lock_guard<std::mutex> lock(g_loadMutex);
    if (!g_dmlModule) {
        const std::wstring path = JoinPath(exeDir, L"DirectML.dll");
        g_dmlModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_dmlModule) g_dmlModule = LoadLibraryW(L"DirectML.dll");   // inbox copy as a last resort
        if (!g_dmlModule) return nullptr;
    }
    return reinterpret_cast<PfnDmlCreateDevice>(GetProcAddress(g_dmlModule, "DMLCreateDevice"));
}

std::string StatusText(OrtStatus* st) {
    std::string s = g_ort->GetErrorMessage(st);
    g_ort->ReleaseStatus(st);
    return s;
}

// SEH-guarded wrappers: a fault inside the runtime or the DirectML driver must not take the whole app down.
OrtStatus* SafeCreateSession(const OrtEnv* env, const wchar_t* path, const OrtSessionOptions* opts, OrtSession** out, unsigned long* seh) noexcept {
    OrtStatus* st = nullptr;
    VDC_SEH_TRY { st = g_ort->CreateSession(env, path, opts, out); }
    VDC_SEH_EXCEPT(*seh) { *out = nullptr; }
    return st;
}
OrtStatus* SafeRun(OrtSession* session, const char* inName, const OrtValue* input, const char* outName, OrtValue** output, unsigned long* seh) noexcept {
    OrtStatus* st = nullptr;
    VDC_SEH_TRY { st = g_ort->Run(session, nullptr, &inName, &input, 1, &outName, 1, output); }
    VDC_SEH_EXCEPT(*seh) { *output = nullptr; }
    return st;
}
HRESULT SafeDmlCreateDevice(PfnDmlCreateDevice fn, ID3D12Device* dev, void** out, unsigned long* seh) noexcept {
    HRESULT hr = E_FAIL;
    VDC_SEH_TRY { hr = fn(dev, DML_CREATE_DEVICE_FLAG_NONE, kIidDmlDevice, out); }
    VDC_SEH_EXCEPT(*seh) { *out = nullptr; }
    return hr;
}

} // namespace

struct DepthEstimator::Session {
    OrtSessionOptions* options = nullptr;
    OrtSession*        session = nullptr;
    OrtMemoryInfo*     memInfo = nullptr;
    OrtValue*          input = nullptr;
    std::string        inputName, outputName;
    std::vector<float> inputData;
    ComPtr<IUnknown>   dmlDevice;
    ComPtr<ID3D12CommandQueue> queue;
};

std::wstring DepthEstimator::DefaultModelPath(const std::wstring& exeDir) {
    return JoinPath(JoinPath(exeDir, L"models"), L"depth_anything_v2_small_fp16.onnx");
}
std::wstring DepthEstimator::RuntimePath(const std::wstring& exeDir) {
    return JoinPath(exeDir, L"onnxruntime.dll");
}

DepthEstimator::~DepthEstimator() { Stop(); }

void DepthEstimator::SetState(DepthEstimatorState s, const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_message = message;
    }
    m_state.store(s, std::memory_order_release);
}

std::string DepthEstimator::Message() const { std::lock_guard<std::mutex> lock(m_mutex); return m_message; }
std::string DepthEstimator::Backend() const { std::lock_guard<std::mutex> lock(m_mutex); return m_backend; }

bool DepthEstimator::Idle() const {
    if (!Ready()) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_jobPending && !m_jobRunning;
}

bool DepthEstimator::WaitIdle(double maxSeconds) const {
    if (!Ready()) return true;
    const double deadline = NowSeconds() + maxSeconds;
    while (!Idle()) {
        if (NowSeconds() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void DepthEstimator::Start(Device& device, const std::wstring& exeDir, const std::wstring& modelPath, UINT inferWidth, UINT inferHeight) {
    Stop();
    m_exeDir = exeDir;
    m_modelPath = modelPath;
    m_w = inferWidth;
    m_h = inferHeight;
    m_d3d12 = device.D3D12();
    m_inferences.store(0);
    m_warmupMs = 0.0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = false;
        m_jobPending = m_jobRunning = false;
        m_resultReady = false;
        m_backend.clear();
    }
    if (!FileExists(modelPath)) {
        SetState(DepthEstimatorState::Unavailable, "model not found: " + WideToUtf8(modelPath));
        Log::Warn("Depth estimator: model not found (%s); depth guidance falls back to zero", WideToUtf8(modelPath).c_str());
        return;
    }
    if (!FileExists(RuntimePath(exeDir))) {
        SetState(DepthEstimatorState::Unavailable, "onnxruntime.dll not found next to the executable");
        Log::Warn("Depth estimator: onnxruntime.dll not found in %s", WideToUtf8(exeDir).c_str());
        return;
    }
    SetState(DepthEstimatorState::Initializing, "loading");
    m_thread = std::thread(&DepthEstimator::WorkerMain, this);
}

void DepthEstimator::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    ReleaseSession();
    m_d3d12.Reset();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobPending = m_jobRunning = false;
        m_resultReady = false;
    }
    m_state.store(DepthEstimatorState::Unavailable, std::memory_order_release);
}

bool DepthEstimator::Submit(const float* values, size_t count) {
    if (!Ready() || count != size_t(3) * m_w * m_h) return false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_jobPending || m_jobRunning) return false;
        m_pendingInput.assign(values, values + count);
        m_jobPending = true;
    }
    m_cv.notify_one();
    return true;
}

bool DepthEstimator::TryTakeResult(DepthResult& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_resultReady) return false;
    out.depth.swap(m_result.depth);
    out.width = m_result.width; out.height = m_result.height;
    out.p02 = m_result.p02; out.p98 = m_result.p98;
    out.inferenceMs = m_result.inferenceMs;
    out.sequence = m_result.sequence;
    m_resultReady = false;
    return true;
}

void DepthEstimator::ReleaseSession() {
    Session* s = m_session;
    m_session = nullptr;
    if (!s) return;
    if (g_ort) {
        if (s->input) g_ort->ReleaseValue(s->input);
        if (s->session) g_ort->ReleaseSession(s->session);
        if (s->options) g_ort->ReleaseSessionOptions(s->options);
        if (s->memInfo) g_ort->ReleaseMemoryInfo(s->memInfo);
    }
    delete s;
}

bool DepthEstimator::Initialize(std::string& error) {
    if (!LoadOrt(m_exeDir, error)) return false;
    Session* s = new Session();
    m_session = s;
    OrtStatus* st = nullptr;
    auto fail = [&](const char* what, OrtStatus* status) {
        error = std::string(what) + ": " + StatusText(status);
        return false;
    };

    if ((st = g_ort->CreateSessionOptions(&s->options))) return fail("CreateSessionOptions", st);
    // Session configuration that is reliable for Depth Anything V2 on DirectML: basic graph optimisation only (the
    // extended fusions mis-compile this network), no memory pattern planning, sequential execution.
    if ((st = g_ort->SetSessionGraphOptimizationLevel(s->options, ORT_ENABLE_BASIC))) return fail("SetSessionGraphOptimizationLevel", st);
    if ((st = g_ort->DisableMemPattern(s->options))) return fail("DisableMemPattern", st);
    if ((st = g_ort->SetSessionExecutionMode(s->options, ORT_SEQUENTIAL))) return fail("SetSessionExecutionMode", st);
    if ((st = g_ort->SetIntraOpNumThreads(s->options, 2))) return fail("SetIntraOpNumThreads", st);
    if ((st = g_ort->SetInterOpNumThreads(s->options, 1))) return fail("SetInterOpNumThreads", st);
    if ((st = g_ort->SetSessionLogSeverityLevel(s->options, ORT_LOGGING_LEVEL_WARNING))) return fail("SetSessionLogSeverityLevel", st);
    if ((st = g_ort->SetSessionLogId(s->options, "depth"))) return fail("SetSessionLogId", st);

    // DirectML execution provider on the app's D3D12 device (a dedicated compute queue keeps it off the render queue).
    std::string backend = "CPU";
    const OrtDmlApi* dml = nullptr;
    st = g_ort->GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml));
    if (st) { Log::Warn("Depth estimator: DirectML provider API unavailable (%s)", StatusText(st).c_str()); dml = nullptr; }
    if (dml) {
        bool attached = false;
        if (PfnDmlCreateDevice create = LoadDml(m_exeDir)) {
            D3D12_COMMAND_QUEUE_DESC qd{};
            qd.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            HRESULT hr = m_d3d12->CreateCommandQueue(&qd, IID_PPV_ARGS(&s->queue));
            if (SUCCEEDED(hr)) {
                s->queue->SetName(L"DepthEstimator queue");
                unsigned long seh = 0;
                void* dev = nullptr;
                hr = SafeDmlCreateDevice(create, m_d3d12.Get(), &dev, &seh);
                if (SUCCEEDED(hr) && dev && !seh) {
                    s->dmlDevice.Attach(static_cast<IUnknown*>(dev));
                    st = dml->SessionOptionsAppendExecutionProvider_DML1(s->options, reinterpret_cast<IDMLDevice*>(s->dmlDevice.Get()), s->queue.Get());
                    if (st) Log::Warn("Depth estimator: DML1 provider rejected (%s)", StatusText(st).c_str());
                    else { attached = true; backend = "DirectML"; }
                } else {
                    Log::Warn("Depth estimator: DMLCreateDevice failed (%s, seh 0x%08lx)", FormatHr(hr).c_str(), seh);
                }
            } else {
                Log::Warn("Depth estimator: compute queue creation failed (%s)", FormatHr(hr).c_str());
            }
        } else {
            Log::Warn("Depth estimator: DirectML.dll not available");
        }
        if (!attached) {
            s->dmlDevice.Reset();
            s->queue.Reset();
            st = dml->SessionOptionsAppendExecutionProvider_DML(s->options, 0);
            if (st) Log::Warn("Depth estimator: DML provider (adapter 0) rejected (%s); using the CPU", StatusText(st).c_str());
            else backend = "DirectML (adapter 0)";
        }
    }

    unsigned long seh = 0;
    st = SafeCreateSession(g_env, m_modelPath.c_str(), s->options, &s->session, &seh);
    if (seh) { error = StrPrintf("CreateSession crashed (seh 0x%08lx)", seh); return false; }
    if (st) return fail("CreateSession", st);

    OrtAllocator* alloc = nullptr;
    if ((st = g_ort->GetAllocatorWithDefaultOptions(&alloc))) return fail("GetAllocatorWithDefaultOptions", st);
    size_t inputs = 0, outputs = 0;
    if ((st = g_ort->SessionGetInputCount(s->session, &inputs))) return fail("SessionGetInputCount", st);
    if ((st = g_ort->SessionGetOutputCount(s->session, &outputs))) return fail("SessionGetOutputCount", st);
    if (inputs < 1 || outputs < 1) { error = "model has no input/output"; return false; }
    char* name = nullptr;
    if ((st = g_ort->SessionGetInputName(s->session, 0, alloc, &name))) return fail("SessionGetInputName", st);
    s->inputName = name ? name : "";
    g_ort->AllocatorFree(alloc, name);
    name = nullptr;
    if ((st = g_ort->SessionGetOutputName(s->session, 0, alloc, &name))) return fail("SessionGetOutputName", st);
    s->outputName = name ? name : "";
    g_ort->AllocatorFree(alloc, name);

    if ((st = g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &s->memInfo))) return fail("CreateCpuMemoryInfo", st);
    s->inputData.assign(size_t(3) * m_w * m_h, 0.0f);
    const int64_t shape[4] = { 1, 3, (int64_t)m_h, (int64_t)m_w };
    if ((st = g_ort->CreateTensorWithDataAsOrtValue(s->memInfo, s->inputData.data(), s->inputData.size() * sizeof(float), shape, 4,
                                                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &s->input)))
        return fail("CreateTensorWithDataAsOrtValue", st);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backend = backend;
    }
    // Warm-up: the first run compiles the DirectML graph and can take a while.
    DepthResult warm;
    const double t0 = NowSeconds();
    if (!RunInference(warm, error)) return false;
    m_warmupMs = (NowSeconds() - t0) * 1000.0;
    Log::Info("Depth estimator ready: %s, %ux%u -> %ux%u, input '%s', output '%s', warm-up %.0f ms, model %s", backend.c_str(),
              m_w, m_h, warm.width, warm.height, s->inputName.c_str(), s->outputName.c_str(), m_warmupMs, WideToUtf8(m_modelPath).c_str());
    return true;
}

bool DepthEstimator::RunInference(DepthResult& result, std::string& error) {
    Session* s = m_session;
    if (!s || !s->session) { error = "no session"; return false; }
    const double t0 = NowSeconds();
    OrtValue* output = nullptr;
    unsigned long seh = 0;
    OrtStatus* st = SafeRun(s->session, s->inputName.c_str(), s->input, s->outputName.c_str(), &output, &seh);
    if (seh) { error = StrPrintf("Run crashed (seh 0x%08lx)", seh); return false; }
    if (st) { error = "Run: " + StatusText(st); return false; }
    if (!output) { error = "Run produced no output"; return false; }

    bool ok = false;
    OrtTensorTypeAndShapeInfo* info = nullptr;
    if (!(st = g_ort->GetTensorTypeAndShape(output, &info))) {
        size_t dims = 0;
        int64_t shape[8] = {};
        ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
        g_ort->GetTensorElementType(info, &type);
        if (!(st = g_ort->GetDimensionsCount(info, &dims)) && dims >= 2 && dims <= 8 && !(st = g_ort->GetDimensions(info, shape, dims))) {
            // Accept [1,H,W], [H,W] and [1,1,H,W].
            const int64_t h = shape[dims - 2], w = shape[dims - 1];
            bool leadingOnes = true;
            for (size_t i = 0; i + 2 < dims; ++i) leadingOnes = leadingOnes && shape[i] == 1;
            if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT && leadingOnes && h > 0 && w > 0 && h * w <= (1 << 24)) {
                float* data = nullptr;
                if (!(st = g_ort->GetTensorMutableData(output, reinterpret_cast<void**>(&data))) && data) {
                    result.width = (UINT)w; result.height = (UINT)h;
                    result.depth.assign(data, data + size_t(h) * size_t(w));
                    ok = true;
                }
            } else {
                error = StrPrintf("unexpected output tensor (type %d, %zu dims)", (int)type, dims);
            }
        }
        g_ort->ReleaseTensorTypeAndShapeInfo(info);
    }
    g_ort->ReleaseValue(output);
    if (!ok) {
        if (st) error = "output: " + StatusText(st);
        else if (error.empty()) error = "unexpected output tensor";
        return false;
    }

    // Robust normalisation range: 2nd / 98th percentiles of the finite values.
    std::vector<float>& d = result.depth;
    static thread_local std::vector<float> scratch;
    scratch.clear();
    scratch.reserve(d.size());
    for (float v : d) if (std::isfinite(v)) scratch.push_back(v);
    if (scratch.empty()) { error = "output contains no finite values"; return false; }
    const size_t lo = (size_t)((scratch.size() - 1) * 0.02), hi = (size_t)((scratch.size() - 1) * 0.98);
    std::nth_element(scratch.begin(), scratch.begin() + lo, scratch.end());
    result.p02 = scratch[lo];
    std::nth_element(scratch.begin() + lo, scratch.begin() + hi, scratch.end());
    result.p98 = scratch[hi];
    if (!(result.p98 > result.p02 + 1e-6f)) result.p98 = result.p02 + 1e-3f;
    for (float& v : d) if (!std::isfinite(v)) v = result.p02;
    result.inferenceMs = (NowSeconds() - t0) * 1000.0;
    return true;
}

void DepthEstimator::WorkerMain() {
    SetThreadDescription(GetCurrentThread(), L"VDC depth estimator");
    std::string error;
    if (!Initialize(error)) {
        ReleaseSession();
        SetState(DepthEstimatorState::Failed, error);
        Log::Warn("Depth estimator failed: %s", error.c_str());
        return;
    }
    SetState(DepthEstimatorState::Ready, Backend());
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&] { return m_stop || m_jobPending; });
            if (m_stop) break;
            m_workInput.swap(m_pendingInput);
            m_jobPending = false;
            m_jobRunning = true;
        }
        Session* s = m_session;
        if (m_workInput.size() == s->inputData.size()) std::memcpy(s->inputData.data(), m_workInput.data(), m_workInput.size() * sizeof(float));
        DepthResult r;
        const bool ok = RunInference(r, error);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobRunning = false;
        if (!ok) {
            m_message = error;
            m_state.store(DepthEstimatorState::Failed, std::memory_order_release);
            Log::Warn("Depth estimator: inference failed: %s", error.c_str());
            break;
        }
        r.sequence = ++m_sequence;
        m_result.depth.swap(r.depth);
        m_result.width = r.width; m_result.height = r.height;
        m_result.p02 = r.p02; m_result.p98 = r.p98;
        m_result.inferenceMs = r.inferenceMs;
        m_result.sequence = r.sequence;
        m_resultReady = true;
        m_inferences.fetch_add(1, std::memory_order_relaxed);
    }
    ReleaseSession();
}

} // namespace vdc
