// VRChat DLSS5 Cam - lossless PNG capture worker (WIC encoder on a background thread).
#pragma once
#include "core/Util.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

struct CaptureJob {
    std::vector<uint8_t> pixels;   // RGBA8, rowPitch bytes per row
    UINT         width = 0;
    UINT         height = 0;
    UINT         rowPitch = 0;
    bool         keepAlpha = false;
    std::wstring path;
};

struct CaptureResult {
    std::wstring path;
    bool         ok = false;
    std::string  error;
    double       seconds = 0.0;
    uint64_t     bytes = 0;
};

class Capture {
public:
    ~Capture() { Shutdown(); }
    bool Init();
    void Shutdown();
    void Enqueue(CaptureJob&& job);
    bool PollResult(CaptureResult& out);
    size_t Pending() const;

    // Builds "<folder>\VRChat_DLSS5_<timestamp>_<w>x<h>[suffix].png".
    static std::wstring MakeFileName(const std::wstring& folder, UINT width, UINT height, const wchar_t* suffix);
    // Builds "<folder>\<stem>_DLSS5_<w>x<h>[suffix].png" for a processed still image, appending _2, _3, ... while the
    // name is taken.
    static std::wstring MakeImageFileName(const std::wstring& folder, const std::wstring& stem, UINT width, UINT height,
                                          const wchar_t* suffix);

private:
    void WorkerMain();
    static bool EncodePng(const CaptureJob& job, std::string& error, uint64_t& bytes);

    std::thread                 m_thread;
    mutable std::mutex          m_mutex;
    std::condition_variable     m_cv;
    std::deque<CaptureJob>      m_jobs;
    std::deque<CaptureResult>   m_results;
    bool                        m_quit = false;
    bool                        m_running = false;
};

} // namespace vdc
