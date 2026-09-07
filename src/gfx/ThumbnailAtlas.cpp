#include "gfx/ThumbnailAtlas.h"
#include "core/Log.h"
#include <algorithm>
#include <cstring>

namespace vdc {

namespace {
constexpr UINT kAtlasWidth = ThumbnailAtlas::kColumns * ThumbnailAtlas::kCellWidth;
constexpr UINT kAtlasHeight = ThumbnailAtlas::kRows * ThumbnailAtlas::kCellHeight;
}

bool ThumbnailAtlas::Init(Device& device, std::string& error) {
    Shutdown();
    m_device = &device;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = kAtlasWidth;
    rd.Height = kAtlasHeight;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    HRESULT hr = device.D3D12()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                         IID_PPV_ARGS(&m_tex));
    if (FAILED(hr)) { error = "cannot create the thumbnail atlas: " + FormatHr(hr); m_tex.Reset(); return false; }
    m_tex->SetName(L"Thumbnail atlas");

    m_uploadPitch = (kCellWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const UINT64 cellBytes = ((UINT64)m_uploadPitch * kCellHeight + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) &
                             ~(UINT64)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    D3D12_HEAP_PROPERTIES up{};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = cellBytes * kUploadsPerFrame;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT i = 0; i < Device::kFramesInFlight; ++i) {
        hr = device.D3D12()->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&m_upload[i]));
        if (FAILED(hr)) { error = "cannot create the thumbnail upload buffer: " + FormatHr(hr); Shutdown(); return false; }
        void* mapped = nullptr;
        const D3D12_RANGE none{ 0, 0 };
        hr = m_upload[i]->Map(0, &none, &mapped);
        if (FAILED(hr) || !mapped) { error = "cannot map the thumbnail upload buffer: " + FormatHr(hr); Shutdown(); return false; }
        m_uploadPtr[i] = static_cast<uint8_t*>(mapped);
    }
    m_srv = device.AllocStatic();
    if (!m_srv.Valid()) { error = "no descriptor for the thumbnail atlas"; Shutdown(); return false; }
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device.D3D12()->CreateShaderResourceView(m_tex.Get(), &sd, m_srv.cpu);
    m_used.assign(kCells, false);
    m_filled.assign(kCells, false);
    m_shaderState = false;
    return true;
}

void ThumbnailAtlas::Shutdown() {
    if (!m_device) return;
    GpuContext& ui = m_device->Ui();
    for (UINT i = 0; i < Device::kFramesInFlight; ++i) {
        if (m_upload[i]) {
            if (m_uploadPtr[i]) m_upload[i]->Unmap(0, nullptr);
            ui.DeferRelease(m_upload[i]);
            m_upload[i].Reset();
        }
        m_uploadPtr[i] = nullptr;
    }
    if (m_tex) { ui.DeferRelease(m_tex); m_tex.Reset(); }
    if (m_srv.Valid()) { ui.DeferFreeStatic(m_srv); m_srv = {}; }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
    }
    m_used.clear();
    m_filled.clear();
    m_device = nullptr;
}

int ThumbnailAtlas::Alloc() {
    for (UINT i = 0; i < (UINT)m_used.size(); ++i) {
        if (!m_used[i]) { m_used[i] = true; m_filled[i] = false; return (int)i; }
    }
    return -1;
}

void ThumbnailAtlas::Free(int cell) {
    if (cell < 0 || cell >= (int)m_used.size()) return;
    m_used[cell] = false;
    m_filled[cell] = false;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->cell == cell) it = m_pending.erase(it);
        else ++it;
    }
}

void ThumbnailAtlas::Set(int cell, std::vector<uint8_t>&& bgra) {
    if (cell < 0 || cell >= (int)kCells || bgra.size() != (size_t)kCellWidth * kCellHeight * 4) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->cell == cell) it = m_pending.erase(it);
        else ++it;
    }
    m_pending.push_back(Pending{ cell, std::move(bgra) });
}

bool ThumbnailAtlas::Filled(int cell) const {
    return cell >= 0 && cell < (int)m_filled.size() && m_filled[cell];
}

void ThumbnailAtlas::Upload(ID3D12GraphicsCommandList* cmd, GpuContext& ui) {
    if (!m_tex || !cmd) return;
    std::vector<Pending> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_pending.empty() && batch.size() < kUploadsPerFrame) {
            batch.push_back(std::move(m_pending.front()));
            m_pending.pop_front();
        }
    }
    if (batch.empty()) return;
    const UINT slot = ui.FrameIndex() % Device::kFramesInFlight;
    uint8_t* base = m_uploadPtr[slot];
    ID3D12Resource* up = m_upload[slot].Get();
    if (!base || !up) return;
    const UINT64 cellBytes = ((UINT64)m_uploadPitch * kCellHeight + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) &
                             ~(UINT64)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    Device::Barrier(cmd, m_tex.Get(), m_shaderState ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_COPY_DEST);
    for (size_t i = 0; i < batch.size(); ++i) {
        const Pending& p = batch[i];
        if (p.cell < 0 || p.cell >= (int)m_used.size() || !m_used[p.cell]) continue;
        uint8_t* dst = base + cellBytes * i;
        for (UINT y = 0; y < kCellHeight; ++y)
            std::memcpy(dst + (size_t)y * m_uploadPitch, p.bgra.data() + (size_t)y * kCellWidth * 4, (size_t)kCellWidth * 4);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = up;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = cellBytes * i;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = kCellWidth;
        src.PlacedFootprint.Footprint.Height = kCellHeight;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = m_uploadPitch;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = m_tex.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;
        const UINT cx = ((UINT)p.cell % kColumns) * kCellWidth, cy = ((UINT)p.cell / kColumns) * kCellHeight;
        cmd->CopyTextureRegion(&dstLoc, cx, cy, 0, &src, nullptr);
        m_filled[p.cell] = true;
    }
    Device::Barrier(cmd, m_tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_shaderState = true;
}

void ThumbnailAtlas::Uv(int cell, float& u0, float& v0, float& u1, float& v1) const {
    const UINT c = cell < 0 ? 0 : (UINT)cell;
    const float cw = 1.0f / (float)kColumns, chh = 1.0f / (float)kRows;
    u0 = (float)(c % kColumns) * cw;
    v0 = (float)(c / kColumns) * chh;
    u1 = u0 + cw;
    v1 = v0 + chh;
}

} // namespace vdc
