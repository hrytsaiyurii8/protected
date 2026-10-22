#pragma once
#include "Capture.h"
#include "Composite.h"
#include "Ipc.h"
#include <atomic>
#include <mutex>
#include <thread>

// Main service: ties together capture, composite, virtual display feed, and IPC.
// Call Start() once from the Scraper Python layer (via ctypes LoadLibrary).
class DisplayService {
public:
    static DisplayService& Instance();

    bool Start();   // called once; spawns background thread
    void Stop();

    // Called by IPC handler on any thread
    void SetRegion(int x, int y, int w, int h);
    void SetDecoyImage(const std::wstring& path);
    void SetEnabled(bool on);
    bool IsEnabled() const { return m_enabled; }

private:
    DisplayService() = default;
    void RunLoop();
    std::string HandleCommand(const IpcCommand& cmd);

    DesktopCapture m_capture;
    Compositor     m_compositor;
    IpcServer      m_ipc;

    std::thread      m_thread;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_enabled{ false };

    std::mutex m_mutex;
    Region     m_region{};
    std::wstring m_imagePath;
    bool       m_imageDirty = false;

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_ctx;
};

// DLL export — called from Python via ctypes
extern "C" {
    __declspec(dllexport) BOOL  SvcStart();
    __declspec(dllexport) void  SvcStop();
    __declspec(dllexport) void  SvcSetRegion(int x, int y, int w, int h);
    __declspec(dllexport) void  SvcSetImage(const wchar_t* path);
    __declspec(dllexport) void  SvcSetEnabled(BOOL on);
    __declspec(dllexport) BOOL  SvcIsEnabled();
}
