// VRChat DLSS5 Cam - compute shaders (compiled at runtime with D3DCompile), shared root signature and dispatch helper.
#pragma once
#include "gfx/Device.h"

namespace vdc {

enum class ShaderId : UINT { Convert = 0, Downsample, BlockMatch, MedianMv, Densify, Stats, Composite, DepthPre, DepthPost, Expose, ToneHist, ToneResolve, ToneResidual, ToneBlur, Count };

// Mirrors cbuffer Constants in the HLSL (32 DWORDs of root constants).
struct alignas(16) ShaderConstants {
    UINT  srcWidth = 0, srcHeight = 0, dstWidth = 0, dstHeight = 0;
    UINT  flags = 0, level = 0, intA = 0, intB = 0;
    float scaleX = 1, scaleY = 1, paramA = 0, paramB = 0;
    float paramC = 0, paramD = 0, paramE = 0, paramF = 0;
    float extra0[4]{};
    float extra1[4]{};
    float extra2[4]{};
    float extra3[4]{};
};
static_assert(sizeof(ShaderConstants) == 32 * 4, "root constant size");

struct DispatchDesc {
    ShaderId                    id = ShaderId::Convert;
    ShaderConstants             constants;
    D3D12_CPU_DESCRIPTOR_HANDLE srv[12]{};
    D3D12_CPU_DESCRIPTOR_HANDLE uav[4]{};
    UINT                        groupsX = 1, groupsY = 1, groupsZ = 1;
};

class Shaders {
public:
    bool Init(Device& device, std::wstring& error);
    void Shutdown();
    // Records one compute dispatch. The descriptor table lives in gpu's per-frame ring, so cmd must be that
    // context's command list.
    void Dispatch(ID3D12GraphicsCommandList* cmd, GpuContext& gpu, const DispatchDesc& d);

    static UINT Groups(UINT size, UINT threads) { return (size + threads - 1) / threads; }

private:
    bool Compile(Device& device, ShaderId id, const char* name, const char* source, std::wstring& error);
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso[(UINT)ShaderId::Count];
};

} // namespace vdc
