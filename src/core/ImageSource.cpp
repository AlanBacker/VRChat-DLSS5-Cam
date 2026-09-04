#include "core/ImageSource.h"
#include "core/Log.h"
#include <wincodec.h>
#include <propvarutil.h>
#include <algorithm>
#include <cwctype>

namespace vdc {

namespace {

// EXIF orientation (tag 274) -> WIC transform. Rotation is applied before the flip.
WICBitmapTransformOptions TransformForOrientation(UINT o) {
    switch (o) {
        case 2: return WICBitmapTransformFlipHorizontal;
        case 3: return WICBitmapTransformRotate180;
        case 4: return WICBitmapTransformFlipVertical;
        case 5: return (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
        case 6: return WICBitmapTransformRotate90;
        case 7: return (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipVertical);
        case 8: return WICBitmapTransformRotate270;
        default: return WICBitmapTransformRotate0;
    }
}

UINT ReadOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader)) || !reader) return 1;
    static const wchar_t* kQueries[] = {
        L"/app1/ifd/{ushort=274}",   // JPEG
        L"/ifd/{ushort=274}",        // TIFF
        L"/xmp/tiff:Orientation",    // XMP (PNG/WebP)
    };
    for (const wchar_t* q : kQueries) {
        PROPVARIANT v;
        PropVariantInit(&v);
        UINT out = 1;
        if (SUCCEEDED(reader->GetMetadataByName(q, &v))) {
            switch (v.vt) {
                case VT_UI2: out = v.uiVal; break;
                case VT_UI4: out = v.ulVal; break;
                case VT_I4:  out = (UINT)v.lVal; break;
                case VT_UI1: out = v.bVal; break;
                case VT_LPWSTR: out = (UINT)wcstoul(v.pwszVal, nullptr, 10); break;
                default: break;
            }
            PropVariantClear(&v);
            if (out >= 1 && out <= 8) return out;
        } else {
            PropVariantClear(&v);
        }
    }
    return 1;
}

} // namespace

bool ImageSource::IsSupportedExtension(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)std::towlower(c);
    static const wchar_t* kExt[] = { L"png", L"jpg", L"jpeg", L"jpe", L"jfif", L"bmp", L"dib", L"tif", L"tiff", L"gif",
                                     L"webp", L"heic", L"heif", L"avif", L"jxr", L"wdp", L"hdp", L"ico", L"dds" };
    for (const wchar_t* e : kExt) if (ext == e) return true;
    return false;
}

std::wstring ImageSource::Stem() const {
    size_t start = m_path.find_last_of(L"\\/");
    start = (start == std::wstring::npos) ? 0 : start + 1;
    size_t dot = m_path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < start) dot = m_path.size();
    return m_path.substr(start, dot - start);
}

SourceFrame ImageSource::Frame() const {
    SourceFrame f;
    if (!m_tex) return f;
    f.texture = m_tex.Get();
    f.srv = m_srv;
    f.width = m_width;
    f.height = m_height;
    f.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    f.viewFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    f.linear = false;
    f.stillImage = true;
    f.hasFrame = m_uploaded;
    return f;
}

void ImageSource::Release(GpuContext& gpu) {
    if (m_tex) gpu.DeferRelease(m_tex);
    if (m_upload) gpu.DeferRelease(m_upload);
    m_tex.Reset();
    m_upload.Reset();
    if (m_srv.ptr) { gpu.Dev().FreeStaging(m_srv); m_srv = {}; }
    m_path.clear();
    m_width = m_height = m_origWidth = m_origHeight = 0;
    m_uploadPending = m_uploaded = false;
}

bool ImageSource::Load(GpuContext& gpu, const std::wstring& path, std::string& error) {
    error.clear();
    const double t0 = NowSeconds();
    Device& device = gpu.Dev();

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = "WIC unavailable: " + FormatHr(hr); return false; }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        if (hr == WINCODEC_ERR_COMPONENTNOTFOUND) error = "no decoder installed for this image format";
        else error = "cannot decode the file: " + FormatHr(hr);
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { error = "GetFrame failed: " + FormatHr(hr); return false; }

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (w == 0 || h == 0) { error = "the image is empty"; return false; }
    m_origWidth = w;
    m_origHeight = h;

    ComPtr<IWICBitmapSource> source = frame;
    const UINT orientation = ReadOrientation(frame.Get());
    if (orientation != 1) {
        ComPtr<IWICBitmapFlipRotator> rot;
        if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rot)) &&
            SUCCEEDED(rot->Initialize(source.Get(), TransformForOrientation(orientation)))) {
            source = rot;
            source->GetSize(&w, &h);
        }
    }

    // Keep very large pictures within what the pipeline (and the neural runtime) can take.
    const UINT longSide = std::max(w, h);
    if (longSide > kMaxLongSide) {
        const double scale = (double)kMaxLongSide / (double)longSide;
        const UINT nw = std::max<UINT>(1, (UINT)(w * scale + 0.5));
        const UINT nh = std::max<UINT>(1, (UINT)(h * scale + 0.5));
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(source.Get(), nw, nh, WICBitmapInterpolationModeFant))) {
            error = "cannot downscale the image";
            return false;
        }
        source = scaler;
        w = nw; h = nh;
    }
    // Even dimensions keep the block-based passes simple.
    const UINT texW = w & ~1u ? (w & ~1u) : 2u;
    const UINT texH = h & ~1u ? (h & ~1u) : 2u;

    ComPtr<IWICBitmapSource> bgra;
    hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, source.Get(), &bgra);
    if (FAILED(hr)) { error = "pixel format conversion failed: " + FormatHr(hr); return false; }

    // GPU resources: the texture and an upload buffer laid out with the D3D12 row pitch.
    ComPtr<ID3D12Resource> tex, upload;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = texW;
    rd.Height = texH;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    hr = device.D3D12()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                 IID_PPV_ARGS(&tex));
    if (FAILED(hr)) { error = "cannot create the image texture: " + FormatHr(hr); return false; }
    tex->SetName(L"Still image");

    const UINT pitch = (texW * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    D3D12_HEAP_PROPERTIES up{};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (UINT64)pitch * texH;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device.D3D12()->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&upload));
    if (FAILED(hr)) { error = "cannot create the upload buffer: " + FormatHr(hr); return false; }

    void* mapped = nullptr;
    D3D12_RANGE none{ 0, 0 };
    hr = upload->Map(0, &none, &mapped);
    if (FAILED(hr) || !mapped) { error = "cannot map the upload buffer: " + FormatHr(hr); return false; }
    WICRect rect{ 0, 0, (INT)texW, (INT)texH };
    hr = bgra->CopyPixels(&rect, pitch, pitch * texH, static_cast<BYTE*>(mapped));
    upload->Unmap(0, nullptr);
    if (FAILED(hr)) { error = "decoding the pixels failed: " + FormatHr(hr); return false; }

    // Swap in the new picture.
    Release(gpu);
    m_tex = tex;
    m_upload = upload;
    m_uploadPitch = pitch;
    m_srv = device.AllocStaging();
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device.D3D12()->CreateShaderResourceView(m_tex.Get(), &sd, m_srv);
    m_path = path;
    m_width = texW;
    m_height = texH;
    m_uploadPending = true;
    m_uploaded = false;
    Log::Info("Image: %s %ux%u (decoded %ux%u, orientation %u) in %.0f ms", WideToUtf8(path).c_str(), texW, texH,
              m_origWidth, m_origHeight, orientation, (NowSeconds() - t0) * 1000.0);
    return true;
}

void ImageSource::Upload(ID3D12GraphicsCommandList* cmd, GpuContext& gpu) {
    if (!m_uploadPending || !m_tex || !m_upload) return;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = m_tex.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = m_upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = m_width;
    src.PlacedFootprint.Footprint.Height = m_height;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = m_uploadPitch;
    // COMMON promotes to COPY_DEST implicitly; the copy state does not decay, so return to COMMON explicitly.
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Device::Barrier(cmd, m_tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    gpu.DeferRelease(m_upload);
    m_upload.Reset();
    m_uploadPending = false;
    m_uploaded = true;
}

} // namespace vdc
