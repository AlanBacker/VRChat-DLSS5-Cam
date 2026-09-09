// VRChat DLSS5 Cam - FSR host: an FSR 3.1 upscaling context through AMD's FidelityFX API (amd_fidelityfx_dx12.dll).
//
// On a Radeon card the neural pass is done by DLSS-NR-on-AMD, a separate program that attaches to the FidelityFX
// API of a DirectX 12 process running FSR and applies its result to the upscaler's output. This class makes the
// app such a process: an FSR context at the neural pass size (1:1 unless neural upscaling is on), dispatched with
// the same colour, depth and motion vectors the NGX route hands to the NVIDIA runtime. Without that program the
// dispatch is plain FSR at native size, and the picture comes out close to the input.
#pragma once
#include "gfx/Device.h"
#include <cstdint>
#include <string>

namespace vdc {

class FsrHost {
public:
    struct Inputs {
        ID3D12Resource* color = nullptr;    // R8G8B8A8_UNORM, display-referred (sRGB), non-pixel shader resource
        ID3D12Resource* depth = nullptr;    // R32_FLOAT, inverted (1 = near)
        ID3D12Resource* mvec = nullptr;     // RG16_FLOAT, pixels from the current frame to the previous one
        ID3D12Resource* output = nullptr;   // R8G8B8A8_UNORM, unordered access
        bool  reset = false;
        float frameTimeMs = 16.7f;
    };

    // Loads amd_fidelityfx_dx12.dll from the executable's folder and asks it for the FSR versions it provides. The
    // module stays loaded for the rest of the process: a program attached to it keeps its hooks there.
    bool Load(ID3D12Device* device, const std::wstring& exeDir, std::string& error);
    bool Loaded() const { return m_fn.dispatch != nullptr; }
    const std::string&  Version() const { return m_version; }   // "FSR 3.1.4"
    const std::wstring& Path() const { return m_path; }

    bool Create(ID3D12Device* device, UINT inW, UINT inH, UINT outW, UINT outH, std::string& error);
    bool Created() const { return m_ctx != nullptr; }
    void Release();   // the GPU must be idle
    UINT InputWidth() const { return m_inW; }
    UINT InputHeight() const { return m_inH; }
    UINT OutputWidth() const { return m_outW; }
    UINT OutputHeight() const { return m_outH; }

    bool Dispatch(ID3D12GraphicsCommandList* cmd, const Inputs& in, std::string& error);
    UINT64 DispatchCount() const { return m_dispatches; }

    // The file name of a DLSS-NR-on-AMD module in this process: one carrying its name, or one of the proxy names
    // it installs under (version, winmm, dbghelp, wininet, winhttp, dxgi) loaded from the executable's folder
    // instead of from Windows. Empty when none is loaded.
    static std::string PortModule(const std::wstring& exeDir);

private:
    struct Fn { void* create = nullptr; void* destroy = nullptr; void* configure = nullptr; void* query = nullptr; void* dispatch = nullptr; };
    Fn           m_fn;
    HMODULE      m_module = nullptr;
    std::wstring m_path;
    std::string  m_version;
    void*        m_ctx = nullptr;
    UINT         m_inW = 0, m_inH = 0, m_outW = 0, m_outH = 0;
    UINT64       m_dispatches = 0;
};

} // namespace vdc
