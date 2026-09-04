// VRChat DLSS5 Cam - a still image (PNG/JPEG/BMP/TIFF/GIF/WebP/HEIC via WIC) as a pipeline source.
#pragma once
#include "core/SourceFrame.h"
#include "gfx/Device.h"
#include <string>
#include <vector>

namespace vdc {

class ImageSource {
public:
    static constexpr UINT kMaxLongSide = 8192;

    // Decodes the file (EXIF orientation applied, long side capped at kMaxLongSide) into a BGRA8 texture. Runs on
    // the processing thread; the pixels reach the GPU with the next Upload().
    bool Load(GpuContext& gpu, const std::wstring& path, std::string& error);
    void Release(GpuContext& gpu);

    // Records the pending copy into cmd (the processing context's list). No-op when nothing is pending.
    void Upload(ID3D12GraphicsCommandList* cmd, GpuContext& gpu);

    bool                Loaded() const { return m_tex != nullptr; }
    SourceFrame         Frame() const;
    const std::wstring& Path() const { return m_path; }
    std::wstring        Stem() const;         // file name without folder and extension
    UINT                Width() const { return m_width; }
    UINT                Height() const { return m_height; }
    UINT                OriginalWidth() const { return m_origWidth; }
    UINT                OriginalHeight() const { return m_origHeight; }

    // Extensions the decoder accepts (lower case, without the dot).
    static bool IsSupportedExtension(const std::wstring& path);

private:
    ComPtr<ID3D12Resource>      m_tex;
    ComPtr<ID3D12Resource>      m_upload;
    D3D12_CPU_DESCRIPTOR_HANDLE m_srv{};
    std::wstring                m_path;
    UINT                        m_width = 0, m_height = 0;
    UINT                        m_origWidth = 0, m_origHeight = 0;
    UINT                        m_uploadPitch = 0;
    bool                        m_uploadPending = false;
    bool                        m_uploaded = false;
};

} // namespace vdc
