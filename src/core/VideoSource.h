// VRChat DLSS5 Cam - video file source (Media Foundation): a seekable still preview, a paced or as-fast-as-possible
// frame sequence over a time range with its sound track, and thumbnails for the media library and the seek bar.
#pragma once
#include "core/MediaFoundation.h"
#include "core/SourceFrame.h"
#include "gfx/Device.h"
#include <d3d11.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

// One decoded frame of a sequence, handed over with its timing (100 ns units, as in the file).
struct VideoFrameData {
    std::vector<uint8_t> bgra;
    UINT     width = 0, height = 0;
    UINT64   index = 0;          // 0-based frame number within the sequence
    LONGLONG pts = 0;            // presentation time
    LONGLONG duration = 0;       // 0 = unknown
};

struct VideoInfo {
    UINT   width = 0, height = 0;             // as processed (rotated, cropped, limited to kMaxLongSide)
    UINT   fileWidth = 0, fileHeight = 0;     // as stored
    UINT   fpsNum = 30, fpsDen = 1;
    double durationSeconds = 0.0;
    UINT64 frameEstimate = 0;
    bool   hasAudio = false;
    UINT   audioRate = 0, audioChannels = 0;
    UINT32 videoBitrateKbps = 0;              // average video bitrate of the file (0 = unknown)
    UINT32 audioBitrateKbps = 0;
    bool   hardwareDecode = false;            // the GPU decoder is in use
    std::string codec;                        // "HEVC", "H.264", ...
    std::string decoderOutput;                // "NV12", "RGB32", ...
};

struct VideoReader;   // a Media Foundation source reader with its parsed video type (VideoSource.cpp)

// A D3D11 device with video support for Media Foundation's GPU decoders. Readers made with the same device share it.
struct DecoderDevice {
    ComPtr<ID3D11Device>        dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IMFDXGIDeviceManager> manager;
    UINT                        token = 0;
    bool Create(IDXGIAdapter* adapter, std::string& error);
    void Reset();
    bool Ready() const { return manager != nullptr; }
};

// Fits a BGRA picture into w x h with its aspect kept (dark bars) using a box filter; the thumbnail cells.
void FitThumbnail(const std::vector<uint8_t>& src, UINT srcW, UINT srcH, UINT w, UINT h, std::vector<uint8_t>& dst);
// Mean luma of a BGRA picture (0..1), sampled on a coarse grid.
float MeanLuma(const std::vector<uint8_t>& bgra, UINT w, UINT h);

class VideoSource {
public:
    static constexpr UINT   kMaxLongSide = 8192;
    static constexpr double kBlackSkipSeconds = 2.0;   // leading black frames passed over for the preview
    enum class Next { Frame, Wait, End, Error };

    VideoSource();
    ~VideoSource();

    // Opens the file, decodes its preview frame (the first frame that is not black) and creates the source texture.
    bool Open(GpuContext& gpu, const std::wstring& path, bool hardwareDecode, std::string& error);
    void Close(GpuContext& gpu);

    // Still preview: shows the frame at `seconds` (clamped to the file). The frame is uploaded by the next Upload().
    bool   SeekPreview(double seconds, std::string& error);
    bool   ReloadPreview(std::string& error) { return SeekPreview(m_previewSeconds, error); }
    double PreviewSeconds() const { return m_previewSeconds; }
    float  PreviewLuma() const { return m_previewLuma; }
    double FrameSeconds() const;   // duration of one frame

    // Frame sequence from `fromSeconds` up to `toSeconds` (<= 0: to the end), decoded on a thread. Frames whose
    // interval ends before the start are skipped; the sequence ends with the first frame at or after `toSeconds`.
    bool StartSequence(bool withAudio, double fromSeconds, double toSeconds, std::string& error);
    Next NextFrame(double timeoutSeconds, VideoFrameData& timing);
    void StopSequence();
    bool SequenceRunning() const { return m_seqRunning; }
    std::string SequenceError();
    bool PopAudio(ComPtr<IMFSample>& sample);
    ComPtr<IMFMediaType> AudioType() const { return m_audioType; }

    // Copies the pending preview/sequence frame into the source texture (processing command list).
    void Upload(ID3D12GraphicsCommandList* cmd, GpuContext& gpu);
    bool Loaded() const { return m_tex != nullptr; }
    SourceFrame Frame(bool still) const;
    const std::wstring& Path() const { return m_path; }
    std::wstring Stem() const;
    const VideoInfo& Info() const { return m_info; }
    IMFDXGIDeviceManager* DecoderManager() const { return m_decoder.manager.Get(); }

    static bool IsSupportedExtension(const std::wstring& path);

private:
    bool CreateTexture(GpuContext& gpu, UINT w, UINT h, std::string& error);
    void ReleaseTexture(GpuContext& gpu);
    bool OpenPreviewReader(bool hardware, std::string& error);
    void SetPending(std::vector<uint8_t>&& bgra);
    void DecodeMain();

    ComPtr<ID3D12Resource>  m_tex;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srv{};
    ComPtr<ID3D12Resource>  m_upload[GpuContext::kFramesInFlight];
    uint8_t*                m_uploadPtr[GpuContext::kFramesInFlight] = {};
    UINT                    m_uploadIndex = 0;
    UINT                    m_uploadPitch = 0;
    std::vector<uint8_t>    m_pendingBgra;      // guarded by m_qm
    bool                    m_uploadPending = false;
    bool                    m_uploaded = false;

    std::wstring  m_path;
    VideoInfo     m_info;
    DecoderDevice m_decoder;

    std::unique_ptr<VideoReader> m_preview;    // kept open so the preview can seek
    double m_previewSeconds = 0.0;
    float  m_previewLuma = 0.0f;

    std::unique_ptr<VideoReader> m_seq;
    std::thread                  m_seqThread;
    mutable std::mutex           m_qm;
    std::condition_variable      m_qcv, m_spaceCv;
    std::deque<VideoFrameData>   m_queue;
    std::deque<ComPtr<IMFSample>> m_audioQueue;
    ComPtr<IMFMediaType>         m_audioType;
    LONGLONG                     m_seqStartPts = 0, m_seqEndPts = 0;
    std::atomic<bool>            m_seqStop{false};
    std::atomic<bool>            m_seqDone{false};
    std::atomic<bool>            m_seqRunning{false};
    std::string                  m_seqError;
};

// Reads single frames of a file for thumbnails, on any thread, with its own reader (software or the given GPU decoder).
class VideoScanner {
public:
    VideoScanner();
    ~VideoScanner();
    bool Open(const std::wstring& path, IMFDXGIDeviceManager* manager, std::string& error);
    void Close();
    bool Opened() const { return m_reader != nullptr; }
    const VideoInfo& Info() const { return m_info; }
    // The frame covering `seconds`, fitted into w x h BGRA. skipBlack passes over leading black frames like the still
    // preview does. gotSeconds receives the time of the frame used.
    bool Thumbnail(double seconds, bool skipBlack, UINT w, UINT h, std::vector<uint8_t>& bgra, double& gotSeconds,
                   std::string& error);

private:
    std::unique_ptr<VideoReader> m_reader;
    VideoInfo    m_info;
    std::wstring m_path;
    IMFDXGIDeviceManager* m_manager = nullptr;
};

} // namespace vdc
