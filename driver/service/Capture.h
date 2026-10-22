#pragma once
#include <windows.h>
#include <dxgi1_5.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// Captures one frame from the primary display using DXGI Desktop Duplication.
// Thread-safe for one producer thread.
class DesktopCapture {
public:
    struct Frame {
        ComPtr<ID3D11Texture2D> Texture; // BGRA, GPU-resident
        UINT Width  = 0;
        UINT Height = 0;
    };

    bool Init(ID3D11Device* device);
    // Returns true when a new frame is available; fills |out|.
    // Caller must call ReleaseFrame() after consuming.
    bool AcquireFrame(Frame& out, UINT timeoutMs = 16);
    void ReleaseFrame();
    void Shutdown();

    ID3D11Device*        Device()  const { return m_device.Get(); }
    ID3D11DeviceContext* Context() const { return m_ctx.Get(); }

private:
    ComPtr<ID3D11Device>            m_device;
    ComPtr<ID3D11DeviceContext>     m_ctx;
    ComPtr<IDXGIOutputDuplication>  m_dup;
    bool m_frameHeld = false;
};
