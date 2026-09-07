// VRChat DLSS5 Cam - a video file (Media Foundation) as a pipeline source: the first frame as a still preview, or
// every frame in order for offline processing.
#pragma once
#include "core/SourceFrame.h"
#include "core/MediaFoundation.h"
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

// One decoded frame: BGRA8 rows without padding.
struct VideoFrameData {
    std::vector<uint8_t> bgra;
    UINT     width = 0, height = 0;
    UINT64   index = 0;                 // 0-based position in the sequence
    LONGLONG pts = 0, duration = 0;     // 100 ns units
};

struct VideoInfo {
    UINT        width = 0, height = 0;          // as handed to the pipeline (display aperture, rotation applied)
    UINT        fileWidth = 0, fileHeight = 0;  // coded size
    UINT32      fpsNum = 30, fpsDen = 1;
    double      durationSeconds = 0.0;
    UINT64      frameEstimate = 0;              // duration x frame rate
    bool        hasAudio = false;               // an audio stream exists in the file
    UINT32      audioRate = 0, audioChannels = 0;
    bool        hardwareDecode = false;         // the decoder runs on the GPU
    std::string codec;                          // "H264", "HEVC", ...
    std::string decoderOutput;                  // "NV12" / "RGB32"
};

class VideoSource {
public:
    static constexpr UINT kMaxLongSide = 8192;
    enum class Next { Frame, Wait, End, Error };

    // Opens the file and decodes its first frame into a BGRA8 texture (processing thread). hardwareDecode asks for
    // a GPU decoder; the software decoder is the fallback either way.
    bool Open(GpuContext& gpu, const std::wstring& path, bool hardwareDecode, std::string& error);
    void Close(GpuContext& gpu);

    // Sequence: a fresh reader from the start of the file feeds a small frame queue from a decode thread. withAudio
    // also decodes the audio stream to PCM samples (PopAudio) for the output file.
    bool StartSequence(bool withAudio, std::string& error);
    // Frame: the next picture is pending for Upload(); timing carries its index and time stamps (no pixels).
    Next NextFrame(double timeoutSeconds, VideoFrameData& timing);
    void StopSequence();
    bool SequenceRunning() const { return m_seqRunning; }
    std::string SequenceError();
    bool PopAudio(ComPtr<IMFSample>& sample);
    ComPtr<IMFMediaType> AudioType() const { return m_audioType; }   // PCM type of the sequence's audio (null: none)

    // Back to the first frame once a sequence ended.
    bool ReloadPreview(std::string& error);

    // Records the pending frame copy into cmd (the processing context's list). No-op when nothing is pending.
    void Upload(ID3D12GraphicsCommandList* cmd, GpuContext& gpu);

    bool                Loaded() const { return m_tex != nullptr; }
    SourceFrame         Frame(bool still) const;   // still: the temporal history converges on one picture
    const std::wstring& Path() const { return m_path; }
    std::wstring        Stem() const;              // file name without folder and extension
    const VideoInfo&    Info() const { return m_info; }

    // Extensions Media Foundation usually demuxes (lower case, without the dot).
    static bool IsSupportedExtension(const std::wstring& path);

private:
    struct Reader {
        ComPtr<IMFSourceReader> reader;
        DWORD  videoStream = 0, audioStream = 0;
        bool   hasAudioStream = false;
        bool   audio = false;                       // audio selected and converted to PCM
        GUID   subtype{};                           // decoder output: NV12, RGB32 or ARGB32
        UINT   frameW = 0, frameH = 0;              // coded frame (plane) size
        LONG   stride = 0;                          // default stride from the type (0 = unknown)
        UINT   cropX = 0, cropY = 0, cropW = 0, cropH = 0;
        UINT   outW = 0, outH = 0;                  // after the crop and the rotation
        UINT   rotation = 0;                        // degrees clockwise the picture has to be turned
        bool   bt709 = true, fullRange = false;
        UINT32 fpsNum = 30, fpsDen = 1;
        double durationSeconds = 0.0;
        std::string codec;
        ComPtr<IMFMediaType> audioType;
        UINT32 audioRate = 0, audioChannels = 0;
        bool   hardware = false;
    };

    bool CreateReader(const std::wstring& path, bool withAudio, bool hardware, Reader& r, std::string& error);
    bool ParseVideoType(IMFMediaType* type, Reader& r, std::string& error);
    bool ReadFirstFrame(Reader& r, std::vector<uint8_t>& bgra, std::string& error);
    bool ConvertSample(const Reader& r, IMFSample* sample, std::vector<uint8_t>& bgra, std::string& error);
    bool CreateDecoderDevice(GpuContext& gpu, std::string& error);
    bool CreateTexture(GpuContext& gpu, UINT w, UINT h, std::string& error);
    void ReleaseTexture(GpuContext& gpu);
    void SetPending(std::vector<uint8_t>&& bgra);
    void DecodeMain();

    // Picture
    ComPtr<ID3D12Resource>      m_tex;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srv{};
    ComPtr<ID3D12Resource>      m_upload[GpuContext::kFramesInFlight];   // persistently mapped ring, one per frame
    uint8_t*                    m_uploadPtr[GpuContext::kFramesInFlight] = {};
    UINT                        m_uploadIndex = 0;
    UINT                        m_uploadPitch = 0;
    std::vector<uint8_t>        m_pendingBgra;
    bool                        m_uploadPending = false;
    bool                        m_uploaded = false;

    std::wstring m_path;
    VideoInfo    m_info;

    // Hardware decoding
    ComPtr<ID3D11Device>          m_dev11;
    ComPtr<ID3D11DeviceContext>   m_ctx11;
    ComPtr<IMFDXGIDeviceManager>  m_dxgiManager;
    UINT                          m_dxgiToken = 0;

    // Sequence
    std::unique_ptr<Reader>       m_seq;
    std::thread                   m_seqThread;
    std::mutex                    m_qm;
    std::condition_variable       m_qcv, m_spaceCv;
    std::deque<VideoFrameData>    m_queue;
    std::deque<ComPtr<IMFSample>> m_audioQueue;
    ComPtr<IMFMediaType>          m_audioType;
    std::atomic<bool>             m_seqStop{false};
    bool                          m_seqDone = false;
    bool                          m_seqRunning = false;
    std::string                   m_seqError;
};

} // namespace vdc
