// VRChat DLSS5 Cam - monocular depth estimation (Depth Anything V2 Small) through ONNX Runtime + DirectML.
//
// VRChat's Spout stream carries no depth buffer, but DLSS 5 neural rendering is a temporal model that uses depth
// to separate subject and background. The app therefore estimates a relative depth map from the picture
// itself: the frame is downsampled to a small network resolution on the GPU, the
// network runs on a worker thread (DirectML), and the pipeline resamples / temporally filters the result.
// Everything here is optional: when onnxruntime.dll, DirectML.dll or the model are missing the estimator reports
// Unavailable and the pipeline feeds a zero depth buffer instead.
#pragma once
#include "gfx/Device.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

enum class DepthEstimatorState { Unavailable = 0, Initializing, Ready, Failed };

struct DepthResult {
    std::vector<float> depth;         // raw relative inverse depth (larger = nearer), row-major height x width
    UINT   width = 0, height = 0;
    float  p02 = 0.0f, p98 = 1.0f;    // 2nd / 98th percentile of the raw values (normalisation range)
    double inferenceMs = 0.0;
    UINT64 sequence = 0;
};

class DepthEstimator {
public:
    ~DepthEstimator();

    static std::wstring DefaultModelPath(const std::wstring& exeDir);   // <exe>\models\depth_anything_v2_small_fp16.onnx
    static std::wstring RuntimePath(const std::wstring& exeDir);        // <exe>\onnxruntime.dll

    // Starts (or restarts) the worker for the given model and network resolution. Returns immediately; the worker
    // loads ONNX Runtime, creates the DirectML session on the app's D3D12 device and runs a warm-up inference.
    void Start(Device& device, const std::wstring& exeDir, const std::wstring& modelPath, UINT inferWidth, UINT inferHeight);
    void Stop();
    bool Matches(const std::wstring& modelPath, UINT inferWidth, UINT inferHeight) const {
        return m_modelPath == modelPath && m_w == inferWidth && m_h == inferHeight;
    }

    DepthEstimatorState State() const { return m_state.load(std::memory_order_acquire); }
    bool Ready() const { return State() == DepthEstimatorState::Ready; }
    bool Idle() const;                  // ready and no inference queued or running
    std::string Message() const;        // failure reason or backend description
    std::string Backend() const;        // "DirectML" / "CPU"
    UINT   InferWidth() const { return m_w; }
    UINT   InferHeight() const { return m_h; }
    UINT64 Inferences() const { return m_inferences.load(std::memory_order_relaxed); }
    double WarmupMs() const { return m_warmupMs; }

    // Hands an ImageNet-normalised planar float image (3 * height * width values, NCHW) to the worker.
    // Returns false when the worker is not ready or still busy with the previous frame.
    bool Submit(const float* values, size_t count);
    bool TryTakeResult(DepthResult& out);

private:
    void WorkerMain();
    bool Initialize(std::string& error);
    bool RunInference(DepthResult& result, std::string& error);
    void ReleaseSession();
    void SetState(DepthEstimatorState s, const std::string& message);

    std::wstring m_exeDir, m_modelPath;
    UINT m_w = 0, m_h = 0;

    std::thread              m_thread;
    mutable std::mutex       m_mutex;
    std::condition_variable  m_cv;
    bool                     m_stop = false;
    bool                     m_jobPending = false;
    bool                     m_jobRunning = false;
    std::vector<float>       m_pendingInput;
    std::vector<float>       m_workInput;
    bool                     m_resultReady = false;
    DepthResult              m_result;
    std::atomic<DepthEstimatorState> m_state{ DepthEstimatorState::Unavailable };
    std::string              m_message;
    std::string              m_backend;
    std::atomic<UINT64>      m_inferences{ 0 };
    UINT64                   m_sequence = 0;
    double                   m_warmupMs = 0.0;

    // ONNX Runtime objects (owned by the worker thread; opaque here to keep the header free of the ORT API).
    struct Session;
    Session* m_session = nullptr;
    ComPtr<ID3D12Device> m_d3d12;
};

} // namespace vdc
