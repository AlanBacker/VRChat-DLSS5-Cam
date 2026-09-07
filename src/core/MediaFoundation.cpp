#include "core/MediaFoundation.h"
#include "core/Log.h"
#include <mutex>

namespace vdc::mf {

namespace {

typedef HRESULT (STDAPICALLTYPE* PFN_MFStartup)(ULONG version, DWORD flags);
typedef HRESULT (STDAPICALLTYPE* PFN_MFShutdown)(void);
typedef HRESULT (STDAPICALLTYPE* PFN_MFCreateAttributes)(IMFAttributes** out, UINT32 initialSize);
typedef HRESULT (STDAPICALLTYPE* PFN_MFCreateMediaType)(IMFMediaType** out);
typedef HRESULT (STDAPICALLTYPE* PFN_MFCreateSample)(IMFSample** out);
typedef HRESULT (STDAPICALLTYPE* PFN_MFCreateAlignedMemoryBuffer)(DWORD maxLength, DWORD alignment, IMFMediaBuffer** out);
typedef HRESULT (STDAPICALLTYPE* PFN_MFCreateDXGIDeviceManager)(UINT* resetToken, IMFDXGIDeviceManager** out);
typedef HRESULT (STDAPICALLTYPE* PFN_MFCreateSourceReaderFromURL)(LPCWSTR url, IMFAttributes* attributes, IMFSourceReader** out);
typedef HRESULT (STDAPICALLTYPE* PFN_MFCreateSinkWriterFromURL)(LPCWSTR url, IMFByteStream* stream, IMFAttributes* attributes,
                                                                  IMFSinkWriter** out);

struct Api {
    HMODULE plat = nullptr;
    HMODULE readWrite = nullptr;
    PFN_MFStartup                   startup = nullptr;
    PFN_MFShutdown                  shutdown = nullptr;
    PFN_MFCreateAttributes          createAttributes = nullptr;
    PFN_MFCreateMediaType           createMediaType = nullptr;
    PFN_MFCreateSample              createSample = nullptr;
    PFN_MFCreateAlignedMemoryBuffer createAlignedMemoryBuffer = nullptr;
    PFN_MFCreateDXGIDeviceManager   createDxgiDeviceManager = nullptr;
    PFN_MFCreateSourceReaderFromURL createSourceReaderFromUrl = nullptr;
    PFN_MFCreateSinkWriterFromURL   createSinkWriterFromUrl = nullptr;
    bool        started = false;
    bool        ok = false;
    std::string error;
};

Api            g_api;
std::once_flag g_once;

template <class T>
bool Resolve(HMODULE module, const char* name, T& fn) {
    fn = reinterpret_cast<T>(GetProcAddress(module, name));
    return fn != nullptr;
}

void LoadOnce() {
    Api& a = g_api;
    a.plat = LoadLibraryW(L"mfplat.dll");
    a.readWrite = LoadLibraryW(L"mfreadwrite.dll");
    if (!a.plat || !a.readWrite) {
        a.error = "Media Foundation is not installed (Windows N/KN editions need the Media Feature Pack)";
        Log::Warn("Video: %s", a.error.c_str());
        return;
    }
    const bool resolved =
        Resolve(a.plat, "MFStartup", a.startup) && Resolve(a.plat, "MFShutdown", a.shutdown) &&
        Resolve(a.plat, "MFCreateAttributes", a.createAttributes) && Resolve(a.plat, "MFCreateMediaType", a.createMediaType) &&
        Resolve(a.plat, "MFCreateSample", a.createSample) &&
        Resolve(a.plat, "MFCreateAlignedMemoryBuffer", a.createAlignedMemoryBuffer) &&
        Resolve(a.plat, "MFCreateDXGIDeviceManager", a.createDxgiDeviceManager) &&
        Resolve(a.readWrite, "MFCreateSourceReaderFromURL", a.createSourceReaderFromUrl) &&
        Resolve(a.readWrite, "MFCreateSinkWriterFromURL", a.createSinkWriterFromUrl);
    if (!resolved) {
        a.error = "Media Foundation on this system is incomplete (Windows 8 or newer is required)";
        Log::Warn("Video: %s", a.error.c_str());
        return;
    }
    const HRESULT hr = a.startup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        a.error = "MFStartup failed: " + FormatHr(hr);
        Log::Warn("Video: %s", a.error.c_str());
        return;
    }
    a.started = true;
    a.ok = true;
    Log::Info("Media Foundation ready");
}

constexpr HRESULT kNotLoaded = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);

} // namespace

bool Available(std::string& error) {
    std::call_once(g_once, LoadOnce);
    if (!g_api.ok) error = g_api.error;
    return g_api.ok;
}

bool Available() {
    std::string ignored;
    return Available(ignored);
}

void Shutdown() {
    if (g_api.started) {
        g_api.shutdown();
        g_api.started = false;
        g_api.ok = false;
        g_api.error = "Media Foundation was shut down";
    }
}

HRESULT CreateAttributes(IMFAttributes** out, UINT32 initialSize) {
    return Available() ? g_api.createAttributes(out, initialSize) : kNotLoaded;
}
HRESULT CreateMediaType(IMFMediaType** out) {
    return Available() ? g_api.createMediaType(out) : kNotLoaded;
}
HRESULT CreateSample(IMFSample** out) {
    return Available() ? g_api.createSample(out) : kNotLoaded;
}
HRESULT CreateAlignedMemoryBuffer(DWORD maxLength, DWORD alignment, IMFMediaBuffer** out) {
    return Available() ? g_api.createAlignedMemoryBuffer(maxLength, alignment, out) : kNotLoaded;
}
HRESULT CreateDXGIDeviceManager(UINT* resetToken, IMFDXGIDeviceManager** out) {
    return Available() ? g_api.createDxgiDeviceManager(resetToken, out) : kNotLoaded;
}
HRESULT CreateSourceReaderFromURL(const wchar_t* url, IMFAttributes* attributes, IMFSourceReader** out) {
    return Available() ? g_api.createSourceReaderFromUrl(url, attributes, out) : kNotLoaded;
}
HRESULT CreateSinkWriterFromURL(const wchar_t* url, IMFByteStream* byteStream, IMFAttributes* attributes, IMFSinkWriter** out) {
    return Available() ? g_api.createSinkWriterFromUrl(url, byteStream, attributes, out) : kNotLoaded;
}

HRESULT GetSize(IMFAttributes* a, const GUID& key, UINT32& width, UINT32& height) {
    UINT64 v = 0;
    const HRESULT hr = a->GetUINT64(key, &v);
    if (SUCCEEDED(hr)) Unpack2(v, width, height);
    return hr;
}
HRESULT GetRatio(IMFAttributes* a, const GUID& key, UINT32& numerator, UINT32& denominator) {
    return GetSize(a, key, numerator, denominator);
}
HRESULT SetSize(IMFAttributes* a, const GUID& key, UINT32 width, UINT32 height) {
    return a->SetUINT64(key, Pack2(width, height));
}
HRESULT SetRatio(IMFAttributes* a, const GUID& key, UINT32 numerator, UINT32 denominator) {
    return a->SetUINT64(key, Pack2(numerator, denominator));
}

std::string SubtypeName(const GUID& subtype) {
    // Video subtypes are FOURCC codes in Data1 with the MFVideoFormat_Base tail; anything else is shown as hex.
    const UINT32 code = subtype.Data1;
    char text[5] = {};
    bool printable = true;
    for (int i = 0; i < 4; ++i) {
        const char c = (char)((code >> (8 * i)) & 0xFF);
        text[i] = c;
        if (c < 0x20 || c > 0x7E) printable = false;
    }
    if (printable) return text;
    if (subtype == MFVideoFormat_RGB32) return "RGB32";
    if (subtype == MFVideoFormat_ARGB32) return "ARGB32";
    return StrPrintf("0x%08X", (unsigned)code);
}

} // namespace vdc::mf
