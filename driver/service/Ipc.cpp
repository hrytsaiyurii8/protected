#include "Ipc.h"
#include <string>
#include <sstream>

// Minimal JSON parser — enough for our simple protocol
static std::string GetJsonString(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

static int GetJsonInt(const std::string& json, const std::string& key, int def = 0)
{
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return def;
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ')) ++pos;
    return std::stoi(json.substr(pos));
}

static bool GetJsonBool(const std::string& json, const std::string& key, bool def = false)
{
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return def;
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ')) ++pos;
    return json.substr(pos, 4) == "true";
}

static IpcCommand ParseCommand(const std::string& line)
{
    IpcCommand cmd;
    std::string type = GetJsonString(line, "cmd");
    if (type == "set_region") {
        cmd.type = IpcCommand::SetRegion;
        cmd.x = GetJsonInt(line, "x");
        cmd.y = GetJsonInt(line, "y");
        cmd.w = GetJsonInt(line, "w");
        cmd.h = GetJsonInt(line, "h");
    } else if (type == "set_image") {
        cmd.type = IpcCommand::SetImage;
        std::string p = GetJsonString(line, "path");
        // Unescape backslashes
        std::wstring wp;
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '\\' && i + 1 < p.size() && p[i+1] == '\\') { wp += L'\\'; ++i; }
            else wp += (wchar_t)(unsigned char)p[i];
        }
        cmd.imagePath = wp;
    } else if (type == "enable") {
        cmd.type = IpcCommand::Enable;
        cmd.enabled = GetJsonBool(line, "on");
    } else if (type == "status") {
        cmd.type = IpcCommand::Status;
    }
    return cmd;
}

// ── IpcServer ─────────────────────────────────────────────────────────────────
bool IpcServer::Start(Handler handler)
{
    m_handler = handler;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_thread = std::thread([this]{ RunLoop(); });
    return true;
}

void IpcServer::Stop()
{
    m_stop = true;
    if (m_stopEvent != INVALID_HANDLE_VALUE) SetEvent(m_stopEvent);
    if (m_thread.joinable()) m_thread.join();
    if (m_stopEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(m_stopEvent);
        m_stopEvent = INVALID_HANDLE_VALUE;
    }
}

void IpcServer::RunLoop()
{
    while (!m_stop) {
        HANDLE pipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 0, nullptr);

        if (pipe == INVALID_HANDLE_VALUE) { Sleep(500); continue; }

        // Wait for a client or stop signal
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ConnectNamedPipe(pipe, &ov);

        HANDLE waitHandles[] = { ov.hEvent, m_stopEvent };
        DWORD r = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        CloseHandle(ov.hEvent);

        if (r != WAIT_OBJECT_0) { CloseHandle(pipe); break; }

        HandleClient(pipe);
        CloseHandle(pipe);
    }
}

void IpcServer::HandleClient(HANDLE pipe)
{
    char buf[8192];
    std::string leftover;
    while (!m_stop) {
        DWORD read = 0;
        BOOL ok = ReadFile(pipe, buf, sizeof(buf) - 1, &read, nullptr);
        if (!ok || read == 0) break;
        buf[read] = '\0';
        leftover += buf;

        size_t pos;
        while ((pos = leftover.find('\n')) != std::string::npos) {
            std::string line = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);
            if (line.empty()) continue;

            IpcCommand cmd = ParseCommand(line);
            std::string reply = m_handler(cmd);
            reply += "\n";
            DWORD written = 0;
            WriteFile(pipe, reply.c_str(), (DWORD)reply.size(), &written, nullptr);
        }
    }
}
