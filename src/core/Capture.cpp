#include "core/Capture.h"
#include "core/Log.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <objbase.h>
#include <cwctype>

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

// Jobs waiting plus the one being encoded: the count reaches zero only once the last file is written.
size_t Capture::Pending() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_jobs.size() + m_active;
}

// The settings are held while these are written; a screenshot or a preview's copy is a picture already made.
size_t Capture::PendingSaves() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t n = m_activeSave;
    for (const CaptureJob& job : m_jobs) if (!job.quiet) ++n;
    return n;
}

const wchar_t* const Capture::kDefaultCaptureName = L"VRChat_DLSS5_{date}_{time}_{size}";
const wchar_t* const Capture::kDefaultOutputName  = L"{name}_DLSS5_{size}";

std::wstring Capture::Template(const std::string& utf8, bool liveCapture) {
    std::wstring t = Utf8ToWide(utf8);
    while (!t.empty() && iswspace(t.back())) t.pop_back();
    size_t b = 0;
    while (b < t.size() && iswspace(t[b])) ++b;
    t.erase(0, b);
    return t.empty() ? std::wstring(liveCapture ? kDefaultCaptureName : kDefaultOutputName) : t;
}

std::wstring Capture::ExpandName(const std::wstring& tmpl, const std::wstring& name, UINT inW, UINT inH, UINT width, UINT height,
                                 const SYSTEMTIME* at) {
    SYSTEMTIME st;
    if (at) st = *at; else GetLocalTime(&st);
    wchar_t date[32], time[32];
    swprintf_s(date, L"%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    swprintf_s(time, L"%02u-%02u-%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    const std::wstring who = name.empty() ? L"VRChat" : name;
    struct Token { const wchar_t* key; std::wstring value; };
    const Token tokens[] = {
        { L"{name}", who }, { L"{date}", date }, { L"{time}", time },
        { L"{size}", std::to_wstring(width) + L"x" + std::to_wstring(height) },
        { L"{width}", std::to_wstring(width) }, { L"{height}", std::to_wstring(height) },
        { L"{insize}", std::to_wstring(inW) + L"x" + std::to_wstring(inH) },
        { L"{inwidth}", std::to_wstring(inW) }, { L"{inheight}", std::to_wstring(inH) },
    };
    std::wstring out;
    for (size_t i = 0; i < tmpl.size();) {
        bool hit = false;
        if (tmpl[i] == L'{') {
            for (const auto& t : tokens) {
                const size_t n = wcslen(t.key);
                if (i + n <= tmpl.size() && _wcsnicmp(tmpl.c_str() + i, t.key, n) == 0) { out += t.value; i += n; hit = true; break; }
            }
        }
        if (!hit) out += tmpl[i++];
    }
    // What a file name cannot hold (the separators too: the file stays in its folder) becomes "_"; Windows drops a
    // trailing dot or blank itself, so they go here to keep the name and its extension together.
    for (auto& c : out) if (c < 32 || wcschr(L"\\/:*?\"<>|", c)) c = L'_';
    while (!out.empty() && (out.back() == L'.' || out.back() == L' ')) out.pop_back();
    if (out.empty()) out = who;
    return out;
}

std::wstring Capture::MakeFileName(const std::wstring& folder, const std::wstring& tmpl, const std::wstring& name, UINT inW, UINT inH,
                                   UINT width, UINT height, const wchar_t* suffix, const wchar_t* ext, const SYSTEMTIME* at) {
    const std::wstring base = ExpandName(tmpl, name, inW, inH, width, height, at) + (suffix ? suffix : L"");
    for (int n = 1; n < 10000; ++n) {
        const std::wstring path = JoinPath(folder, base + (n == 1 ? std::wstring() : L"_" + std::to_wstring(n)) + L"." + ext);
        if (!FileExists(path)) return path;
    }
    return JoinPath(folder, base + L"_" + TimestampForFileName() + L"." + ext);
}

std::wstring Capture::MakeFolderName(const std::wstring& folder, const std::wstring& tmpl, const std::wstring& name, UINT inW, UINT inH,
                                     UINT width, UINT height) {
    const std::wstring base = ExpandName(tmpl, name, inW, inH, width, height);
    for (int n = 1; n < 10000; ++n) {
        const std::wstring path = JoinPath(folder, base + (n == 1 ? std::wstring() : L"_" + std::to_wstring(n)));
        if (!DirectoryExists(path) && !FileExists(path)) return path;
    }
    return JoinPath(folder, base + L"_" + TimestampForFileName());
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
            ++m_active;
            if (!job.quiet) ++m_activeSave;
        }
        CaptureResult result;
        result.path = job.path;
        result.quiet = job.quiet;
        result.tag = job.tag;
        result.width = job.width; result.height = job.height;
        const double t0 = NowSeconds();
        result.ok = EncodePng(job, result.error, result.bytes);
        result.seconds = NowSeconds() - t0;
        if (result.ok && !result.quiet) Log::Info("Saved %s (%ux%u, %.0f KB, %.2f s)", WideToUtf8(job.path).c_str(), job.width, job.height,
                                 result.bytes / 1024.0, result.seconds);
        else if (!result.ok) Log::Error("Capture failed for %s: %s", WideToUtf8(job.path).c_str(), result.error.c_str());
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_results.push_back(std::move(result));
            --m_active;
            if (!job.quiet) --m_activeSave;
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
    if (!EncodePngStream(factory.Get(), stream.Get(), job, error)) return false;
    stream.Reset();
    bytes = GetFileSizeBytes(job.path);
    return true;
}

bool Capture::EncodePngMemory(const CaptureJob& job, std::vector<uint8_t>& png, std::string& error) {
    png.clear();
    if (!job.width || !job.height || job.pixels.size() < (size_t)job.rowPitch * job.height) {
        error = "invalid capture buffer";
        return false;
    }
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { error = "WIC factory: " + FormatHr(hr); return false; }
    ComPtr<IStream> stream;
    hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (FAILED(hr)) { error = "memory stream: " + FormatHr(hr); return false; }
    if (!EncodePngStream(factory.Get(), stream.Get(), job, error)) return false;
    STATSTG st{};
    if (FAILED(stream->Stat(&st, STATFLAG_NONAME))) { error = "memory stream size"; return false; }
    HGLOBAL mem = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.Get(), &mem)) || !mem) { error = "memory stream handle"; return false; }
    const uint8_t* p = static_cast<const uint8_t*>(GlobalLock(mem));
    if (!p) { error = "memory stream lock"; return false; }
    png.assign(p, p + (size_t)st.cbSize.QuadPart);
    GlobalUnlock(mem);
    return true;
}

bool Capture::EncodePngStream(IWICImagingFactory* factory, IStream* stream, const CaptureJob& job, std::string& error) {
    ComPtr<IWICBitmapEncoder> encoder;
    HRESULT hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
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
    return true;
}

} // namespace vdc
