// VRChat DLSS5 Cam - the picture handed to the pipeline: a Spout frame or a still image, as a D3D12 texture.
#pragma once
#include "core/Util.h"
#include <d3d12.h>
#include <dxgi.h>

namespace vdc {

struct SourceFrame {
    ID3D12Resource*             texture = nullptr;   // null: no source connected
    D3D12_CPU_DESCRIPTOR_HANDLE srv{};               // staging SRV (viewFormat)
    UINT                        width = 0;
    UINT                        height = 0;
    DXGI_FORMAT                 format = DXGI_FORMAT_UNKNOWN;      // resource format
    DXGI_FORMAT                 viewFormat = DXGI_FORMAT_UNKNOWN;  // SRV format (sRGB view for 8-bit UNORM)
    bool                        linear = false;      // scene-linear HDR input (float formats)
    bool                        stillImage = false;  // a single picture: no motion, temporal history converges on it
    bool                        hasFrame = false;    // the texture holds a picture

    bool Connected() const { return texture != nullptr && width > 0 && height > 0; }
    bool IsHdr() const {
        return format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
               format == DXGI_FORMAT_R11G11B10_FLOAT;
    }
};

} // namespace vdc
