// VRChat DLSS5 Cam - MP4 output (Media Foundation sink writer): processed frames in index order, H.264 or HEVC,
// the source's audio re-encoded as AAC.
#pragma once
#include "core/MediaFoundation.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

struct VideoWriterConfig {
    std::wstring path;
    UINT32       fpsNum = 30, fpsDen = 1;
    int          codec = 0;              // 0 H.264, 1 HEVC
    UINT32       bitrateKbps = 40000;
    ComPtr<IMFMediaType> audio;          // PCM type of the audio samples (null: no audio track)
};

class VideoWriter {
public:
    ~VideoWriter() { Abort(); }

    // Starts the writer thread; the file itself is created with the first frame, whose size sets the picture size.
    void Prepare(const VideoWriterConfig& cfg);
    // Frames may arrive in any order and are written by index. Blocks while too many frames wait for the encoder.
    // rgba: RGBA8 rows of pitch bytes.
    void PushFrame(UINT64 index, LONGLONG pts, LONGLONG duration, std::vector<uint8_t>&& rgba, UINT pitch, UINT w, UINT h);
    void PushAudio(const ComPtr<IMFSample>& sample);
    // Writes everything queued and closes the file. False with the first error.
    bool Finish(std::string& error);
    // Stops early; the frames written so far stay in a playable file.
    void Abort();

    bool                Running() const { return m_running; }
    bool                Failed() const { std::lock_guard<std::mutex> lock(m_mutex); return m_failed; }
    std::string         Error() const { std::lock_guard<std::mutex> lock(m_mutex); return m_error; }
    UINT64              Written() const { return m_written.load(); }
    bool                AudioEnabled() const { return m_cfg.audio != nullptr; }
    const std::wstring& Path() const { return m_cfg.path; }
    bool                HardwareEncoder() const { return m_hardware; }

private:
    struct Frame {
        LONGLONG pts = 0, duration = 0;
        std::vector<uint8_t> rgba;
        UINT pitch = 0, w = 0, h = 0;
    };

    void WriterMain();
    bool OpenFile(UINT w, UINT h, bool withAudio, std::string& error);
    bool WriteFrame(const Frame& f, std::string& error);
    bool WriteAudioUpTo(LONGLONG limit, std::string& error);
    void Fail(const std::string& error);

    VideoWriterConfig       m_cfg;
    ComPtr<IMFSinkWriter>   m_writer;
    DWORD                   m_videoStream = 0, m_audioStream = 0;
    bool                    m_audio = false;
    bool                    m_opened = false;
    bool                    m_hardware = false;
    UINT                    m_w = 0, m_h = 0;
    bool                    m_baseSet = false;
    LONGLONG                m_base = 0;                 // time stamp of the first frame
    LONGLONG                m_lastVideoTime = -1;
    std::vector<uint8_t>    m_nv12;
    std::deque<ComPtr<IMFSample>> m_audioHeld;          // writer thread: samples not yet due
    UINT64                  m_sizeMismatch = 0;

    std::thread             m_thread;
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv, m_space;
    std::map<UINT64, Frame> m_frames;
    UINT64                  m_nextIndex = 0;
    std::deque<ComPtr<IMFSample>> m_audioQueue;
    size_t                  m_maxQueued = 8;
    bool                    m_finishing = false;
    bool                    m_abort = false;
    bool                    m_running = false;
    bool                    m_failed = false;
    std::string             m_error;
    std::atomic<UINT64>     m_written{ 0 };
};

} // namespace vdc
