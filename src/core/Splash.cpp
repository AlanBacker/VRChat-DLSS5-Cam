#include "core/Splash.h"
#include "core/Util.h"
#include "../../resources/resource.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace vdc {
namespace {

constexpr int   kPanelW = 420, kPanelH = 196, kMargin = 28, kRadius = 14, kIcon = 56;
constexpr double kFadeIn = 0.20, kFadeOut = 0.24, kSweep = 1.6;

struct Rgb { float r, g, b; };
constexpr Rgb kPanelDark   { 27.f, 29.f, 36.f },  kPanelLight   { 252.f, 252.f, 253.f };
constexpr Rgb kBorderDark  { 62.f, 66.f, 78.f },  kBorderLight  { 214.f, 216.f, 222.f };
constexpr Rgb kAccent      { 96.f, 140.f, 255.f };

inline uint32_t Premul(float r, float g, float b, float a) {
    const int A = (int)std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    const int R = (int)std::lround(std::clamp(r, 0.0f, 255.0f) * (float)A / 255.0f);
    const int G = (int)std::lround(std::clamp(g, 0.0f, 255.0f) * (float)A / 255.0f);
    const int B = (int)std::lround(std::clamp(b, 0.0f, 255.0f) * (float)A / 255.0f);
    return ((uint32_t)A << 24) | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
}

// Blends a straight colour with coverage over a premultiplied pixel.
inline void BlendOver(uint32_t& dst, float r, float g, float b, float a) {
    if (a <= 0.0f) return;
    a = std::min(a, 1.0f);
    const float da = (float)(dst >> 24), dr = (float)((dst >> 16) & 255), dg = (float)((dst >> 8) & 255), db = (float)(dst & 255);
    const float k = 1.0f - a;
    const int A = (int)std::lround(a * 255.0f + da * k);
    const int R = (int)std::lround(r * a + dr * k);
    const int G = (int)std::lround(g * a + dg * k);
    const int B = (int)std::lround(b * a + db * k);
    dst = ((uint32_t)std::clamp(A, 0, 255) << 24) | ((uint32_t)std::clamp(R, 0, 255) << 16) | ((uint32_t)std::clamp(G, 0, 255) << 8) | (uint32_t)std::clamp(B, 0, 255);
}

// Signed distance from a point to a rounded rectangle (negative inside).
inline float RoundedRectDistance(float px, float py, float x0, float y0, float x1, float y1, float r) {
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float hx = (x1 - x0) * 0.5f - r, hy = (y1 - y0) * 0.5f - r;
    const float qx = std::fabs(px - cx) - hx, qy = std::fabs(py - cy) - hy;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

inline float Smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

} // namespace

Splash::~Splash() { Close(); Join(); }

void Splash::Show(HINSTANCE instance, bool light, const std::string& version, bool prerelease, const std::string& status) {
    if (m_thread.joinable()) return;
    m_instance = instance;
    m_light = light;
    m_prerelease = prerelease;
    m_version = Utf8ToWide(version);
    m_status = Utf8ToWide(status);
    m_closing = false;
    m_thread = std::thread([this]() { ThreadMain(); });
}

void Splash::SetStatus(const std::string& utf8) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = Utf8ToWide(utf8);
}

void Splash::Close() { m_closing = true; }

void Splash::SetDumpPath(const std::wstring& bmp) { m_dump = bmp; }

void Splash::Join() { if (m_thread.joinable()) m_thread.join(); }

LRESULT CALLBACK Splash::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Splash* self = (Splash*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        return TRUE;
    case WM_TIMER:
        if (self) self->Paint();
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void Splash::ThreadMain() {
    const wchar_t* cls = L"VRChatDLSS5CamSplash";
    WNDCLASSW wc{};
    wc.lpfnWndProc = &Splash::WndProc;
    wc.hInstance = m_instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    const UINT dpi = GetDpiForSystem();
    m_scale = (float)dpi / 96.0f;
    m_margin = (int)std::lround(kMargin * m_scale);
    m_panelW = (int)std::lround(kPanelW * m_scale);
    m_panelH = (int)std::lround(kPanelH * m_scale);
    m_w = m_panelW + m_margin * 2;
    m_h = m_panelH + m_margin * 2;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    m_x = work.left + ((work.right - work.left) - m_w) / 2;
    m_y = work.top + ((work.bottom - work.top) - m_h) / 2 - (int)(24 * m_scale);

    m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls, L"VRChat DLSS5 Cam",
                             WS_POPUP, m_x, m_y, m_w, m_h, nullptr, nullptr, m_instance, this);
    if (!m_hwnd) { UnregisterClassW(cls, m_instance); return; }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = m_w;
    bi.bmiHeader.biHeight = -m_h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    m_dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    m_dib = CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    m_bits = (uint32_t*)bits;
    if (!m_dib || !m_bits) { DestroyWindow(m_hwnd); m_hwnd = nullptr; DeleteDC(m_dc); UnregisterClassW(cls, m_instance); return; }
    m_oldBitmap = SelectObject(m_dc, m_dib);
    auto font = [&](float px, int weight) {
        return CreateFontW(-(int)std::lround(px * m_scale), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    m_titleFont = font(22.0f, FW_SEMIBOLD);
    m_textFont = font(13.5f, FW_NORMAL);
    m_smallFont = font(12.5f, FW_NORMAL);
    const int iconPx = (int)std::lround(kIcon * m_scale);
    m_icon = (HICON)LoadImageW(m_instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, iconPx, iconPx, LR_DEFAULTCOLOR);

    BuildBase();
    m_start = NowSeconds();
    Paint();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(m_hwnd, 1, 15, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (m_icon) DestroyIcon(m_icon);
    if (m_oldBitmap) SelectObject(m_dc, m_oldBitmap);
    if (m_dib) DeleteObject(m_dib);
    if (m_dc) DeleteDC(m_dc);
    for (HFONT f : { m_titleFont, m_textFont, m_smallFont }) if (f) DeleteObject(f);
    m_hwnd = nullptr;
    UnregisterClassW(cls, m_instance);
}

// The still part of the card: shadow, panel, border, icon, name and version.
void Splash::BuildBase() {
    m_base.assign((size_t)m_w * m_h, 0);
    m_inside.assign((size_t)m_w * m_h, 0);
    const Rgb panel = m_light ? kPanelLight : kPanelDark;
    const Rgb border = m_light ? kBorderLight : kBorderDark;
    const float x0 = (float)m_margin, y0 = (float)m_margin, x1 = (float)(m_margin + m_panelW), y1 = (float)(m_margin + m_panelH);
    const float r = kRadius * m_scale;
    const float shadowReach = (float)m_margin;
    const float shadowStrength = m_light ? 0.22f : 0.55f;
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            const float d = RoundedRectDistance(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r);
            uint32_t& px = m_base[(size_t)y * m_w + x];
            if (d > 0.0f) {
                // Shadow: darkest at the edge, gone at the margin; shifted a little downward.
                const float ds = RoundedRectDistance(x + 0.5f, y + 0.5f - 6.0f * m_scale, x0, y0, x1, y1, r);
                const float t = std::clamp(1.0f - ds / shadowReach, 0.0f, 1.0f);
                px = Premul(0.0f, 0.0f, 0.0f, shadowStrength * t * t * t);
                continue;
            }
            const float cover = std::clamp(0.5f - d, 0.0f, 1.0f);
            const float edge = std::clamp(1.0f + d, 0.0f, 1.0f);   // 1 within the outer pixel: the border
            px = Premul(panel.r + (border.r - panel.r) * edge, panel.g + (border.g - panel.g) * edge, panel.b + (border.b - panel.b) * edge, cover);
            if (d < -1.5f) m_inside[(size_t)y * m_w + x] = 255;
        }
    }
    // The icon, name and version are drawn with GDI, which drops the alpha of the pixels it writes; they are all
    // over the opaque part of the card, so the alpha is put back afterwards.
    std::copy(m_base.begin(), m_base.end(), m_bits);
    GdiFlush();
    const int pad = (int)std::lround(28 * m_scale);
    const int iconPx = (int)std::lround(kIcon * m_scale);
    const int iconX = m_margin + pad, iconY = m_margin + pad;
    if (m_icon) DrawIconEx(m_dc, iconX, iconY, m_icon, iconPx, iconPx, 0, nullptr, DI_NORMAL);
    SetBkMode(m_dc, TRANSPARENT);
    const COLORREF text = m_light ? RGB(30, 33, 40) : RGB(242, 243, 245);
    const COLORREF dim = m_light ? RGB(107, 114, 128) : RGB(154, 160, 171);
    RECT rc{ iconX + iconPx + (int)std::lround(18 * m_scale), iconY + (int)std::lround(2 * m_scale), m_margin + m_panelW - pad, iconY + iconPx };
    SelectObject(m_dc, m_titleFont);
    SetTextColor(m_dc, text);
    DrawTextW(m_dc, L"VRChat DLSS5 Cam", -1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    rc.top += (int)std::lround(30 * m_scale);
    SelectObject(m_dc, m_smallFont);
    SetTextColor(m_dc, dim);
    const std::wstring ver = L"Version " + m_version;
    DrawTextW(m_dc, ver.c_str(), -1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
    if (m_prerelease) {
        // A small rounded badge after the version: this build is a pre-release.
        RECT measure = rc;
        DrawTextW(m_dc, ver.c_str(), -1, &measure, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
        const wchar_t* tag = L"Pre-release";
        RECT tagRc{ 0, 0, 0, 0 };
        DrawTextW(m_dc, tag, -1, &tagRc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
        const int padX = (int)std::lround(6 * m_scale), padY = (int)std::lround(2 * m_scale);
        RECT badge{ measure.right + (int)std::lround(8 * m_scale), measure.top - padY,
                    measure.right + (int)std::lround(8 * m_scale) + (tagRc.right - tagRc.left) + padX * 2, measure.bottom + padY };
        const COLORREF accent = m_light ? RGB(37, 99, 235) : RGB(96, 165, 250);
        HBRUSH fill = CreateSolidBrush(m_light ? RGB(219, 234, 254) : RGB(30, 58, 138));
        HPEN pen = CreatePen(PS_SOLID, 1, m_light ? RGB(191, 219, 254) : RGB(59, 130, 246));
        HGDIOBJ oldBrush = SelectObject(m_dc, fill), oldPen = SelectObject(m_dc, pen);
        const int r = (int)std::lround(6 * m_scale);
        RoundRect(m_dc, badge.left, badge.top, badge.right, badge.bottom, r, r);
        SelectObject(m_dc, oldBrush); SelectObject(m_dc, oldPen);
        DeleteObject(fill); DeleteObject(pen);
        SetTextColor(m_dc, accent);
        RECT tr{ badge.left + padX, measure.top, badge.right - padX, measure.bottom };
        DrawTextW(m_dc, tag, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        for (int y = badge.top; y < badge.bottom; ++y)
            for (int x = badge.left; x < badge.right; ++x)
                if (x >= 0 && y >= 0 && x < m_w && y < m_h) m_inside[(size_t)y * m_w + x] = 255;
    }
    GdiFlush();
    for (size_t i = 0; i < m_base.size(); ++i) {
        if (m_inside[i]) m_bits[i] |= 0xFF000000u;
        m_base[i] = m_bits[i];
    }
}

void Splash::Paint() {
    if (!m_hwnd || !m_bits) return;
    const double now = NowSeconds();
    const double t = now - m_start;
    if (m_closing && m_closeAt < 0.0) m_closeAt = now;
    float alpha = Smooth((float)(t / kFadeIn));
    float rise = 1.0f - Smooth((float)(t / (kFadeIn * 1.6)));
    if (m_closeAt >= 0.0) {
        const float out = (float)((now - m_closeAt) / kFadeOut);
        if (out >= 1.0f) { KillTimer(m_hwnd, 1); DestroyWindow(m_hwnd); return; }
        alpha *= 1.0f - Smooth(out);
    }

    std::copy(m_base.begin(), m_base.end(), m_bits);
    GdiFlush();
    // Status line (GDI, then the alpha is put back on the opaque part).
    std::wstring status;
    { std::lock_guard<std::mutex> lock(m_mutex); status = m_status; }
    const int pad = (int)std::lround(28 * m_scale);
    const COLORREF dim = m_light ? RGB(107, 114, 128) : RGB(154, 160, 171);
    SetBkMode(m_dc, TRANSPARENT);
    SelectObject(m_dc, m_textFont);
    SetTextColor(m_dc, dim);
    RECT rc{ m_margin + pad, m_margin + m_panelH - (int)std::lround(66 * m_scale), m_margin + m_panelW - pad, m_margin + m_panelH - (int)std::lround(40 * m_scale) };
    DrawTextW(m_dc, status.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    GdiFlush();
    for (size_t i = 0; i < m_inside.size(); ++i) if (m_inside[i]) m_bits[i] |= 0xFF000000u;

    // The bar: a dim track and a soft highlight that sweeps along it.
    const float trackX0 = (float)(m_margin + pad), trackX1 = (float)(m_margin + m_panelW - pad);
    const float trackY = (float)(m_margin + m_panelH) - 30.0f * m_scale;
    const float trackH = 4.0f * m_scale;
    const float trackW = trackX1 - trackX0;
    const float segW = trackW * 0.30f;
    const float phase = (float)std::fmod(t / kSweep, 1.0);
    const float segX = trackX0 - segW + (trackW + segW) * Smooth(phase);
    const float trackShade = m_light ? 0.0f : 255.0f;
    const int ya = (int)std::floor(trackY - 1.0f), yb = (int)std::ceil(trackY + trackH + 1.0f);
    for (int y = std::max(ya, 0); y < std::min(yb, m_h); ++y) {
        const float cy = std::clamp(std::min(y + 1.0f - trackY, trackY + trackH - y), 0.0f, 1.0f);   // vertical coverage
        if (cy <= 0.0f) continue;
        for (int x = (int)trackX0; x < (int)trackX1; ++x) {
            uint32_t& px = m_bits[(size_t)y * m_w + x];
            const float cx = std::clamp(std::min(x + 1.0f - trackX0, trackX1 - x), 0.0f, 1.0f);
            const float cov = cx * cy;
            BlendOver(px, trackShade, trackShade, trackShade, (m_light ? 0.08f : 0.10f) * cov);
            const float u = (x + 0.5f - segX) / segW;
            if (u > 0.0f && u < 1.0f) {
                const float bump = std::sin(u * 3.14159265f);
                BlendOver(px, kAccent.r, kAccent.g, kAccent.b, bump * bump * cov);
            }
        }
    }

    if (!m_dump.empty() && !m_dumped && t >= kFadeIn * 2.0 && m_closeAt < 0.0) {
        m_dumped = true;
        GdiFlush();
        FILE* f = nullptr;
        if (_wfopen_s(&f, m_dump.c_str(), L"wb") == 0 && f) {
            BITMAPFILEHEADER fh{};
            BITMAPINFOHEADER ih{};
            const uint32_t bytes = (uint32_t)m_w * (uint32_t)m_h * 4u;
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + bytes;
            ih.biSize = sizeof(ih); ih.biWidth = m_w; ih.biHeight = -m_h; ih.biPlanes = 1; ih.biBitCount = 32; ih.biSizeImage = bytes;
            fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(m_bits, 1, bytes, f);
            fclose(f);
        }
    }
    POINT dst{ m_x, m_y + (int)std::lround(rise * 10.0f * m_scale) };
    SIZE size{ m_w, m_h };
    POINT src{ 0, 0 };
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, (BYTE)std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f), AC_SRC_ALPHA };
    UpdateLayeredWindow(m_hwnd, nullptr, &dst, &size, m_dc, &src, 0, &blend, ULW_ALPHA);
}

} // namespace vdc
