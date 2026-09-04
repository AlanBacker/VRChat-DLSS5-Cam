#include "gfx/Shaders.h"
#include "core/Log.h"
#include <d3dcompiler.h>

namespace vdc {

namespace {

// ---------------------------------------------------------------------------
// Shared HLSL prologue

const char* kCommon = R"HLSL(
cbuffer Constants : register(b0) {
    uint  SrcWidth; uint SrcHeight; uint DstWidth; uint DstHeight;
    uint  Flags; uint Level; uint IntA; uint IntB;
    float ScaleX; float ScaleY; float ParamA; float ParamB;
    float ParamC; float ParamD; float ParamE; float ParamF;
    float4 Extra0; float4 Extra1; float4 Extra2; float4 Extra3;
};
SamplerState LinearClamp : register(s0);
SamplerState PointClamp  : register(s1);
)HLSL";

// ---------------------------------------------------------------------------
// Convert: sender texture -> RGBA8 colour (optionally resampled), luma and NVOF input.
// Flags: 1 = input is linear (encode to sRGB), 2 = resample, 4 = write NVOF input, 16 = box downsample.

const char* kConvert = R"HLSL(
Texture2D<float4>   Src      : register(t0);
RWTexture2D<float4> OutColor : register(u0);
RWTexture2D<float>  OutLuma  : register(u1);
RWTexture2D<float4> OutNvof  : register(u2);

float3 LinearToSrgb(float3 c) {
    c = saturate(c);
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 1e-6), 1.0 / 2.4) - 0.055;
    return (c < 0.0031308) ? lo : hi;
}

float4 SampleCatmullRom(float2 uv) {
    float2 texSize = float2(SrcWidth, SrcHeight);
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - texPos1;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;
    float2 texPos0 = (texPos1 - 1.0) / texSize;
    float2 texPos3 = (texPos1 + 2.0) / texSize;
    float2 texPos12 = (texPos1 + offset12) / texSize;
    float4 r = 0;
    r += Src.SampleLevel(LinearClamp, float2(texPos0.x,  texPos0.y), 0) * w0.x  * w0.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos12.x, texPos0.y), 0) * w12.x * w0.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos3.x,  texPos0.y), 0) * w3.x  * w0.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos0.x,  texPos12.y), 0) * w0.x  * w12.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos12.x, texPos12.y), 0) * w12.x * w12.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos3.x,  texPos12.y), 0) * w3.x  * w12.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos0.x,  texPos3.y), 0) * w0.x  * w3.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos12.x, texPos3.y), 0) * w12.x * w3.y;
    r += Src.SampleLevel(LinearClamp, float2(texPos3.x,  texPos3.y), 0) * w3.x  * w3.y;
    return r;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= DstWidth || id.y >= DstHeight) return;
    float4 c;
    if (Flags & 2) {
        float2 uv = (float2(id.xy) + 0.5) / float2(DstWidth, DstHeight);
        if (Flags & 16) {
            float2 srcPos = uv * float2(SrcWidth, SrcHeight);
            float2 halfExtent = float2(SrcWidth, SrcHeight) / float2(DstWidth, DstHeight) * 0.5;
            int2 lo = max(int2(floor(srcPos - halfExtent)), int2(0, 0));
            int2 hi = min(int2(ceil(srcPos + halfExtent)) - 1, int2(SrcWidth - 1, SrcHeight - 1));
            float4 sum = 0; float n = 0;
            [loop] for (int y = lo.y; y <= hi.y; ++y)
                [loop] for (int x = lo.x; x <= hi.x; ++x) { sum += Src.Load(int3(x, y, 0)); n += 1.0; }
            c = sum / max(n, 1.0);
        } else {
            c = SampleCatmullRom(uv);
        }
    } else {
        c = Src.Load(int3(id.xy, 0));
    }
    if (Flags & 1) c.rgb = LinearToSrgb(c.rgb);
    c = saturate(c);
    OutColor[id.xy] = c;
    OutLuma[id.xy] = dot(c.rgb, float3(0.299, 0.587, 0.114));
    if (Flags & 4) OutNvof[id.xy] = c;
}
)HLSL";

// ---------------------------------------------------------------------------
// Downsample: 2x2 box on a single-channel texture.

const char* kDownsample = R"HLSL(
Texture2D<float>   In  : register(t0);
RWTexture2D<float> Out : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= DstWidth || id.y >= DstHeight) return;
    int2 p = int2(id.xy) * 2;
    int2 mx = int2(SrcWidth - 1, SrcHeight - 1);
    float s = In.Load(int3(min(p, mx), 0)) + In.Load(int3(min(p + int2(1, 0), mx), 0)) +
              In.Load(int3(min(p + int2(0, 1), mx), 0)) + In.Load(int3(min(p + int2(1, 1), mx), 0));
    Out[id.xy] = s * 0.25;
}
)HLSL";

// ---------------------------------------------------------------------------
// BlockMatch: hierarchical 8x8 block matching, one thread group per block.
// Flags: 1 = full search (radius IntA), 2 = coarse predictors available, 4 = temporal predictor available,
//        8 = sub-pixel refinement. IntB = refinement radius (<= 2). ParamB = motion penalty per pixel.
// Extra0.xy = temporal grid size, Extra1.xy = coarse grid size. SrcWidth/Height = luma size at this level.
// Output vectors point from the current frame to the previous frame, in pixels of this level.

const char* kBlockMatch = R"HLSL(
Texture2D<float>    Cur      : register(t0);
Texture2D<float>    Prev     : register(t1);
Texture2D<float2>   Coarse   : register(t2);
Texture2D<float2>   Temporal : register(t3);
RWTexture2D<float2> OutMv    : register(u0);
RWTexture2D<float>  OutCost  : register(u1);

groupshared float  sCur[64];
groupshared float  sCost[64];
groupshared float  sRaw[64];
groupshared float2 sMv[64];
groupshared float  sGrid[25];

float Sad(int2 origin, int2 mv) {
    float sum = 0;
    int2 mx = int2(SrcWidth - 1, SrcHeight - 1);
    [unroll] for (int y = 0; y < 8; ++y) {
        [unroll] for (int x = 0; x < 8; ++x) {
            int2 p = clamp(origin + int2(x, y) + mv, int2(0, 0), mx);
            sum += abs(sCur[y * 8 + x] - Prev.Load(int3(p, 0)));
        }
    }
    return sum * (1.0 / 64.0);
}

void Reduce(uint tid) {
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32; s > 0; s >>= 1) {
        if (tid < s) {
            if (sCost[tid + s] < sCost[tid]) {
                sCost[tid] = sCost[tid + s];
                sRaw[tid] = sRaw[tid + s];
                sMv[tid] = sMv[tid + s];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

int2 TemporalCell(uint2 g) {
    int2 tg = int2(g) * int(1u << Level) + (int(1u << Level) >> 1);
    return clamp(tg, int2(0, 0), int2(Extra0.xy) - 1);
}

[numthreads(64, 1, 1)]
void main(uint3 gid : SV_GroupID, uint tid : SV_GroupIndex) {
    int2 origin = int2(gid.xy) * 8;
    int2 mx = int2(SrcWidth - 1, SrcHeight - 1);
    int2 p = clamp(origin + int2(tid & 7, tid >> 3), int2(0, 0), mx);
    sCur[tid] = Cur.Load(int3(p, 0));
    GroupMemoryBarrierWithGroupSync();

    float lambda = ParamB;
    float bestCost = 1e9, bestRaw = 1e9;
    float2 bestMv = 0;

    if (Flags & 1) {
        int R = int(IntA);
        int W = 2 * R + 1;
        int N = W * W;
        [loop] for (int c = int(tid); c < N; c += 64) {
            int2 mv = int2(c % W - R, c / W - R);
            float raw = Sad(origin, mv);
            float cost = raw + lambda * (abs(mv.x) + abs(mv.y));
            if (cost < bestCost) { bestCost = cost; bestRaw = raw; bestMv = mv; }
        }
        if ((Flags & 4) && tid == 0) {
            int2 mv = int2(round(Temporal.Load(int3(TemporalCell(gid.xy), 0)) / float(1u << Level)));
            float raw = Sad(origin, mv);
            float cost = raw + lambda * (abs(mv.x) + abs(mv.y));
            if (cost < bestCost) { bestCost = cost; bestRaw = raw; bestMv = mv; }
        }
        sCost[tid] = bestCost; sRaw[tid] = bestRaw; sMv[tid] = bestMv;
        Reduce(tid);
        bestCost = sCost[0]; bestRaw = sRaw[0]; bestMv = sMv[0];
    } else {
        // Phase 1: predictors (zero, parent, four parent neighbours, temporal).
        int2 cg = int2(gid.xy) >> 1;
        int2 cdim = int2(Extra1.xy);
        float2 cand = 0;
        bool valid = true;
        if (tid == 0) {
            cand = 0;
        } else if (tid <= 5 && (Flags & 2)) {
            int2 off = int2(0, 0);
            if (tid == 2) off = int2(-1, 0);
            else if (tid == 3) off = int2(1, 0);
            else if (tid == 4) off = int2(0, -1);
            else if (tid == 5) off = int2(0, 1);
            int2 q = clamp(cg + off, int2(0, 0), cdim - 1);
            cand = Coarse.Load(int3(q, 0)) * 2.0;
        } else if (tid == 6 && (Flags & 4)) {
            cand = Temporal.Load(int3(TemporalCell(gid.xy), 0)) / float(1u << Level);
        } else {
            valid = false;
        }
        int2 mvi = int2(round(cand));
        float raw = valid ? Sad(origin, mvi) : 1e9;
        sCost[tid] = valid ? raw + lambda * (abs(mvi.x) + abs(mvi.y)) : 1e9;
        sRaw[tid] = raw;
        sMv[tid] = mvi;
        Reduce(tid);
        int2 center = int2(sMv[0]);
        GroupMemoryBarrierWithGroupSync();

        // Phase 2: refine around the best predictor.
        int r = int(IntB);
        int W = 2 * r + 1;
        int N = W * W;
        bestCost = 1e9; bestRaw = 1e9; bestMv = center;
        if (int(tid) < N) {
            int2 d = int2(int(tid) % W - r, int(tid) / W - r);
            int2 mv = center + d;
            bestRaw = Sad(origin, mv);
            bestCost = bestRaw + lambda * (abs(mv.x) + abs(mv.y));
            bestMv = mv;
            sGrid[tid] = bestRaw;
        }
        sCost[tid] = bestCost; sRaw[tid] = bestRaw; sMv[tid] = bestMv;
        Reduce(tid);
        bestCost = sCost[0]; bestRaw = sRaw[0]; bestMv = sMv[0];
        if ((Flags & 8) && tid == 0) {
            int2 d = int2(bestMv) - center;
            if (abs(d.x) < r && abs(d.y) < r) {
                int ci = (d.y + r) * W + (d.x + r);
                float c0 = sGrid[ci];
                float cl = sGrid[ci - 1], cr = sGrid[ci + 1];
                float cu = sGrid[ci - W], cd = sGrid[ci + W];
                float ax = cl - 2.0 * c0 + cr;
                float ay = cu - 2.0 * c0 + cd;
                float sx = (ax > 1e-6) ? clamp(0.5 * (cl - cr) / ax, -0.5, 0.5) : 0.0;
                float sy = (ay > 1e-6) ? clamp(0.5 * (cu - cd) / ay, -0.5, 0.5) : 0.0;
                bestMv += float2(sx, sy);
            }
        }
    }
    if (tid == 0) {
        OutMv[gid.xy] = bestMv;
        OutCost[gid.xy] = bestRaw;
    }
}
)HLSL";

// ---------------------------------------------------------------------------
// MedianMv: component-wise 3x3 median over the block motion grid.

const char* kMedianMv = R"HLSL(
Texture2D<float2>   In  : register(t0);
RWTexture2D<float2> Out : register(u0);

#define S2(a, b)            { float2 t = min(a, b); b = max(a, b); a = t; }
#define MN3(a, b, c)        S2(a, b); S2(a, c);
#define MX3(a, b, c)        S2(b, c); S2(a, c);
#define MNMX3(a, b, c)      MX3(a, b, c); S2(a, b);
#define MNMX4(a, b, c, d)   S2(a, b); S2(c, d); S2(a, c); S2(b, d);
#define MNMX5(a, b, c, d, e) S2(a, b); S2(c, d); MN3(a, c, e); MX3(b, d, e);
#define MNMX6(a, b, c, d, e, f) S2(a, d); S2(b, e); S2(c, f); MN3(a, b, c); MX3(d, e, f);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= DstWidth || id.y >= DstHeight) return;
    int2 mx = int2(DstWidth - 1, DstHeight - 1);
    float2 v[9];
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            v[(y + 1) * 3 + (x + 1)] = In.Load(int3(clamp(int2(id.xy) + int2(x, y), int2(0, 0), mx), 0));
    MNMX6(v[0], v[1], v[2], v[3], v[4], v[5]);
    MNMX5(v[1], v[2], v[3], v[4], v[6]);
    MNMX4(v[2], v[3], v[4], v[7]);
    MNMX3(v[3], v[4], v[8]);
    Out[id.xy] = v[4];
}
)HLSL";

// ---------------------------------------------------------------------------
// Densify: block/NVOF motion grid -> per-pixel motion (RG16F), confidence (R8) and synthetic depth (R32F).
// Flags & 7: 1 = zero motion, 2 = block matching grid (8 px), 4 = NVIDIA Optical Flow grid (IntA px, S10.5).
// IntB = depth mode (0 flat, 1 gradient, 2 zero). ParamA = confidence threshold. SrcWidth/Height = grid size.

const char* kDensify = R"HLSL(
Texture2D<float2>   BlockMv   : register(t0);
Texture2D<float>    BlockCost : register(t1);
Texture2D<int2>     Flow      : register(t2);
Texture2D<uint>     FlowCost  : register(t3);
RWTexture2D<float2> OutMv     : register(u0);
RWTexture2D<float>  OutConf   : register(u1);
RWTexture2D<float>  OutDepth  : register(u2);

float Attenuate(float conf) {
    return saturate((conf - ParamA) / max(0.05, 1.0 - ParamA));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= DstWidth || id.y >= DstHeight) return;
    float2 mv = 0;
    float conf = 0;
    uint mode = Flags & 7;
    int2 gdim = int2(SrcWidth, SrcHeight);
    if (mode == 2 || mode == 4) {
        float cell = (mode == 2) ? 8.0 : float(IntA);
        float2 p = (float2(id.xy) + 0.5) / cell - 0.5;
        int2 i0 = int2(floor(p));
        float2 f = p - float2(i0);
        int2 a = clamp(i0, int2(0, 0), gdim - 1);
        int2 b = clamp(i0 + int2(1, 0), int2(0, 0), gdim - 1);
        int2 c = clamp(i0 + int2(0, 1), int2(0, 0), gdim - 1);
        int2 d = clamp(i0 + int2(1, 1), int2(0, 0), gdim - 1);
        float4 w = float4((1 - f.x) * (1 - f.y), f.x * (1 - f.y), (1 - f.x) * f.y, f.x * f.y);
        if (mode == 2) {
            mv = BlockMv.Load(int3(a, 0)) * w.x + BlockMv.Load(int3(b, 0)) * w.y +
                 BlockMv.Load(int3(c, 0)) * w.z + BlockMv.Load(int3(d, 0)) * w.w;
            float cost = BlockCost.Load(int3(a, 0)) * w.x + BlockCost.Load(int3(b, 0)) * w.y +
                         BlockCost.Load(int3(c, 0)) * w.z + BlockCost.Load(int3(d, 0)) * w.w;
            conf = saturate(1.0 - cost * 4.0);
        } else {
            mv = (float2(Flow.Load(int3(a, 0))) * w.x + float2(Flow.Load(int3(b, 0))) * w.y +
                  float2(Flow.Load(int3(c, 0))) * w.z + float2(Flow.Load(int3(d, 0))) * w.w) * (1.0 / 32.0);
            float cost = (float(FlowCost.Load(int3(a, 0))) * w.x + float(FlowCost.Load(int3(b, 0))) * w.y +
                          float(FlowCost.Load(int3(c, 0))) * w.z + float(FlowCost.Load(int3(d, 0))) * w.w);
            conf = saturate(1.0 - cost / 255.0);
        }
        mv *= Attenuate(conf);
        mv *= float2(ScaleX, ScaleY);
    }
    OutMv[id.xy] = mv;
    OutConf[id.xy] = conf;
    float depth = 0.5;
    if (IntB == 1) depth = lerp(0.3, 0.85, (float(id.y) + 0.5) / float(DstHeight));
    else if (IntB == 2) depth = 0.0;
    OutDepth[id.xy] = depth;
}
)HLSL";

// ---------------------------------------------------------------------------
// Stats: reduce the block cost / motion grid to a few numbers (scene-cut detection).

const char* kStats = R"HLSL(
Texture2D<float>          BlockCost : register(t0);
Texture2D<float2>         BlockMv   : register(t1);
RWStructuredBuffer<float> Out       : register(u0);

groupshared float sSum[256];
groupshared float sMax[256];
groupshared float sMag[256];

[numthreads(256, 1, 1)]
void main(uint tid : SV_GroupIndex) {
    uint n = SrcWidth * SrcHeight;
    float sum = 0, mx = 0, mag = 0;
    [loop] for (uint i = tid; i < n; i += 256) {
        int2 p = int2(i % SrcWidth, i / SrcWidth);
        float c = BlockCost.Load(int3(p, 0));
        sum += c; mx = max(mx, c);
        mag += length(BlockMv.Load(int3(p, 0)));
    }
    sSum[tid] = sum; sMax[tid] = mx; sMag[tid] = mag;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 128; s > 0; s >>= 1) {
        if (tid < s) { sSum[tid] += sSum[tid + s]; sMax[tid] = max(sMax[tid], sMax[tid + s]); sMag[tid] += sMag[tid + s]; }
        GroupMemoryBarrierWithGroupSync();
    }
    if (tid == 0) {
        float inv = 1.0 / max(float(n), 1.0);
        Out[0] = sSum[0] * inv;
        Out[1] = sMax[0];
        Out[2] = sMag[0] * inv;
        Out[3] = float(n);
    }
}
)HLSL";

// ---------------------------------------------------------------------------
// Composite: final image (for capture) and display image (compare modes, checkerboard, motion view).
// Flags: 1 = keep alpha, 2 = checkerboard, 4 = bypass, 32 = original/motion are at a different resolution.
// IntA = compare mode (0 output, 1 original, 2 wipe, 3 motion). ParamA = wipe position, ParamB = motion scale.

const char* kComposite = R"HLSL(
Texture2D<float4>   Original   : register(t0);
Texture2D<float4>   Processed  : register(t1);
Texture2D<float2>   Mv         : register(t2);
Texture2D<float>    Conf       : register(t3);
RWTexture2D<float4> OutFinal   : register(u0);
RWTexture2D<float4> OutDisplay : register(u1);

float3 Hsv(float h, float s, float v) {
    float3 k = float3(1.0, 2.0 / 3.0, 1.0 / 3.0);
    float3 p = abs(frac(h + k) * 6.0 - 3.0);
    return v * lerp(1.0, saturate(p - 1.0), s);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= DstWidth || id.y >= DstHeight) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(DstWidth, DstHeight);
    bool scaled = (Flags & 32) != 0;
    float4 orig = scaled ? Original.SampleLevel(LinearClamp, uv, 0) : Original.Load(int3(id.xy, 0));
    float4 proc = Processed.Load(int3(id.xy, 0));
    float3 outRgb = (Flags & 4) ? orig.rgb : proc.rgb;
    float alpha = (Flags & 1) ? orig.a : 1.0;
    OutFinal[id.xy] = float4(outRgb, alpha);

    float3 rgb = outRgb;
    float a = orig.a;
    uint mode = IntA;
    if (mode == 1) {
        rgb = orig.rgb;
    } else if (mode == 2) {
        rgb = (uv.x < ParamA) ? orig.rgb : outRgb;
    } else if (mode == 3) {
        float2 mv = scaled ? Mv.SampleLevel(LinearClamp, uv, 0) : Mv.Load(int3(id.xy, 0));
        float c = scaled ? Conf.SampleLevel(LinearClamp, uv, 0) : Conf.Load(int3(id.xy, 0));
        float len = length(mv) * ParamB;
        float hue = atan2(mv.y, mv.x) / 6.2831853 + 0.5;
        rgb = Hsv(hue, saturate(len), 1.0) * lerp(0.35, 1.0, c);
        a = 1.0;
    }
    if ((Flags & 2) && mode != 3) {
        float ch = (((id.x >> 4) + (id.y >> 4)) & 1) ? 0.30 : 0.22;
        rgb = lerp(ch.xxx, rgb, a);
    }
    OutDisplay[id.xy] = float4(rgb, 1.0);
}
)HLSL";

struct ShaderSource { ShaderId id; const char* name; const char* body; };
const ShaderSource kSources[] = {
    { ShaderId::Convert,    "Convert",    kConvert },
    { ShaderId::Downsample, "Downsample", kDownsample },
    { ShaderId::BlockMatch, "BlockMatch", kBlockMatch },
    { ShaderId::MedianMv,   "MedianMv",   kMedianMv },
    { ShaderId::Densify,    "Densify",    kDensify },
    { ShaderId::Stats,      "Stats",      kStats },
    { ShaderId::Composite,  "Composite",  kComposite },
};

} // namespace

bool Shaders::Compile(Device& device, ShaderId id, const char* name, const char* body, std::wstring& error) {
    std::string source = kCommon;
    source += body;
    ComPtr<ID3DBlob> code, errors;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS;
    HRESULT hr = D3DCompile(source.data(), source.size(), name, nullptr, nullptr, "main", "cs_5_1", flags, 0,
                            &code, &errors);
    if (FAILED(hr)) {
        std::string msg = errors ? std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize()) : FormatHr(hr);
        Log::Error("Shader %s failed to compile: %s", name, msg.c_str());
        error = L"Shader compilation failed: " + Utf8ToWide(name);
        return false;
    }
    if (errors && errors->GetBufferSize() > 1)
        Log::Warn("Shader %s: %s", name, (const char*)errors->GetBufferPointer());

    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_rootSignature.Get();
    pd.CS.pShaderBytecode = code->GetBufferPointer();
    pd.CS.BytecodeLength = code->GetBufferSize();
    hr = device.D3D12()->CreateComputePipelineState(&pd, IID_PPV_ARGS(&m_pso[(UINT)id]));
    if (FAILED(hr)) {
        Log::Hr(LogLevel::Error, StrPrintf("CreateComputePipelineState(%s)", name).c_str(), hr);
        error = L"Pipeline creation failed: " + Utf8ToWide(name);
        return false;
    }
    m_pso[(UINT)id]->SetName(Utf8ToWide(name).c_str());
    return true;
}

bool Shaders::Init(Device& device, std::wstring& error) {
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 8;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 4;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 32;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (UINT i = 0; i < 2; ++i) {
        samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 3;
    rs.pParameters = params;
    rs.NumStaticSamplers = 2;
    rs.pStaticSamplers = samplers;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errBlob);
    if (FAILED(hr)) {
        Log::Error("D3D12SerializeRootSignature: %s", errBlob ? (const char*)errBlob->GetBufferPointer() : "");
        error = L"Root signature serialization failed";
        return false;
    }
    hr = device.D3D12()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                             IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr)) { error = L"CreateRootSignature failed: " + Utf8ToWide(FormatHr(hr)); return false; }

    for (const ShaderSource& s : kSources)
        if (!Compile(device, s.id, s.name, s.body, error)) return false;
    Log::Info("Compiled %u compute shaders", (unsigned)(sizeof(kSources) / sizeof(kSources[0])));
    return true;
}

void Shaders::Shutdown() {
    for (auto& p : m_pso) p.Reset();
    m_rootSignature.Reset();
}

void Shaders::Dispatch(ID3D12GraphicsCommandList* cmd, Device& device, const DispatchDesc& d) {
    ID3D12PipelineState* pso = m_pso[(UINT)d.id].Get();
    if (!pso) return;
    DescriptorPair table = device.AllocFrame(12);
    if (!table.Valid()) return;
    ID3D12Device* dev = device.D3D12();
    const UINT inc = device.SrvDescriptorSize();
    for (UINT i = 0; i < 8; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dst{ table.cpu.ptr + (SIZE_T)i * inc };
        D3D12_CPU_DESCRIPTOR_HANDLE src = d.srv[i].ptr ? d.srv[i] : device.NullSrv();
        dev->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    for (UINT i = 0; i < 4; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dst{ table.cpu.ptr + (SIZE_T)(8 + i) * inc };
        D3D12_CPU_DESCRIPTOR_HANDLE src = d.uav[i].ptr ? d.uav[i] : device.NullUav();
        dev->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu{ table.gpu.ptr + (UINT64)8 * inc };
    cmd->SetComputeRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(pso);
    cmd->SetComputeRoot32BitConstants(0, 32, &d.constants, 0);
    cmd->SetComputeRootDescriptorTable(1, table.gpu);
    cmd->SetComputeRootDescriptorTable(2, uavGpu);
    cmd->Dispatch(d.groupsX, d.groupsY, d.groupsZ);
}

} // namespace vdc
