// VRChat DLSS5 Cam - hosts the DLSS 5 neural renderer (DLSSNR, NGX feature 18) from a user-supplied nvngx_dlssnr.dll.
// The runtime itself is not distributed with this application.
#pragma once
#include "ngx/NgxCore.h"
#include <string>

namespace vdc {

class DlssnrFeature {
public:
    struct Params {
        int   preset = 0;            // DLSSNR.Hint.Render.Preset 0..3
        int   style = 0;             // DLSSNR.Style 0..2
        float intensity = 1.0f;
        float globalTone = 1.0f;
        float localTone = 1.0f;
        float localStructure = 1.0f;
        float skinStructure = -1.0f; // < 0 = runtime default
        bool  autoMask = false;
        bool  uiCorrection = false;
    };
    struct Inputs {
        ID3D12Resource* color = nullptr;   // NON_PIXEL_SHADER_RESOURCE, input size
        ID3D12Resource* depth = nullptr;   // R32F, input size
        ID3D12Resource* mvec = nullptr;    // RG16F, input size, pixels, current -> previous
        ID3D12Resource* output = nullptr;  // UNORDERED_ACCESS, output size
        bool            reset = false;
    };

    ~DlssnrFeature() { UnloadRuntime(); }

    // Loads nvngx_dlssnr.dll from dllPath (LoadLibraryEx), installs the caller-compatibility shim and calls Init_Ext.
    bool LoadRuntime(ID3D12Device* device, const std::wstring& dllPath, const std::wstring& appDir, std::string& error);
    void UnloadRuntime();
    bool RuntimeLoaded() const { return m_module != nullptr && m_snippetInitialized; }
    const std::wstring& RuntimePath() const { return m_runtimePath; }
    const std::string& RuntimeVersion() const { return m_runtimeVersion; }

    // useCore = create feature 18 through the NGX core instead of the snippet exports (experimental).
    bool Create(NgxCore& core, ID3D12GraphicsCommandList* cmd, UINT inW, UINT inH, UINT outW, UINT outH,
                int preset, bool useCore, std::string& error);
    void Release(NgxCore& core);
    bool Created() const { return m_feature != nullptr; }
    bool Evaluate(ID3D12GraphicsCommandList* cmd, const Inputs& in, const Params& p, std::string& error);

    UINT InputWidth() const { return m_inW; }
    UINT InputHeight() const { return m_inH; }
    UINT OutputWidth() const { return m_outW; }
    UINT OutputHeight() const { return m_outH; }
    bool Upscaling() const { return m_inW != m_outW || m_inH != m_outH; }
    UINT64 EvaluateCount() const { return m_evaluateCount; }
    UINT64 FailureCount() const { return m_failureCount; }

private:
    bool InstallCallerShim(std::string& error);
    void RemoveCallerShim();

    HMODULE              m_module = nullptr;
    std::wstring         m_runtimePath;
    std::string          m_runtimeVersion;
    ID3D12Device*        m_device = nullptr;
    bool                 m_snippetInitialized = false;
    void**               m_iatSlot = nullptr;
    bool                 m_shimInstalled = false;

    NVSDK_NGX_Parameter* m_params = nullptr;
    NVSDK_NGX_Handle*    m_feature = nullptr;
    bool                 m_useCore = false;
    UINT                 m_inW = 0, m_inH = 0, m_outW = 0, m_outH = 0;
    UINT64               m_evaluateCount = 0;
    UINT64               m_failureCount = 0;
};

} // namespace vdc
