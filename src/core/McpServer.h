// VRChat DLSS5 Cam - MCP (Model Context Protocol) server: an AI assistant works the program the way the interface does.
//
// The server speaks the Streamable HTTP transport of MCP on 127.0.0.1 (POST /mcp with JSON-RPC 2.0), and the program
// started with --mcp is a stdio bridge to it, so the usual client configuration ("command": this executable,
// "args": ["--mcp"]) works with Claude Desktop, Claude Code, Cursor and every other client. Every tool call travels
// to the interface thread and runs there between the interface's own draw and its events: what the assistant does
// shows in the window, enters the undo history and is saved like a change made by hand.
#pragma once
#include "core/Json.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

// A tool call on its way from the server thread to the interface thread, and its answer on the way back.
struct McpCall {
    std::string name;
    Json        args;
    Json        content = Json::Arr();      // the MCP content items of the answer (text, image)
    Json        structured;                 // structuredContent, when an object
    bool        isError = false;
    double      started = 0.0;              // NowSeconds() when the server took the call

    void Text(const std::string& t);        // adds a text item
    void Json_(const Json& j);              // adds the JSON as an indented text item and as the structured content
    void Image(const std::vector<uint8_t>& png);   // adds a PNG image item
    void Fail(const std::string& t);        // the answer is an error with this text; finished
    void Finish();                          // the answer is complete
    bool Wait(double seconds);              // server thread: true when the answer arrived in time
    bool Done() const;
private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;
    bool                    m_done = false;
};

struct McpTool {
    const char* name;
    const char* description;
    const char* schema;        // the JSON schema of the arguments
    bool        mutating;      // changes settings, files or the run: refused in read-only mode
};

// A setting the assistant may read and write, with what the interface knows about it.
struct McpSettingInfo {
    const char* key;
    const char* group;
    const char* type;          // bool, int, float, string, enum
    const char* range;         // "0..2", "1024..65535", the enum values "0 = ..., 1 = ..."; empty for a string
    const char* description;
};

class McpServer {
public:
    struct Status {
        bool        running = false;
        int         port = 0;
        std::string error;             // why it is not running (the port is taken, ...)
        unsigned    calls = 0;
        std::string lastTool;
        double      lastTime = -1.0;   // NowSeconds() of the last call, -1 = none yet
    };
    using Dispatch = std::function<void(std::shared_ptr<McpCall>)>;   // hands a call to the interface thread (called on a server thread)

    ~McpServer();
    bool Start(int port, Dispatch dispatch);
    void Stop();
    bool Running() const { return m_running.load(); }
    void SetReadOnly(bool v) { m_readOnly.store(v); }
    void SetToken(const std::string& token);
    Status Get() const;

    static const std::vector<McpTool>& Tools();
    static const std::vector<McpSettingInfo>& SettingInfos();
    static const McpSettingInfo* FindSetting(const std::string& key);
    static int  DefaultPort() { return 51550; }
    static std::string Url(int port);                                    // http://127.0.0.1:<port>/mcp
    static std::string ClientConfig(const std::wstring& exePath);        // the JSON block for an MCP client's configuration file
    static std::string Base64(const uint8_t* data, size_t size);

    // The stdio bridge (VRChatDLSS5Cam.exe --mcp): JSON-RPC lines on stdin and stdout, relayed to the running program's
    // server; a program is started when none answers. Returns the process exit code.
    static int BridgeMain(const std::wstring& exePath, int port, const std::wstring& dataDir, const std::wstring& passThrough);
    // The window message a bridge posts to a running program that has no server yet: wParam = the port to listen on.
    static constexpr unsigned kWakeMessage = 0x8000 + 41;   // WM_APP + 41
    static const wchar_t* WindowClass();

private:
    struct Request {
        std::string method, path, query, body, origin, auth, accept;
        bool keepAlive = true;
    };
    struct Response {
        int         status = 200;
        std::string type = "application/json";
        std::string body;
        std::string extraHeaders;
    };
    void ListenMain();
    void Connection(uintptr_t socket);
    bool ReadRequest(uintptr_t socket, Request& r);
    void Handle(const Request& r, Response& out);
    Json HandleRpc(const Json& msg, bool& noReply);
    Json CallTool(const std::string& name, const Json& args, Json* rpcError);
    Json ReadResource(const std::string& uri, Json* rpcError);
    std::string InfoPage();
    void NoteCall(const std::string& tool);

    std::atomic<bool>        m_running{false};
    std::atomic<bool>        m_quit{false};
    std::atomic<bool>        m_readOnly{false};
    uintptr_t                m_listen = ~(uintptr_t)0;
    int                      m_port = 0;
    Dispatch                 m_dispatch;
    std::thread              m_thread;
    mutable std::mutex       m_mutex;
    std::string              m_token;
    std::string              m_error;
    unsigned                 m_calls = 0;
    std::string              m_lastTool;
    double                   m_lastTime = -1.0;
    std::atomic<int>         m_live{0};      // connection threads still running
    std::vector<uintptr_t>   m_clientSockets;
    bool                     m_wsa = false;
};

} // namespace vdc
