// VRChat DLSS5 Cam - the picture handed to the pipeline: a Spout frame or a still image, as a D3D12 texture.
#pragma once
#include <algorithm>
#include <cmath>
#include "core/Util.h"
#include <d3d12.h>
#include <dxgi.h>

namespace vdc {

// How a file's picture is turned, mirrored and cropped before the passes see it (library files only; the live feed
// is always shown as it comes). The crop is kept in fractions of the turned picture, so it survives a different
// decode size of the same file.
struct SourceTransform {
    int   rotate = 0;                  // quarter turns clockwise, 0..3
    bool  flipH = false, flipV = false;
    float cropX = 0.0f, cropY = 0.0f, cropW = 1.0f, cropH = 1.0f;

    bool Cropped() const { return cropX > 0.0005f || cropY > 0.0005f || cropW < 0.9995f || cropH < 0.9995f; }
    bool Identity() const { return rotate == 0 && !flipH && !flipV && !Cropped(); }
    bool operator==(const SourceTransform& o) const {
        return rotate == o.rotate && flipH == o.flipH && flipV == o.flipV &&
               cropX == o.cropX && cropY == o.cropY && cropW == o.cropW && cropH == o.cropH;
    }
    bool operator!=(const SourceTransform& o) const { return !(*this == o); }
    UINT Bits() const { return (UINT)(rotate & 3) | (flipH ? 4u : 0u) | (flipV ? 8u : 0u); }
    // Size of the picture after the transform and the crop's top-left corner, in whole pixels of the turned
    // picture. A crop takes even sizes (the video encoders want them); an uncropped picture keeps its exact size.
    void Resolve(UINT rawW, UINT rawH, UINT& w, UINT& h, UINT& x0, UINT& y0) const {
        const UINT tw = (rotate & 1) ? rawH : rawW, th = (rotate & 1) ? rawW : rawH;
        if (!Cropped()) { w = tw; h = th; x0 = y0 = 0; return; }
        auto px = [](float f, UINT n) { return (UINT)std::clamp((long)std::lround((double)f * n), 0L, (long)n); };
        x0 = std::min(px(cropX, tw), tw - 1); y0 = std::min(px(cropY, th), th - 1);
        UINT x1 = std::max(px(cropX + cropW, tw), x0 + 1), y1 = std::max(px(cropY + cropH, th), y0 + 1);
        w = x1 - x0; h = y1 - y0;
        if (w > 2 && (w & 1)) --w;
        if (h > 2 && (h & 1)) --h;
        w = std::max(w, std::min(2u, tw)); h = std::max(h, std::min(2u, th));
        if (x0 + w > tw) x0 = tw - w;
        if (y0 + h > th) y0 = th - h;
    }
};

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
    SourceTransform             transform;           // orientation and crop applied by the first pass

    bool Connected() const { return texture != nullptr && width > 0 && height > 0; }
    bool IsHdr() const {
        return format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
               format == DXGI_FORMAT_R11G11B10_FLOAT;
    }
};

} // namespace vdc
