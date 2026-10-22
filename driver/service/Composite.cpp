#include "Composite.h"
#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

// ── Minimal HLSL shaders compiled at runtime ─────────────────────────────────
static const char kVS[] = R"(
struct VS_IN  { float2 pos : POSITION; float2 uv : TEXCOORD; };
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
VS_OUT main(VS_IN i) {
    VS_OUT o;
    o.pos = float4(i.pos, 0, 1);
    o.uv  = i.uv;
    return o;
}
)";

static const char kPS[] = R"(
Texture2D    tex : register(t0);
SamplerState smp : register(s0);
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
float4 main(VS_OUT i) : SV_TARGET { return tex.Sample(smp, i.uv); }
)";

struct Vertex { float x, y, u, v; };

bool Compositor::Init(ID3D11Device* device)
{
    m_device = device;
    device->GetImmediateContext(&m_ctx);
    return CompileShaders();
}

bool Compositor::CompileShaders()
{
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    if (FAILED(D3DCompile(kVS, sizeof(kVS)-1, nullptr, nullptr, nullptr,
            "main", "vs_5_0", 0, 0, &vsBlob, &err))) return false;
    if (FAILED(D3DCompile(kPS, sizeof(kPS)-1, nullptr, nullptr, nullptr,
            "main", "ps_5_0", 0, 0, &psBlob, &err))) return false;

    if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(), nullptr, &m_vs))) return false;
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(), nullptr, &m_ps))) return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(m_device->CreateInputLayout(layout, 2,
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_layout)))
        return false;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    m_device->CreateSamplerState(&sd, &m_sampler);

    // Full-screen quad (NDC)
    Vertex quad[] = {
        {-1, 1, 0, 0}, {1, 1, 1, 0}, {-1,-1, 0, 1},
        {-1,-1, 0, 1}, {1, 1, 1, 0}, { 1,-1, 1, 1},
    };
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(quad);
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd2 = { quad };
    m_device->CreateBuffer(&bd, &sd2, &m_vb);

    return true;
}

bool Compositor::EnsureOutput(UINT w, UINT h, DXGI_FORMAT fmt)
{
    if (m_outW == w && m_outH == h && m_output) return true;
    m_output.Reset(); m_outputRTV.Reset();
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage     = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&td, nullptr, &m_output))) return false;
    if (FAILED(m_device->CreateRenderTargetView(m_output.Get(), nullptr, &m_outputRTV))) return false;
    m_outW = w; m_outH = h;
    return true;
}

void Compositor::SetDecoyImage(const std::wstring& path)
{
    if (path == m_decoyPath) return;
    m_decoyPath = path;
    m_decoy.Reset(); m_decoySRV.Reset();
    LoadDecoy(path);
}

bool Compositor::LoadDecoy(const std::wstring& path)
{
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(&conv);
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);

    std::vector<BYTE> pixels(w * h * 4);
    conv->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage     = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = { pixels.data(), w * 4 };
    if (FAILED(m_device->CreateTexture2D(&td, &init, &m_decoy))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    return SUCCEEDED(m_device->CreateShaderResourceView(m_decoy.Get(), &srvd, &m_decoySRV));
}

ComPtr<ID3D11Texture2D> Compositor::Composite(ID3D11Texture2D* desktop,
                                               const Region& region)
{
    D3D11_TEXTURE2D_DESC dd = {};
    desktop->GetDesc(&dd);

    if (!EnsureOutput(dd.Width, dd.Height, dd.Format)) return nullptr;

    // 1. Copy entire desktop to output
    m_ctx->CopyResource(m_output.Get(), desktop);

    // If no decoy loaded or region empty, return as-is
    if (!m_decoySRV || region.w <= 0 || region.h <= 0) return m_output;

    // 2. Draw decoy image into the region using a viewport scissor
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (float)std::max(0, region.x);
    vp.TopLeftY = (float)std::max(0, region.y);
    vp.Width    = (float)std::min((int)dd.Width  - region.x, region.w);
    vp.Height   = (float)std::min((int)dd.Height - region.y, region.h);
    vp.MinDepth = 0; vp.MaxDepth = 1;

    UINT stride = sizeof(Vertex), offset = 0;
    m_ctx->IASetInputLayout(m_layout.Get());
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
    m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    m_ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    m_ctx->PSSetShaderResources(0, 1, m_decoySRV.GetAddressOf());
    m_ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    m_ctx->RSSetViewports(1, &vp);
    m_ctx->OMSetRenderTargets(1, m_outputRTV.GetAddressOf(), nullptr);
    m_ctx->Draw(6, 0);

    // Unbind RTV
    ID3D11RenderTargetView* nullRTV = nullptr;
    m_ctx->OMSetRenderTargets(1, &nullRTV, nullptr);

    return m_output;
}

void Compositor::Shutdown()
{
    m_decoy.Reset(); m_decoySRV.Reset();
    m_output.Reset(); m_outputRTV.Reset();
    m_vs.Reset(); m_ps.Reset(); m_sampler.Reset();
    m_vb.Reset(); m_layout.Reset();
    m_ctx.Reset(); m_device.Reset();
}
