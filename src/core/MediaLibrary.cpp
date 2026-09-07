#include "core/MediaLibrary.h"
#include "core/ImageSource.h"
#include "core/Log.h"
#include <wincodec.h>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>

namespace vdc {

bool IsLibraryFile(const std::wstring& path, bool& isVideo) {
    isVideo = VideoSource::IsSupportedExtension(path);
    if (isVideo) return true;
    return ImageSource::IsSupportedExtension(path);
}

namespace {

void NameThread(const wchar_t* name) {
    typedef HRESULT(WINAPI * PFN_SetThreadDescription)(HANDLE, PCWSTR);
    if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll"))
        if (auto fn = (PFN_SetThreadDescription)GetProcAddress(k32, "SetThreadDescription")) fn(GetCurrentThread(), name);
}

WICBitmapTransformOptions TransformFor(UINT o) {
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

UINT Orientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader)) || !reader) return 1;
    static const wchar_t* kQueries[] = { L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}", L"/xmp/tiff:Orientation" };
    for (const wchar_t* q : kQueries) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(reader->GetMetadataByName(q, &v))) {
            UINT o = 1;
            if (v.vt == VT_UI2) o = v.uiVal;
            else if (v.vt == VT_UI4) o = v.ulVal;
            else if (v.vt == VT_I4) o = (UINT)v.lVal;
            else if (v.vt == VT_LPWSTR && v.pwszVal) o = (UINT)_wtoi(v.pwszVal);
            PropVariantClear(&v);
            if (o >= 1 && o <= 8) return o;
        }
    }
    return 1;
}

} // namespace

void LibraryScanner::Start(IDXGIAdapter* adapter) {
    Stop();
    m_adapter = adapter;
    m_stop = false;
    m_thread = std::thread([this] { Main(); });
}

void LibraryScanner::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_probes.clear();
    m_story.clear();
    m_results.clear();
    m_hoverPending = false;
    m_stop = false;
}

void LibraryScanner::Probe(unsigned id, const std::wstring& path, bool isVideo, int cell) {
    ScanRequest r;
    r.kind = ScanRequest::Probe;
    r.id = id;
    r.path = path;
    r.isVideo = isVideo;
    r.cell = cell;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_probes.push_back(std::move(r));
    }
    m_cv.notify_one();
}

void LibraryScanner::SetStoryboard(unsigned generation, const std::wstring& path, std::vector<ScanRequest>&& slots) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_generation = generation;
        m_storyPath = path;
        m_story.clear();
        for (ScanRequest& s : slots) {
            s.kind = ScanRequest::Storyboard;
            s.id = generation;
            s.path = path;
            s.isVideo = true;
            m_story.push_back(std::move(s));
        }
        m_hoverPending = false;
    }
    m_cv.notify_one();
}

void LibraryScanner::Hover(unsigned generation, double seconds, int cell) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (generation != m_generation || m_storyPath.empty()) return;
        m_hover = ScanRequest{};
        m_hover.kind = ScanRequest::Hover;
        m_hover.id = generation;
        m_hover.path = m_storyPath;
        m_hover.isVideo = true;
        m_hover.seconds = seconds;
        m_hover.cell = cell;
        m_hoverPending = true;
    }
    m_cv.notify_one();
}

void LibraryScanner::ClearStoryboard() {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_generation;
    m_storyPath.clear();
    m_story.clear();
    m_hoverPending = false;
}

void LibraryScanner::Forget(unsigned id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_probes.begin(); it != m_probes.end();) {
        if (it->id == id) it = m_probes.erase(it);
        else ++it;
    }
    for (auto it = m_results.begin(); it != m_results.end();) {
        if (it->kind == ScanRequest::Probe && it->id == id) it = m_results.erase(it);
        else ++it;
    }
}

bool LibraryScanner::Poll(ScanResult& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.empty()) return false;
    out = std::move(m_results.front());
    m_results.pop_front();
    return true;
}

size_t LibraryScanner::Pending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_probes.size() + m_story.size() + (m_hoverPending ? 1 : 0);
}

bool LibraryScanner::Take(ScanRequest& out) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [&] { return m_stop || m_hoverPending || !m_probes.empty() || !m_story.empty(); });
    if (m_stop) return false;
    if (m_hoverPending) { out = m_hover; m_hoverPending = false; return true; }
    if (!m_probes.empty()) { out = std::move(m_probes.front()); m_probes.pop_front(); return true; }
    out = std::move(m_story.front());
    m_story.pop_front();
    return true;
}

void LibraryScanner::Main() {
    NameThread(L"VDC library scanner");
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        std::string e;
        if (!m_decoder.Create(m_adapter, e)) Log::Warn("Library: thumbnails use the software decoder (%s)", e.c_str());
    }
    ScanRequest r;
    while (Take(r)) {
        ScanResult out;
        out.kind = r.kind;
        out.id = r.id;
        out.cell = r.cell;
        out.slot = r.slot;
        out.seconds = r.seconds;
        if (r.kind == ScanRequest::Probe) {
            if (r.isVideo) ProbeVideo(r, out);
            else ProbeImage(r, out);
        } else {
            Frame(r, out);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (r.kind != ScanRequest::Probe && r.id != m_generation) continue;   // stale storyboard
        m_results.push_back(std::move(out));
    }
    m_storyScanner.Close();
    m_decoder.Reset();
    if (SUCCEEDED(coHr)) CoUninitialize();
}

void LibraryScanner::ProbeImage(const ScanRequest& r, ScanResult& out) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { out.error = "WIC unavailable: " + FormatHr(hr); return; }
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(r.path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        out.error = hr == WINCODEC_ERR_COMPONENTNOTFOUND ? "no decoder installed for this image format" : "cannot decode the file: " + FormatHr(hr);
        return;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { out.error = "GetFrame failed: " + FormatHr(hr); return; }
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h) { out.error = "the image is empty"; return; }
    ComPtr<IWICBitmapSource> source = frame;
    const UINT orientation = Orientation(frame.Get());
    if (orientation != 1) {
        ComPtr<IWICBitmapFlipRotator> rot;
        if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rot)) && SUCCEEDED(rot->Initialize(source.Get(), TransformFor(orientation)))) {
            source = rot;
            source->GetSize(&w, &h);
        }
    }
    out.width = w;
    out.height = h;
    // Scale to the size that fits the cell, then place it.
    const double scale = std::min((double)kThumbWidth / w, (double)kThumbHeight / h);
    const UINT tw = std::clamp((UINT)std::lround(w * scale), 1u, kThumbWidth);
    const UINT th = std::clamp((UINT)std::lround(h * scale), 1u, kThumbHeight);
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(source.Get(), tw, th, WICBitmapInterpolationModeFant))) {
        out.error = "cannot scale the image";
        return;
    }
    ComPtr<IWICBitmapSource> bgra;
    hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, scaler.Get(), &bgra);
    if (FAILED(hr)) { out.error = "pixel format conversion failed: " + FormatHr(hr); return; }
    std::vector<uint8_t> scaled((size_t)tw * th * 4);
    WICRect rect{ 0, 0, (INT)tw, (INT)th };
    hr = bgra->CopyPixels(&rect, tw * 4, (UINT)scaled.size(), scaled.data());
    if (FAILED(hr)) { out.error = "CopyPixels failed: " + FormatHr(hr); return; }
    FitThumbnail(scaled, tw, th, kThumbWidth, kThumbHeight, out.thumb);
    out.ok = true;
}

void LibraryScanner::ProbeVideo(const ScanRequest& r, ScanResult& out) {
    VideoScanner scanner;
    std::string error;
    if (!scanner.Open(r.path, m_decoder.manager.Get(), error)) { out.error = error; return; }
    const VideoInfo& info = scanner.Info();
    out.duration = info.durationSeconds;
    out.fps = (double)info.fpsNum / (double)std::max(1u, info.fpsDen);
    out.hasAudio = info.hasAudio;
    double got = 0.0;
    if (!scanner.Thumbnail(0.0, true, kThumbWidth, kThumbHeight, out.thumb, got, error)) { out.error = error; return; }
    out.width = info.width;      // as settled with the first decoded frame
    out.height = info.height;
    out.ok = true;
}

void LibraryScanner::Frame(const ScanRequest& r, ScanResult& out) {
    std::string error;
    if (m_storyOpen != r.path || !m_storyScanner.Opened()) {
        m_storyScanner.Close();
        m_storyOpen.clear();
        if (!m_storyScanner.Open(r.path, m_decoder.manager.Get(), error)) { out.error = error; return; }
        m_storyOpen = r.path;
    }
    double got = 0.0;
    if (!m_storyScanner.Thumbnail(r.seconds, false, kThumbWidth, kThumbHeight, out.thumb, got, error)) { out.error = error; return; }
    out.seconds = got;
    out.ok = true;
}

} // namespace vdc
