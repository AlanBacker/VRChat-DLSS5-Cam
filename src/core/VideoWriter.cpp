#include "core/VideoWriter.h"
#include "core/Log.h"
#include <codecapi.h>
#include <algorithm>
#include <climits>
#include <cstring>

namespace vdc {

namespace {

constexpr LONGLONG kAudioLead = 5000000;       // audio is written up to half a second ahead of the video
constexpr size_t   kQueueBytes = 256u << 20;   // frames waiting for the encoder: about 256 MB at most

inline uint8_t Clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// RGBA8 -> NV12 (BT.709, limited range) in 16.16 fixed point. Even dimensions; the chroma of a 2x2 block is the
// average of its four pixels.
void ConvertToNv12(const uint8_t* rgba, UINT pitch, UINT w, UINT h, uint8_t* yPlane, uint8_t* uvPlane) {
    for (UINT j = 0; j < h; ++j) {
        const uint8_t* row = rgba + (size_t)j * pitch;
        uint8_t* yr = yPlane + (size_t)j * w;
        for (UINT i = 0; i < w; ++i) {
            const int r = row[4 * i], g = row[4 * i + 1], b = row[4 * i + 2];
            yr[i] = Clamp8(16 + ((11966 * r + 40254 * g + 4064 * b + 32768) >> 16));
        }
    }
    for (UINT j = 0; j < h; j += 2) {
        const uint8_t* r0 = rgba + (size_t)j * pitch;
        const uint8_t* r1 = r0 + pitch;
        uint8_t* uvr = uvPlane + (size_t)(j / 2) * w;
        for (UINT i = 0; i < w; i += 2) {
            const uint8_t* p0 = r0 + 4 * i;
            const uint8_t* p1 = r1 + 4 * i;
            const int r = p0[0] + p0[4] + p1[0] + p1[4];
            const int g = p0[1] + p0[5] + p1[1] + p1[5];
            const int b = p0[2] + p0[6] + p1[2] + p1[6];
            uvr[i]     = Clamp8(128 + ((-6597 * r - 22189 * g + 28784 * b + 131072) >> 18));
            uvr[i + 1] = Clamp8(128 + ((28784 * r - 26145 * g - 2639 * b + 131072) >> 18));
        }
    }
}

} // namespace

void VideoWriter::Prepare(const VideoWriterConfig& cfg) {
    Abort();
    m_cfg = cfg;
    m_writer.Reset();
    m_opened = false;
    m_hardware = false;
    m_w = m_h = 0;
    m_baseSet = false;
    m_base = 0;
    m_lastVideoTime = -1;
    m_audioHeld.clear();
    m_sizeMismatch = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frames.clear();
        m_audioQueue.clear();
        m_nextIndex = 0;
        m_maxQueued = 8;
        m_finishing = false;
        m_abort = false;
        m_failed = false;
        m_error.clear();
        m_running = true;
    }
    m_written = 0;
    m_thread = std::thread([this] { WriterMain(); });
}

void VideoWriter::PushFrame(UINT64 index, LONGLONG pts, LONGLONG duration, std::vector<uint8_t>&& rgba, UINT pitch, UINT w, UINT h) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_running || m_failed || m_abort) return;
    m_maxQueued = std::clamp<size_t>(kQueueBytes / std::max<size_t>(rgba.size(), 1), 2, 8);
    m_space.wait(lock, [&] { return m_frames.size() < m_maxQueued || m_failed || m_abort || !m_running; });
    if (m_failed || m_abort || !m_running) return;
    Frame f;
    f.pts = pts; f.duration = duration;
    f.rgba = std::move(rgba);
    f.pitch = pitch; f.w = w; f.h = h;
    m_frames.emplace(index, std::move(f));
    lock.unlock();
    m_cv.notify_all();
}

void VideoWriter::PushAudio(const ComPtr<IMFSample>& sample) {
    if (!sample) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_running || m_abort) return;
    m_audioQueue.push_back(sample);
}

bool VideoWriter::Finish(std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finishing = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    error = m_error;
    return !m_failed;
}

void VideoWriter::Abort() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_abort = true;
    }
    m_cv.notify_all();
    m_space.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void VideoWriter::Fail(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_failed) { m_failed = true; m_error = error; }
    }
    m_space.notify_all();
    Log::Error("Video output: %s", error.c_str());
}

// --- writer thread ------------------------------------------------------------------------------

bool VideoWriter::OpenFile(UINT w, UINT h, bool withAudio, std::string& error) {
    m_w = w & ~1u;
    m_h = h & ~1u;
    if (m_w < 2 || m_h < 2) { error = "the picture is too small"; return false; }
    const size_t slash = m_cfg.path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) CreateDirectories(m_cfg.path.substr(0, slash));

    ComPtr<IMFAttributes> attrs;
    HRESULT hr = mf::CreateAttributes(&attrs, 4);
    if (FAILED(hr)) { error = "MFCreateAttributes: " + FormatHr(hr); return false; }
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    ComPtr<IMFSinkWriter> writer;
    hr = mf::CreateSinkWriterFromURL(m_cfg.path.c_str(), nullptr, attrs.Get(), &writer);
    if (FAILED(hr) || !writer) { error = "cannot create the output file: " + FormatHr(hr); return false; }

    const bool hevc = m_cfg.codec == 1;
    const char* codecName = hevc ? "HEVC" : "H.264";
    ComPtr<IMFMediaType> out;
    hr = mf::CreateMediaType(&out);
    if (FAILED(hr)) { error = "MFCreateMediaType: " + FormatHr(hr); return false; }
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, hevc ? MFVideoFormat_HEVC : MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, m_cfg.bitrateKbps * 1000u);
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    mf::SetSize(out.Get(), MF_MT_FRAME_SIZE, m_w, m_h);
    mf::SetRatio(out.Get(), MF_MT_FRAME_RATE, m_cfg.fpsNum, m_cfg.fpsDen);
    mf::SetRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    out->SetUINT32(MF_MT_MPEG2_PROFILE, hevc ? (UINT32)eAVEncH265VProfile_Main_420_8 : (UINT32)eAVEncH264VProfile_High);
    DWORD videoStream = 0;
    hr = writer->AddStream(out.Get(), &videoStream);
    if (FAILED(hr)) { error = StrPrintf("cannot add the %s video stream: %s", codecName, FormatHr(hr).c_str()); return false; }

    ComPtr<IMFMediaType> in;
    hr = mf::CreateMediaType(&in);
    if (FAILED(hr)) { error = "MFCreateMediaType: " + FormatHr(hr); return false; }
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    mf::SetSize(in.Get(), MF_MT_FRAME_SIZE, m_w, m_h);
    mf::SetRatio(in.Get(), MF_MT_FRAME_RATE, m_cfg.fpsNum, m_cfg.fpsDen);
    mf::SetRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, m_w);
    in->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    in->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    in->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    in->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    in->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    hr = writer->SetInputMediaType(videoStream, in.Get(), nullptr);
    if (FAILED(hr)) {
        if (hr == MF_E_TOPO_CODEC_NOT_FOUND || hr == MF_E_INVALIDMEDIATYPE)
            error = StrPrintf("no %s encoder is available on this system%s (%s)", codecName, hevc ? "; try H.264" : "", FormatHr(hr).c_str());
        else
            error = StrPrintf("the %s encoder rejected the picture format: %s", codecName, FormatHr(hr).c_str());
        return false;
    }

    bool audio = false;
    DWORD audioStream = 0;
    if (withAudio && m_cfg.audio) {
        UINT32 rate = 0, channels = 0, bits = 0;
        m_cfg.audio->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        m_cfg.audio->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        m_cfg.audio->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
        if ((rate == 44100 || rate == 48000) && (channels == 1 || channels == 2) && bits == 16) {
            ComPtr<IMFMediaType> aac;
            hr = mf::CreateMediaType(&aac);
            if (SUCCEEDED(hr)) {
                aac->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                aac->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
                aac->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                aac->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
                aac->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
                aac->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, channels == 1 ? 12000u : 24000u);
                aac->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
                hr = writer->AddStream(aac.Get(), &audioStream);
            }
            if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audioStream, m_cfg.audio.Get(), nullptr);
            if (SUCCEEDED(hr)) {
                audio = true;
            } else {
                Log::Warn("Video output: no AAC encoder for the audio (%s); writing the file without audio", FormatHr(hr).c_str());
                writer.Reset();
                return OpenFile(w, h, false, error);
            }
        } else {
            Log::Warn("Video output: audio at %u Hz, %u channels, %u bit cannot be encoded as AAC (44.1/48 kHz mono or stereo, 16 bit); "
                      "writing the file without audio", rate, channels, bits);
        }
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) {
        if (hr == MF_E_TOPO_CODEC_NOT_FOUND)
            error = StrPrintf("no %s encoder is available on this system%s (%s)", codecName, hevc ? "; try H.264" : "", FormatHr(hr).c_str());
        else
            error = "BeginWriting: " + FormatHr(hr);
        return false;
    }
    // Which encoder the writer picked (hardware transforms carry a device URL).
    ComPtr<IMFSinkWriterEx> ex;
    if (SUCCEEDED(writer.As(&ex)) && ex) {
        for (DWORD i = 0; i < 8; ++i) {
            GUID category{};
            ComPtr<IMFTransform> transform;
            if (FAILED(ex->GetTransformForStream(videoStream, i, &category, &transform)) || !transform) break;
            ComPtr<IMFAttributes> ta;
            UINT32 length = 0;
            if (SUCCEEDED(transform->GetAttributes(&ta)) && ta && SUCCEEDED(ta->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute, &length)))
                m_hardware = true;
        }
    }
    m_writer = writer;
    m_videoStream = videoStream;
    m_audioStream = audioStream;
    m_audio = audio;
    m_opened = true;
    if (!m_audio) {
        m_audioHeld.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_audioQueue.clear();
    }
    Log::Info("Video output: %s %ux%u %s %u kbit/s, %.3f fps, %s, %s encoder", WideToUtf8(m_cfg.path).c_str(), m_w, m_h, codecName,
              m_cfg.bitrateKbps, (double)m_cfg.fpsNum / (double)std::max(1u, m_cfg.fpsDen), audio ? "AAC audio" : "no audio",
              m_hardware ? "hardware" : "software");
    return true;
}

bool VideoWriter::WriteAudioUpTo(LONGLONG limit, std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_audioQueue.empty()) {
            m_audioHeld.push_back(std::move(m_audioQueue.front()));
            m_audioQueue.pop_front();
        }
    }
    if (!m_audio) { m_audioHeld.clear(); return true; }
    while (!m_audioHeld.empty()) {
        ComPtr<IMFSample>& s = m_audioHeld.front();
        LONGLONG at = 0;
        if (FAILED(s->GetSampleTime(&at))) { m_audioHeld.pop_front(); continue; }
        const LONGLONG t = at - m_base;
        if (t < 0) { m_audioHeld.pop_front(); continue; }   // before the first picture
        if (t > limit) break;
        s->SetSampleTime(t);
        const HRESULT hr = m_writer->WriteSample(m_audioStream, s.Get());
        if (FAILED(hr)) { error = "WriteSample (audio): " + FormatHr(hr); return false; }
        m_audioHeld.pop_front();
    }
    return true;
}

bool VideoWriter::WriteFrame(const Frame& f, std::string& error) {
    if ((f.w & ~1u) != m_w || (f.h & ~1u) != m_h || f.pitch < f.w * 4 || f.rgba.size() < (size_t)f.pitch * f.h) {
        if (++m_sizeMismatch == 1)
            Log::Warn("Video output: a frame of %ux%u does not fit the %ux%u file; skipped (the output size changed during the run?)",
                      f.w, f.h, m_w, m_h);
        return true;
    }
    LONGLONG t = f.pts - m_base;
    if (t <= m_lastVideoTime) t = m_lastVideoTime + 1;
    const LONGLONG d = f.duration > 0 ? f.duration : (LONGLONG)(10000000.0 * (double)m_cfg.fpsDen / (double)std::max(1u, m_cfg.fpsNum));
    if (!WriteAudioUpTo(t + kAudioLead, error)) return false;

    const DWORD size = (DWORD)((size_t)m_w * m_h * 3 / 2);
    ComPtr<IMFMediaBuffer> buf;
    HRESULT hr = mf::CreateAlignedMemoryBuffer(size, MF_64_BYTE_ALIGNMENT, &buf);
    if (FAILED(hr)) { error = "MFCreateAlignedMemoryBuffer: " + FormatHr(hr); return false; }
    BYTE* p = nullptr;
    DWORD maxLen = 0;
    hr = buf->Lock(&p, &maxLen, nullptr);
    if (FAILED(hr) || !p || maxLen < size) { error = "IMFMediaBuffer::Lock: " + FormatHr(hr); return false; }
    ConvertToNv12(f.rgba.data(), f.pitch, m_w, m_h, p, p + (size_t)m_w * m_h);
    buf->Unlock();
    buf->SetCurrentLength(size);
    ComPtr<IMFSample> sample;
    hr = mf::CreateSample(&sample);
    if (FAILED(hr)) { error = "MFCreateSample: " + FormatHr(hr); return false; }
    sample->AddBuffer(buf.Get());
    sample->SetSampleTime(t);
    sample->SetSampleDuration(d);
    hr = m_writer->WriteSample(m_videoStream, sample.Get());
    if (FAILED(hr)) { error = "WriteSample (video): " + FormatHr(hr); return false; }
    m_lastVideoTime = t;
    return true;
}

void VideoWriter::WriterMain() {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string error;
    bool aborted = false;
    for (;;) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&] { return m_abort || m_finishing || m_frames.count(m_nextIndex) > 0 || m_frames.size() >= m_maxQueued; });
        if (m_abort) { aborted = true; break; }
        auto it = m_frames.find(m_nextIndex);
        if (it == m_frames.end()) {
            if (m_frames.empty()) {
                if (m_finishing) break;
                continue;
            }
            // The frame never arrived (a skipped readback): carry on with the next one there is.
            it = m_frames.begin();
            Log::Warn("Video output: frame %llu never arrived; continuing with frame %llu", (unsigned long long)m_nextIndex,
                      (unsigned long long)it->first);
        }
        Frame f = std::move(it->second);
        m_nextIndex = it->first + 1;
        m_frames.erase(it);
        lock.unlock();
        m_space.notify_all();
        if (!m_opened && !OpenFile(f.w, f.h, true, error)) { Fail(error); break; }
        if (!m_baseSet) { m_base = f.pts; m_baseSet = true; }
        if (!WriteFrame(f, error)) { Fail(error); break; }
        ++m_written;
    }
    if (m_opened && m_writer) {
        if (!Failed()) {
            if (!aborted && !WriteAudioUpTo(LLONG_MAX, error)) Fail(error);
            const HRESULT hr = m_writer->Finalize();
            if (FAILED(hr)) Fail("Finalize: " + FormatHr(hr));
        } else {
            m_writer->Finalize();   // best effort: leave what was written in a playable file
        }
    } else if (!aborted && !Failed()) {
        Fail("no frames reached the encoder");
    }
    m_writer.Reset();
    m_audioHeld.clear();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frames.clear();
        m_audioQueue.clear();
        m_running = false;
    }
    m_space.notify_all();
    if (SUCCEEDED(coHr)) CoUninitialize();
}

} // namespace vdc
