// VRChat DLSS5 Cam - the media library: pictures and videos dropped into the app, with thumbnails and file details
// read on a background thread, ready to be previewed one by one or processed together.
#pragma once
#include "core/VideoSource.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

// One entry of the library (interface thread).
struct LibraryItem {
    enum State { Idle = 0, Queued, Processing, Done, Failed };
    unsigned     id = 0;
    std::wstring path;
    std::string  name;             // file name, UTF-8
    bool         isVideo = false;
    bool         selected = true;
    int          probe = 0;        // 0 = pending, 1 = read, 2 = failed
    UINT         width = 0, height = 0;
    double       duration = 0.0;   // seconds (videos)
    double       fps = 0.0;
    bool         hasAudio = false;
    std::string  error;
    int          thumbCell = -1;   // atlas cell
    State        state = Idle;
    float        progress = 0.0f;  // 0..1 while processing
    std::string  outName;          // the file or folder written
    double       inSec = 0.0, outSec = 0.0;   // processing range for videos (outSec <= 0: to the end)
};

// Work for the scanner thread and what comes back.
struct ScanRequest {
    enum Kind { Probe = 0, Storyboard, Hover };
    Kind         kind = Probe;
    unsigned     id = 0;           // Probe: the item; Storyboard/Hover: the storyboard generation
    std::wstring path;
    bool         isVideo = false;
    double       seconds = 0.0;    // Storyboard/Hover: the time wanted
    int          cell = -1;        // atlas cell to fill
    int          slot = -1;        // Storyboard: slot index
};

struct ScanResult {
    ScanRequest::Kind kind = ScanRequest::Probe;
    unsigned     id = 0;
    bool         ok = false;
    UINT         width = 0, height = 0;
    double       duration = 0.0, fps = 0.0, seconds = 0.0;
    bool         hasAudio = false;
    std::string  error;
    int          cell = -1, slot = -1;
    std::vector<uint8_t> thumb;    // kCellWidth x kCellHeight BGRA
};

// Decodes thumbnails and reads file details on its own thread: library items as they are added, the storyboard of
// the opened video (evenly spaced frames for the seek bar) and the frame under the cursor.
class LibraryScanner {
public:
    static constexpr UINT kThumbWidth = 192;
    static constexpr UINT kThumbHeight = 108;

    ~LibraryScanner() { Stop(); }
    void Start(IDXGIAdapter* adapter);
    void Stop();

    void Probe(unsigned id, const std::wstring& path, bool isVideo, int cell);
    // Replaces the storyboard: `generation` marks results of an older one as stale. Slots decode in order, a Hover
    // request for the same generation goes first, and a second Hover request replaces the one still waiting.
    void SetStoryboard(unsigned generation, const std::wstring& path, std::vector<ScanRequest>&& slots);
    void Hover(unsigned generation, double seconds, int cell);
    void ClearStoryboard();
    // Drops every request for the item.
    void Forget(unsigned id);
    bool Poll(ScanResult& out);
    size_t Pending() const;

private:
    void Main();
    bool Take(ScanRequest& out);
    void ProbeImage(const ScanRequest& r, ScanResult& out);
    void ProbeVideo(const ScanRequest& r, ScanResult& out);
    void Frame(const ScanRequest& r, ScanResult& out);

    std::thread               m_thread;
    mutable std::mutex        m_mutex;
    std::condition_variable   m_cv;
    std::deque<ScanRequest>   m_probes;
    std::deque<ScanRequest>   m_story;
    bool                      m_hoverPending = false;
    ScanRequest               m_hover;
    unsigned                  m_generation = 0;
    std::wstring              m_storyPath;
    std::deque<ScanResult>    m_results;
    std::atomic<bool>         m_stop{false};
    IDXGIAdapter*             m_adapter = nullptr;

    // Scanner-thread state.
    DecoderDevice             m_decoder;
    VideoScanner              m_storyScanner;
    std::wstring              m_storyOpen;
};

// True for names the library takes: pictures (WIC) or videos (Media Foundation).
bool IsLibraryFile(const std::wstring& path, bool& isVideo);

} // namespace vdc
