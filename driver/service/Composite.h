#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

struct Region { int x, y, w, h; };

// Composites a decoy image over a captured desktop frame.
// Keeps the decoy texture cached; reloads only when the path changes.
class Compositor {
public:
    bool Init(ID3D11Device* device);

    // Replace |region| in |desktop| with the loaded decoy image.
    // Returns a new GPU texture (same size as desktop) with the region replaced.
    ComPtr<ID3D11Texture2D> Composite(ID3D11Texture2D* desktop,
                                      const Region& region);

    void SetDecoyImage(const std::wstring& path);
    void Shutdown();

private:
    bool LoadDecoy(const std::wstring& path);
    void DrawDecoyOver(ID3D11Texture2D* dst, const Region& region);

    ComPtr<ID3D11Device>            m_device;
    ComPtr<ID3D11DeviceContext>     m_ctx;
    ComPtr<ID3D11Texture2D>         m_decoy;      // GPU copy of decoy image
    ComPtr<ID3D11ShaderResourceView> m_decoySRV;
    ComPtr<ID3D11Texture2D>         m_output;     // reused output texture
    ComPtr<ID3D11RenderTargetView>  m_outputRTV;
    ComPtr<ID3D11VertexShader>      m_vs;
    ComPtr<ID3D11PixelShader>       m_ps;
    ComPtr<ID3D11SamplerState>      m_sampler;
    ComPtr<ID3D11Buffer>            m_vb;         // full-screen quad
    ComPtr<ID3D11InputLayout>       m_layout;
    ComPtr<ID3D11BlendState>        m_blend;

    std::wstring m_decoyPath;
    UINT m_outW = 0, m_outH = 0;

    bool EnsureOutput(UINT w, UINT h, DXGI_FORMAT fmt);
    bool CompileShaders();
};
