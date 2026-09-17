// VRChat DLSS5 Cam - lossless PNG capture worker (WIC encoder on a background thread).
#pragma once
#include "core/Util.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct IWICImagingFactory;
struct IStream;

namespace vdc {

struct CaptureJob {
    std::vector<uint8_t> pixels;   // RGBA8, rowPitch bytes per row
    UINT         width = 0;
    UINT         height = 0;
    UINT         rowPitch = 0;
    bool         keepAlpha = false;
    bool         quiet = false;     // a frame of a video sequence: no per-file log line or toast
    unsigned     tag = 0;           // who asked for it (a library item id), carried into the result
    std::wstring path;
};

struct CaptureResult {
    std::wstring path;
    bool         ok = false;
    bool         quiet = false;
    unsigned     tag = 0;
    std::string  error;
    double       seconds = 0.0;
    uint64_t     bytes = 0;
    UINT         width = 0, height = 0;
};

class Capture {
public:
    ~Capture() { Shutdown(); }
    bool Init();
    void Shutdown();
    void Enqueue(CaptureJob&& job);
    bool PollResult(CaptureResult& out);
    size_t Pending() const;

    // Output file names come from a template with tokens: {name} the source file's name without its extension
    // ("VRChat" for a live capture), {date} 2026-09-14, {time} 12-34-56.123, {size} 1920x1080, {width} and {height}
    // (the saved picture's), {insize}, {inwidth} and {inheight} (the source's). Everything else stays as typed;
    // characters a file name cannot hold become "_". Live captures and processed files have their own default.
    static const wchar_t* const kDefaultCaptureName;   // "VRChat_DLSS5_{date}_{time}_{size}"
    static const wchar_t* const kDefaultOutputName;    // "{name}_DLSS5_{size}"
    // The setting's template (UTF-8, blanks trimmed), or the default when it is empty.
    static std::wstring Template(const std::string& utf8, bool liveCapture);
    static std::wstring ExpandName(const std::wstring& tmpl, const std::wstring& name, UINT inW, UINT inH, UINT width, UINT height,
                                   const SYSTEMTIME* at = nullptr);
    // "<folder>\<expanded template>[suffix].<ext>", with _2, _3, ... before the extension while the name is taken.
    static std::wstring MakeFileName(const std::wstring& folder, const std::wstring& tmpl, const std::wstring& name, UINT inW, UINT inH,
                                     UINT width, UINT height, const wchar_t* suffix, const wchar_t* ext, const SYSTEMTIME* at = nullptr);
    // "<folder>\<expanded template>" as the folder of an image sequence, with _2, _3, ... while the name is taken.
    static std::wstring MakeFolderName(const std::wstring& folder, const std::wstring& tmpl, const std::wstring& name, UINT inW, UINT inH,
                                       UINT width, UINT height);
    // The PNG bytes of a picture, in memory (the MCP preview): the same encoder as the files.
    static bool EncodePngMemory(const CaptureJob& job, std::vector<uint8_t>& png, std::string& error);

private:
    void WorkerMain();
    static bool EncodePng(const CaptureJob& job, std::string& error, uint64_t& bytes);
    static bool EncodePngStream(IWICImagingFactory* factory, IStream* stream, const CaptureJob& job, std::string& error);

    std::thread                 m_thread;
    mutable std::mutex          m_mutex;
    std::condition_variable     m_cv;
    std::deque<CaptureJob>      m_jobs;
    std::deque<CaptureResult>   m_results;
    bool                        m_quit = false;
    bool                        m_running = false;
};

} // namespace vdc
