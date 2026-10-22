#include "Capture.h"
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

bool DesktopCapture::Init(ID3D11Device* device)
{
    m_device = device;
    device->GetImmediateContext(&m_ctx);

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice),
            reinterpret_cast<void**>(dxgiDevice.GetAddressOf()))))
        return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;

    // Use the first output (primary monitor)
    ComPtr<IDXGIOutput> output;
    if (FAILED(adapter->EnumOutputs(0, &output))) return false;

    ComPtr<IDXGIOutput1> output1;
    if (FAILED(output.As(&output1))) return false;

    HRESULT hr = output1->DuplicateOutput(device, &m_dup);
    return SUCCEEDED(hr);
}

bool DesktopCapture::AcquireFrame(Frame& out, UINT timeoutMs)
{
    if (m_frameHeld) ReleaseFrame();

    DXGI_OUTDUPL_FRAME_INFO info = {};
    ComPtr<IDXGIResource> resource;
    if (!m_dup) {
        // Attempt reinit after a previous failure (e.g. resolution switch)
        Init(m_device.Get());
        if (!m_dup) return false;
    }

    HRESULT hr = m_dup->AcquireNextFrame(timeoutMs, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (FAILED(hr)) {
        // Output changed — will reinitialise on next call
        m_dup.Reset();
        return false;
    }
    m_frameHeld = true;

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(resource.As(&tex))) { ReleaseFrame(); return false; }

    D3D11_TEXTURE2D_DESC desc = {};
    tex->GetDesc(&desc);
    out.Texture = tex;
    out.Width   = desc.Width;
    out.Height  = desc.Height;
    return true;
}

void DesktopCapture::ReleaseFrame()
{
    if (m_frameHeld && m_dup) {
        m_dup->ReleaseFrame();
        m_frameHeld = false;
    }
}

void DesktopCapture::Shutdown()
{
    ReleaseFrame();
    m_dup.Reset();
    m_ctx.Reset();
    m_device.Reset();
}
