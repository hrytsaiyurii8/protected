#include "Service.h"
#include <d3d11.h>
#include <dxgi1_5.h>
#include <sstream>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ── Singleton ─────────────────────────────────────────────────────────────────
DisplayService& DisplayService::Instance()
{
    static DisplayService s;
    return s;
}

// ── Start / Stop ──────────────────────────────────────────────────────────────
bool DisplayService::Start()
{
    if (m_thread.joinable()) return true; // already running

    // Create D3D11 device (shared between capture and compositor)
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, nullptr, 0, D3D11_SDK_VERSION,
        &m_device, nullptr, &m_ctx);
    if (FAILED(hr)) return false;

    if (!m_capture.Init(m_device.Get())) return false;
    if (!m_compositor.Init(m_device.Get())) return false;

    m_ipc.Start([this](const IpcCommand& cmd) {
        return HandleCommand(cmd);
    });

    m_stop = false;
    m_thread = std::thread([this]{ RunLoop(); });
    return true;
}

void DisplayService::Stop()
{
    m_stop = true;
    m_ipc.Stop();
    if (m_thread.joinable()) m_thread.join();
    m_compositor.Shutdown();
    m_capture.Shutdown();
    m_ctx.Reset();
    m_device.Reset();
}

// ── Control (called from IPC or Python) ──────────────────────────────────────
void DisplayService::SetRegion(int x, int y, int w, int h)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_region = { x, y, w, h };
}

void DisplayService::SetDecoyImage(const std::wstring& path)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_imagePath  = path;
    m_imageDirty = true;
}

void DisplayService::SetEnabled(bool on) { m_enabled = on; }

// ── IPC command handler ───────────────────────────────────────────────────────
std::string DisplayService::HandleCommand(const IpcCommand& cmd)
{
    switch (cmd.type) {
    case IpcCommand::SetRegion:
        SetRegion(cmd.x, cmd.y, cmd.w, cmd.h);
        return R"({"ok":true})";
    case IpcCommand::SetImage:
        SetDecoyImage(cmd.imagePath);
        return R"({"ok":true})";
    case IpcCommand::Enable:
        SetEnabled(cmd.enabled);
        return R"({"ok":true})";
    case IpcCommand::Status: {
        std::ostringstream ss;
        ss << R"({"ok":true,"enabled":)" << (m_enabled ? "true" : "false") << "}";
        return ss.str();
    }
    default:
        return R"({"ok":false,"error":"unknown command"})";
    }
}

// ── Main render loop ──────────────────────────────────────────────────────────
void DisplayService::RunLoop()
{
    // Find the virtual display output (created by the IDD driver).
    // We enumerate all DXGI outputs and pick the one whose DeviceName contains
    // our virtual monitor (which Windows assigns as \\.\DISPLAY2 or similar).
    // For now we output to the first available swap-chain surface exported
    // by the IDD driver via a shared texture handle stored in a named mapping.

    // The IDD driver writes frames it needs to the virtual display automatically
    // via IddCxSwapChainReleaseAndAcquireBuffer. Our job here is to push
    // composed frames into a shared surface that the IDD driver's swap-chain
    // processor reads from.
    //
    // We use a named shared memory + event to hand off frames:
    //   Mapping name:  "Local\\ScrDisplayFrame"
    //   Event name:    "Local\\ScrDisplayFrameReady"
    //   Layout:        [UINT width][UINT height][BGRA pixels...]

    const size_t kBufMax = 1920 * 1080 * 4 + 8;
    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, (DWORD)kBufMax, L"Local\\ScrDisplayFrame");
    HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, L"Local\\ScrDisplayFrameReady");
    if (!hMap || !hEvent) return;

    BYTE* buf = reinterpret_cast<BYTE*>(MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, kBufMax));
    if (!buf) { CloseHandle(hMap); CloseHandle(hEvent); return; }

    // Staging texture to read pixels back from GPU → CPU → shared memory
    ComPtr<ID3D11Texture2D> staging;

    while (!m_stop) {
        DesktopCapture::Frame frame;
        if (!m_capture.AcquireFrame(frame, 16)) {
            Sleep(1);
            continue;
        }

        ComPtr<ID3D11Texture2D> composed;
        if (m_enabled) {
            Region reg;
            std::wstring imgPath;
            bool dirty;
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                reg     = m_region;
                imgPath = m_imagePath;
                dirty   = m_imageDirty;
                m_imageDirty = false;
            }
            if (dirty) m_compositor.SetDecoyImage(imgPath);
            composed = m_compositor.Composite(frame.Texture.Get(), reg);
        } else {
            composed = frame.Texture;
        }

        // Copy GPU → staging → shared memory
        if (composed) {
            D3D11_TEXTURE2D_DESC td = {};
            composed->GetDesc(&td);
            UINT W = td.Width, H = td.Height;

            // (Re)create staging texture if dimensions changed or not yet created
            D3D11_TEXTURE2D_DESC stagingDesc = {};
            if (staging) staging->GetDesc(&stagingDesc);
            if (!staging || stagingDesc.Width != W || stagingDesc.Height != H) {
                staging.Reset();
                D3D11_TEXTURE2D_DESC sd = td;
                sd.Usage          = D3D11_USAGE_STAGING;
                sd.BindFlags      = 0;
                sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                sd.MiscFlags      = 0;
                m_device->CreateTexture2D(&sd, nullptr, &staging);
            }
            if (staging) {
                m_ctx->CopyResource(staging.Get(), composed.Get());
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (SUCCEEDED(m_ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                    UINT* header = reinterpret_cast<UINT*>(buf);
                    header[0] = W; header[1] = H;
                    BYTE* dst = buf + 8;
                    const BYTE* src = reinterpret_cast<const BYTE*>(mapped.pData);
                    for (UINT row = 0; row < H; ++row)
                        memcpy(dst + row * W * 4, src + row * mapped.RowPitch, W * 4);
                    m_ctx->Unmap(staging.Get(), 0);
                    SetEvent(hEvent);
                }
            }
        }

        m_capture.ReleaseFrame();
    }

    UnmapViewOfFile(buf);
    CloseHandle(hMap);
    CloseHandle(hEvent);
}

// ── DLL exports ───────────────────────────────────────────────────────────────
extern "C" {
    BOOL  SvcStart()                          { return DisplayService::Instance().Start() ? TRUE : FALSE; }
    void  SvcStop()                           { DisplayService::Instance().Stop(); }
    void  SvcSetRegion(int x,int y,int w,int h){ DisplayService::Instance().SetRegion(x,y,w,h); }
    void  SvcSetImage(const wchar_t* path)    { DisplayService::Instance().SetDecoyImage(path); }
    void  SvcSetEnabled(BOOL on)              { DisplayService::Instance().SetEnabled(on != 0); }
    BOOL  SvcIsEnabled()                      { return DisplayService::Instance().IsEnabled() ? TRUE : FALSE; }
}
