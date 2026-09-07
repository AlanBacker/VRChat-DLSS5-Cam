#include "core/VideoSource.h"
#include "core/Log.h"
#include <d3d10.h>
#include <propidl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>

namespace vdc {

namespace {

constexpr size_t kQueueFrames = 3;     // decoded frames waiting for the pipeline
constexpr size_t kQueueAudio = 512;    // PCM samples waiting for the output file

// ID3D10Multithread (also implemented by D3D11 contexts): the decoder and this app share the device.
const GUID kIidD3D10Multithread = { 0x9b7e4e00, 0x342c, 0x4106, { 0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0 } };

// YCbCr -> RGB in 16.16 fixed point: R = (Y' * yMul + rv * V) >> 16 with Y' = Y - yOff, and so on.
struct YuvCoef { int yOff, yMul, rv, gu, gv, bu; };

YuvCoef Coef(bool bt709, bool fullRange) {
    if (fullRange) return bt709 ? YuvCoef{ 0, 65536, 103206, -12276, -30679, 121608 } : YuvCoef{ 0, 65536, 91881, -22553, -46802, 116130 };
    return bt709 ? YuvCoef{ 16, 76309, 117489, -13975, -34925, 138438 } : YuvCoef{ 16, 76309, 104597, -25675, -53279, 132201 };
}

inline uint8_t Clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

void ConvertNv12(const uint8_t* yPlane, LONG yPitch, const uint8_t* uvPlane, LONG uvPitch, UINT cropX, UINT cropY, UINT w, UINT h,
                 const YuvCoef& c, uint8_t* out) {
    for (UINT j = 0; j < h; ++j) {
        const uint8_t* yr = yPlane + (ptrdiff_t)(cropY + j) * yPitch + cropX;
        const uint8_t* uvr = uvPlane + (ptrdiff_t)((cropY + j) >> 1) * uvPitch + (cropX & ~1u);
        uint8_t* o = out + (size_t)j * w * 4;
        for (UINT i = 0; i < w; ++i, o += 4) {
            const int yy = (yr[i] - c.yOff) * c.yMul;
            const int u = uvr[i & ~1u] - 128;
            const int v = uvr[(i & ~1u) + 1] - 128;
            o[0] = Clamp8((yy + c.bu * u + 32768) >> 16);
            o[1] = Clamp8((yy + c.gu * u + c.gv * v + 32768) >> 16);
            o[2] = Clamp8((yy + c.rv * v + 32768) >> 16);
            o[3] = 255;
        }
    }
}

// Turns a BGRA picture by 90, 180 or 270 degrees clockwise.
void Rotate(const std::vector<uint8_t>& in, UINT w, UINT h, UINT degrees, std::vector<uint8_t>& out) {
    const uint32_t* s = reinterpret_cast<const uint32_t*>(in.data());
    out.resize(in.size());
    uint32_t* d = reinterpret_cast<uint32_t*>(out.data());
    if (degrees == 180) {
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) d[(size_t)y * w + x] = s[(size_t)(h - 1 - y) * w + (w - 1 - x)];
        return;
    }
    const UINT ow = h, oh = w;
    for (UINT y = 0; y < oh; ++y) {
        for (UINT x = 0; x < ow; ++x) {
            UINT sx, sy;
            if (degrees == 90) { sx = y; sy = h - 1 - x; }
            else               { sx = w - 1 - y; sy = x; }
            d[(size_t)y * ow + x] = s[(size_t)sy * w + sx];
        }
    }
}

} // namespace

bool VideoSource::IsSupportedExtension(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)std::towlower(c);
    static const wchar_t* kExt[] = { L"mp4", L"m4v", L"mov", L"mkv", L"webm", L"avi", L"wmv", L"mpg", L"mpeg", L"ts",
                                     L"m2ts", L"mts", L"3gp", L"3g2", L"flv", L"asf" };
    for (const wchar_t* e : kExt) if (ext == e) return true;
    return false;
}

std::wstring VideoSource::Stem() const {
    size_t start = m_path.find_last_of(L"\\/");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    size_t dot = m_path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < start) dot = m_path.size();
    return m_path.substr(start, dot - start);
}

SourceFrame VideoSource::Frame(bool still) const {
    SourceFrame f;
    if (!m_tex) return f;
    f.texture = m_tex.Get();
    f.srv = m_srv;
    f.width = m_info.width;
    f.height = m_info.height;
    f.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    f.viewFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    f.linear = false;
    f.stillImage = still;
    f.hasFrame = m_uploaded;
    return f;
}

// --- decoder device -------------------------------------------------------------------------

bool VideoSource::CreateDecoderDevice(GpuContext& gpu, std::string& error) {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(gpu.Dev().Adapter(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                   D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                                   D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (FAILED(hr)) { error = "D3D11CreateDevice (video decoder device): " + FormatHr(hr); return false; }
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(ctx->QueryInterface(kIidD3D10Multithread, reinterpret_cast<void**>(mt.GetAddressOf()))) && mt)
        mt->SetMultithreadProtected(TRUE);
    ComPtr<IMFDXGIDeviceManager> manager;
    UINT token = 0;
    hr = mf::CreateDXGIDeviceManager(&token, &manager);
    if (FAILED(hr)) { error = "MFCreateDXGIDeviceManager: " + FormatHr(hr); return false; }
    hr = manager->ResetDevice(dev.Get(), token);
    if (FAILED(hr)) { error = "IMFDXGIDeviceManager::ResetDevice: " + FormatHr(hr); return false; }
    m_dev11 = dev;
    m_ctx11 = ctx;
    m_dxgiManager = manager;
    m_dxgiToken = token;
    return true;
}

// --- reader -----------------------------------------------------------------------------------

bool VideoSource::ParseVideoType(IMFMediaType* type, Reader& r, std::string& error) {
    GUID sub{};
    type->GetGUID(MF_MT_SUBTYPE, &sub);
    r.subtype = sub;
    UINT32 w = 0, h = 0;
    if (FAILED(mf::GetSize(type, MF_MT_FRAME_SIZE, w, h)) || w == 0 || h == 0) { error = "the decoder reports no frame size"; return false; }
    r.frameW = w; r.frameH = h;
    UINT32 num = 0, den = 0;
    if (SUCCEEDED(mf::GetRatio(type, MF_MT_FRAME_RATE, num, den)) && num > 0 && den > 0) { r.fpsNum = num; r.fpsDen = den; }
    else { r.fpsNum = 30; r.fpsDen = 1; }
    UINT32 v = 0;
    r.stride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &v)) ? (LONG)(INT32)v : 0;
    v = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_YUV_MATRIX, &v)) && v != MFVideoTransferMatrix_Unknown) r.bt709 = (v != MFVideoTransferMatrix_BT601);
    else r.bt709 = h > 576;
    v = 0;
    r.fullRange = SUCCEEDED(type->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &v)) && v == MFNominalRange_0_255;
    // Display aperture: the coded picture may carry padding rows (1088 for 1080p).
    r.cropX = r.cropY = 0; r.cropW = w; r.cropH = h;
    MFVideoArea area{};
    UINT32 got = 0;
    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &got)) && got == sizeof(area)) {
        const int ax = area.OffsetX.value, ay = area.OffsetY.value;
        const long aw = area.Area.cx, ah = area.Area.cy;
        if (ax >= 0 && ay >= 0 && aw > 0 && ah > 0 && (UINT)ax + (UINT)aw <= w && (UINT)ay + (UINT)ah <= h) {
            r.cropX = (UINT)ax; r.cropY = (UINT)ay; r.cropW = (UINT)aw; r.cropH = (UINT)ah;
        }
    }
    r.cropW &= ~1u; r.cropH &= ~1u;
    if (r.cropW < 2 || r.cropH < 2) { error = "the video picture is too small"; return false; }
    if (r.rotation != 90 && r.rotation != 180 && r.rotation != 270) r.rotation = 0;
    const bool turned = r.rotation == 90 || r.rotation == 270;
    r.outW = turned ? r.cropH : r.cropW;
    r.outH = turned ? r.cropW : r.cropH;
    return true;
}

bool VideoSource::CreateReader(const std::wstring& path, bool withAudio, bool hardware, Reader& r, std::string& error) {
    ComPtr<IMFAttributes> attrs;
    HRESULT hr = mf::CreateAttributes(&attrs, 4);
    if (FAILED(hr)) { error = "MFCreateAttributes: " + FormatHr(hr); return false; }
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    const bool useHardware = hardware && m_dxgiManager;
    if (useHardware) {
        attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_dxgiManager.Get());
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    }
    hr = mf::CreateSourceReaderFromURL(path.c_str(), attrs.Get(), &r.reader);
    if (FAILED(hr) || !r.reader) {
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) error = "file not found";
        else if (hr == MF_E_UNSUPPORTED_BYTESTREAM_TYPE) error = "unsupported container format";
        else if (hr == MF_E_INVALIDMEDIATYPE || hr == MF_E_TOPO_CODEC_NOT_FOUND) error = "no decoder installed for this file: " + FormatHr(hr);
        else error = "cannot open the file: " + FormatHr(hr);
        return false;
    }
    r.hardware = useHardware;

    // Streams: the first video stream, the first audio stream.
    bool haveVideo = false;
    UINT32 nativeW = 0, nativeH = 0;
    for (DWORD i = 0; i < 64; ++i) {
        ComPtr<IMFMediaType> native;
        hr = r.reader->GetNativeMediaType(i, 0, &native);
        if (hr == MF_E_INVALIDSTREAMNUMBER) break;
        if (FAILED(hr) || !native) continue;
        GUID major{};
        native->GetGUID(MF_MT_MAJOR_TYPE, &major);
        if (major == MFMediaType_Video && !haveVideo) {
            haveVideo = true;
            r.videoStream = i;
            GUID sub{};
            native->GetGUID(MF_MT_SUBTYPE, &sub);
            r.codec = mf::SubtypeName(sub);
            UINT32 rot = 0;
            if (SUCCEEDED(native->GetUINT32(MF_MT_VIDEO_ROTATION, &rot))) r.rotation = rot % 360;
            mf::GetSize(native.Get(), MF_MT_FRAME_SIZE, nativeW, nativeH);
        } else if (major == MFMediaType_Audio && !r.hasAudioStream) {
            r.hasAudioStream = true;
            r.audioStream = i;
        }
    }
    if (!haveVideo) { error = "the file has no video stream"; return false; }
    r.reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = r.reader->SetStreamSelection(r.videoStream, TRUE);
    if (FAILED(hr)) { error = "cannot select the video stream: " + FormatHr(hr); return false; }

    // Decoder output: NV12 straight from the decoder, RGB32 through the reader's converter as the fallback.
    const GUID candidates[] = { MFVideoFormat_NV12, MFVideoFormat_RGB32, MFVideoFormat_ARGB32 };
    HRESULT last = E_FAIL;
    bool set = false;
    for (const GUID& sub : candidates) {
        ComPtr<IMFMediaType> t;
        if (FAILED(mf::CreateMediaType(&t))) break;
        t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        t->SetGUID(MF_MT_SUBTYPE, sub);
        last = r.reader->SetCurrentMediaType(r.videoStream, nullptr, t.Get());
        if (SUCCEEDED(last)) { set = true; break; }
    }
    if (!set) {
        error = StrPrintf("no decoder for %s video (%s)", r.codec.c_str(), FormatHr(last).c_str());
        return false;
    }
    ComPtr<IMFMediaType> current;
    hr = r.reader->GetCurrentMediaType(r.videoStream, &current);
    if (FAILED(hr) || !current) { error = "GetCurrentMediaType: " + FormatHr(hr); return false; }
    if (!ParseVideoType(current.Get(), r, error)) return false;
    // A reader that already turned the picture reports the swapped size: then there is nothing left to rotate here.
    if ((r.rotation == 90 || r.rotation == 270) && nativeW != nativeH && nativeW && nativeH) {
        auto within = [](UINT a, UINT b) { return a <= b + 16 && b <= a + 16; };
        if (within(r.frameW, nativeH) && within(r.frameH, nativeW)) {
            r.rotation = 0;
            r.outW = r.cropW; r.outH = r.cropH;
        }
    }

    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(r.reader->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var))) {
        if (var.vt == VT_UI8) r.durationSeconds = (double)var.uhVal.QuadPart / 1e7;
        PropVariantClear(&var);
    }

    if (withAudio && r.hasAudioStream) {
        hr = r.reader->SetStreamSelection(r.audioStream, TRUE);
        ComPtr<IMFMediaType> pcm;
        if (SUCCEEDED(hr)) hr = mf::CreateMediaType(&pcm);
        if (SUCCEEDED(hr)) {
            pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            hr = r.reader->SetCurrentMediaType(r.audioStream, nullptr, pcm.Get());
        }
        if (SUCCEEDED(hr)) hr = r.reader->GetCurrentMediaType(r.audioStream, &r.audioType);
        if (SUCCEEDED(hr) && r.audioType) {
            r.audioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r.audioRate);
            r.audioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &r.audioChannels);
            r.audio = true;
        } else {
            Log::Warn("Video: the audio stream cannot be decoded (%s); the output gets no audio", FormatHr(hr).c_str());
            r.reader->SetStreamSelection(r.audioStream, FALSE);
            r.audioType.Reset();
            r.audio = false;
        }
    }
    return true;
}

bool VideoSource::ConvertSample(const Reader& r, IMFSample* sample, std::vector<uint8_t>& bgra, std::string& error) {
    ComPtr<IMFMediaBuffer> buf;
    HRESULT hr = sample->ConvertToContiguousBuffer(&buf);
    if (FAILED(hr) || !buf) { error = "ConvertToContiguousBuffer: " + FormatHr(hr); return false; }
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer2> b2;
    ComPtr<IMF2DBuffer> b1;
    bool locked2d = false, locked = false;
    if (SUCCEEDED(buf.As(&b2)) && b2) {
        BYTE* start = nullptr;
        DWORD length = 0;
        if (SUCCEEDED(b2->Lock2DSize(MF2DBuffer_LockFlags_Read, &scan0, &pitch, &start, &length))) locked2d = true;
        else b2.Reset();
    }
    if (!locked2d && SUCCEEDED(buf.As(&b1)) && b1) {
        if (SUCCEEDED(b1->Lock2D(&scan0, &pitch))) locked2d = true;
        else b1.Reset();
    }
    if (!locked2d) {
        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        hr = buf->Lock(&data, &maxLen, &curLen);
        if (FAILED(hr) || !data) { error = "IMFMediaBuffer::Lock: " + FormatHr(hr); return false; }
        locked = true;
        const LONG stride = r.stride ? r.stride : (LONG)(r.subtype == MFVideoFormat_NV12 ? r.frameW : r.frameW * 4);
        if (stride < 0) { scan0 = data + (size_t)(r.frameH - 1) * (size_t)(-stride); pitch = stride; }
        else { scan0 = data; pitch = stride; }
    }

    const UINT w = r.cropW, h = r.cropH;
    std::vector<uint8_t> flat((size_t)w * h * 4);
    if (r.subtype == MFVideoFormat_NV12) {
        const uint8_t* yPlane = scan0;
        const uint8_t* uvPlane = scan0 + (ptrdiff_t)pitch * r.frameH;
        ConvertNv12(yPlane, pitch, uvPlane, pitch, r.cropX, r.cropY, w, h, Coef(r.bt709, r.fullRange), flat.data());
    } else {
        for (UINT j = 0; j < h; ++j) {
            const uint8_t* srow = scan0 + (ptrdiff_t)(r.cropY + j) * pitch + (size_t)r.cropX * 4;
            uint8_t* o = flat.data() + (size_t)j * w * 4;
            std::memcpy(o, srow, (size_t)w * 4);
            for (UINT i = 0; i < w; ++i) o[4 * i + 3] = 255;
        }
    }
    if (b2) b2->Unlock2D();
    else if (b1) b1->Unlock2D();
    else if (locked) buf->Unlock();

    if (r.rotation == 0) bgra = std::move(flat);
    else Rotate(flat, w, h, r.rotation, bgra);
    return true;
}

bool VideoSource::ReadFirstFrame(Reader& r, std::vector<uint8_t>& bgra, std::string& error) {
    for (int attempt = 0; attempt < 512; ++attempt) {
        DWORD stream = 0, flags = 0;
        LONGLONG pts = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = r.reader->ReadSample(r.videoStream, 0, &stream, &flags, &pts, &sample);
        if (FAILED(hr)) { error = "ReadSample: " + FormatHr(hr); return false; }
        if (flags & MF_SOURCE_READERF_ERROR) { error = "the decoder reported an error on the first frame"; return false; }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            ComPtr<IMFMediaType> current;
            if (SUCCEEDED(r.reader->GetCurrentMediaType(r.videoStream, &current)) && current) {
                const UINT rot = r.rotation;
                if (!ParseVideoType(current.Get(), r, error)) return false;
                r.rotation = rot;
            }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { error = "the file contains no video frames"; return false; }
        if (sample) return ConvertSample(r, sample.Get(), bgra, error);
    }
    error = "no video frame within the first samples";
    return false;
}

// --- picture --------------------------------------------------------------------------------

void VideoSource::ReleaseTexture(GpuContext& gpu) {
    for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i) {
        if (m_upload[i]) {
            if (m_uploadPtr[i]) m_upload[i]->Unmap(0, nullptr);
            gpu.DeferRelease(m_upload[i]);
            m_upload[i].Reset();
        }
        m_uploadPtr[i] = nullptr;
    }
    if (m_tex) { gpu.DeferRelease(m_tex); m_tex.Reset(); }
    if (m_srv.ptr) { gpu.Dev().FreeStaging(m_srv); m_srv = {}; }
    m_uploadIndex = 0;
    m_uploadPending = false;
    m_uploaded = false;
}

bool VideoSource::CreateTexture(GpuContext& gpu, UINT w, UINT h, std::string& error) {
    ReleaseTexture(gpu);
    Device& device = gpu.Dev();
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    HRESULT hr = device.D3D12()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                         IID_PPV_ARGS(&m_tex));
    if (FAILED(hr)) { error = "cannot create the video texture: " + FormatHr(hr); m_tex.Reset(); return false; }
    m_tex->SetName(L"Video frame");

    m_uploadPitch = (w * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    D3D12_HEAP_PROPERTIES up{};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (UINT64)m_uploadPitch * h;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT i = 0; i < GpuContext::kFramesInFlight; ++i) {
        hr = device.D3D12()->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&m_upload[i]));
        if (FAILED(hr)) { error = "cannot create the video upload buffer: " + FormatHr(hr); ReleaseTexture(gpu); return false; }
        void* mapped = nullptr;
        D3D12_RANGE none{ 0, 0 };
        hr = m_upload[i]->Map(0, &none, &mapped);
        if (FAILED(hr) || !mapped) { error = "cannot map the video upload buffer: " + FormatHr(hr); ReleaseTexture(gpu); return false; }
        m_uploadPtr[i] = static_cast<uint8_t*>(mapped);
    }
    m_srv = device.AllocStaging();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device.D3D12()->CreateShaderResourceView(m_tex.Get(), &sd, m_srv);
    return true;
}

void VideoSource::SetPending(std::vector<uint8_t>&& bgra) {
    if (bgra.size() != (size_t)m_info.width * m_info.height * 4) {
        Log::Warn("Video: a decoded frame does not match the picture size; dropped");
        return;
    }
    m_pendingBgra = std::move(bgra);
    m_uploadPending = true;
}

void VideoSource::Upload(ID3D12GraphicsCommandList* cmd, GpuContext& /*gpu*/) {
    if (!m_uploadPending || !m_tex) return;
    ID3D12Resource* up = m_upload[m_uploadIndex].Get();
    uint8_t* dst = m_uploadPtr[m_uploadIndex];
    if (!up || !dst) { m_uploadPending = false; return; }
    const size_t rowBytes = (size_t)m_info.width * 4;
    for (UINT j = 0; j < m_info.height; ++j)
        std::memcpy(dst + (size_t)j * m_uploadPitch, m_pendingBgra.data() + (size_t)j * rowBytes, rowBytes);
    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = m_tex.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = up;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint.Offset = 0;
    srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srcLoc.PlacedFootprint.Footprint.Width = m_info.width;
    srcLoc.PlacedFootprint.Footprint.Height = m_info.height;
    srcLoc.PlacedFootprint.Footprint.Depth = 1;
    srcLoc.PlacedFootprint.Footprint.RowPitch = m_uploadPitch;
    // COMMON promotes to COPY_DEST implicitly; the copy state does not decay, so return to COMMON explicitly.
    cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    Device::Barrier(cmd, m_tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    // One upload per recorded frame: the ring slot comes round again only after that frame's fence has passed.
    m_uploadIndex = (m_uploadIndex + 1) % GpuContext::kFramesInFlight;
    m_uploadPending = false;
    m_uploaded = true;
}

// --- open / close -------------------------------------------------------------------------------

bool VideoSource::Open(GpuContext& gpu, const std::wstring& path, bool hardwareDecode, std::string& error) {
    error.clear();
    const double t0 = NowSeconds();
    if (!mf::Available(error)) return false;
    StopSequence();
    if (hardwareDecode && !m_dxgiManager) {
        std::string e;
        if (!CreateDecoderDevice(gpu, e)) Log::Warn("Video: hardware decoding unavailable (%s)", e.c_str());
    }
    Reader r;
    std::vector<uint8_t> bgra;
    bool hw = hardwareDecode && m_dxgiManager;
    bool ok = CreateReader(path, false, hw, r, error) && ReadFirstFrame(r, bgra, error);
    if (!ok && hw) {
        Log::Warn("Video: the hardware decoder failed for %s (%s); using the software decoder", WideToUtf8(path).c_str(), error.c_str());
        r = Reader();
        error.clear();
        hw = false;
        ok = CreateReader(path, false, false, r, error) && ReadFirstFrame(r, bgra, error);
    }
    if (!ok) return false;
    if (std::max(r.outW, r.outH) > kMaxLongSide) {
        error = StrPrintf("the video is too large (%ux%u; at most %u pixels on the long side)", r.outW, r.outH, kMaxLongSide);
        return false;
    }
    if (!CreateTexture(gpu, r.outW, r.outH, error)) return false;

    m_path = path;
    m_info = VideoInfo{};
    m_info.width = r.outW; m_info.height = r.outH;
    m_info.fileWidth = r.frameW; m_info.fileHeight = r.frameH;
    m_info.fpsNum = r.fpsNum; m_info.fpsDen = r.fpsDen;
    m_info.durationSeconds = r.durationSeconds;
    m_info.frameEstimate = (UINT64)std::llround(r.durationSeconds * (double)r.fpsNum / (double)r.fpsDen);
    m_info.hasAudio = r.hasAudioStream;
    m_info.hardwareDecode = r.hardware;
    m_info.codec = r.codec;
    m_info.decoderOutput = mf::SubtypeName(r.subtype);
    SetPending(std::move(bgra));
    Log::Info("Video: %s %ux%u (coded %ux%u, %s -> %s, %.3f fps, %.1f s, ~%llu frames, rotation %u, %s decoder, audio stream %s) in %.0f ms",
              WideToUtf8(path).c_str(), m_info.width, m_info.height, m_info.fileWidth, m_info.fileHeight, m_info.codec.c_str(),
              m_info.decoderOutput.c_str(), (double)r.fpsNum / (double)r.fpsDen, r.durationSeconds,
              (unsigned long long)m_info.frameEstimate, r.rotation, r.hardware ? "hardware" : "software",
              r.hasAudioStream ? "yes" : "no", (NowSeconds() - t0) * 1000.0);
    return true;
}

bool VideoSource::ReloadPreview(std::string& error) {
    error.clear();
    if (!Loaded()) { error = "no video is open"; return false; }
    Reader r;
    std::vector<uint8_t> bgra;
    if (!CreateReader(m_path, false, m_info.hardwareDecode, r, error) || !ReadFirstFrame(r, bgra, error)) return false;
    if (r.outW != m_info.width || r.outH != m_info.height) { error = "the picture size changed"; return false; }
    SetPending(std::move(bgra));
    return true;
}

void VideoSource::Close(GpuContext& gpu) {
    StopSequence();
    ReleaseTexture(gpu);
    m_path.clear();
    m_info = VideoInfo{};
    m_pendingBgra.clear();
    m_audioType.Reset();
    m_dxgiManager.Reset();
    m_ctx11.Reset();
    m_dev11.Reset();
}

// --- sequence -----------------------------------------------------------------------------------

bool VideoSource::StartSequence(bool withAudio, std::string& error) {
    error.clear();
    StopSequence();
    if (!Loaded()) { error = "no video is open"; return false; }
    auto r = std::make_unique<Reader>();
    bool hw = m_info.hardwareDecode;
    if (!CreateReader(m_path, withAudio, hw, *r, error)) {
        if (!hw) return false;
        Log::Warn("Video: the hardware decoder failed to restart (%s); using the software decoder", error.c_str());
        *r = Reader();
        error.clear();
        if (!CreateReader(m_path, withAudio, false, *r, error)) return false;
        m_info.hardwareDecode = false;
    }
    if (r->outW != m_info.width || r->outH != m_info.height) { error = "the picture size changed between openings"; return false; }
    {
        std::lock_guard<std::mutex> lock(m_qm);
        m_queue.clear();
        m_audioQueue.clear();
        m_seqDone = false;
        m_seqError.clear();
    }
    m_audioType = r->audio ? r->audioType : nullptr;
    m_info.audioRate = r->audioRate;
    m_info.audioChannels = r->audioChannels;
    m_seq = std::move(r);
    m_seqStop = false;
    m_seqRunning = true;
    m_seqThread = std::thread([this] { DecodeMain(); });
    return true;
}

void VideoSource::DecodeMain() {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Reader& r = *m_seq;
    bool videoEnded = false, audioEnded = !r.audio;
    UINT64 index = 0;
    std::string err;
    auto fail = [&](const std::string& e) {
        std::lock_guard<std::mutex> lock(m_qm);
        if (m_seqError.empty()) m_seqError = e;
    };
    while (!m_seqStop.load(std::memory_order_acquire)) {
        DWORD stream = 0, flags = 0;
        LONGLONG pts = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = r.reader->ReadSample((DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &stream, &flags, &pts, &sample);
        if (FAILED(hr)) { fail("ReadSample: " + FormatHr(hr)); break; }
        if (flags & MF_SOURCE_READERF_ERROR) { fail("the decoder reported an error"); break; }
        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && stream == r.videoStream) {
            ComPtr<IMFMediaType> current;
            if (SUCCEEDED(r.reader->GetCurrentMediaType(r.videoStream, &current)) && current) {
                const UINT rot = r.rotation;
                if (!ParseVideoType(current.Get(), r, err)) { fail(err); break; }
                r.rotation = rot;
                const bool turned = rot == 90 || rot == 270;
                r.outW = turned ? r.cropH : r.cropW;
                r.outH = turned ? r.cropW : r.cropH;
                if (r.outW != m_info.width || r.outH != m_info.height) { fail("the picture size changes inside the file, which is not supported"); break; }
            }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (stream == r.videoStream) videoEnded = true;
            else if (r.audio && stream == r.audioStream) audioEnded = true;
        }
        if (sample) {
            if (stream == r.videoStream) {
                VideoFrameData f;
                if (!ConvertSample(r, sample.Get(), f.bgra, err)) { fail(err); break; }
                f.width = r.outW; f.height = r.outH;
                f.index = index++;
                f.pts = pts;
                LONGLONG d = 0;
                if (SUCCEEDED(sample->GetSampleDuration(&d)) && d > 0) f.duration = d;
                else f.duration = (LONGLONG)(10000000.0 * (double)r.fpsDen / (double)r.fpsNum);
                std::unique_lock<std::mutex> lock(m_qm);
                m_spaceCv.wait(lock, [&] { return m_queue.size() < kQueueFrames || m_seqStop.load(); });
                if (m_seqStop.load()) break;
                m_queue.push_back(std::move(f));
                lock.unlock();
                m_qcv.notify_all();
            } else if (r.audio && stream == r.audioStream) {
                std::unique_lock<std::mutex> lock(m_qm);
                m_spaceCv.wait(lock, [&] { return m_audioQueue.size() < kQueueAudio || m_seqStop.load(); });
                if (m_seqStop.load()) break;
                m_audioQueue.push_back(sample);
            }
        }
        if (videoEnded && audioEnded) break;
    }
    {
        std::lock_guard<std::mutex> lock(m_qm);
        m_seqDone = true;
    }
    m_qcv.notify_all();
    if (SUCCEEDED(coHr)) CoUninitialize();
}

VideoSource::Next VideoSource::NextFrame(double timeoutSeconds, VideoFrameData& timing) {
    std::unique_lock<std::mutex> lock(m_qm);
    if (!m_seqRunning) return Next::End;
    const auto wait = std::chrono::microseconds((long long)(std::max(0.0, timeoutSeconds) * 1e6));
    if (!m_qcv.wait_for(lock, wait, [&] { return !m_queue.empty() || m_seqDone || !m_seqError.empty(); })) return Next::Wait;
    if (!m_queue.empty()) {
        VideoFrameData f = std::move(m_queue.front());
        m_queue.pop_front();
        lock.unlock();
        m_spaceCv.notify_all();
        timing.width = f.width; timing.height = f.height;
        timing.index = f.index; timing.pts = f.pts; timing.duration = f.duration;
        timing.bgra.clear();
        SetPending(std::move(f.bgra));
        return Next::Frame;
    }
    if (!m_seqError.empty()) return Next::Error;
    return Next::End;
}

bool VideoSource::PopAudio(ComPtr<IMFSample>& sample) {
    std::lock_guard<std::mutex> lock(m_qm);
    if (m_audioQueue.empty()) return false;
    sample = std::move(m_audioQueue.front());
    m_audioQueue.pop_front();
    m_spaceCv.notify_all();
    return true;
}

std::string VideoSource::SequenceError() {
    std::lock_guard<std::mutex> lock(m_qm);
    return m_seqError;
}

void VideoSource::StopSequence() {
    {
        std::lock_guard<std::mutex> lock(m_qm);   // the flag changes under the lock so that no waiter misses it
        m_seqStop = true;
    }
    m_qcv.notify_all();
    m_spaceCv.notify_all();
    if (m_seqThread.joinable()) m_seqThread.join();
    m_seq.reset();
    {
        std::lock_guard<std::mutex> lock(m_qm);
        m_queue.clear();
        m_audioQueue.clear();
        m_seqDone = false;
    }
    m_seqRunning = false;
    m_seqStop = false;
}

} // namespace vdc
