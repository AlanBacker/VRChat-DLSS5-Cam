#include "core/Capture.h"
#include "core/Log.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <objbase.h>

using Microsoft::WRL::ComPtr;

namespace vdc {

bool Capture::Init() {
    if (m_running) return true;
    m_quit = false;
    m_running = true;
    m_thread = std::thread([this] { WorkerMain(); });
    return true;
}

void Capture::Shutdown() {
    if (!m_running) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    m_running = false;
}

void Capture::Enqueue(CaptureJob&& job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

bool Capture::PollResult(CaptureResult& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.empty()) return false;
    out = std::move(m_results.front());
    m_results.pop_front();
    return true;
}

size_t Capture::Pending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_jobs.size();
}

std::wstring Capture::MakeFileName(const std::wstring& folder, UINT width, UINT height, const wchar_t* suffix) {
    wchar_t buf[128];
    swprintf_s(buf, L"VRChat_DLSS5_%s_%ux%u%s.png", TimestampForFileName().c_str(), width, height, suffix ? suffix : L"");
    return JoinPath(folder, buf);
}

std::wstring Capture::MakeVideoFileName(const std::wstring& folder, const std::wstring& stem, UINT width, UINT height,
                                        const wchar_t* ext) {
    const std::wstring base = stem.empty() ? L"video" : stem;
    for (int n = 1; n < 10000; ++n) {
        wchar_t buf[256];
        if (n == 1) swprintf_s(buf, L"_DLSS5_%ux%u.%s", width, height, ext);
        else        swprintf_s(buf, L"_DLSS5_%ux%u_%d.%s", width, height, n, ext);
        const std::wstring path = JoinPath(folder, base + buf);
        if (!FileExists(path)) return path;
    }
    return JoinPath(folder, base + L"_DLSS5_" + TimestampForFileName() + L"." + ext);
}

std::wstring Capture::MakeImageFileName(const std::wstring& folder, const std::wstring& stem, UINT width, UINT height,
                                        const wchar_t* suffix) {
    std::wstring base = stem.empty() ? L"image" : stem;
    for (int n = 1; n < 10000; ++n) {
        wchar_t buf[256];
        if (n == 1) swprintf_s(buf, L"_DLSS5_%ux%u%s.png", width, height, suffix ? suffix : L"");
        else        swprintf_s(buf, L"_DLSS5_%ux%u%s_%d.png", width, height, suffix ? suffix : L"", n);
        std::wstring path = JoinPath(folder, base + buf);
        if (!FileExists(path)) return path;
    }
    return MakeFileName(folder, width, height, suffix);
}

void Capture::WorkerMain() {
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        CaptureJob job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_quit || !m_jobs.empty(); });
            if (m_jobs.empty()) {
                if (m_quit) break;
                continue;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        CaptureResult result;
        result.path = job.path;
        result.quiet = job.quiet;
        const double t0 = NowSeconds();
        result.ok = EncodePng(job, result.error, result.bytes);
        result.seconds = NowSeconds() - t0;
        if (result.ok && !result.quiet) Log::Info("Saved %s (%ux%u, %.0f KB, %.2f s)", WideToUtf8(job.path).c_str(), job.width, job.height,
                                 result.bytes / 1024.0, result.seconds);
        else Log::Error("Capture failed for %s: %s", WideToUtf8(job.path).c_str(), result.error.c_str());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_results.push_back(std::move(result));
        }
    }
    if (SUCCEEDED(coInit)) CoUninitialize();
}

bool Capture::EncodePng(const CaptureJob& job, std::string& error, uint64_t& bytes) {
    bytes = 0;
    if (!job.width || !job.height || job.pixels.size() < (size_t)job.rowPitch * job.height) {
        error = "invalid capture buffer";
        return false;
    }
    std::wstring dir = job.path;
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        dir.resize(slash);
        if (!CreateDirectories(dir)) { error = "cannot create folder " + WideToUtf8(dir); return false; }
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = "WIC factory: " + FormatHr(hr); return false; }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(job.path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) { error = "open file: " + FormatHr(hr); return false; }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) { error = "PNG encoder: " + FormatHr(hr); return false; }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr) && props) {
        PROPBAG2 opt{};
        wchar_t optName[] = L"FilterOption";
        opt.pstrName = optName;
        VARIANT v; VariantInit(&v);
        v.vt = VT_UI1;
        v.bVal = WICPngFilterAdaptive;
        props->Write(1, &opt, &v);   // best-effort: smaller files, still lossless
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(job.width, job.height);
    if (FAILED(hr)) { error = "PNG frame: " + FormatHr(hr); return false; }

    // Convert RGBA8 -> BGRA8 / BGR8 (WIC's native PNG pixel formats).
    const bool alpha = job.keepAlpha;
    const UINT bpp = alpha ? 4 : 3;
    const UINT outPitch = job.width * bpp;
    std::vector<uint8_t> out((size_t)outPitch * job.height);
    for (UINT y = 0; y < job.height; ++y) {
        const uint8_t* src = job.pixels.data() + (size_t)y * job.rowPitch;
        uint8_t* dst = out.data() + (size_t)y * outPitch;
        if (alpha) {
            for (UINT x = 0; x < job.width; ++x, src += 4, dst += 4) {
                dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
            }
        } else {
            for (UINT x = 0; x < job.width; ++x, src += 4, dst += 3) {
                dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
            }
        }
    }
    WICPixelFormatGUID fmt = alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    hr = frame->SetPixelFormat(&fmt);
    if (FAILED(hr)) { error = "PNG pixel format: " + FormatHr(hr); return false; }
    const bool formatAccepted = alpha ? (fmt == GUID_WICPixelFormat32bppBGRA) : (fmt == GUID_WICPixelFormat24bppBGR);
    if (!formatAccepted) { error = "PNG pixel format not accepted by encoder"; return false; }
    hr = frame->WritePixels(job.height, outPitch, (UINT)out.size(), out.data());
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) { error = "PNG write: " + FormatHr(hr); return false; }
    stream.Reset();
    bytes = GetFileSizeBytes(job.path);
    return true;
}

} // namespace vdc
