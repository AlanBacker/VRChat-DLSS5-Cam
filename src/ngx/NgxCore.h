// VRChat DLSS5 Cam - NVIDIA NGX core (nvsdk_ngx static library + driver NGX runtime) wrapper.
// This software contains source code provided by NVIDIA Corporation.
#pragma once
#include "gfx/Device.h"
#include <string>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

namespace vdc {

class NgxCore {
public:
    bool Init(Device& device, const std::wstring& exeDir, const std::wstring& appDataDir);
    void Shutdown();

    bool Initialized() const { return m_initialized; }
    bool DlssAvailable() const { return m_dlssAvailable; }
    bool NeedsDriverUpdate() const { return m_needsDriverUpdate; }
    const std::string& Status() const { return m_status; }
    ID3D12Device* DeviceD3D12() const { return m_device; }

    NVSDK_NGX_Parameter* AllocateParameters(std::string& error);
    void DestroyParameters(NVSDK_NGX_Parameter* params);

    static const char* ResultName(NVSDK_NGX_Result r);

private:
    ID3D12Device*        m_device = nullptr;
    NVSDK_NGX_Parameter* m_caps = nullptr;
    std::wstring         m_exeDir;
    std::string          m_status;
    bool                 m_initialized = false;
    bool                 m_dlssAvailable = false;
    bool                 m_needsDriverUpdate = false;
};

} // namespace vdc
