#include "core/AnimatedImage.h"
#include "core/Log.h"
#include <wincodec.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/demux.h>
#include <webp/mux.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <numeric>

namespace vdc {

namespace {

constexpr size_t   kMaxFileBytes = 1024u << 20;   // an animated image larger than 1 GB is not read into memory
constexpr LONGLONG kMinFrameTicks = 100000;       // 10 ms: frames with no delay are shown at least this long
constexpr LONGLONG kGifDefaultTicks = 1000000;    // 100 ms: GIF frames with a delay of 0 or 1 cs (what browsers do)

// --- files ----------------------------------------------------------------------------------------

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out, std::string& error) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { error = "cannot open the file: " + LastErrorText(); return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || (ULONGLONG)size.QuadPart > kMaxFileBytes) {
        CloseHandle(h);
        error = "the file is too large to read into memory";
        return false;
    }
    out.resize((size_t)size.QuadPart);
    size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = (DWORD)std::min<size_t>(out.size() - done, 16u << 20);
        if (!ReadFile(h, out.data() + done, want, &got, nullptr) || got == 0) { CloseHandle(h); error = "cannot read the file: " + LastErrorText(); return false; }
        done += got;
    }
    CloseHandle(h);
    return true;
}

// Buffered forward reader with seeks, for looking at file structure without reading everything.
class FileCursor {
public:
    explicit FileCursor(const std::wstring& path) {
        m_h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~FileCursor() { if (m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h); }
    bool Ok() const { return m_h != INVALID_HANDLE_VALUE; }
    bool Read(void* dst, size_t n) {
        uint8_t* p = static_cast<uint8_t*>(dst);
        while (n) {
            if (m_pos == m_end && !Fill()) return false;
            const size_t take = std::min(n, m_end - m_pos);
            memcpy(p, m_buf + m_pos, take);
            m_pos += take; p += take; n -= take;
        }
        return true;
    }
    bool Skip(UINT64 n) {
        if (n <= m_end - m_pos) { m_pos += (size_t)n; return true; }
        n -= m_end - m_pos;
        m_pos = m_end = 0;
        LARGE_INTEGER d; d.QuadPart = (LONGLONG)n;
        return SetFilePointerEx(m_h, d, nullptr, FILE_CURRENT) != 0;
    }
    bool U8(uint8_t& v) { return Read(&v, 1); }
    bool BE32(uint32_t& v) { uint8_t b[4]; if (!Read(b, 4)) return false; v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; return true; }
    bool LE32(uint32_t& v) { uint8_t b[4]; if (!Read(b, 4)) return false; v = ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | b[0]; return true; }

private:
    bool Fill() {
        DWORD got = 0;
        if (!ReadFile(m_h, m_buf, sizeof(m_buf), &got, nullptr) || got == 0) return false;
        m_pos = 0; m_end = got;
        return true;
    }
    HANDLE  m_h = INVALID_HANDLE_VALUE;
    uint8_t m_buf[64 * 1024];
    size_t  m_pos = 0, m_end = 0;
};

// GIF: counts image descriptors (stops at two). Extensions and image data are sub-block chains.
int CountGifFrames(FileCursor& f) {
    uint8_t head[13];
    if (!f.Read(head, 13) || memcmp(head, "GIF8", 4) != 0) return 0;
    if (head[10] & 0x80) { if (!f.Skip(3ull << ((head[10] & 7) + 1))) return 0; }
    auto skipSubBlocks = [&]() {
        for (;;) {
            uint8_t n = 0;
            if (!f.U8(n)) return false;
            if (n == 0) return true;
            if (!f.Skip(n)) return false;
        }
    };
    int frames = 0;
    for (;;) {
        uint8_t b = 0;
        if (!f.U8(b)) return frames;
        if (b == 0x3B) return frames;
        if (b == 0x21) {
            uint8_t label = 0;
            if (!f.U8(label) || !skipSubBlocks()) return frames;
        } else if (b == 0x2C) {
            uint8_t d[9];
            if (!f.Read(d, 9)) return frames;
            if (d[8] & 0x80) { if (!f.Skip(3ull << ((d[8] & 7) + 1))) return frames; }
            uint8_t minCode = 0;
            if (!f.U8(minCode) || !skipSubBlocks()) return frames;
            if (++frames >= 2) return frames;
        } else {
            return frames;   // not a GIF block
        }
    }
}

// PNG: an acTL chunk before the first IDAT makes an APNG.
int CountApngFrames(FileCursor& f) {
    static const uint8_t kSig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    uint8_t sig[8];
    if (!f.Read(sig, 8) || memcmp(sig, kSig, 8) != 0) return 0;
    for (;;) {
        uint32_t len = 0;
        char type[4];
        if (!f.BE32(len) || !f.Read(type, 4)) return 0;
        if (memcmp(type, "acTL", 4) == 0) {
            uint32_t frames = 0;
            if (len < 8 || !f.BE32(frames)) return 0;
            return (int)std::min<uint32_t>(frames, INT_MAX);
        }
        if (memcmp(type, "IDAT", 4) == 0 || memcmp(type, "IEND", 4) == 0) return 0;
        if (!f.Skip((UINT64)len + 4)) return 0;
    }
}

// WebP: the VP8X animation flag, and at least two ANMF chunks.
int CountWebPFrames(FileCursor& f) {
    uint8_t head[12];
    if (!f.Read(head, 12) || memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WEBP", 4) != 0) return 0;
    int frames = 0;
    for (;;) {
        char type[4];
        uint32_t len = 0;
        if (!f.Read(type, 4) || !f.LE32(len)) return frames;
        if (memcmp(type, "VP8X", 4) == 0) {
            uint8_t flags = 0;
            if (len < 10 || !f.U8(flags)) return 0;
            if (!(flags & 0x02)) return 0;   // no animation
            if (!f.Skip((UINT64)len - 1 + (len & 1))) return 0;
            continue;
        }
        if (memcmp(type, "ANMF", 4) == 0 && ++frames >= 2) return frames;
        if (!f.Skip((UINT64)len + (len & 1))) return frames;
    }
}

// --- helpers --------------------------------------------------------------------------------------

std::wstring LowerExtension(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

inline uint32_t BE32At(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
inline uint16_t BE16At(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
inline void PutBE32(std::vector<uint8_t>& v, uint32_t x) { v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }
inline void PutBE16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }

// A PNG chunk: length, type, data, CRC over type + data.
void AppendChunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data, size_t len) {
    PutBE32(out, (uint32_t)len);
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (len) out.insert(out.end(), data, data + len);
    PutBE32(out, Crc32(out.data() + start, out.size() - start));
}

bool ReadMetaUInt(IWICMetadataQueryReader* r, const wchar_t* name, UINT& out) {
    PROPVARIANT v;
    PropVariantInit(&v);
    if (FAILED(r->GetMetadataByName(name, &v))) { PropVariantClear(&v); return false; }
    bool ok = true;
    switch (v.vt) {
        case VT_UI1: out = v.bVal; break;
        case VT_UI2: out = v.uiVal; break;
        case VT_UI4: out = v.ulVal; break;
        case VT_I2:  out = (UINT)std::max<int>(0, v.iVal); break;
        case VT_I4:  out = (UINT)std::max<LONG>(0, v.lVal); break;
        case VT_BOOL: out = v.boolVal ? 1u : 0u; break;
        default: ok = false; break;
    }
    PropVariantClear(&v);
    return ok;
}

bool ReadMetaBytes(IWICMetadataQueryReader* r, const wchar_t* name, std::vector<uint8_t>& out) {
    PROPVARIANT v;
    PropVariantInit(&v);
    if (FAILED(r->GetMetadataByName(name, &v))) { PropVariantClear(&v); return false; }
    bool ok = false;
    if (v.vt == (VT_UI1 | VT_VECTOR)) { out.assign(v.caub.pElems, v.caub.pElems + v.caub.cElems); ok = true; }
    PropVariantClear(&v);
    return ok;
}

HRESULT WriteMetaUInt16(IWICMetadataQueryWriter* w, const wchar_t* name, UINT16 value) {
    PROPVARIANT v; PropVariantInit(&v); v.vt = VT_UI2; v.uiVal = value;
    return w->SetMetadataByName(name, &v);
}
HRESULT WriteMetaUInt8(IWICMetadataQueryWriter* w, const wchar_t* name, UINT8 value) {
    PROPVARIANT v; PropVariantInit(&v); v.vt = VT_UI1; v.bVal = value;
    return w->SetMetadataByName(name, &v);
}
HRESULT WriteMetaBool(IWICMetadataQueryWriter* w, const wchar_t* name, bool value) {
    PROPVARIANT v; PropVariantInit(&v); v.vt = VT_BOOL; v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return w->SetMetadataByName(name, &v);
}
HRESULT WriteMetaBytes(IWICMetadataQueryWriter* w, const wchar_t* name, const uint8_t* data, ULONG n) {
    PROPVARIANT v; PropVariantInit(&v);
    v.vt = VT_UI1 | VT_VECTOR;
    v.caub.cElems = n;
    v.caub.pElems = const_cast<UCHAR*>(data);
    return w->SetMetadataByName(name, &v);
}

// Decodes a picture held in memory with WIC into tightly packed BGRA (the frame's own size).
bool DecodeWicMemory(IWICImagingFactory* factory, const uint8_t* data, size_t size, UINT& w, UINT& h, std::vector<uint8_t>& bgra, std::string& error) {
    ComPtr<IWICStream> stream;
    HRESULT hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(const_cast<BYTE*>(data), (DWORD)size);
    if (FAILED(hr)) { error = "WIC stream: " + FormatHr(hr); return false; }
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) { error = "cannot decode the frame: " + FormatHr(hr); return false; }
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { error = "GetFrame: " + FormatHr(hr); return false; }
    frame->GetSize(&w, &h);
    if (!w || !h) { error = "the frame is empty"; return false; }
    ComPtr<IWICBitmapSource> conv;
    hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &conv);
    if (FAILED(hr)) { error = "pixel format conversion failed: " + FormatHr(hr); return false; }
    bgra.resize((size_t)w * h * 4);
    hr = conv->CopyPixels(nullptr, w * 4, (UINT)bgra.size(), bgra.data());
    if (FAILED(hr)) { error = "decoding the pixels failed: " + FormatHr(hr); return false; }
    return true;
}

// Source-over of a straight-alpha BGRA frame onto the canvas at (x, y). copyPixels: the frame replaces the canvas
// pixels (APNG blend "source"); otherwise it is composited over them. GIF frames leave transparent pixels untouched.
void Composite(std::vector<uint8_t>& canvas, UINT cw, UINT ch, const std::vector<uint8_t>& frame, UINT fw, UINT fh, UINT x, UINT y,
               bool copyPixels, bool skipTransparent) {
    if (x >= cw || y >= ch) return;
    const UINT w = std::min(fw, cw - x), h = std::min(fh, ch - y);
    for (UINT j = 0; j < h; ++j) {
        const uint8_t* s = frame.data() + ((size_t)j * fw) * 4;
        uint8_t* d = canvas.data() + (((size_t)(y + j) * cw) + x) * 4;
        for (UINT i = 0; i < w; ++i, s += 4, d += 4) {
            const unsigned a = s[3];
            if (copyPixels) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; continue; }
            if (a == 0) { if (skipTransparent) continue; if (d[3] == 0) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; } continue; }
            if (a == 255 || d[3] == 0) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; continue; }
            const unsigned da = d[3];
            const unsigned oa = a * 255 + da * (255 - a);           // x255
            for (int c = 0; c < 3; ++c) d[c] = (uint8_t)((s[c] * a * 255 + d[c] * da * (255 - a) + oa / 2) / oa);
            d[3] = (uint8_t)((oa + 127) / 255);
        }
    }
}

void ClearRect(std::vector<uint8_t>& canvas, UINT cw, UINT ch, UINT x, UINT y, UINT w, UINT h) {
    if (x >= cw || y >= ch) return;
    w = std::min(w, cw - x); h = std::min(h, ch - y);
    for (UINT j = 0; j < h; ++j) memset(canvas.data() + (((size_t)(y + j) * cw) + x) * 4, 0, (size_t)w * 4);
}

} // namespace

// --- format names / probing -----------------------------------------------------------------------

const char* AnimFormatName(AnimFormat f) {
    switch (f) {
        case AnimFormat::Gif:  return "GIF";
        case AnimFormat::Apng: return "APNG";
        case AnimFormat::WebP: return "WebP";
        default: return "";
    }
}

const wchar_t* AnimFormatExtension(AnimFormat f) {
    switch (f) {
        case AnimFormat::Gif:  return L"gif";
        case AnimFormat::Apng: return L"png";
        case AnimFormat::WebP: return L"webp";
        default: return L"";
    }
}

AnimFormat ProbeAnimatedImage(const std::wstring& path) {
    const std::wstring ext = LowerExtension(path);
    AnimFormat guess = AnimFormat::None;
    if (ext == L"gif") guess = AnimFormat::Gif;
    else if (ext == L"png" || ext == L"apng") guess = AnimFormat::Apng;
    else if (ext == L"webp") guess = AnimFormat::WebP;
    if (guess == AnimFormat::None) return guess;
    FileCursor f(path);
    if (!f.Ok()) return AnimFormat::None;
    int frames = 0;
    switch (guess) {
        case AnimFormat::Gif:  frames = CountGifFrames(f); break;
        case AnimFormat::Apng: frames = CountApngFrames(f); break;
        case AnimFormat::WebP: frames = CountWebPFrames(f); break;
        default: break;
    }
    return frames >= 2 ? guess : AnimFormat::None;
}

bool IsWebPPath(const std::wstring& path) { return LowerExtension(path) == L"webp"; }

bool DecodeStillWebP(const std::wstring& path, UINT& width, UINT& height, std::vector<uint8_t>& bgra, std::string& error) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, file, error)) return false;
    int w = 0, h = 0;
    if (!WebPGetInfo(file.data(), file.size(), &w, &h) || w <= 0 || h <= 0) { error = "not a WebP picture"; return false; }
    uint8_t* pixels = WebPDecodeBGRA(file.data(), file.size(), &w, &h);
    if (!pixels) { error = "the WebP decoder rejected the file"; return false; }
    width = (UINT)w; height = (UINT)h;
    bgra.assign(pixels, pixels + (size_t)w * h * 4);
    WebPFree(pixels);
    return true;
}

// --- AnimReader -----------------------------------------------------------------------------------

struct AnimReader::Impl {
    // GIF (WIC)
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder>  decoder;
    struct GifFrame { UINT left = 0, top = 0, w = 0, h = 0, disposal = 0; };
    std::vector<GifFrame> gif;
    // APNG
    struct ApngFrame {
        UINT w = 0, h = 0, x = 0, y = 0;
        uint8_t dispose = 0, blend = 0;
        std::vector<std::pair<size_t, size_t>> data;   // offset and length of each IDAT / fdAT payload in the file
    };
    std::vector<ApngFrame> apng;
    std::vector<uint8_t>   pngHead;     // IHDR fields (13 bytes) followed by the raw chunks copied into every frame
    // WebP (libwebp)
    WebPAnimDecoder* webp = nullptr;

    ~Impl() { if (webp) WebPAnimDecoderDelete(webp); }
};

AnimReader::AnimReader() = default;
AnimReader::~AnimReader() { Close(); }

void AnimReader::Close() {
    m_impl.reset();
    m_file.clear();
    m_file.shrink_to_fit();
    m_frames.clear();
    m_canvas.clear();
    m_previous.clear();
    m_path.clear();
    m_format = AnimFormat::None;
    m_width = m_height = 0;
    m_duration = 0;
    m_loops = 0;
    m_hasAlpha = m_lossless = false;
    m_next = 0;
}

bool AnimReader::Open(const std::wstring& path, std::string& error) {
    error.clear();
    Close();
    const AnimFormat format = ProbeAnimatedImage(path);
    if (format == AnimFormat::None) { error = "not an animated image"; return false; }
    if (!ReadWholeFile(path, m_file, error)) return false;
    m_impl = std::make_unique<Impl>();
    m_path = path;
    m_format = format;
    bool ok = false;
    switch (format) {
        case AnimFormat::Gif:  ok = OpenGif(error); break;
        case AnimFormat::Apng: ok = OpenApng(error); break;
        case AnimFormat::WebP: ok = OpenWebP(error); break;
        default: break;
    }
    if (ok && (m_frames.size() < 2 || !m_width || !m_height)) { ok = false; error = "the file holds no animation"; }
    if (!ok) { const std::string e = error; Close(); error = e; return false; }
    // Timing: consecutive starts; frames with no delay are shown for a short moment.
    m_duration = 0;
    for (auto& f : m_frames) {
        if (f.duration < kMinFrameTicks) f.duration = kMinFrameTicks;
        f.pts = m_duration;
        m_duration += f.duration;
    }
    m_canvas.assign((size_t)m_width * m_height * 4, 0);
    m_next = 0;
    return true;
}

size_t AnimReader::FrameAt(LONGLONG pts) const {
    if (m_frames.empty()) return 0;
    size_t lo = 0, hi = m_frames.size();
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (m_frames[mid].pts <= pts) lo = mid; else hi = mid;
    }
    return lo;
}

void AnimReader::FrameRate(UINT& num, UINT& den) const {
    std::map<LONGLONG, unsigned> histogram;
    for (const auto& f : m_frames) ++histogram[f.duration];
    LONGLONG mode = 0;
    unsigned best = 0;
    for (const auto& [d, n] : histogram) if (n > best) { best = n; mode = d; }
    if (mode <= 0) { num = 30; den = 1; return; }
    const LONGLONG g = std::gcd<LONGLONG>(10000000, mode);
    LONGLONG n = 10000000 / g, d = mode / g;
    while (n > UINT_MAX || d > UINT_MAX) { n = std::max<LONGLONG>(1, n / 2); d = std::max<LONGLONG>(1, d / 2); }
    num = (UINT)n; den = (UINT)d;
}

void AnimReader::Rewind() {
    m_next = 0;
    std::fill(m_canvas.begin(), m_canvas.end(), (uint8_t)0);
    if (m_format == AnimFormat::WebP && m_impl && m_impl->webp) WebPAnimDecoderReset(m_impl->webp);
}

bool AnimReader::Decode(size_t index, std::vector<uint8_t>& bgra, std::string& error) {
    error.clear();
    if (!Opened()) { error = "no animation is open"; return false; }
    if (index >= m_frames.size()) index = m_frames.size() - 1;
    if (index + 1 == m_next) { bgra = m_canvas; return true; }   // the frame shown last
    if (index < m_next) Rewind();
    while (m_next <= index) {
        bool ok = false;
        switch (m_format) {
            case AnimFormat::Gif:  ok = DecodeNextGif(error); break;
            case AnimFormat::Apng: ok = DecodeNextApng(error); break;
            case AnimFormat::WebP: ok = DecodeNextWebP(error); break;
            default: break;
        }
        if (!ok) { Rewind(); return false; }
        ++m_next;
    }
    bgra = m_canvas;
    return true;
}

// GIF: WIC decodes the frames (indexed colour with the transparent index as alpha 0); the logical screen, offsets,
// delays and disposal come from the metadata.
bool AnimReader::OpenGif(std::string& error) {
    Impl& im = *m_impl;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&im.factory));
    if (FAILED(hr)) { error = "WIC unavailable: " + FormatHr(hr); return false; }
    ComPtr<IWICStream> stream;
    hr = im.factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(m_file.data(), (DWORD)m_file.size());
    if (FAILED(hr)) { error = "WIC stream: " + FormatHr(hr); return false; }
    hr = im.factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &im.decoder);
    if (FAILED(hr)) { error = "cannot decode the GIF: " + FormatHr(hr); return false; }
    UINT count = 0;
    hr = im.decoder->GetFrameCount(&count);
    if (FAILED(hr) || count < 2) { error = "the GIF holds no animation"; return false; }

    UINT screenW = 0, screenH = 0;
    m_loops = 1;   // no NETSCAPE block: played once
    ComPtr<IWICMetadataQueryReader> meta;
    if (SUCCEEDED(im.decoder->GetMetadataQueryReader(&meta)) && meta) {
        ReadMetaUInt(meta.Get(), L"/logscrdesc/Width", screenW);
        ReadMetaUInt(meta.Get(), L"/logscrdesc/Height", screenH);
        std::vector<uint8_t> app, data;
        if (ReadMetaBytes(meta.Get(), L"/appext/Application", app) && app.size() >= 11 &&
            (memcmp(app.data(), "NETSCAPE2.0", 11) == 0 || memcmp(app.data(), "ANIMEXTS1.0", 11) == 0) &&
            ReadMetaBytes(meta.Get(), L"/appext/Data", data) && data.size() >= 4 && data[0] == 3 && data[1] == 1) {
            m_loops = data[2] | (data[3] << 8);
        }
    }
    UINT maxRight = 0, maxBottom = 0, prevDisposal = 0, prevLeft = 0, prevTop = 0, prevW = 0, prevH = 0;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(im.decoder->GetFrame(i, &frame))) { error = StrPrintf("cannot read frame %u of the GIF", i); return false; }
        Impl::GifFrame g;
        frame->GetSize(&g.w, &g.h);
        UINT delay = 10, disposal = 0, transparent = 0;
        ComPtr<IWICMetadataQueryReader> fm;
        if (SUCCEEDED(frame->GetMetadataQueryReader(&fm)) && fm) {
            UINT v = 0;
            if (ReadMetaUInt(fm.Get(), L"/imgdesc/Left", v)) g.left = v;
            if (ReadMetaUInt(fm.Get(), L"/imgdesc/Top", v)) g.top = v;
            if (ReadMetaUInt(fm.Get(), L"/imgdesc/Width", v) && v) g.w = v;
            if (ReadMetaUInt(fm.Get(), L"/imgdesc/Height", v) && v) g.h = v;
            if (ReadMetaUInt(fm.Get(), L"/grctlext/Delay", v)) delay = v;
            if (ReadMetaUInt(fm.Get(), L"/grctlext/Disposal", v)) disposal = v;
            if (ReadMetaUInt(fm.Get(), L"/grctlext/TransparencyFlag", v)) transparent = v;
        }
        g.disposal = disposal;
        // Transparent pixels reach the picture on the first frame, where the first frame leaves the screen uncovered,
        // and over an area the previous frame restored to the background. The transparent pixels of later frames of
        // an opaque GIF are only the encoder's way of keeping unchanged pixels from the frame before.
        if (i == 0) {
            if (transparent || (screenW && screenH && (g.left || g.top || g.w < screenW || g.h < screenH))) m_hasAlpha = true;
        } else if (prevDisposal == 2 && (transparent || g.left > prevLeft || g.top > prevTop ||
                                         g.left + g.w < prevLeft + prevW || g.top + g.h < prevTop + prevH)) {
            m_hasAlpha = true;
        }
        prevDisposal = disposal; prevLeft = g.left; prevTop = g.top; prevW = g.w; prevH = g.h;
        maxRight = std::max(maxRight, g.left + g.w);
        maxBottom = std::max(maxBottom, g.top + g.h);
        im.gif.push_back(g);
        AnimFrameInfo info;
        info.duration = delay <= 1 ? kGifDefaultTicks : (LONGLONG)delay * 100000;
        m_frames.push_back(info);
    }
    m_width = std::max(screenW, maxRight);
    m_height = std::max(screenH, maxBottom);
    if (!m_width || !m_height) { error = "the GIF has no size"; return false; }
    if (screenW && screenH && (maxRight > screenW || maxBottom > screenH)) m_hasAlpha = true;   // frames leave parts uncovered
    return true;
}

bool AnimReader::DecodeNextGif(std::string& error) {
    Impl& im = *m_impl;
    const size_t i = m_next;
    if (i >= im.gif.size()) { error = "no more frames"; return false; }
    if (i > 0) {
        const Impl::GifFrame& p = im.gif[i - 1];
        if (p.disposal == 2) ClearRect(m_canvas, m_width, m_height, p.left, p.top, p.w, p.h);
        else if (p.disposal == 3 && m_previous.size() == m_canvas.size()) m_canvas = m_previous;
    }
    const Impl::GifFrame& g = im.gif[i];
    if (g.disposal == 3) m_previous = m_canvas;
    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = im.decoder->GetFrame((UINT)i, &frame);
    if (FAILED(hr)) { error = StrPrintf("cannot read frame %u: %s", (unsigned)i, FormatHr(hr).c_str()); return false; }
    ComPtr<IWICBitmapSource> conv;
    hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &conv);
    if (FAILED(hr)) { error = "pixel format conversion failed: " + FormatHr(hr); return false; }
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    if (!w || !h) { error = "an empty frame"; return false; }
    std::vector<uint8_t> pixels((size_t)w * h * 4);
    hr = conv->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());
    if (FAILED(hr)) { error = "decoding the pixels failed: " + FormatHr(hr); return false; }
    Composite(m_canvas, m_width, m_height, pixels, w, h, g.left, g.top, false, true);
    return true;
}

// APNG: the chunk table is read once; each frame becomes a small PNG in memory (the file's IHDR and palette with
// the frame's own size and data) that WIC decodes, then it is placed on the canvas with its dispose and blend rules.
bool AnimReader::OpenApng(std::string& error) {
    Impl& im = *m_impl;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&im.factory));
    if (FAILED(hr)) { error = "WIC unavailable: " + FormatHr(hr); return false; }
    const uint8_t* p = m_file.data();
    const size_t n = m_file.size();
    if (n < 8 + 25) { error = "not a PNG file"; return false; }
    size_t pos = 8;
    bool haveIhdr = false, haveActl = false, sawIdat = false, defaultIsFrame = false;
    UINT colorType = 0, bitDepth = 0;
    Impl::ApngFrame* current = nullptr;
    while (pos + 12 <= n) {
        const uint32_t len = BE32At(p + pos);
        const uint8_t* type = p + pos + 4;
        const uint8_t* data = p + pos + 8;
        if ((UINT64)pos + 12 + len > n) break;
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) {
            m_width = BE32At(data); m_height = BE32At(data + 4);
            bitDepth = data[8]; colorType = data[9];
            im.pngHead.assign(data, data + 13);
            haveIhdr = true;
        } else if (memcmp(type, "acTL", 4) == 0 && len >= 8) {
            haveActl = true;
            m_loops = (int)std::min<uint32_t>(BE32At(data + 4), INT_MAX);
        } else if (memcmp(type, "fcTL", 4) == 0 && len >= 26) {
            Impl::ApngFrame f;
            f.w = BE32At(data + 4); f.h = BE32At(data + 8);
            f.x = BE32At(data + 12); f.y = BE32At(data + 16);
            const uint16_t num = BE16At(data + 20), den = BE16At(data + 22);
            f.dispose = data[24]; f.blend = data[25];
            im.apng.push_back(f);
            current = &im.apng.back();
            AnimFrameInfo info;
            info.duration = (LONGLONG)std::llround(10000000.0 * (double)num / (double)(den ? den : 100));
            m_frames.push_back(info);
            if (!sawIdat) defaultIsFrame = true;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            sawIdat = true;
            if (defaultIsFrame && current && im.apng.size() == 1) current->data.emplace_back(pos + 8, len);
        } else if (memcmp(type, "fdAT", 4) == 0 && len >= 4) {
            if (current) current->data.emplace_back(pos + 12, len - 4);
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        } else if (!sawIdat && haveIhdr && memcmp(type, "tEXt", 4) != 0 && memcmp(type, "zTXt", 4) != 0 && memcmp(type, "iTXt", 4) != 0 &&
                   memcmp(type, "tIME", 4) != 0 && memcmp(type, "eXIf", 4) != 0) {
            im.pngHead.insert(im.pngHead.end(), p + pos, p + pos + 12 + len);   // PLTE, tRNS, gAMA, sRGB, ...: copied into every frame
        }
        pos += 12 + (size_t)len;
    }
    if (!haveIhdr || !haveActl) { error = "the PNG holds no animation"; return false; }
    // Frames without data (a stray fcTL) are dropped.
    for (size_t i = 0; i < im.apng.size();) {
        if (im.apng[i].data.empty()) { im.apng.erase(im.apng.begin() + i); m_frames.erase(m_frames.begin() + i); }
        else ++i;
    }
    if (im.apng.size() < 2) { error = "the PNG holds no animation"; return false; }
    m_hasAlpha = colorType == 4 || colorType == 6;
    if (!m_hasAlpha) {
        // An opaque pixel format leaves the canvas see-through only where the first frame does not cover it and
        // where a frame restored to the background is not painted over by the next one.
        for (size_t i = 0; i < im.apng.size(); ++i) {
            const auto& f = im.apng[i];
            if (i == 0) {
                if (f.x || f.y || f.w != m_width || f.h != m_height) { m_hasAlpha = true; break; }
            } else if (im.apng[i - 1].dispose == 1) {
                const auto& b = im.apng[i - 1];
                if (f.x > b.x || f.y > b.y || f.x + f.w < b.x + b.w || f.y + f.h < b.y + b.h) { m_hasAlpha = true; break; }
            }
        }
        // A palette with transparency counts as alpha as well.
        size_t q = 13;
        while (q + 12 <= im.pngHead.size()) {
            const uint32_t len = BE32At(im.pngHead.data() + q);
            if (memcmp(im.pngHead.data() + q + 4, "tRNS", 4) == 0) { m_hasAlpha = true; break; }
            q += 12 + (size_t)len;
        }
    }
    (void)bitDepth;
    m_lossless = true;
    return true;
}

bool AnimReader::DecodeNextApng(std::string& error) {
    Impl& im = *m_impl;
    const size_t i = m_next;
    if (i >= im.apng.size()) { error = "no more frames"; return false; }
    if (i > 0) {
        const Impl::ApngFrame& p = im.apng[i - 1];
        if (p.dispose == 1) ClearRect(m_canvas, m_width, m_height, p.x, p.y, p.w, p.h);
        else if (p.dispose == 2 && m_previous.size() == m_canvas.size()) m_canvas = m_previous;
    }
    const Impl::ApngFrame& f = im.apng[i];
    if (f.dispose == 2) m_previous = m_canvas;
    // The frame as a PNG of its own.
    static const uint8_t kSig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    std::vector<uint8_t> png;
    png.reserve(64 + im.pngHead.size() + 16 * f.data.size() + 4096);
    png.insert(png.end(), kSig, kSig + 8);
    uint8_t ihdr[13];
    memcpy(ihdr, im.pngHead.data(), 13);
    ihdr[0] = (uint8_t)(f.w >> 24); ihdr[1] = (uint8_t)(f.w >> 16); ihdr[2] = (uint8_t)(f.w >> 8); ihdr[3] = (uint8_t)f.w;
    ihdr[4] = (uint8_t)(f.h >> 24); ihdr[5] = (uint8_t)(f.h >> 16); ihdr[6] = (uint8_t)(f.h >> 8); ihdr[7] = (uint8_t)f.h;
    AppendChunk(png, "IHDR", ihdr, 13);
    png.insert(png.end(), im.pngHead.begin() + 13, im.pngHead.end());
    for (const auto& [offset, len] : f.data) AppendChunk(png, "IDAT", m_file.data() + offset, len);
    AppendChunk(png, "IEND", nullptr, 0);
    UINT w = 0, h = 0;
    std::vector<uint8_t> pixels;
    if (!DecodeWicMemory(im.factory.Get(), png.data(), png.size(), w, h, pixels, error)) {
        error = StrPrintf("frame %u: %s", (unsigned)i, error.c_str());
        return false;
    }
    Composite(m_canvas, m_width, m_height, pixels, w, h, f.x, f.y, f.blend == 0, false);
    return true;
}

// WebP: libwebp's animation decoder composites the canvas itself; the frame table comes from its demuxer.
bool AnimReader::OpenWebP(std::string& error) {
    Impl& im = *m_impl;
    WebPData data{ m_file.data(), m_file.size() };
    WebPAnimDecoderOptions opt;
    if (!WebPAnimDecoderOptionsInit(&opt)) { error = "libwebp version mismatch"; return false; }
    opt.color_mode = MODE_BGRA;
    opt.use_threads = 1;
    im.webp = WebPAnimDecoderNew(&data, &opt);
    if (!im.webp) { error = "the WebP decoder rejected the file"; return false; }
    WebPAnimInfo info{};
    if (!WebPAnimDecoderGetInfo(im.webp, &info)) { error = "cannot read the WebP animation"; return false; }
    m_width = info.canvas_width;
    m_height = info.canvas_height;
    m_loops = (int)std::min<uint32_t>(info.loop_count, INT_MAX);
    const WebPDemuxer* dmux = WebPAnimDecoderGetDemuxer(im.webp);
    if (!dmux) { error = "cannot read the WebP frames"; return false; }
    m_hasAlpha = false;
    m_lossless = true;
    WebPIterator it;
    if (WebPDemuxGetFrame(dmux, 1, &it)) {
        bool first = true;
        int  prevDispose = WEBP_MUX_DISPOSE_NONE, prevX = 0, prevY = 0, prevW = 0, prevH = 0;
        do {
            AnimFrameInfo f;
            f.duration = (LONGLONG)std::max(0, it.duration) * 10000;
            m_frames.push_back(f);
            bool alpha = false;
            WebPBitstreamFeatures features;
            if (WebPGetFeatures(it.fragment.bytes, it.fragment.size, &features) == VP8_STATUS_OK) {
                if (features.format != 2) m_lossless = false;
                alpha = features.has_alpha != 0;
            } else {
                m_lossless = false;
            }
            // Alpha reaches the canvas on the first frame, where the first frame leaves the canvas uncovered, on a
            // frame that replaces rather than blends, and over an area the previous frame disposed to the background.
            // The alpha of a blended sub-frame of an opaque animation only marks the encoder's unchanged pixels.
            const bool full = it.x_offset == 0 && it.y_offset == 0 && (UINT)it.width == m_width && (UINT)it.height == m_height;
            if (first) {
                if (alpha || !full) m_hasAlpha = true;
            } else if (alpha && it.blend_method == WEBP_MUX_NO_BLEND) {
                m_hasAlpha = true;
            } else if (prevDispose == WEBP_MUX_DISPOSE_BACKGROUND &&
                       (alpha || it.x_offset > prevX || it.y_offset > prevY ||
                        it.x_offset + it.width < prevX + prevW || it.y_offset + it.height < prevY + prevH)) {
                m_hasAlpha = true;
            }
            first = false;
            prevDispose = it.dispose_method; prevX = it.x_offset; prevY = it.y_offset; prevW = it.width; prevH = it.height;
        } while (WebPDemuxNextFrame(&it));
        WebPDemuxReleaseIterator(&it);
    }
    if (m_frames.size() != info.frame_count) m_frames.resize(std::min<size_t>(m_frames.size(), info.frame_count));
    return true;
}

bool AnimReader::DecodeNextWebP(std::string& error) {
    Impl& im = *m_impl;
    if (!WebPAnimDecoderHasMoreFrames(im.webp)) { error = "no more frames"; return false; }
    uint8_t* buf = nullptr;
    int timestamp = 0;
    if (!WebPAnimDecoderGetNext(im.webp, &buf, &timestamp) || !buf) { error = StrPrintf("the WebP decoder failed at frame %u", (unsigned)m_next); return false; }
    memcpy(m_canvas.data(), buf, m_canvas.size());
    return true;
}

// --- AnimWriter -----------------------------------------------------------------------------------

namespace {

constexpr size_t kWriterQueueBytes = 256u << 20;   // frames waiting for the encoder: about 256 MB at most
constexpr UINT   kWebPMaxSide = 16383;

// The frames of the file in memory, as the PNG encoder wrote them: IHDR fields and the IDAT chunks.
struct EncodedPng {
    uint8_t ihdr[13] = {};
    std::vector<std::pair<size_t, size_t>> idat;   // offset and length of every IDAT chunk (length, type, data, CRC)
};

bool ParsePng(const std::vector<uint8_t>& png, EncodedPng& out) {
    if (png.size() < 8 + 25) return false;
    size_t pos = 8;
    bool haveIhdr = false;
    while (pos + 12 <= png.size()) {
        const uint32_t len = BE32At(png.data() + pos);
        if ((UINT64)pos + 12 + len > png.size()) return false;
        const uint8_t* type = png.data() + pos + 4;
        if (memcmp(type, "IHDR", 4) == 0 && len >= 13) { memcpy(out.ihdr, png.data() + pos + 8, 13); haveIhdr = true; }
        else if (memcmp(type, "IDAT", 4) == 0) out.idat.emplace_back(pos, 12 + (size_t)len);
        else if (memcmp(type, "IEND", 4) == 0) break;
        pos += 12 + (size_t)len;
    }
    return haveIhdr && !out.idat.empty();
}

bool WriteAll(HANDLE h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (n) {
        DWORD wrote = 0;
        if (!WriteFile(h, p, (DWORD)std::min<size_t>(n, 16u << 20), &wrote, nullptr) || !wrote) return false;
        p += wrote; n -= wrote;
    }
    return true;
}

} // namespace

struct AnimWriter::Impl {
    ComPtr<IWICImagingFactory> factory;
    // GIF
    ComPtr<IWICStream>         stream;
    ComPtr<IWICBitmapEncoder>  encoder;
    LONGLONG                   gifWrittenCs = 0;   // centiseconds of delay emitted so far
    // APNG
    HANDLE                     file = INVALID_HANDLE_VALUE;
    LONGLONG                   actlOffset = 0;
    uint32_t                   sequence = 0;
    uint32_t                   frames = 0;
    LONGLONG                   apngWrittenMs = 0;
    // WebP
    WebPAnimEncoder*           webp = nullptr;
    WebPConfig                 config{};
    int                        lastTimestamp = -1;
    int                        lastDuration = 0;
    ~Impl() {
        if (webp) WebPAnimEncoderDelete(webp);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
};

AnimWriter::AnimWriter() = default;
AnimWriter::~AnimWriter() { Abort(); }

void AnimWriter::Prepare(const AnimWriterConfig& cfg) {
    Abort();
    m_cfg = cfg;
    m_impl.reset();
    m_opened = false;
    m_w = m_h = 0;
    m_baseSet = false;
    m_base = 0;
    m_lastEnd = 0;
    m_sizeMismatch = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frames.clear();
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

void AnimWriter::PushFrame(UINT64 index, LONGLONG pts, LONGLONG duration, std::vector<uint8_t>&& rgba, UINT pitch, UINT w, UINT h) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_running || m_failed || m_abort) return;
    m_maxQueued = std::clamp<size_t>(kWriterQueueBytes / std::max<size_t>(rgba.size(), 1), 2, 8);
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

bool AnimWriter::Finish(std::string& error) {
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

void AnimWriter::Abort() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_abort = true;
    }
    m_cv.notify_all();
    m_space.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void AnimWriter::Reset() {
    Abort();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_failed = false;
    m_error.clear();
}

void AnimWriter::Fail(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_failed) { m_failed = true; m_error = error; }
    }
    m_space.notify_all();
    Log::Error("Animation output: %s", error.c_str());
}

bool AnimWriter::OpenFile(UINT w, UINT h, std::string& error) {
    m_w = w; m_h = h;
    if (m_w < 1 || m_h < 1) { error = "the picture is empty"; return false; }
    const size_t slash = m_cfg.path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) CreateDirectories(m_cfg.path.substr(0, slash));
    m_impl = std::make_unique<Impl>();
    Impl& im = *m_impl;
    HRESULT hr = S_OK;
    switch (m_cfg.format) {
    case AnimFormat::Gif: {
        if (m_w > 65535 || m_h > 65535) { error = "GIF allows at most 65535 pixels a side"; return false; }
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&im.factory));
        if (FAILED(hr)) { error = "WIC unavailable: " + FormatHr(hr); return false; }
        hr = im.factory->CreateStream(&im.stream);
        if (SUCCEEDED(hr)) hr = im.stream->InitializeFromFilename(m_cfg.path.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) { error = "cannot create the output file: " + FormatHr(hr); return false; }
        hr = im.factory->CreateEncoder(GUID_ContainerFormatGif, nullptr, &im.encoder);
        if (SUCCEEDED(hr)) hr = im.encoder->Initialize(im.stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) { error = "cannot start the GIF encoder: " + FormatHr(hr); return false; }
        ComPtr<IWICMetadataQueryWriter> meta;
        if (SUCCEEDED(im.encoder->GetMetadataQueryWriter(&meta)) && meta) {
            WriteMetaUInt16(meta.Get(), L"/logscrdesc/Width", (UINT16)m_w);
            WriteMetaUInt16(meta.Get(), L"/logscrdesc/Height", (UINT16)m_h);
            if (m_cfg.loopCount != 1) {
                const int loops = std::clamp(m_cfg.loopCount, 0, 65535);
                static const uint8_t kNetscape[11] = { 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0' };
                const uint8_t data[5] = { 3, 1, (uint8_t)(loops & 0xFF), (uint8_t)(loops >> 8), 0 };
                if (FAILED(WriteMetaBytes(meta.Get(), L"/appext/Application", kNetscape, 11)) ||
                    FAILED(WriteMetaBytes(meta.Get(), L"/appext/Data", data, 5)))
                    Log::Warn("Animation output: the loop count could not be written into the GIF");
            }
        }
        break;
    }
    case AnimFormat::Apng: {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&im.factory));
        if (FAILED(hr)) { error = "WIC unavailable: " + FormatHr(hr); return false; }
        im.file = CreateFileW(m_cfg.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (im.file == INVALID_HANDLE_VALUE) { error = "cannot create the output file: " + LastErrorText(); return false; }
        static const uint8_t kSig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
        std::vector<uint8_t> head(kSig, kSig + 8);
        std::vector<uint8_t> ihdr;
        PutBE32(ihdr, m_w); PutBE32(ihdr, m_h);
        ihdr.push_back(8); ihdr.push_back((uint8_t)(m_cfg.keepAlpha ? 6 : 2)); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
        AppendChunk(head, "IHDR", ihdr.data(), ihdr.size());
        im.actlOffset = (LONGLONG)head.size();
        std::vector<uint8_t> actl;
        PutBE32(actl, 0); PutBE32(actl, (uint32_t)std::max(0, m_cfg.loopCount));
        AppendChunk(head, "acTL", actl.data(), actl.size());
        if (!WriteAll(im.file, head.data(), head.size())) { error = "cannot write the output file: " + LastErrorText(); return false; }
        break;
    }
    case AnimFormat::WebP: {
        if (m_w > kWebPMaxSide || m_h > kWebPMaxSide) { error = StrPrintf("WebP allows at most %u pixels a side", kWebPMaxSide); return false; }
        WebPAnimEncoderOptions opt;
        if (!WebPAnimEncoderOptionsInit(&opt) || !WebPConfigInit(&im.config)) { error = "libwebp version mismatch"; return false; }
        opt.anim_params.loop_count = std::max(0, m_cfg.loopCount);
        opt.anim_params.bgcolor = 0;
        opt.minimize_size = 0;
        opt.allow_mixed = 0;
        im.webp = WebPAnimEncoderNew((int)m_w, (int)m_h, &opt);
        if (!im.webp) { error = "cannot start the WebP encoder"; return false; }
        im.config.lossless = m_cfg.lossless ? 1 : 0;
        im.config.quality = (float)std::clamp(m_cfg.quality, 0, 100);
        im.config.method = 4;
        im.config.thread_level = 1;
        im.config.exact = 0;
        if (!WebPValidateConfig(&im.config)) { error = "invalid WebP settings"; return false; }
        break;
    }
    default:
        error = "unknown animation format";
        return false;
    }
    m_opened = true;
    Log::Info("Animation output: %s %ux%u %s%s, %s, loop %s", WideToUtf8(m_cfg.path).c_str(), m_w, m_h, AnimFormatName(m_cfg.format),
              m_cfg.format == AnimFormat::WebP ? (m_cfg.lossless ? " lossless" : StrPrintf(" quality %d", m_cfg.quality).c_str()) : "",
              m_cfg.keepAlpha ? "with transparency" : "opaque", m_cfg.loopCount == 0 ? "forever" : StrPrintf("%d", m_cfg.loopCount).c_str());
    return true;
}

bool AnimWriter::WriteFrame(Frame& f, std::string& error) {
    if (f.w != m_w || f.h != m_h || f.pitch < f.w * 4 || f.rgba.size() < (size_t)f.pitch * f.h) {
        if (++m_sizeMismatch == 1)
            Log::Warn("Animation output: a frame of %ux%u does not fit the %ux%u file; skipped (the output size changed during the run?)", f.w, f.h, m_w, m_h);
        return true;
    }
    Impl& im = *m_impl;
    // Timing: the frame lasts from the end of the previous one to its own end, so rounding never drifts.
    const LONGLONG start = f.pts - m_base;
    const LONGLONG duration = f.duration > 0 ? f.duration : m_cfg.frameTicks;
    const LONGLONG end = std::max(start + duration, m_lastEnd + kMinFrameTicks);
    // Alpha: opaque unless transparency is kept.
    for (UINT j = 0; j < m_h; ++j) {
        uint8_t* row = f.rgba.data() + (size_t)j * f.pitch;
        if (m_cfg.keepAlpha) {
            if (m_cfg.format == AnimFormat::Gif) for (UINT i = 0; i < m_w; ++i) row[4 * i + 3] = (uint8_t)(row[4 * i + 3] < 128 ? 0 : 255);
        } else {
            for (UINT i = 0; i < m_w; ++i) row[4 * i + 3] = 255;
        }
    }
    switch (m_cfg.format) {
    case AnimFormat::Gif: {
        ComPtr<IWICBitmap> bitmap;
        HRESULT hr = im.factory->CreateBitmapFromMemory(m_w, m_h, GUID_WICPixelFormat32bppRGBA, f.pitch, (UINT)((size_t)f.pitch * m_h), f.rgba.data(), &bitmap);
        if (FAILED(hr)) { error = "CreateBitmapFromMemory: " + FormatHr(hr); return false; }
        bool transparent = false;
        if (m_cfg.keepAlpha) for (UINT j = 0; j < m_h && !transparent; ++j) { const uint8_t* row = f.rgba.data() + (size_t)j * f.pitch; for (UINT i = 0; i < m_w; ++i) if (row[4 * i + 3] == 0) { transparent = true; break; } }
        ComPtr<IWICPalette> palette;
        hr = im.factory->CreatePalette(&palette);
        if (SUCCEEDED(hr)) hr = palette->InitializeFromBitmap(bitmap.Get(), 256, transparent ? TRUE : FALSE);
        if (FAILED(hr)) { error = "palette: " + FormatHr(hr); return false; }
        ComPtr<IWICFormatConverter> conv;
        hr = im.factory->CreateFormatConverter(&conv);
        if (SUCCEEDED(hr)) hr = conv->Initialize(bitmap.Get(), GUID_WICPixelFormat8bppIndexed, WICBitmapDitherTypeErrorDiffusion, palette.Get(), 50.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) { error = "colour quantization: " + FormatHr(hr); return false; }
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;
        hr = im.encoder->CreateNewFrame(&frame, &props);
        if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
        if (SUCCEEDED(hr)) hr = frame->SetSize(m_w, m_h);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat8bppIndexed;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(hr)) hr = frame->SetPalette(palette.Get());
        if (FAILED(hr)) { error = "cannot add a GIF frame: " + FormatHr(hr); return false; }
        const LONGLONG endCs = (end + 50000) / 100000;
        const LONGLONG delay = std::clamp<LONGLONG>(endCs - im.gifWrittenCs, 2, 65535);
        im.gifWrittenCs += delay;
        ComPtr<IWICMetadataQueryWriter> meta;
        if (SUCCEEDED(frame->GetMetadataQueryWriter(&meta)) && meta) {
            WriteMetaUInt16(meta.Get(), L"/grctlext/Delay", (UINT16)delay);
            WriteMetaUInt8(meta.Get(), L"/grctlext/Disposal", (UINT8)(transparent ? 2 : 1));
            if (transparent) {
                UINT count = 0;
                palette->GetColorCount(&count);
                std::vector<WICColor> colors(count);
                UINT got = 0;
                if (count && SUCCEEDED(palette->GetColors(count, colors.data(), &got))) {
                    for (UINT i = 0; i < got; ++i) {
                        if ((colors[i] >> 24) == 0) {
                            WriteMetaBool(meta.Get(), L"/grctlext/TransparencyFlag", true);
                            WriteMetaUInt8(meta.Get(), L"/grctlext/TransparentColorIndex", (UINT8)i);
                            break;
                        }
                    }
                }
            }
        }
        hr = frame->WriteSource(conv.Get(), nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (FAILED(hr)) { error = "cannot write a GIF frame: " + FormatHr(hr); return false; }
        break;
    }
    case AnimFormat::Apng: {
        // The frame as a PNG in memory (WIC's encoder), of which only the IDAT chunks are used.
        ComPtr<IStream> mem;
        mem.Attach(SHCreateMemStream(nullptr, 0));
        if (!mem) { error = "cannot create the frame buffer"; return false; }
        ComPtr<IWICBitmapEncoder> enc;
        HRESULT hr = im.factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        if (SUCCEEDED(hr)) hr = enc->Initialize(mem.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) { error = "cannot start the PNG encoder: " + FormatHr(hr); return false; }
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> props;
        hr = enc->CreateNewFrame(&frame, &props);
        if (SUCCEEDED(hr) && props) {
            PROPBAG2 opt{};
            opt.dwType = PROPBAG2_TYPE_DATA;
            opt.vt = VT_UI1;
            opt.pstrName = const_cast<LPOLESTR>(L"FilterOption");
            VARIANT v; VariantInit(&v); v.vt = VT_UI1; v.bVal = WICPngFilterAdaptive;
            props->Write(1, &opt, &v);
        }
        if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
        if (SUCCEEDED(hr)) hr = frame->SetSize(m_w, m_h);
        WICPixelFormatGUID fmt = m_cfg.keepAlpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
        if (FAILED(hr)) { error = "cannot add a PNG frame: " + FormatHr(hr); return false; }
        ComPtr<IWICBitmap> bitmap;
        hr = im.factory->CreateBitmapFromMemory(m_w, m_h, GUID_WICPixelFormat32bppRGBA, f.pitch, (UINT)((size_t)f.pitch * m_h), f.rgba.data(), &bitmap);
        if (FAILED(hr)) { error = "CreateBitmapFromMemory: " + FormatHr(hr); return false; }
        ComPtr<IWICBitmapSource> src;
        hr = WICConvertBitmapSource(fmt, bitmap.Get(), &src);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(src.Get(), nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = enc->Commit();
        if (FAILED(hr)) { error = "cannot encode a PNG frame: " + FormatHr(hr); return false; }
        STATSTG st{};
        if (FAILED(mem->Stat(&st, STATFLAG_NONAME))) { error = "frame buffer: Stat failed"; return false; }
        std::vector<uint8_t> png((size_t)st.cbSize.QuadPart);
        LARGE_INTEGER zero{};
        mem->Seek(zero, STREAM_SEEK_SET, nullptr);
        ULONG got = 0;
        if (FAILED(mem->Read(png.data(), (ULONG)png.size(), &got)) || got != png.size()) { error = "frame buffer: Read failed"; return false; }
        EncodedPng parsed;
        if (!ParsePng(png, parsed)) { error = "the PNG encoder produced an unreadable frame"; return false; }
        if (parsed.ihdr[8] != 8 || parsed.ihdr[9] != (m_cfg.keepAlpha ? 6 : 2) || parsed.ihdr[12] != 0) { error = "the PNG encoder produced an unexpected pixel layout"; return false; }
        const LONGLONG endMs = (end + 5000) / 10000;
        const LONGLONG delay = std::clamp<LONGLONG>(endMs - im.apngWrittenMs, 1, 65535);
        im.apngWrittenMs += delay;
        std::vector<uint8_t> out;
        out.reserve(64 + png.size());
        std::vector<uint8_t> fctl;
        PutBE32(fctl, im.sequence++);
        PutBE32(fctl, m_w); PutBE32(fctl, m_h); PutBE32(fctl, 0); PutBE32(fctl, 0);
        PutBE16(fctl, (uint16_t)delay); PutBE16(fctl, 1000);
        fctl.push_back(0); fctl.push_back(0);   // dispose none, blend source
        AppendChunk(out, "fcTL", fctl.data(), fctl.size());
        for (const auto& [offset, len] : parsed.idat) {
            if (im.frames == 0) {
                out.insert(out.end(), png.begin() + offset, png.begin() + offset + len);
            } else {
                std::vector<uint8_t> fdat;
                PutBE32(fdat, im.sequence++);
                fdat.insert(fdat.end(), png.begin() + offset + 8, png.begin() + offset + len - 4);
                AppendChunk(out, "fdAT", fdat.data(), fdat.size());
            }
        }
        if (!WriteAll(im.file, out.data(), out.size())) { error = "cannot write the output file: " + LastErrorText(); return false; }
        ++im.frames;
        break;
    }
    case AnimFormat::WebP: {
        WebPPicture pic;
        if (!WebPPictureInit(&pic)) { error = "libwebp version mismatch"; return false; }
        pic.use_argb = 1;
        pic.width = (int)m_w;
        pic.height = (int)m_h;
        const int ok = m_cfg.keepAlpha ? WebPPictureImportRGBA(&pic, f.rgba.data(), (int)f.pitch) : WebPPictureImportRGBX(&pic, f.rgba.data(), (int)f.pitch);
        if (!ok) { WebPPictureFree(&pic); error = "cannot import a frame into the WebP encoder"; return false; }
        int timestamp = (int)std::clamp<LONGLONG>((start + 5000) / 10000, 0, INT_MAX);
        if (timestamp <= im.lastTimestamp) timestamp = im.lastTimestamp + 1;
        const int added = WebPAnimEncoderAdd(im.webp, &pic, timestamp, &im.config);
        WebPPictureFree(&pic);
        if (!added) { error = std::string("WebP encoder: ") + WebPAnimEncoderGetError(im.webp); return false; }
        im.lastTimestamp = timestamp;
        im.lastDuration = (int)std::clamp<LONGLONG>((end - start + 5000) / 10000, 1, INT_MAX);
        break;
    }
    default:
        error = "unknown animation format";
        return false;
    }
    m_lastEnd = end;
    return true;
}

bool AnimWriter::CloseFile(bool completed, std::string& error) {
    if (!m_opened || !m_impl) return true;
    Impl& im = *m_impl;
    bool ok = true;
    switch (m_cfg.format) {
    case AnimFormat::Gif: {
        const HRESULT hr = im.encoder->Commit();
        if (FAILED(hr)) { error = "cannot finish the GIF: " + FormatHr(hr); ok = false; }
        im.encoder.Reset();
        im.stream.Reset();
        break;
    }
    case AnimFormat::Apng: {
        std::vector<uint8_t> iend;
        AppendChunk(iend, "IEND", nullptr, 0);
        std::vector<uint8_t> actl;
        std::vector<uint8_t> actlData;
        PutBE32(actlData, im.frames); PutBE32(actlData, (uint32_t)std::max(0, m_cfg.loopCount));
        AppendChunk(actl, "acTL", actlData.data(), actlData.size());
        LARGE_INTEGER at; at.QuadPart = im.actlOffset;
        if (!WriteAll(im.file, iend.data(), iend.size()) || !SetFilePointerEx(im.file, at, nullptr, FILE_BEGIN) || !WriteAll(im.file, actl.data(), actl.size())) {
            error = "cannot finish the APNG: " + LastErrorText();
            ok = false;
        }
        CloseHandle(im.file);
        im.file = INVALID_HANDLE_VALUE;
        break;
    }
    case AnimFormat::WebP: {
        if (im.lastTimestamp >= 0) {
            if (!WebPAnimEncoderAdd(im.webp, nullptr, im.lastTimestamp + im.lastDuration, nullptr)) {
                error = std::string("WebP encoder: ") + WebPAnimEncoderGetError(im.webp);
                ok = false;
            }
        }
        WebPData data;
        WebPDataInit(&data);
        if (ok && !WebPAnimEncoderAssemble(im.webp, &data)) { error = std::string("WebP encoder: ") + WebPAnimEncoderGetError(im.webp); ok = false; }
        if (ok) {
            HANDLE h = CreateFileW(m_cfg.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) { error = "cannot create the output file: " + LastErrorText(); ok = false; }
            else {
                if (!WriteAll(h, data.bytes, data.size)) { error = "cannot write the output file: " + LastErrorText(); ok = false; }
                CloseHandle(h);
            }
        }
        WebPDataClear(&data);
        WebPAnimEncoderDelete(im.webp);
        im.webp = nullptr;
        break;
    }
    default: break;
    }
    (void)completed;
    return ok;
}

void AnimWriter::WriterMain() {
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
            it = m_frames.begin();
            Log::Warn("Animation output: frame %llu never arrived; continuing with frame %llu", (unsigned long long)m_nextIndex, (unsigned long long)it->first);
        }
        Frame f = std::move(it->second);
        m_nextIndex = it->first + 1;
        m_frames.erase(it);
        lock.unlock();
        m_space.notify_all();
        if (!m_opened && !OpenFile(f.w, f.h, error)) { Fail(error); break; }
        if (!m_baseSet) { m_base = f.pts; m_baseSet = true; }
        if (!WriteFrame(f, error)) { Fail(error); break; }
        ++m_written;
    }
    if (m_opened) {
        if (!CloseFile(!aborted && !Failed(), error) && !Failed()) Fail(error);
    } else if (!aborted && !Failed()) {
        Fail("no frames reached the encoder");
    }
    m_impl.reset();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frames.clear();
        m_running = false;
    }
    m_space.notify_all();
    if (SUCCEEDED(coHr)) CoUninitialize();
}

} // namespace vdc
