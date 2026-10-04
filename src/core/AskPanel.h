// VRChat DLSS5 Cam - the Ask AI panel: the documentation's AI Q&A (Mintlify's widget) in a WebView2 control that the
// main window docks at its right edge. The control and its browser processes are created at the first opening only,
// so nothing is started and nothing is sent to Mintlify before the user opens the panel. The page is the program's
// own (built in AskPanel.cpp), served under the documentation site's address so the widget sees the site it belongs
// to; every link leaves for the default browser, the control itself never navigates away. Interface thread only.
#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;
struct ICoreWebView2WebResourceRequestedEventArgs;

namespace vdc {

// What the page shows: its language, theme and text (the program's strings for the interface's language).
struct AskPageConfig {
    std::string mintLang;    // Mintlify's language code: en, cn, jp, ko
    std::string dir;         // the documentation site's folder of that language: "" (English), "zh/", "ja/", "ko/"
    std::string htmlLang;    // en, zh-Hans, ja, ko
    bool        light = false;
    std::string font;        // a CSS font-family list (the interface's fonts)
    float       radius = 10.0f;   // the corner radius of the interface's cards, in CSS pixels
    std::string title, trigger, placeholder, disclaimer, suggestions;
    std::vector<std::string> questions;
    bool        test = false;     // test runs: the page reports an answer's text (the start of it) for the log
};

class AskPanel {
public:
    enum class State { Idle, Creating, Loading, Ready, Failed };
    struct Event {
        enum Type { Ready, LoadFailed, Close, Link, Answered, Escape, Fullscreen, CreateFailed, Log } type;
        std::string text;
    };

    AskPanel() = default;
    AskPanel(const AskPanel&) = delete;
    AskPanel& operator=(const AskPanel&) = delete;
    ~AskPanel() { Destroy(); }

    static bool RuntimeVersion(std::string& version);    // the WebView2 Runtime on this computer (false: none)
    static std::string PageUrl();                        // the page's address
    static bool IsPageUrl(const std::string& url);        // an address of the page's own folder
    // A PNG (from CapturePng) as 8-bit RGBA rows, top row first.
    static bool DecodePng(const std::vector<uint8_t>& png, std::vector<uint8_t>& rgba, int& width, int& height);

    // Starts creating the control in "parent" (asynchronous; the outcome comes as an event). False when it cannot even
    // start (the reason is in "error").
    bool Start(HWND parent, const std::wstring& userDataFolder, const AskPageConfig& cfg, uint32_t background,
               std::string& error);
    void Retry();                                         // after a failure: the page again (the control again if it is gone)
    void Configure(const AskPageConfig& cfg, uint32_t background);   // the theme or the language changed
    void SetBounds(const RECT& r);                        // in the parent's client pixels
    void Show(bool focus, bool instant = false);          // on screen; the page fades its content in (instant: at once)
    void BeginHide();                                     // the page fades its content out (Hide() after kHideFade)
    void Hide();                                          // off the screen
    void Focus();                                         // keyboard focus to the page
    void Ask(const std::string& question);                // asks a question (test runs)
    void Test(const char* what);                          // test runs: "link" clicks an answer's first link, "close" the widget's close control
    bool SendKey(UINT vk);                                // test runs: a key press posted to the page's window
    void NotifyMoved();                                   // the parent window moved on the screen
    bool CapturePng(std::function<void(std::vector<uint8_t>&&)> done);   // the page as shown, as a PNG (empty: failed)
    void Destroy();
    std::vector<Event> Poll();                            // once a frame: what happened since, and the waits that ran out

    State GetState() const { return m_state; }
    bool  Visible() const { return m_visible; }
    bool  Focused() const { return m_focused; }
    bool  Created() const { return m_controller != nullptr; }

    static constexpr double kHideFade = 0.13;            // the page's fade-out (120 ms) and a frame

private:
    void OnEnvironment(HRESULT hr, ICoreWebView2Environment* env, unsigned gen);
    void OnController(HRESULT hr, ICoreWebView2Controller* controller, unsigned gen);
    void Serve(ICoreWebView2WebResourceRequestedEventArgs* args);
    void Navigate();
    void Fail(bool creating, const std::string& why);
    void OnMessage(const std::string& json);
    void Post(const std::string& json);
    std::string PageHtml() const;
    std::string ConfigJson() const;
    void ApplyBackground();
    void FocusBack();
    void Push(Event::Type type, std::string text = std::string()) { m_events.push_back({ type, std::move(text) }); }

    HWND m_parent = nullptr;
    std::wstring m_folder;
    ICoreWebView2Environment* m_env = nullptr;
    ICoreWebView2Controller*  m_controller = nullptr;
    ICoreWebView2*            m_view = nullptr;
    State         m_state = State::Idle;
    double        m_since = 0.0;        // when the current creation or page load began
    AskPageConfig m_cfg;
    uint32_t      m_background = 0;     // the panel's colour behind the page while it loads (0xRRGGBB)
    RECT          m_bounds{};
    bool          m_visible = false;
    bool          m_focused = false;
    bool          m_pageAlive = false;  // the page's script runs (it said hello)
    bool          m_lost = false;       // the browser process ended: the control goes at the next Poll()
    bool          m_reload = false;     // the page's process ended: the page opens again at the next Poll()
    std::vector<std::string> m_queued;  // messages that wait for the page's script
    std::vector<Event> m_events;
    unsigned      m_generation = 0;     // Destroy() makes the callbacks of an earlier control fall silent
};

} // namespace vdc
