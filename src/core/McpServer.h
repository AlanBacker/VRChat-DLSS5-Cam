// VRChat DLSS5 Cam - MCP (Model Context Protocol) server: an AI assistant works the program the way the interface does,
// and other computers' assistants (chat bots, for example) send it files to process through a job queue.
//
// The server speaks the Streamable HTTP transport of MCP (POST /mcp with JSON-RPC 2.0) on 127.0.0.1, or on every
// address of this computer when the user opens it to the local network; the program started with --mcp is a stdio
// bridge to it, so the usual client configuration ("command": this executable, "args": ["--mcp"]) works with Claude
// Desktop, Claude Code, Cursor and every other client. Every tool call travels to the interface thread and runs there
// between the interface's own draw and its events: what the assistant does shows in the window, enters the undo
// history and is saved like a change made by hand.
//
// Access: a client on this computer needs no key (unless the user says so); every other client carries one of the
// keys the user created (Authorization: Bearer <key>). A key has a role: viewer (looks), jobs (sends files to the
// queue and fetches the results) or admin (everything, as the interface).
#pragma once
#include "core/Json.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

enum McpRole { McpRoleViewer = 0, McpRoleJobs = 1, McpRoleAdmin = 2 };

// A client's key, as the user created it in the sidebar (mcp-keys.json in the settings folder).
struct McpKey {
    std::string name;          // what the user calls the client ("qq-bot-1")
    std::string secret;        // the key itself
    int         role = McpRoleJobs;
    long long   created = 0;   // Unix seconds
    long long   lastUsed = 0;  // Unix seconds, 0 = never
    unsigned    calls = 0;
};

// Who is calling: the outcome of the request's authorization, carried with every call.
struct McpCaller {
    int         role = McpRoleAdmin;
    std::string keyName;       // the key's name, "local" for a keyless client on this computer, "token" for the legacy token
    std::string peer;          // the client's address
    std::string host;          // what the client called this server (the Host header): the download links use it
    bool        local = true;  // the client is on this computer
};

// A tool call on its way from the server thread to the interface thread, and its answer on the way back.
struct McpCall {
    std::string name;
    Json        args;
    McpCaller   caller;
    Json        content = Json::Arr();      // the MCP content items of the answer (text, image)
    Json        structured;                 // structuredContent, when an object
    bool        isError = false;
    double      started = 0.0;              // NowSeconds() when the server took the call

    void Text(const std::string& t);        // adds a text item
    void Json_(const Json& j);              // adds the JSON as an indented text item and as the structured content
    void Image(const std::vector<uint8_t>& bytes, const char* mime = "image/png");   // adds an image item
    void Fail(const std::string& t);        // the answer is an error with this text; finished
    void Fail(const std::string& t, const Json& detail);   // ... with structured detail (a reason, a retry time)
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
    bool        jobs;          // the jobs role may call it in full (submit, upload, jobs, ...)
    bool        hidden;        // not listed: the server's own internal calls (_upload, _download)
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
        int         bind = 0;                  // 0 = this computer only, 1 = the local network
        std::string error;                     // why it is not running (the port is taken, ...)
        unsigned    calls = 0;
        std::string lastTool;
        std::string lastKey;                   // who made the last call
        double      lastTime = -1.0;           // NowSeconds() of the last call, -1 = none yet
        int         connections = 0;           // open right now
        int         pending = 0;               // calls waiting for the interface
    };
    struct Access {
        std::vector<McpKey> keys;
        std::string legacyToken;               // settings.ini mcpToken: an admin key
        bool        localNoKey = true;
        bool        readOnly = false;          // everyone is a viewer
    };
    // A key's use since the last take, for the sidebar and mcp-keys.json.
    struct KeyUse { std::string name; long long lastUsed = 0; unsigned calls = 0; };
    using Dispatch = std::function<void(std::shared_ptr<McpCall>)>;   // hands a call to the interface thread (called on a server thread)

    ~McpServer();
    bool Start(int port, int bind, Dispatch dispatch);
    void Stop();
    bool Running() const { return m_running.load(); }
    void SetAccess(const Access& access);
    void SetUploads(const std::wstring& dir, uint64_t maxBytes);   // where POST /upload streams to, and how large it may be
    Status Get() const;
    std::vector<KeyUse> TakeKeyUse();

    static const std::vector<McpTool>& Tools();
    static const std::vector<McpSettingInfo>& SettingInfos();
    static const McpSettingInfo* FindSetting(const std::string& key);
    static int  DefaultPort() { return 51550; }
    static std::string Url(int port);                                    // http://127.0.0.1:<port>/mcp
    static std::string Url(const std::string& host, int port);           // http://<host>:<port>/mcp
    static std::vector<std::string> LocalAddresses();                    // the IPv4 addresses of this computer's adapters
    static std::string ClientConfig(const std::wstring& exePath);        // the JSON block for an MCP client's configuration file (the bridge)
    static std::string ClientConfigRemote(const std::string& url, const std::string& key);   // ... for a client elsewhere (direct HTTP with a key)
    static std::string Base64(const uint8_t* data, size_t size);
    static bool Base64Decode(const std::string& text, std::vector<uint8_t>& out);
    static const char* RoleName(int role);
    static int  RoleFromName(const std::string& name);                   // -1 when unknown
    static std::string NewSecret();                                      // a fresh random key
    static bool LoadKeys(const std::wstring& file, std::vector<McpKey>& out);
    static bool SaveKeys(const std::wstring& file, const std::vector<McpKey>& keys);

    // The stdio bridge (VRChatDLSS5Cam.exe --mcp): JSON-RPC lines on stdin and stdout, relayed to the running program's
    // server; a program is started when none answers. With a URL (--mcp-url) the bridge talks to a server elsewhere
    // and starts nothing. Returns the process exit code.
    static int BridgeMain(const std::wstring& exePath, int port, const std::wstring& dataDir, const std::wstring& passThrough,
                          const std::string& url, const std::string& key);
    // The window message a bridge posts to a running program that has no server yet: wParam = the port to listen on.
    static constexpr unsigned kWakeMessage = 0x8000 + 41;   // WM_APP + 41
    static const wchar_t* WindowClass();
    static constexpr int kMaxConnections = 64;   // open connections at most: beyond, a request is answered 503 at once
    static constexpr int kMaxPending = 32;       // calls waiting for the interface at most: beyond, a call is answered busy at once

private:
    struct Request {
        std::string method, path, query, body, origin, auth, accept, host, peer;
        std::wstring bodyFile;                 // POST /upload: the body went to this file instead
        uint64_t     bodyBytes = 0;
        bool keepAlive = true;
        bool local = true;
    };
    struct Response {
        int         status = 200;
        std::string type = "application/json";
        std::string body;
        std::string extraHeaders;
        std::wstring filePath;                 // a file streamed as the body (downloads)
    };
    void ListenMain();
    void Connection(uintptr_t socket, const std::string& peer, bool local);
    bool ReadRequest(uintptr_t socket, Request& r);
    void Handle(const Request& r, Response& out);
    bool Authorize(const Request& r, McpCaller& caller, Response& out);
    Json HandleRpc(const Json& msg, const McpCaller& caller, bool& noReply);
    Json CallTool(const std::string& name, const Json& args, const McpCaller& caller, Json* rpcError);
    Json ReadResource(const std::string& uri, const McpCaller& caller, Json* rpcError);
    std::string InfoPage(const Request& r, const McpCaller& caller);
    void NoteCall(const std::string& tool, const std::string& key);

    std::atomic<bool>        m_running{false};
    std::atomic<bool>        m_quit{false};
    uintptr_t                m_listen = ~(uintptr_t)0;
    int                      m_port = 0;
    int                      m_bind = 0;
    Dispatch                 m_dispatch;
    std::thread              m_thread;
    mutable std::mutex       m_mutex;
    Access                   m_access;
    std::wstring             m_uploadDir;
    uint64_t                 m_uploadMax = 0;
    std::string              m_error;
    unsigned                 m_calls = 0;
    std::string              m_lastTool, m_lastKey;
    double                   m_lastTime = -1.0;
    std::map<std::string, KeyUse> m_keyUse;
    std::atomic<int>         m_live{0};      // connection threads still running
    std::atomic<int>         m_pending{0};   // calls handed to the interface, not answered yet
    std::vector<uintptr_t>   m_clientSockets;
    bool                     m_wsa = false;
};

} // namespace vdc
