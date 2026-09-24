// VRChat DLSS5 Cam - receives the VRChat "Spout Stream" camera texture into a D3D12 resource (via D3D11On12).
#pragma once
#include "core/SourceFrame.h"
#include "gfx/Device.h"
#include <mutex>
#include <string>
#include <vector>

// Spout2 (BSD-2-Clause) - see third_party/spout/LICENSE
#include "SpoutDX.h"

namespace vdc {

class SpoutReceiver {
public:
    bool Init(Device& device);
    void Shutdown(GpuContext& gpu);

    // Empty name = automatic (prefers the VRChat sender, otherwise the active Spout sender). Thread-safe.
    void SetRequestedSender(const std::string& name);

    // Called by the processing thread. gpu is the processing context (the D3D11On12 device runs on its queue).
    //  changed: the receiving texture was (re)created (size, format or sender changed) - dependent resources must be rebuilt.
    //  fresh:   a new frame was copied into the texture during this call.
    // Returns true when a texture with valid content is available.
    bool Receive(GpuContext& gpu, bool& changed, bool& fresh);

    // The current texture as a pipeline source (processing thread).
    SourceFrame Frame() const;

    bool                        Connected() const { return m_connected && m_tex12; }
    ID3D12Resource*             Texture() const { return m_tex12.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE Srv() const { return m_srv; }
    UINT                        Width() const { return m_width; }
    UINT                        Height() const { return m_height; }
    DXGI_FORMAT                 Format() const { return m_format; }
    DXGI_FORMAT                 ViewFormat() const { return m_viewFormat; }
    bool                        LinearInput() const { return m_linear; }
    const std::string&          SenderName() const { return m_senderName; }
    double                      SenderFps() const { return m_senderFps; }
    UINT64                      FreshFrames() const { return m_freshFrames; }
    bool                        HasFrame() const { return m_freshFrames > 0; }

    std::vector<std::string>    EnumerateSenders();

private:
    bool CreateTexture(GpuContext& gpu, UINT w, UINT h, DXGI_FORMAT fmt);
    void ReleaseTexture(GpuContext& gpu);
    void ApplySenderName(const char* name);

    // New-frame test for a sender without Spout's frame count: a small D3D11 device of its own hashes the sender's
    // shared texture on the GPU, and a received frame counts as new only when the hash changed. Its own device keeps
    // the test out of the processing queue, which may still be busy with the previous frame.
    bool ProbeOpen(Device& device);   // for the sender just connected; false: the test is not available
    void ProbeClose();
    void ProbeStart();
    int  ProbePoll();                 // -1: no answer yet, 0: the same picture, 1: a new picture (or the test failed)

    spoutDX                    m_spout;
    bool                       m_open = false;
    std::mutex                 m_requestMutex;
    std::string                m_requestedShared;  // user request ("" = auto), written by the UI thread
    std::string                m_requested;        // copy used by the processing thread
    std::string                m_applied;     // name currently given to Spout ("" = active sender)
    std::string                m_senderName;
    double                     m_lastScan = 0.0;
    double                     m_lastFrameTime = 0.0;
    double                     m_senderFps = 0.0;
    double                     m_uncountedNext = 0.0;           // a sender without Spout's frame count: the next check
    double                     m_uncountedInterval = 1.0 / 60.0;

    ComPtr<ID3D11Device>              m_probeDevice;
    ComPtr<ID3D11DeviceContext>       m_probeContext;
    ComPtr<ID3D11ComputeShader>       m_probeShader;
    ComPtr<ID3D11Buffer>              m_probeSums, m_probeReadback, m_probeConstants;
    ComPtr<ID3D11UnorderedAccessView> m_probeUav;
    ComPtr<ID3D11Texture2D>           m_probeTexture;              // the sender's texture, opened on the probe device
    ComPtr<ID3D11ShaderResourceView>  m_probeSrv;
    UINT                              m_probeWidth = 0, m_probeHeight = 0;
    bool                              m_probeUnavailable = false;  // the device or the shader could not be made
    bool                              m_probeReady = false;        // the test runs for the current sender
    bool                              m_probePending = false;      // a hash is on its way back
    bool                              m_probeKnown = false;        // m_probeLast holds the last picture's hash
    uint64_t                          m_probeLast = 0;
    double                            m_probeRateStart = 0.0;      // the sender's rate, measured from the new pictures found
    UINT                              m_probeRateCount = 0;

    ComPtr<ID3D12Resource>     m_tex12;
    ComPtr<ID3D11Texture2D>    m_tex11;       // wrapped view of m_tex12
    D3D12_CPU_DESCRIPTOR_HANDLE m_srv{};
    UINT                       m_width = 0, m_height = 0;
    DXGI_FORMAT                m_format = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT                m_viewFormat = DXGI_FORMAT_UNKNOWN;
    bool                       m_linear = false;
    bool                       m_connected = false;
    UINT64                     m_freshFrames = 0;
};

} // namespace vdc
