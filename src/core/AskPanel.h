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
    // the notice over a question that could not be answered: why (the service off or out of its allowance, too many
    // questions, no connection, a conversation too long), and its buttons
    std::string noticeOff, noticeBusy, noticeNet, noticeFull, openDocs, retry, newChat, close;
    bool        test = false;     // test runs: the page reports an answer's text (the start of it) for the log
    bool        breakLoad = false;   // development: the widget's script is asked for where there is none (the load fails)
};

class AskPanel {
public:
    enum class State { Idle, Creating, Loading, Ready, Failed };
    struct Event {
        enum Type { Ready, LoadFailed, Close, Link, Answered, Escape, Fullscreen, CreateFailed, Log, Docs } type;
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
    // Where the page goes: over "card" (the parent's client pixels), its window as large as "canvasW" x "canvasH" with
    // its bottom-right corner at the card's, so that resizing the card never resizes the window (see Place()). "radius"
    // is the card's corner radius in pixels.
    void Place(const RECT& card, int canvasW, int canvasH, int radius);
    void Show(bool focus, bool instant = false);          // on screen; the page fades its content in (instant: at once)
    void BeginHide();                                     // the page fades its content out (Hide() after kHideFade)
    void Hide();                                          // off the screen
    void Focus();                                         // keyboard focus to the page
    void Ask(const std::string& question);                // asks a question (test runs)
    void Test(const char* what);                          // test runs: "link" clicks an answer's first link, "close" the widget's close control,
                                                          // "fail:<status|net|off>" answers every question with that failure (nothing is sent),
                                                          // "note:<docs|retry|fresh|close>" presses that button on the failed-question notice
    bool SendKey(UINT vk);                                // test runs: a key press posted to the page's window
    void NotifyMoved();                                   // the parent window moved on the screen
    struct Hole {
        RECT r; int radius;
        bool operator==(const Hole& o) const { return EqualRect(&r, &o.r) && radius == o.radius; }
    };
    // Holes in the page where the interface's tooltips and popups lie over it, in pixels from the card's top-left
    // corner; none: the whole card shows the page.
    void SetHoles(const std::vector<Hole>& holes);
    RECT Canvas() const { return m_bounds; }              // the page's window (the parent's client pixels)
    RECT Shown() const { return m_shownClient; }          // the part of it on screen (the card inside its hairline)
    int  ShownRadius() const { return m_shownRadius; }    // and that part's corner radius
    bool CapturePng(std::function<void(std::vector<uint8_t>&&)> done);   // the page as shown, as a PNG (empty: failed)
    void Destroy();
    std::vector<Event> Poll();                            // once a frame: what happened since, and the waits that ran out

    State GetState() const { return m_state; }
    bool  Visible() const { return m_visible; }
    bool  Focused() const { return m_focused; }
    bool  Created() const { return m_controller != nullptr; }

    static constexpr double kHideFade = 0.13;            // the page's fade-out (120 ms) and a frame
    double traceBoundsMs = 0.0, traceHolesMs = 0.0;       // --frame-trace: time in put_Bounds and SetWindowRgn (added up)

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
    HWND RegionHost();
    void ApplyRegion();
    void SendSize();
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
    std::vector<Hole> m_holes;          // the holes the interface asks for
    HWND          m_holeHost = nullptr; // the window that hosts the page in this process (its region is cut)
    RECT          m_card{};             // the card this frame and the one before (the parent's client pixels)
    RECT          m_cardBefore{};
    bool          m_cardKnown = false;
    int           m_radius = 0;
    int           m_sizeW = 0, m_sizeH = 0;   // the card's size as told to the page (0: not yet)
    bool          m_sizeSent = false;
    RECT          m_shownClient{};      // the region as last set: its rectangle (client pixels), corner, holes
    int           m_shownRadius = 0;
    RECT          m_rgnRect{};
    POINT         m_rgnOrigin{};
    std::vector<Hole> m_rgnHoles;
    unsigned      m_generation = 0;     // Destroy() makes the callbacks of an earlier control fall silent
};

} // namespace vdc
