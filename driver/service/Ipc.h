#pragma once
#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

// Named-pipe IPC server.
// Python client connects, sends JSON commands, server dispatches callbacks.
//
// Protocol: newline-terminated UTF-8 JSON lines.
// Commands:
//   {"cmd":"set_region","x":0,"y":0,"w":400,"h":300}
//   {"cmd":"set_image","path":"C:\\...\\decoy.png"}
//   {"cmd":"enable","on":true}
//   {"cmd":"status"}   -> replies {"ok":true,"enabled":true}

static constexpr wchar_t kPipeName[] =
    L"\\\\.\\pipe\\ScrDisplaySvc_v1";

struct IpcCommand {
    enum Type { SetRegion, SetImage, Enable, Status, Unknown } type = Unknown;
    int x = 0, y = 0, w = 0, h = 0;
    std::wstring imagePath;
    bool enabled = false;
};

class IpcServer {
public:
    using Handler = std::function<std::string(const IpcCommand&)>;

    bool Start(Handler handler);
    void Stop();
    ~IpcServer() { Stop(); }

private:
    void RunLoop();
    void HandleClient(HANDLE pipe);

    Handler          m_handler;
    std::thread      m_thread;
    std::atomic<bool> m_stop{ false };
    HANDLE           m_stopEvent = INVALID_HANDLE_VALUE;
};
