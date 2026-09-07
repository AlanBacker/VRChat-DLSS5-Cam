// VRChat DLSS5 Cam - Media Foundation (video decode / encode) loaded at run time, so the app still starts on systems
// without it (Windows N/KN editions lack it until the Media Feature Pack is installed).
#pragma once
#include "core/Util.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string>

namespace vdc::mf {

// Loads mfplat.dll / mfreadwrite.dll on first use and starts Media Foundation. False with the reason when it is
// missing; later calls return the same answer without trying again.
bool Available(std::string& error);
bool Available();
void Shutdown();   // at process exit, once every reader / writer is released

HRESULT CreateAttributes(IMFAttributes** out, UINT32 initialSize);
HRESULT CreateMediaType(IMFMediaType** out);
HRESULT CreateSample(IMFSample** out);
HRESULT CreateAlignedMemoryBuffer(DWORD maxLength, DWORD alignment, IMFMediaBuffer** out);
HRESULT CreateDXGIDeviceManager(UINT* resetToken, IMFDXGIDeviceManager** out);
HRESULT CreateSourceReaderFromURL(const wchar_t* url, IMFAttributes* attributes, IMFSourceReader** out);
HRESULT CreateSinkWriterFromURL(const wchar_t* url, IMFByteStream* byteStream, IMFAttributes* attributes, IMFSinkWriter** out);

// Two 32-bit values packed into one 64-bit attribute (frame size, frame rate, aspect ratio).
inline UINT64 Pack2(UINT32 hi, UINT32 lo) { return ((UINT64)hi << 32) | (UINT64)lo; }
inline void   Unpack2(UINT64 v, UINT32& hi, UINT32& lo) { hi = (UINT32)(v >> 32); lo = (UINT32)(v & 0xFFFFFFFFu); }
HRESULT GetSize(IMFAttributes* a, const GUID& key, UINT32& width, UINT32& height);
HRESULT GetRatio(IMFAttributes* a, const GUID& key, UINT32& numerator, UINT32& denominator);
HRESULT SetSize(IMFAttributes* a, const GUID& key, UINT32 width, UINT32 height);
HRESULT SetRatio(IMFAttributes* a, const GUID& key, UINT32 numerator, UINT32 denominator);

// Four-character code of a video subtype ("H264", "HEVC", "NV12"), or the GUID's first word in hex.
std::string SubtypeName(const GUID& subtype);

} // namespace vdc::mf
