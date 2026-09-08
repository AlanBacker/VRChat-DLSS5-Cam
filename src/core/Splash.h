#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vdc {

// The start-up card: a small layered window with the program's icon, name and version, a status line and a
// sweeping bar. It is shown while the program initialises and fades away once the main window has drawn its first
// frame. It runs on its own thread with its own message loop, so nothing the start-up does can stall it.
class Splash {
public:
    ~Splash();
    void Show(HINSTANCE instance, bool light, const std::string& version, bool prerelease, const std::string& status);
    void SetStatus(const std::string& utf8);
    void Close();   // starts the fade-out; returns at once
    void Join();    // waits for the thread to finish (after Close)
    void SetDumpPath(const std::wstring& bmp);   // development: writes one frame of the card as a 32-bit BMP

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void ThreadMain();
    void BuildBase();
    void Paint();

    HINSTANCE   m_instance = nullptr;
    HWND        m_hwnd = nullptr;
    std::thread m_thread;
    std::mutex  m_mutex;
    std::wstring m_status, m_version;
    bool        m_light = false;
    bool        m_prerelease = false;
    std::wstring m_dump;
    bool        m_dumped = false;
    std::atomic<bool> m_closing{false};
    double      m_start = 0.0, m_closeAt = -1.0;
    int         m_w = 0, m_h = 0, m_margin = 0, m_panelW = 0, m_panelH = 0;
    float       m_scale = 1.0f;
    int         m_x = 0, m_y = 0;
    std::vector<uint32_t> m_base;      // the still part of the card, premultiplied BGRA
    std::vector<uint8_t>  m_inside;    // 255 where the card is fully opaque (GDI text lands there)
    HBITMAP     m_dib = nullptr;
    uint32_t*   m_bits = nullptr;
    HDC         m_dc = nullptr;
    HFONT       m_titleFont = nullptr, m_textFont = nullptr, m_smallFont = nullptr;
    HICON       m_icon = nullptr;
    HGDIOBJ     m_oldBitmap = nullptr;
};

} // namespace vdc
