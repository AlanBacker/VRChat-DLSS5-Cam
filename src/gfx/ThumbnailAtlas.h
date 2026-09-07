// VRChat DLSS5 Cam - a texture atlas of small BGRA pictures for the interface: media library thumbnails and the
// seek-bar preview of the video. Cells are filled from any thread; the copies are recorded on the present queue.
#pragma once
#include "gfx/Device.h"
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace vdc {

class ThumbnailAtlas {
public:
    static constexpr UINT kCellWidth = 192;
    static constexpr UINT kCellHeight = 108;
    static constexpr UINT kColumns = 16;
    static constexpr UINT kRows = 16;
    static constexpr UINT kCells = kColumns * kRows;

    bool Init(Device& device, std::string& error);
    void Shutdown();

    // Cell bookkeeping (interface thread).
    int  Alloc();                  // -1 when every cell is taken
    void Free(int cell);
    // Queues the picture (kCellWidth x kCellHeight BGRA8) for the cell; any thread.
    void Set(int cell, std::vector<uint8_t>&& bgra);
    bool Filled(int cell) const;   // a picture has been uploaded
    // Records the pending copies on the present queue's open command list (once per frame, before ImGui draws).
    void Upload(ID3D12GraphicsCommandList* cmd, GpuContext& ui);

    UINT64 TextureHandle() const { return m_srv.gpu.ptr; }   // the ImGui texture id
    void Uv(int cell, float& u0, float& v0, float& u1, float& v1) const;
    bool Ready() const { return m_tex != nullptr; }

private:
    struct Pending { int cell; std::vector<uint8_t> bgra; };
    static constexpr UINT kUploadsPerFrame = 8;

    Device*                m_device = nullptr;
    ComPtr<ID3D12Resource> m_tex;
    DescriptorPair         m_srv{};
    ComPtr<ID3D12Resource> m_upload[Device::kFramesInFlight];
    uint8_t*               m_uploadPtr[Device::kFramesInFlight] = {};
    UINT                   m_uploadPitch = 0;
    std::vector<bool>      m_used;
    std::vector<bool>      m_filled;
    mutable std::mutex     m_mutex;
    std::deque<Pending>    m_pending;
    bool                   m_shaderState = false;   // the texture is in PIXEL_SHADER_RESOURCE (else COMMON)
};

} // namespace vdc
