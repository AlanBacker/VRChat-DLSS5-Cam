// VRChat DLSS5 Cam - animated images (GIF, APNG, animated WebP): the frames of a file decoded in order with the timing
// of each one, so that an animation is processed like a video, and writers that put processed frames back into the
// same three formats. GIF and APNG go through Windows Imaging Component, WebP through libwebp (BSD-3, see
// THIRD_PARTY_NOTICES.md).
#pragma once
#include "core/Util.h"
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

enum class AnimFormat { None = 0, Gif, Apng, WebP };

const char*    AnimFormatName(AnimFormat f);        // "GIF", "APNG", "WebP" ("" for None)
const wchar_t* AnimFormatExtension(AnimFormat f);   // L"gif", L"png", L"webp"

// Looks at the file: the format of a file that holds an animation (more than one frame), None for anything else.
AnimFormat ProbeAnimatedImage(const std::wstring& path);

// A still WebP picture decoded with libwebp, for systems without Windows' optional WebP codec. Tightly packed BGRA rows.
bool DecodeStillWebP(const std::wstring& path, UINT& width, UINT& height, std::vector<uint8_t>& bgra, std::string& error);
bool IsWebPPath(const std::wstring& path);   // the extension is .webp

struct AnimFrameInfo {
    LONGLONG pts = 0;        // start of the frame, 100 ns units
    LONGLONG duration = 0;   // how long it is shown
};

// The frames of an animated image, decoded in order onto a full canvas (GIF and APNG frames build on the ones
// before them). One thread at a time.
class AnimReader {
public:
    AnimReader();
    ~AnimReader();
    AnimReader(const AnimReader&) = delete;
    AnimReader& operator=(const AnimReader&) = delete;

    bool Open(const std::wstring& path, std::string& error);
    void Close();
    bool Opened() const { return m_format != AnimFormat::None; }

    AnimFormat Format() const { return m_format; }
    UINT       Width() const { return m_width; }
    UINT       Height() const { return m_height; }
    size_t     FrameCount() const { return m_frames.size(); }
    LONGLONG   Duration() const { return m_duration; }       // sum of the frame durations
    int        LoopCount() const { return m_loops; }         // 0 = forever
    bool       HasAlpha() const { return m_hasAlpha; }
    bool       Lossless() const { return m_lossless; }       // WebP: every frame is lossless
    UINT64     FileBytes() const { return m_file.size(); }
    const AnimFrameInfo& Frame(size_t i) const { return m_frames[i]; }
    size_t     FrameAt(LONGLONG pts) const;                  // the frame shown at pts (the last one past the end)
    void       FrameRate(UINT& num, UINT& den) const;        // the most common frame interval, as a rate

    // Frame `index` as a BGRA canvas (Width x Height, tightly packed). Going backwards starts over from the first frame.
    bool Decode(size_t index, std::vector<uint8_t>& bgra, std::string& error);

private:
    struct Impl;
    bool OpenGif(std::string& error);
    bool OpenApng(std::string& error);
    bool OpenWebP(std::string& error);
    bool DecodeNextGif(std::string& error);
    bool DecodeNextApng(std::string& error);
    bool DecodeNextWebP(std::string& error);
    void Rewind();

    std::wstring               m_path;
    std::vector<uint8_t>       m_file;
    AnimFormat                 m_format = AnimFormat::None;
    UINT                       m_width = 0, m_height = 0;
    std::vector<AnimFrameInfo> m_frames;
    LONGLONG                   m_duration = 0;
    int                        m_loops = 0;
    bool                       m_hasAlpha = false, m_lossless = false;
    std::vector<uint8_t>       m_canvas;     // the frame decoded last
    std::vector<uint8_t>       m_previous;   // the canvas saved for a "restore previous" disposal
    size_t                     m_next = 0;   // the frame the next DecodeNext* produces
    std::unique_ptr<Impl>      m_impl;
};

struct AnimWriterConfig {
    std::wstring path;
    AnimFormat   format = AnimFormat::Gif;
    int          loopCount = 0;         // 0 = forever
    bool         keepAlpha = false;     // transparency of the processed frames (GIF: one bit)
    int          quality = 90;          // WebP: 0..100
    bool         lossless = false;      // WebP
    LONGLONG     frameTicks = 400000;   // duration of a frame that arrives without one (100 ns units)
};

// Writes processed frames into an animated GIF, APNG or WebP file on its own thread, in index order, with the timing of
// the frames kept (rounded to the format's clock).
class AnimWriter {
public:
    AnimWriter();
    ~AnimWriter();
    AnimWriter(const AnimWriter&) = delete;
    AnimWriter& operator=(const AnimWriter&) = delete;

    // Starts the writer thread; the file itself is created with the first frame, whose size sets the canvas.
    void Prepare(const AnimWriterConfig& cfg);
    // Frames may arrive in any order and are written by index. Blocks while too many frames wait for the encoder.
    void PushFrame(UINT64 index, LONGLONG pts, LONGLONG duration, std::vector<uint8_t>&& rgba, UINT pitch, UINT w, UINT h);
    // Writes everything queued and closes the file. False with the first error.
    bool Finish(std::string& error);
    // Stops early; the frames written so far stay in a viewable file where the format allows it.
    void Abort();
    // Stops and forgets an earlier failure, so a new run starts clean.
    void Reset();

    bool                Running() const { return m_running; }
    bool                Failed() const { std::lock_guard<std::mutex> lock(m_mutex); return m_failed; }
    std::string         Error() const { std::lock_guard<std::mutex> lock(m_mutex); return m_error; }
    UINT64              Written() const { return m_written.load(); }
    const std::wstring& Path() const { return m_cfg.path; }

private:
    struct Frame {
        LONGLONG pts = 0, duration = 0;
        std::vector<uint8_t> rgba;
        UINT pitch = 0, w = 0, h = 0;
    };
    struct Impl;

    void WriterMain();
    bool OpenFile(UINT w, UINT h, std::string& error);
    bool WriteFrame(Frame& f, std::string& error);
    bool CloseFile(bool completed, std::string& error);
    void Fail(const std::string& error);

    AnimWriterConfig        m_cfg;
    std::unique_ptr<Impl>   m_impl;
    bool                    m_opened = false;
    UINT                    m_w = 0, m_h = 0;
    bool                    m_baseSet = false;
    LONGLONG                m_base = 0;              // time stamp of the first frame
    LONGLONG                m_lastEnd = 0;           // end of the last frame written, relative to the base
    UINT64                  m_sizeMismatch = 0;

    std::thread             m_thread;
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv, m_space;
    std::map<UINT64, Frame> m_frames;
    UINT64                  m_nextIndex = 0;
    size_t                  m_maxQueued = 8;
    bool                    m_finishing = false;
    bool                    m_abort = false;
    bool                    m_running = false;
    bool                    m_failed = false;
    std::string             m_error;
    std::atomic<UINT64>     m_written{ 0 };
};

} // namespace vdc
