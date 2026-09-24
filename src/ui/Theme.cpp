#include "ui/Theme.h"
#include "ui/IconFont.h"
#include "core/I18n.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

namespace vdc::ui {

namespace {
ImVec4 C(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

// The two palettes; g_palette holds the blend in use. The accent is the indigo of the lens ring in the program's icon.
const Palette kDark = {
    .accent = IM_COL32(92, 108, 245, 255), .accentHover = IM_COL32(114, 129, 255, 255),
    .accentActive = IM_COL32(76, 91, 222, 255), .accentText = IM_COL32(255, 255, 255, 255),
    .good = IM_COL32(61, 214, 140, 255), .warn = IM_COL32(255, 184, 71, 255), .bad = IM_COL32(255, 99, 105, 255),
    .muted = IM_COL32(128, 134, 150, 255),
    .panel = IM_COL32(25, 28, 34, 255), .panelBorder = IM_COL32(38, 42, 51, 255), .overlayBg = IM_COL32(12, 13, 17, 208),
    .text = IM_COL32(232, 234, 240, 255), .textDim = IM_COL32(150, 156, 172, 255),
    .control = IM_COL32(37, 41, 50, 255), .controlHover = IM_COL32(47, 52, 63, 255), .controlActive = IM_COL32(58, 64, 78, 255),
    .surface = IM_COL32(9, 10, 13, 255),
    .track = IM_COL32(52, 57, 69, 255), .rangeFill = IM_COL32(92, 108, 245, 110), .knob = IM_COL32(240, 242, 247, 255),
    .selection = IM_COL32(92, 108, 245, 60),
    .card = IM_COL32(25, 28, 34, 255), .cardBorder = IM_COL32(38, 42, 51, 255),
    .shadow = IM_COL32(0, 0, 0, 150),
    .window = C(15, 16, 20),
    .light = 0.0f,
};
const Palette kLight = {
    .accent = IM_COL32(70, 88, 230, 255), .accentHover = IM_COL32(92, 109, 242, 255),
    .accentActive = IM_COL32(56, 72, 204, 255), .accentText = IM_COL32(255, 255, 255, 255),
    .good = IM_COL32(22, 156, 86, 255), .warn = IM_COL32(194, 122, 8, 255), .bad = IM_COL32(214, 55, 62, 255),
    .muted = IM_COL32(118, 125, 140, 255),
    .panel = IM_COL32(255, 255, 255, 255), .panelBorder = IM_COL32(223, 226, 233, 255), .overlayBg = IM_COL32(255, 255, 255, 228),
    .text = IM_COL32(27, 30, 37, 255), .textDim = IM_COL32(102, 109, 125, 255),
    .control = IM_COL32(238, 240, 245, 255), .controlHover = IM_COL32(228, 231, 238, 255), .controlActive = IM_COL32(214, 218, 228, 255),
    .surface = IM_COL32(226, 229, 235, 255),
    .track = IM_COL32(212, 216, 225, 255), .rangeFill = IM_COL32(70, 88, 230, 90), .knob = IM_COL32(52, 58, 70, 255),
    .selection = IM_COL32(70, 88, 230, 46),
    .card = IM_COL32(255, 255, 255, 255), .cardBorder = IM_COL32(223, 226, 233, 255),
    .shadow = IM_COL32(22, 28, 50, 44),
    .window = C(241, 243, 247),
    .light = 1.0f,
};
// The palette's colours are the ImU32 members from "accent" up to "window"; SetThemeLight blends them as a run.
static_assert(offsetof(Palette, accent) == 0 && offsetof(Palette, window) % sizeof(ImU32) == 0, "Palette layout");
constexpr int kPaletteColors = (int)(offsetof(Palette, window) / sizeof(ImU32));

Palette g_palette = kDark;
float g_light = 0.0f;
ImFont* g_bold = nullptr;
ImFont* g_mono = nullptr;
ImFont* g_icons = nullptr;
bool g_animating = false;

float Alpha(ImU32 c) { return ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f; }

ImU32 LerpCol(ImU32 a, ImU32 b, float t) {
    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
}

// The ImGui colour table of one theme, from its palette (every entry the widgets use; the rest from ImGui's own
// dark or light table, so a blend never reads an unset colour).
void ThemeColors(ImVec4* c, const Palette& p) {
    const bool light = p.light > 0.5f;
    ImGuiStyle base;
    if (light) ImGui::StyleColorsLight(&base); else ImGui::StyleColorsDark(&base);
    std::memcpy(c, base.Colors, sizeof(base.Colors));
    auto V = [](ImU32 u, float a = 1.0f) { ImVec4 v = ImGui::ColorConvertU32ToFloat4(u); v.w *= a; return v; };
    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_Text]                 = V(p.text);
    c[ImGuiCol_TextDisabled]         = V(p.textDim);
    c[ImGuiCol_WindowBg]             = p.window;
    c[ImGuiCol_ChildBg]              = none;   // panels paint their own card
    c[ImGuiCol_PopupBg]              = V(light ? p.card : LerpCol(p.card, p.control, 0.45f));
    c[ImGuiCol_Border]               = V(p.cardBorder);
    c[ImGuiCol_BorderShadow]         = none;
    c[ImGuiCol_FrameBg]              = V(p.control);
    c[ImGuiCol_FrameBgHovered]       = V(p.controlHover);
    c[ImGuiCol_FrameBgActive]        = V(p.controlActive);
    c[ImGuiCol_TitleBg]              = V(p.card);
    c[ImGuiCol_TitleBgActive]        = V(p.card);
    c[ImGuiCol_TitleBgCollapsed]     = V(p.card);
    c[ImGuiCol_MenuBarBg]            = V(p.card);
    c[ImGuiCol_ScrollbarBg]          = none;
    c[ImGuiCol_ScrollbarGrab]        = light ? C(198, 203, 213) : C(60, 65, 78);
    c[ImGuiCol_ScrollbarGrabHovered] = light ? C(172, 178, 191) : C(80, 87, 104);
    c[ImGuiCol_ScrollbarGrabActive]  = light ? C(150, 157, 172) : C(98, 106, 126);
    c[ImGuiCol_CheckMark]            = V(p.accent);
    c[ImGuiCol_SliderGrab]           = V(p.accent);
    c[ImGuiCol_SliderGrabActive]     = V(p.accentHover);
    c[ImGuiCol_Button]               = V(p.control);
    c[ImGuiCol_ButtonHovered]        = V(p.controlHover);
    c[ImGuiCol_ButtonActive]         = V(p.controlActive);
    c[ImGuiCol_Header]               = V(p.accent, light ? 0.14f : 0.22f);   // the chosen entry of a list
    c[ImGuiCol_HeaderHovered]        = V(p.controlHover);
    c[ImGuiCol_HeaderActive]         = V(p.controlActive);
    c[ImGuiCol_Separator]            = V(p.cardBorder);
    c[ImGuiCol_SeparatorHovered]     = V(p.accent);
    c[ImGuiCol_SeparatorActive]      = V(p.accentHover);
    c[ImGuiCol_ResizeGrip]           = none;
    c[ImGuiCol_ResizeGripHovered]    = V(p.accent, 0.6f);
    c[ImGuiCol_ResizeGripActive]     = V(p.accent);
    c[ImGuiCol_Tab]                  = V(p.card);
    c[ImGuiCol_TabHovered]           = V(p.controlHover);
    c[ImGuiCol_TabSelected]          = V(p.control);
    c[ImGuiCol_TabSelectedOverline]  = V(p.accent);
    c[ImGuiCol_TabDimmed]            = V(p.card);
    c[ImGuiCol_TabDimmedSelected]    = V(p.control);
    c[ImGuiCol_TabDimmedSelectedOverline] = V(p.cardBorder);
    c[ImGuiCol_PlotLines]            = V(p.accent);
    c[ImGuiCol_PlotHistogram]        = V(p.accent);
    c[ImGuiCol_TableHeaderBg]        = V(p.control);
    c[ImGuiCol_TableBorderStrong]    = V(p.cardBorder);
    c[ImGuiCol_TableBorderLight]     = V(LerpCol(p.cardBorder, p.card, 0.5f));
    c[ImGuiCol_TableRowBg]           = none;
    c[ImGuiCol_TableRowBgAlt]        = V(p.text, 0.03f);
    c[ImGuiCol_TextLink]             = V(light ? p.accent : p.accentHover);
    c[ImGuiCol_TextSelectedBg]       = V(p.accent, light ? 0.28f : 0.4f);
    c[ImGuiCol_DragDropTarget]       = V(p.accent);
    c[ImGuiCol_NavCursor]            = V(p.accent, 0.9f);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, light ? 0.3f : 0.55f);
    c[ImGuiCol_InputTextCursor]      = V(p.text);
}

constexpr ImGuiID kHoverKey = 0x9E3779B9u;   // mixed into an item's id for its hover animation
constexpr ImGuiID kShownKey = 0x85EBCA6Bu;   // mixed into an item's id for its fade in and out

float* MotionValue(ImGuiID id, float seed) { return ImGui::GetCurrentWindow()->DC.StateStorage->GetFloatRef(id, seed); }
float FrameStep() { return ImMin(ImGui::GetIO().DeltaTime, 0.05f); }
float Px(float v) { return IM_ROUND(v * Dpi()); }   // a length given at 96 dpi, in whole pixels at the current scale
}

const Palette& Colors() { return g_palette; }

ImU32 WithAlpha(ImU32 color, float alpha) {
    const ImU32 a = (ImU32)(ImClamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = ImSaturate(t);
    if (Alpha(a) <= 0.0f) return WithAlpha(b, Alpha(b) * t);
    if (Alpha(b) <= 0.0f) return WithAlpha(a, Alpha(a) * (1.0f - t));
    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
}

ImU32 Col(ImU32 color) {
    const float alpha = ImGui::GetStyle().Alpha;
    return alpha >= 1.0f ? color : WithAlpha(color, Alpha(color) * alpha);
}

void ApplyTheme(ImGuiStyle& style, float dpiScale) {
    style = ImGuiStyle();
    // Flat: no frame borders, round cards and popups, a little rounding on the controls, regular spacing. The main
    // window pushes its own square corners and no border; floating windows and tooltips keep these.
    style.WindowRounding = 10.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 10.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 10.0f;
    style.ScrollbarPadding = 2.0f;
    style.GrabMinSize = 8.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(0, 4);
    style.CellPadding = ImVec2(6, 4);
    style.DisabledAlpha = 0.45f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    SetThemeLight(style, g_light);
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

float Dpi() { return ImMax(0.5f, ImGui::GetStyle().FontScaleDpi); }

void SetFonts(ImFont* bold, ImFont* mono, ImFont* icons) { g_bold = bold; g_mono = mono; g_icons = icons; }
ImFont* BoldFont() { return g_bold ? g_bold : ImGui::GetFont(); }
ImFont* MonoFont() { return g_mono ? g_mono : ImGui::GetFont(); }

void SetThemeLight(ImGuiStyle& style, float light) {
    light = ImSaturate(light);
    g_light = light;
    if (light <= 0.0f || light >= 1.0f) {
        g_palette = light >= 1.0f ? kLight : kDark;
        ThemeColors(style.Colors, g_palette);
        return;
    }
    const ImU32* a = &kDark.accent;
    const ImU32* b = &kLight.accent;
    ImU32* out = &g_palette.accent;
    for (int i = 0; i < kPaletteColors; ++i) out[i] = LerpCol(a[i], b[i], light);
    g_palette.window = ImLerp(kDark.window, kLight.window, light);
    g_palette.light = light;
    ImVec4 dark[ImGuiCol_COUNT], lite[ImGuiCol_COUNT];
    ThemeColors(dark, kDark);
    ThemeColors(lite, kLight);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) style.Colors[i] = ImLerp(dark[i], lite[i], light);
}

// Motion ------------------------------------------------------------------------------------

void ResetAnimating() { g_animating = false; }
void MarkAnimating() { g_animating = true; }
bool Animating() { return g_animating; }

float Animate(ImGuiID id, float target, float speed) { return AnimateFrom(id, target, target, speed); }

float AnimateFrom(ImGuiID id, float from, float target, float speed) {
    float* v = MotionValue(id, from);
    const float d = target - *v;
    if (ImFabs(d) < 0.002f) { *v = target; return target; }
    *v += d * (1.0f - std::exp(-speed * FrameStep()));
    g_animating = true;
    return *v;
}

float AnimateLinear(ImGuiID id, float target, float seconds) {
    float* v = MotionValue(id, target);
    if (*v == target) return target;
    const float step = seconds > 0.0f ? FrameStep() / seconds : 1.0f;
    *v += ImClamp(target - *v, -step, step);
    if (ImFabs(target - *v) < 0.0005f) *v = target;
    else g_animating = true;
    return *v;
}

void AnimateSnap(ImGuiID id, float value) { *MotionValue(id, value) = value; }

float Ease(float t) { t = ImSaturate(t); return t * t * (3.0f - 2.0f * t); }

void FadeDrawn(ImDrawList* dl, int fromVtx, float alpha, float dy) {
    if (!dl || (alpha >= 0.999f && std::fabs(dy) < 0.01f)) return;
    const float a = ImSaturate(alpha);
    for (int i = std::max(0, fromVtx); i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        const ImU32 va = (ImU32)((float)((v.col >> IM_COL32_A_SHIFT) & 0xFF) * a + 0.5f);
        v.col = (v.col & ~IM_COL32_A_MASK) | (va << IM_COL32_A_SHIFT);
        v.pos.y += dy;
    }
}

// Scrolling ---------------------------------------------------------------------------------

namespace {
constexpr ImGuiID kScrollTargetKey = 0x51A3C9E7u;
constexpr ImGuiID kScrollLastKey = 0x6D2B8F15u;
constexpr ImGuiID kFadeKey = 0x3C6EF372u;
constexpr ImGuiID kOpenKey = 0xA54FF53Au;

// The window's scroll on one axis glides toward a stored target; a scroll moved by other means (scrollbar drag,
// SetScrollHere, a shorter content) resets the target to where the window is.
void GlideScroll(ImGuiWindow* w, bool horizontal, float* newTarget) {
    ImGuiStorage& st = w->StateStorage;
    const ImGuiID axisKey = horizontal ? 0x1u : 0x2u;
    const float scroll = horizontal ? w->Scroll.x : w->Scroll.y;
    const float maxScroll = ImMax(horizontal ? w->ScrollMax.x : w->ScrollMax.y, 0.0f);
    float* target = st.GetFloatRef(w->ID ^ kScrollTargetKey ^ axisKey, scroll);
    float* last = st.GetFloatRef(w->ID ^ kScrollLastKey ^ axisKey, scroll);
    if (ImFabs(scroll - *last) > 0.5f) *target = scroll;
    if (newTarget) *target = ImClamp(*newTarget, 0.0f, maxScroll);
    if (*target > maxScroll && maxScroll > 0.0f) *target = maxScroll;
    const float d = *target - scroll;
    float next = scroll;
    if (ImFabs(d) > 0.3f) next = scroll + d * (1.0f - std::exp(-18.0f * FrameStep()));
    else if (d != 0.0f) next = *target;
    if (next != scroll) {
        if (horizontal) ImGui::SetScrollX(w, next); else ImGui::SetScrollY(w, next);
        g_animating = true;
    }
    *last = next;
}
}

void SmoothScroll(bool horizontal, float step) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const ImGuiIO& io = ImGui::GetIO();
    const float wheel = io.MouseWheel + io.MouseWheelH;
    float* newTarget = nullptr;
    float t = 0.0f;
    // A list opened from the window (a dropdown, a menu) keeps the wheel for itself: without NoPopupHierarchy the
    // window that opened it counts as hovered too and would scroll away underneath the list.
    if (wheel != 0.0f && !io.KeyCtrl && !io.KeyAlt &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_NoPopupHierarchy | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        ImGuiStorage& st = w->StateStorage;
        const ImGuiID axisKey = horizontal ? 0x1u : 0x2u;
        const float scroll = horizontal ? w->Scroll.x : w->Scroll.y;
        float* target = st.GetFloatRef(w->ID ^ kScrollTargetKey ^ axisKey, scroll);
        float* last = st.GetFloatRef(w->ID ^ kScrollLastKey ^ axisKey, scroll);
        t = (ImFabs(scroll - *last) > 0.5f ? scroll : *target) - wheel * step;
        newTarget = &t;
    }
    GlideScroll(w, horizontal, newTarget);
}

void SmoothScrollTo(float target, bool horizontal) {
    GlideScroll(ImGui::GetCurrentWindow(), horizontal, &target);
}

// Shadows and cards ---------------------------------------------------------------------------

namespace {
// A soft shadow as two rings of triangles around a rounded rectangle: from its outline (full colour) out to
// "spread" (transparent), the far edge moved down by "dy" so the shadow falls below the rectangle. Nothing is drawn
// inside the outline, so the ring may go on top of what is under a window without covering the window itself.
void ShadowRing(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float r, float spread, float dy, ImU32 col) {
    if ((col & IM_COL32_A_MASK) == 0 || spread <= 0.0f || b.x <= a.x || b.y <= a.y) return;
    r = ImMin(r, ImMin(b.x - a.x, b.y - a.y) * 0.5f);
    dy = ImMin(dy, spread * 0.6f);
    constexpr int kSeg = 6;
    constexpr int kPts = 4 * (kSeg + 1);
    const ImVec2 ctr[4] = { ImVec2(b.x - r, a.y + r), ImVec2(b.x - r, b.y - r), ImVec2(a.x + r, b.y - r), ImVec2(a.x + r, a.y + r) };
    const ImU32 c0 = col, c1 = WithAlpha(col, Alpha(col) * 0.32f), c2 = col & ~IM_COL32_A_MASK;
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(kPts * 12, kPts * 3);
    const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    for (int c = 0, n = 0; c < 4; ++c) {
        for (int i = 0; i <= kSeg; ++i, ++n) {
            const float ang = -IM_PI * 0.5f + IM_PI * 0.5f * (float)c + IM_PI * 0.5f * (float)i / (float)kSeg;
            const ImVec2 nrm(std::cos(ang), std::sin(ang));
            const ImVec2 p0(ctr[c].x + nrm.x * r, ctr[c].y + nrm.y * r);
            const ImVec2 p1(p0.x + nrm.x * spread * 0.4f, p0.y + nrm.y * spread * 0.4f + dy * 0.4f);
            const ImVec2 p2(p0.x + nrm.x * spread, p0.y + nrm.y * spread + dy);
            dl->PrimWriteVtx(p0, uv, c0);
            dl->PrimWriteVtx(p1, uv, c1);
            dl->PrimWriteVtx(p2, uv, c2);
        }
    }
    for (int i = 0; i < kPts; ++i) {
        const ImDrawIdx i0 = (ImDrawIdx)(base + i * 3), j0 = (ImDrawIdx)(base + ((i + 1) % kPts) * 3);
        for (int ring = 0; ring < 2; ++ring) {
            const ImDrawIdx a0 = (ImDrawIdx)(i0 + ring), a1 = (ImDrawIdx)(j0 + ring), b0 = (ImDrawIdx)(i0 + ring + 1), b1 = (ImDrawIdx)(j0 + ring + 1);
            dl->PrimWriteIdx(a0); dl->PrimWriteIdx(a1); dl->PrimWriteIdx(b1);
            dl->PrimWriteIdx(a0); dl->PrimWriteIdx(b1); dl->PrimWriteIdx(b0);
        }
    }
}
}

float CardRounding() { return ImGui::GetStyle().ChildRounding; }
float CardInset() { return Px(12.0f); }

void DrawShadow(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, float alpha) {
    const ImU32 col = g_palette.shadow;
    ShadowRing(dl, min, max, rounding, Px(14.0f), Px(4.0f), Col(WithAlpha(col, Alpha(col) * alpha)));
}

void DrawCard(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float alpha) {
    const Palette& p = g_palette;
    const float r = CardRounding();
    // Cards barely lift: on the dark theme the hairline does the work, on the light one a faint shadow helps it.
    ShadowRing(dl, min, max, r, Px(8.0f), Px(2.0f), Col(WithAlpha(p.shadow, Alpha(p.shadow) * (0.35f + 0.4f * p.light) * alpha)));
    dl->AddRectFilled(min, max, Col(WithAlpha(p.card, Alpha(p.card) * alpha)), r);
    dl->AddRect(min, max, Col(WithAlpha(p.cardBorder, Alpha(p.cardBorder) * alpha)), r, 1.0f);
}

void DrawCardEdge(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float alpha) {
    const Palette& p = g_palette;
    const float r = CardRounding();
    ShadowRing(dl, min, max, r, Px(8.0f), Px(2.0f), Col(WithAlpha(p.shadow, Alpha(p.shadow) * (0.35f + 0.4f * p.light) * alpha)));
    dl->AddRect(min, max, Col(WithAlpha(p.cardBorder, Alpha(p.cardBorder) * alpha)), r, 1.0f);
}

void WindowShadow() {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (!w || w->SkipItems) return;
    ImDrawList* dl = w->DrawList;
    const ImU32 col = g_palette.shadow;
    dl->PushClipRectFullScreen();
    ShadowRing(dl, w->Pos, ImVec2(w->Pos.x + w->Size.x, w->Pos.y + w->Size.y), w->WindowRounding, Px(16.0f), Px(5.0f),
               Col(WithAlpha(col, ImMin(1.0f, Alpha(col) * 1.4f))));
    dl->PopClipRect();
}

void MaskCorners(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float r, ImU32 bg) {
    r = ImMin(r, ImMin(b.x - a.x, b.y - a.y) * 0.5f);
    if (r < 1.0f || (bg & IM_COL32_A_MASK) == 0) return;
    const ImVec2 corner[4] = { a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y) };
    const ImVec2 ctr[4] = { ImVec2(a.x + r, a.y + r), ImVec2(b.x - r, a.y + r), ImVec2(b.x - r, b.y - r), ImVec2(a.x + r, b.y - r) };
    for (int i = 0; i < 4; ++i) {
        const float a0 = IM_PI + IM_PI * 0.5f * (float)i;
        dl->PathLineTo(corner[i]);
        dl->PathArcTo(ctr[i], r, a0, a0 + IM_PI * 0.5f, 10);
        dl->PathFillConcave(bg);
    }
}

// A label drawn right of a control from "x": it wraps at the right edge of the row when it is longer than the room
// there (a narrow sidebar, a long translation) instead of running past the card.
namespace {
float SideLabelRoom(float x) {
    return ImMax(ImGui::GetCurrentWindow()->WorkRect.Max.x - x, ImGui::GetFontSize() * 4.0f);
}
ImVec2 SideLabelSize(const char* label, float x) {
    const char* end = ImGui::FindRenderedTextEnd(label);
    return end == label ? ImVec2(0.0f, 0.0f) : ImGui::CalcTextSize(label, end, false, SideLabelRoom(x));
}
void SideLabel(const char* label, const ImVec2& pos) {
    ImGui::RenderTextWrapped(pos, label, ImGui::FindRenderedTextEnd(label), SideLabelRoom(pos.x));
}
}

// Popups ------------------------------------------------------------------------------------

bool BeginPopupFade(const char* strId, ImGuiWindowFlags flags) {
    const ImGuiID id = ImGui::GetID(strId);
    const bool open = ImGui::IsPopupOpen(strId);
    if (!open) { AnimateSnap(id ^ kFadeKey, 0.0f); return false; }
    const float t = AnimateFrom(id ^ kFadeKey, 0.0f, 1.0f, 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Ease(t));
    if (!ImGui::BeginPopup(strId, flags)) { ImGui::PopStyleVar(); return false; }
    WindowShadow();
    return true;
}

void EndPopupFade() {
    ImGui::EndPopup();
    ImGui::PopStyleVar();
}

// Where more content lies above or below the visible part of a scrolled region, its edge fades into the background
// instead of ending in a cut line. The fade shortens as the end comes near, so it goes without a jump.
void ScrollEdgeFade(ImU32 bg, float height) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->ScrollMax.y <= 0.5f || height <= 0.0f) return;
    const ImRect r = w->InnerRect;   // without the scrollbar
    const ImU32 clear = bg & ~IM_COL32_A_MASK;
    const float below = ImMin(1.0f, (w->ScrollMax.y - w->Scroll.y) / height);
    const float above = ImMin(1.0f, w->Scroll.y / height);
    if (below > 0.0f) w->DrawList->AddRectFilledMultiColor(ImVec2(r.Min.x, r.Max.y - height * below), r.Max, clear, clear, bg, bg);
    if (above > 0.0f) w->DrawList->AddRectFilledMultiColor(r.Min, ImVec2(r.Max.x, r.Min.y + height * above), bg, bg, clear, clear);
}

bool BeginDropdown(const char* label, const char* preview, ImGuiComboFlags flags) {
    return BeginDropdown(label, preview, Icon::None, flags);
}

bool BeginDropdown(const char* label, const char* preview, Icon icon, ImGuiComboFlags flags) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = g.Style;
    const Palette& p = Colors();
    const ImGuiID id = window->GetID(label);
    const ImGuiID popupId = ImHashStr("##ComboPopup", 0, id);
    const float arrowW = ImGui::GetFrameHeight();
    LabelSeen(label);
    const float w = ImGui::CalcItemWidth();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + ImGui::GetFrameHeight()));
    const ImVec2 labelSize = SideLabelSize(label, bb.Max.x + style.ItemInnerSpacing.x);
    const ImRect total(bb.Min, ImVec2(bb.Max.x + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f),
                                      ImMax(bb.Max.y, bb.Min.y + style.FramePadding.y * 2.0f + labelSize.y)));
    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id, &bb)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    bool open = ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None);
    if (pressed && !open) { ImGui::OpenPopupEx(popupId, ImGuiPopupFlags_None); open = true; }
    const float hov = Animate(id ^ kHoverKey, hovered ? 1.0f : 0.0f, 16.0f);
    const ImU32 bg = (held || open) ? p.controlActive : Mix(p.control, p.controlHover, hov);
    ImDrawList* dl = window->DrawList;
    ImGui::RenderNavCursor(bb, id);
    dl->AddRectFilled(bb.Min, bb.Max, Col(bg), style.FrameRounding);
    if (open) dl->AddRect(bb.Min, bb.Max, Col(WithAlpha(p.accent, 0.7f)), style.FrameRounding, ImMax(1.0f, Dpi()));
    const float angle = Animate(id ^ kOpenKey, open ? IM_PI : 0.0f, 18.0f);
    DrawChevron(dl, ImVec2(bb.Max.x - arrowW * 0.5f, bb.GetCenter().y), IconSize(0.62f), angle, Col(Mix(p.textDim, p.text, hov)));
    float textX = bb.Min.x + style.FramePadding.x;
    if (icon != Icon::None) {
        const float iconS = IconSize();
        DrawIcon(dl, icon, ImVec2(textX + iconS * 0.5f, bb.GetCenter().y), iconS, Col(Mix(p.textDim, p.text, hov)));
        textX += iconS + style.ItemInnerSpacing.x;
    }
    if (preview) {   // a name longer than the box ends in an ellipsis
        const float textMax = bb.Max.x - arrowW;
        ImGui::PushStyleColor(ImGuiCol_Text, p.text);
        if (textMax > textX)
            ImGui::RenderTextEllipsis(dl, ImVec2(textX, bb.Min.y + style.FramePadding.y), ImVec2(textMax, bb.Max.y - style.FramePadding.y),
                                      textMax, preview, nullptr, nullptr);
        ImGui::PopStyleColor();
    }
    if (labelSize.x > 0.0f) SideLabel(label, ImVec2(bb.Max.x + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y));
    if (!open) { AnimateSnap(id ^ kFadeKey, 0.0f); return false; }
    const float t = AnimateFrom(id ^ kFadeKey, 0.0f, 1.0f, 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * Ease(t));
    if (!ImGui::BeginComboPopup(popupId, bb, flags)) { ImGui::PopStyleVar(); return false; }
    WindowShadow();
    return true;
}

void EndDropdown() {
    ImGui::EndCombo();
    ImGui::PopStyleVar();
}

void TooltipShow(ImGuiID key, const char* text) {
    if (!text || !*text) return;
    static ImGuiID lastKey = 0;
    static int lastFrame = -1000;
    static float t = 0.0f;
    const int frame = ImGui::GetFrameCount();
    const bool more = key == lastKey && frame == lastFrame;   // a second text for the same item this frame: it goes below the first
    if (!more) {
        if (key != lastKey || frame - lastFrame > 2) t = 0.0f;   // another item, or the tooltip was away for a while
        lastKey = key; lastFrame = frame;
        t += (1.0f - t) * (1.0f - std::exp(-22.0f * FrameStep()));
        if (t > 0.995f) t = 1.0f; else g_animating = true;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * Ease(t));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(10.0f), Px(7.0f)));
    if (ImGui::BeginTooltip()) {
        if (more) ImGui::Spacing();
        else WindowShadow();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(3);
}

void Tooltip(const char* text) {
    if (!text || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    TooltipShow(ImGui::GetItemID(), text);
}

// Icons -------------------------------------------------------------------------------------

namespace {
ImWchar Glyph(Icon icon) {
    switch (icon) {
    case Icon::ChevronDown:    return lucide::chevron_down;
    case Icon::ChevronUp:      return lucide::chevron_up;
    case Icon::ChevronLeft:    return lucide::chevron_left;
    case Icon::ChevronRight:   return lucide::chevron_right;
    case Icon::Reset:          return lucide::rotate_ccw;
    case Icon::Refresh:        return lucide::refresh_cw;
    case Icon::OpenExternal:   return lucide::external_link;
    case Icon::Close:          return lucide::x;
    case Icon::Lock:           return lucide::lock;
    case Icon::Undo:           return lucide::undo_2;
    case Icon::Redo:           return lucide::redo_2;
    case Icon::Fullscreen:     return lucide::maximize;
    case Icon::ExitFullscreen: return lucide::minimize;
    case Icon::History:        return lucide::history;
    case Icon::Help:           return lucide::circle_question_mark;
    case Icon::Save:           return lucide::save;
    case Icon::Edit:           return lucide::pencil;
    case Icon::Plus:           return lucide::plus;
    case Icon::Search:         return lucide::search;
    case Icon::RotateLeft:     return lucide::rotate_ccw_square;
    case Icon::RotateRight:    return lucide::rotate_cw_square;
    case Icon::FlipH:          return lucide::flip_horizontal_2;
    case Icon::FlipV:          return lucide::flip_vertical_2;
    case Icon::Crop:           return lucide::crop;
    case Icon::Image:          return lucide::image;
    case Icon::Film:           return lucide::film;
    case Icon::Broadcast:      return lucide::radio;
    case Icon::Sparkle:        return lucide::sparkles;
    case Icon::Download:       return lucide::download;
    case Icon::Layers:         return lucide::layers;
    case Icon::Grid:           return lucide::grid_2x2;
    case Icon::Eye:            return lucide::eye;
    case Icon::Plug:           return lucide::plug;
    case Icon::Gauge:          return lucide::gauge;
    case Icon::Info:           return lucide::info;
    case Icon::Folder:         return lucide::folder_open;
    case Icon::Check:          return lucide::check;
    case Icon::Camera:         return lucide::camera;
    case Icon::Warning:        return lucide::triangle_alert;
    case Icon::Sliders:        return lucide::sliders_horizontal;
    case Icon::CircleCheck:    return lucide::circle_check;
    case Icon::CircleX:        return lucide::circle_x;
    case Icon::ImagePlus:      return lucide::image_plus;
    case Icon::Languages:      return lucide::languages;
    case Icon::Trash:          return lucide::trash_2;
    case Icon::Keyboard:       return lucide::keyboard;
    case Icon::Monitor:        return lucide::monitor;
    case Icon::Wand:           return lucide::wand_sparkles;
    case Icon::Settings:       return lucide::settings;
    case Icon::Compare:        return lucide::columns_2;
    case Icon::ZoomIn:         return lucide::zoom_in;
    case Icon::ZoomOut:        return lucide::zoom_out;
    case Icon::Terminal:       return lucide::terminal;
    case Icon::Key:            return lucide::key_round;
    case Icon::Upload:         return lucide::upload;
    case Icon::Copy:           return lucide::copy;
    case Icon::Images:         return lucide::images;
    case Icon::Video:          return lucide::video;
    case Icon::Cpu:            return lucide::cpu;
    case Icon::Loader:         return lucide::loader_circle;
    case Icon::Import:         return lucide::import_;
    case Icon::Minus:          return lucide::minus;
    case Icon::Ellipsis:       return lucide::ellipsis;
    case Icon::ListChecks:     return lucide::list_checks;
    case Icon::SquareCheck:    return lucide::square_check;
    case Icon::ArrowRight:     return lucide::arrow_right;
    case Icon::Sun:            return lucide::sun;
    case Icon::Moon:           return lucide::moon;
    case Icon::Bell:           return lucide::bell;
    case Icon::Scissors:       return lucide::scissors;
    case Icon::Flag:           return lucide::flag;
    case Icon::Repeat:         return lucide::repeat;
    case Icon::FileImage:      return lucide::file_image;
    case Icon::FileVideo:      return lucide::file_video;
    case Icon::Mouse:          return lucide::mouse;
    case Icon::Hand:           return lucide::hand;
    case Icon::Move:           return lucide::move;
    case Icon::Focus:          return lucide::focus;
    case Icon::Aperture:       return lucide::aperture;
    case Icon::Shield:         return lucide::shield_check;
    case Icon::Wifi:           return lucide::wifi;
    case Icon::HardDrive:      return lucide::hard_drive;
    case Icon::PanelLeft:      return lucide::panel_left;
    case Icon::PanelRight:     return lucide::panel_right;
    default:                   return 0;
    }
}

// One glyph of the icon font centred on "c", its em box "size" pixels wide (rounded: every size is baked once, so
// icon sizes never animate). Lucide draws in the whole em box, so the box's centre is the icon's centre.
void DrawGlyph(ImDrawList* dl, ImWchar ch, const ImVec2& c, float size, ImU32 col) {
    if (!g_icons || !ch || size < 1.0f || (col & IM_COL32_A_MASK) == 0) return;
    const float px = ImMax(6.0f, IM_ROUND(size));
    g_icons->RenderChar(dl, px, ImVec2(ImFloor(c.x - px * 0.5f + 0.5f), ImFloor(c.y - px * 0.5f + 0.5f)), col, ch);
}

// The four-pointed star of the logo, as a path (curved sides; "pinch" sets how fat its waist is).
void SparklePath(ImDrawList* dl, const ImVec2& c, float R, float pinch) {
    const float k = R * pinch;
    dl->PathLineTo(ImVec2(c.x, c.y - R));
    dl->PathBezierCubicCurveTo(ImVec2(c.x + k * 0.6f, c.y - k * 1.6f), ImVec2(c.x + k * 1.6f, c.y - k * 0.6f), ImVec2(c.x + R, c.y));
    dl->PathBezierCubicCurveTo(ImVec2(c.x + k * 1.6f, c.y + k * 0.6f), ImVec2(c.x + k * 0.6f, c.y + k * 1.6f), ImVec2(c.x, c.y + R));
    dl->PathBezierCubicCurveTo(ImVec2(c.x - k * 0.6f, c.y + k * 1.6f), ImVec2(c.x - k * 1.6f, c.y + k * 0.6f), ImVec2(c.x - R, c.y));
    dl->PathBezierCubicCurveTo(ImVec2(c.x - k * 1.6f, c.y - k * 0.6f), ImVec2(c.x - k * 0.6f, c.y - k * 1.6f), ImVec2(c.x, c.y - R));
    if (dl->_Path.Size > 1) dl->_Path.pop_back();   // the last point is the first one again
}
}

float IconSize(float scale) { return ImMax(8.0f, IM_ROUND(ImGui::GetFontSize() * 1.1f * scale)); }

void DrawChevron(ImDrawList* dl, const ImVec2& c, float size, float angle, ImU32 col) {
    // "size" is the chevron's width; the glyph's em box is wider than the stroke it holds.
    const float em = size * 1.6f;
    const float quarter = angle / (IM_PI * 0.5f);
    const float nearest = std::round(quarter);
    if (std::fabs(quarter - nearest) < 0.001f) {   // at rest: the upright glyph, pixel-sharp
        const int q = (((int)nearest % 4) + 4) % 4;
        const ImWchar ch[4] = { lucide::chevron_down, lucide::chevron_left, lucide::chevron_up, lucide::chevron_right };
        DrawGlyph(dl, ch[q], c, em, col);
        return;
    }
    const int v0 = dl->VtxBuffer.Size;
    DrawGlyph(dl, lucide::chevron_down, c, em, col);
    const float cs = std::cos(angle), sn = std::sin(angle);
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& p = dl->VtxBuffer[i].pos;
        const float x = p.x - c.x, y = p.y - c.y;
        p = ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
    }
}

void DrawSpinner(ImDrawList* dl, const ImVec2& c, float size, ImU32 col) {
    const float t = (float)ImGui::GetTime();
    const float r = size * 0.38f;
    const float a0 = t * 5.5f;
    const float sweep = IM_PI * (1.0f + 0.45f * std::sin(t * 2.3f));
    dl->PathArcTo(c, r, a0, a0 + sweep, 28);
    dl->PathStroke(col, ImMax(1.5f, size * 0.11f));
    g_animating = true;
}

void DrawIcon(ImDrawList* dl, Icon icon, const ImVec2& c, float size, ImU32 col) {
    // The media controls are filled shapes (the line icons read too thin at the size of the transport buttons).
    const float s = size * 0.5f;
    auto at = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    const float round = ImMax(1.0f, size * 0.07f);
    switch (icon) {
    case Icon::Play:
        dl->PathLineTo(at(-0.5f, -0.72f)); dl->PathLineTo(at(0.78f, 0.0f)); dl->PathLineTo(at(-0.5f, 0.72f));
        dl->PathFillConvex(col);
        return;
    case Icon::Pause:
        dl->AddRectFilled(at(-0.62f, -0.7f), at(-0.16f, 0.7f), col, round);
        dl->AddRectFilled(at(0.16f, -0.7f), at(0.62f, 0.7f), col, round);
        return;
    case Icon::Stop:
        dl->AddRectFilled(at(-0.6f, -0.6f), at(0.6f, 0.6f), col, round * 1.4f);
        return;
    case Icon::StepBack:
        dl->AddRectFilled(at(-0.78f, -0.7f), at(-0.5f, 0.7f), col, round);
        dl->PathLineTo(at(0.72f, -0.7f)); dl->PathLineTo(at(-0.38f, 0.0f)); dl->PathLineTo(at(0.72f, 0.7f));
        dl->PathFillConvex(col);
        return;
    case Icon::StepForward:
        dl->AddRectFilled(at(0.5f, -0.7f), at(0.78f, 0.7f), col, round);
        dl->PathLineTo(at(-0.72f, -0.7f)); dl->PathLineTo(at(0.38f, 0.0f)); dl->PathLineTo(at(-0.72f, 0.7f));
        dl->PathFillConvex(col);
        return;
    case Icon::None:
        return;
    default:
        DrawGlyph(dl, Glyph(icon), c, size, col);
        return;
    }
}

void DrawLogo(ImDrawList* dl, const ImVec2& min, float s, float alpha) {
    // The program icon (tools/make_app_icon.py draws the same shapes): a navy tile, a lens with an indigo ring and a
    // gold sparkle, flat colours. Small sizes leave the details out, as the icon file does.
    const int a = (int)(ImSaturate(alpha) * ImGui::GetStyle().Alpha * 255.0f + 0.5f);
    if (a <= 0) return;
    const ImU32 tile = IM_COL32(0x1E, 0x24, 0x49, a), glass = IM_COL32(0x11, 0x15, 0x2E, a), inner = IM_COL32(0x1A, 0x20, 0x46, a);
    const ImU32 ring = IM_COL32(0x6F, 0x84, 0xFF, a), shine = IM_COL32(0xB7, 0xC4, 0xFF, a), gold = IM_COL32(0xFF, 0xD1, 0x66, a);
    const bool small = s <= 24.0f;
    dl->AddRectFilled(min, ImVec2(min.x + s, min.y + s), tile, s * 0.235f);
    const ImVec2 c = small ? ImVec2(min.x + s * 0.45f, min.y + s * 0.57f) : ImVec2(min.x + s * 0.455f, min.y + s * 0.56f);
    const float R = s * (small ? 0.3f : 0.255f);
    dl->AddCircleFilled(c, R, glass, 0);
    if (s >= 48.0f) dl->AddCircleFilled(c, R * 0.56f, inner, 0);
    dl->AddCircle(c, R, ring, 0, ImMax(small ? 2.0f : 1.8f, s * 0.08f));
    if (s >= 32.0f) dl->AddCircleFilled(ImVec2(c.x - R * 0.36f, c.y - R * 0.36f), R * 0.15f, shine, 0);
    const ImVec2 sc = small ? ImVec2(min.x + s * 0.75f, min.y + s * 0.26f) : ImVec2(min.x + s * 0.735f, min.y + s * 0.265f);
    const float sr = small ? s * (s <= 20.0f ? 0.23f : 0.21f) : s * 0.15f;
    const float pinch = small ? 0.2f : 0.13f;
    SparklePath(dl, sc, sr, pinch);   // a knockout in the tile colour keeps the sparkle apart from the ring
    dl->PathStroke(tile, ImMax(1.0f, s * 0.045f), ImDrawFlags_Closed);
    SparklePath(dl, sc, sr, pinch);
    dl->PathFillConcave(gold);
    if (s >= 40.0f) {
        SparklePath(dl, ImVec2(min.x + s * 0.84f, min.y + s * 0.47f), s * 0.055f, 0.13f);
        dl->PathFillConcave(gold);
    }
}

// Search ------------------------------------------------------------------------------------

namespace {
std::string g_query;              // lower-case; empty while nothing is filtered
bool g_searching = false;
bool g_searchSkipped = false;      // the last labelled widget was left out
bool g_sectionAll = false;         // the current section matched by its title: everything in it shows
ImGuiID g_searchSection = 0;
int g_searchHitsNow = 0;           // hits of the current section so far
int g_searchTotal = 0;
ImGuiStorage g_searchHits;         // hits of every section on the frame it was last drawn

std::string Lower(const char* s, const char* end = nullptr) {
    std::string out;
    if (!s) return out;
    if (!end) end = s + std::strlen(s);
    out.reserve((size_t)(end - s));
    for (const char* c = s; c < end; ++c) out.push_back((*c >= 'A' && *c <= 'Z') ? (char)(*c + 32) : *c);
    return out;
}
bool Contains(const char* text) {
    if (!text || !*text) return false;
    return Lower(text, ImGui::FindRenderedTextEnd(text)).find(g_query) != std::string::npos;
}
void CloseSearchSection() {
    if (g_searchSection) g_searchHits.SetInt(g_searchSection, g_searchHitsNow);
    g_searchSection = 0;
    g_searchHitsNow = 0;
    g_sectionAll = false;
    g_searchSkipped = false;   // nothing of the next section has been left out yet
}
}

void SearchBegin(const char* query) {
    std::string q = Lower(query);
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back() == ' ') q.pop_back();
    if (q != g_query) g_searchHits.Clear();   // a new query: every section gets a look
    g_query = q;
    g_searching = !g_query.empty();
    g_searchSkipped = false;
    g_searchTotal = 0;
    g_searchSection = 0;
    g_searchHitsNow = 0;
    g_sectionAll = false;
}

void SearchEnd() {
    CloseSearchSection();
    g_searching = false;
    g_searchSkipped = false;
}

bool Searching() { return g_searching; }

void SearchHold(bool hold) {
    static bool held = false, was = false;
    if (hold == held) return;
    held = hold;
    if (hold) { was = g_searching; g_searching = false; g_searchSkipped = false; }
    else g_searching = was;
}
bool SearchSkipped() { return g_searchSkipped; }
int SearchHits() { return g_searchTotal; }

bool SearchMatch(const char* label, const char* tooltip) {
    if (!g_searching) { g_searchSkipped = false; return true; }
    const bool hit = g_sectionAll || Contains(label) || Contains(tooltip);
    if (hit) { ++g_searchHitsNow; ++g_searchTotal; }
    g_searchSkipped = !hit;
    return hit;
}

// Buttons -----------------------------------------------------------------------------------

namespace {
void ButtonColors(ButtonKind kind, ImU32& bg, ImU32& bgHover, ImU32& bgActive, ImU32& fg, ImU32& border) {
    const Palette& p = Colors();
    border = 0;
    // A primary or red action that cannot be used now looks like any other unavailable button: its colour would
    // still read as the thing to press.
    if ((kind == ButtonKind::Accent || kind == ButtonKind::Danger) && (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
        kind = ButtonKind::Flat;
    switch (kind) {
    case ButtonKind::Accent: bg = p.accent; bgHover = p.accentHover; bgActive = p.accentActive; fg = p.accentText; break;
    case ButtonKind::Ghost:  bg = 0; bgHover = p.controlHover; bgActive = p.controlActive; fg = p.text; border = Mix(p.cardBorder, p.textDim, 0.25f); break;
    case ButtonKind::Plain:  bg = 0; bgHover = p.controlHover; bgActive = p.controlActive; fg = p.text; break;
    case ButtonKind::Danger: bg = p.bad; bgHover = Mix(p.bad, IM_COL32(255, 255, 255, 255), 0.14f); bgActive = Mix(p.bad, IM_COL32(0, 0, 0, 255), 0.18f); fg = p.accentText; break;
    default:                 bg = p.control; bgHover = p.controlHover; bgActive = p.controlActive; fg = p.text; break;
    }
}

// The frame every button shares: sizing, behaviour, a hover that fades in, the navigation cursor.
bool ButtonFrame(ImGuiID id, const ImVec2& size, ImU32 bg, ImU32 bgHover, ImU32 bgActive, ImU32 border, ImRect& bb, bool& hovered) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = window->DC.CursorPos;
    bb = ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size, style.FramePadding.y);
    hovered = false;
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const float hov = Animate(id ^ kHoverKey, hovered ? 1.0f : 0.0f, 16.0f);
    const ImU32 col = held ? bgActive : Mix(bg, bgHover, hov);
    ImGui::RenderNavCursor(bb, id);
    if (col & IM_COL32_A_MASK) window->DrawList->AddRectFilled(bb.Min, bb.Max, Col(col), style.FrameRounding);
    if (border & IM_COL32_A_MASK) window->DrawList->AddRect(bb.Min, bb.Max, Col(border), style.FrameRounding, 1.0f);
    return pressed;
}

bool TextButton(const char* label, const ImVec2& sizeArg, ButtonKind kind) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    if (!SearchMatch(label)) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    ImVec2 size = ImGui::CalcItemSize(sizeArg, labelSize.x + style.FramePadding.x * 2.0f, labelSize.y + style.FramePadding.y * 2.0f);
    // A label longer than the rest of the row (a long translation in a narrow sidebar): the button stops at the edge
    // and its label ends in an ellipsis.
    if (sizeArg.x == 0.0f) size.x = ImMin(size.x, ImMax(window->WorkRect.Max.x - window->DC.CursorPos.x, ImGui::GetFrameHeight()));
    ImU32 bg, bgHover, bgActive, fg, border;
    ButtonColors(kind, bg, bgHover, bgActive, fg, border);
    ImRect bb;
    bool hovered = false;
    const bool pressed = ButtonFrame(id, size, bg, bgHover, bgActive, border, bb, hovered);
    if (bb.GetWidth() <= 0.0f) return false;
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    const ImVec2 textMin(bb.Min.x + style.FramePadding.x, bb.Min.y + style.FramePadding.y), textMax(bb.Max.x - style.FramePadding.x, bb.Max.y - style.FramePadding.y);
    if (labelSize.x > textMax.x - textMin.x) {
        ImGui::RenderTextEllipsis(window->DrawList, textMin, textMax, textMax.x, label, nullptr, &labelSize);
        if (hovered) Tooltip(std::string(label, ImGui::FindRenderedTextEnd(label)).c_str());   // the whole label
    } else {
        ImGui::RenderTextClipped(textMin, textMax, label, nullptr, &labelSize, style.ButtonTextAlign, &bb);
    }
    ImGui::PopStyleColor();
    return pressed;
}

bool GlyphFrame(const char* id, const ImVec2& sizeArg, const char* tooltip, ButtonKind kind, ImVec2& center, float& iconSize, ImU32& fg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 size = ImGui::CalcItemSize(sizeArg, h, h);
    ImU32 bg, bgHover, bgActive, border;
    ButtonColors(kind, bg, bgHover, bgActive, fg, border);
    ImRect bb;
    bool hovered = false;
    const bool pressed = ButtonFrame(window->GetID(id), size, bg, bgHover, bgActive, border, bb, hovered);
    if (bb.GetWidth() <= 0.0f) { iconSize = 0.0f; return false; }
    center = bb.GetCenter();
    // The usual icon size unless the button is much smaller or larger than a frame (each icon size is baked apart).
    const float fit = ImMax(8.0f, IM_ROUND(ImMin(size.x, size.y) * 0.62f));
    iconSize = ImFabs(fit - IconSize()) <= 3.0f ? IconSize() : fit;
    fg = Col(fg);
    Tooltip(tooltip);
    return pressed;
}

float IconGap() { return IM_ROUND(ImGui::GetStyle().ItemInnerSpacing.x * 0.75f); }
}

bool FlatButton(const char* label, const ImVec2& size) { return TextButton(label, size, ButtonKind::Flat); }
bool AccentButton(const char* label, const ImVec2& size) { return TextButton(label, size, ButtonKind::Accent); }
bool GhostButton(const char* label, const ImVec2& size) { return TextButton(label, size, ButtonKind::Ghost); }

float IconTextButtonWidth(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float tw = ImGui::CalcTextSize(label, nullptr, true).x;
    return style.FramePadding.x * 2.0f + IconSize() + (tw > 0.0f ? IconGap() + tw : 0.0f);
}

bool IconTextButton(const char* label, Icon icon, const ImVec2& sizeArg, ButtonKind kind) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    if (!SearchMatch(label)) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 size = ImGui::CalcItemSize(sizeArg, IconTextButtonWidth(label), labelSize.y + style.FramePadding.y * 2.0f);
    ImU32 bg, bgHover, bgActive, fg, border;
    ButtonColors(kind, bg, bgHover, bgActive, fg, border);
    ImRect bb;
    bool hovered = false;
    const bool pressed = ButtonFrame(id, size, bg, bgHover, bgActive, border, bb, hovered);
    if (bb.GetWidth() <= 0.0f) return false;
    // Icon and label together, centred in the button (or from the left edge when the button is too narrow).
    const float is = IconSize();
    const float contentW = is + (labelSize.x > 0.0f ? IconGap() + labelSize.x : 0.0f);
    const float x = bb.Min.x + ImMax(style.FramePadding.x, IM_ROUND((bb.GetWidth() - contentW) * 0.5f));
    ImDrawList* dl = window->DrawList;
    DrawIcon(dl, icon, ImVec2(x + is * 0.5f, bb.GetCenter().y), is, Col(fg));
    if (labelSize.x > 0.0f) {
        ImGui::PushStyleColor(ImGuiCol_Text, fg);
        ImGui::RenderTextClipped(ImVec2(x + is + IconGap(), bb.Min.y + style.FramePadding.y),
                                 ImVec2(bb.Max.x - style.FramePadding.x * 0.5f, bb.Max.y - style.FramePadding.y),
                                 label, nullptr, &labelSize, ImVec2(0.0f, 0.5f), &bb);
        ImGui::PopStyleColor();
    }
    return pressed;
}

bool DangerButton(const char* label, const ImVec2& sizeArg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    if (!SearchMatch(label)) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 size = ImGui::CalcItemSize(sizeArg, labelSize.x + style.FramePadding.x * 2.0f, labelSize.y + style.FramePadding.y * 2.0f);
    ImRect bb;
    bool hovered = false;
    const bool pressed = ButtonFrame(id, size, p.bad, Mix(p.bad, IM_COL32(255, 255, 255, 255), 0.14f), Mix(p.bad, IM_COL32(0, 0, 0, 255), 0.18f), 0, bb, hovered);
    if (bb.GetWidth() <= 0.0f) return false;
    ImGui::PushStyleColor(ImGuiCol_Text, p.accentText);
    ImGui::RenderTextClipped(ImVec2(bb.Min.x + style.FramePadding.x, bb.Min.y + style.FramePadding.y),
                             ImVec2(bb.Max.x - style.FramePadding.x, bb.Max.y - style.FramePadding.y),
                             label, nullptr, &labelSize, style.ButtonTextAlign, &bb);
    ImGui::PopStyleColor();
    return pressed;
}

bool IconButton(const char* id, Icon icon, const ImVec2& size, const char* tooltip, ButtonKind kind) {
    ImVec2 center; float iconSize = 0.0f; ImU32 fg = 0;
    const bool pressed = GlyphFrame(id, size, tooltip, kind, center, iconSize, fg);
    if (iconSize > 0.0f) DrawIcon(ImGui::GetWindowDrawList(), icon, center, iconSize, fg);
    return pressed;
}

bool ChevronButton(const char* id, float angle, const ImVec2& size, const char* tooltip, ButtonKind kind) {
    ImVec2 center; float iconSize = 0.0f; ImU32 fg = 0;
    const bool pressed = GlyphFrame(id, size, tooltip, kind, center, iconSize, fg);
    if (iconSize > 0.0f) DrawChevron(ImGui::GetWindowDrawList(), center, iconSize * 0.625f, angle, fg);
    return pressed;
}

// Widgets -----------------------------------------------------------------------------------

bool Toggle(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    if (!SearchMatch(label)) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowH = ImGui::GetFrameHeight();
    const float height = IM_ROUND(rowH * 0.72f);
    const float width = IM_ROUND(height * 1.8f);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 labelPos(pos.x + width + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y);
    const ImVec2 labelSize = SideLabelSize(label, labelPos.x);
    const ImRect total(pos, ImVec2(pos.x + width + (labelSize.x > 0 ? style.ItemInnerSpacing.x + labelSize.x : 0),
                                   ImMax(pos.y + rowH, labelPos.y + labelSize.y + style.FramePadding.y)));
    const ImGuiID id = window->GetID(label);
    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(total, id, &hovered, &held);
    if (pressed) { *v = !*v; ImGui::MarkItemEdited(id); }
    // The knob slides and the track's colour crosses over instead of jumping.
    const float on = Animate(id, *v ? 1.0f : 0.0f, 18.0f);
    const float hov = Animate(id ^ kHoverKey, hovered ? 1.0f : 0.0f, 16.0f);
    const Palette& p = Colors();
    const ImU32 offTrack = Mix(Mix(p.controlActive, p.textDim, 0.18f), Mix(p.controlActive, p.textDim, 0.3f), hov);
    const ImU32 bg = Mix(offTrack, Mix(p.accent, p.accentHover, hov), on);
    ImDrawList* dl = window->DrawList;
    const float radius = height * 0.5f;
    const float y0 = pos.y + IM_ROUND((rowH - height) * 0.5f);
    ImGui::RenderNavCursor(total, id);
    dl->AddRectFilled(ImVec2(pos.x, y0), ImVec2(pos.x + width, y0 + height), Col(bg), radius);
    const float knobR = radius - ImMax(2.0f, Px(2.5f));
    const ImVec2 knob(pos.x + radius + (width - height) * Ease(on), y0 + radius);
    dl->AddCircleFilled(ImVec2(knob.x, knob.y + ImMax(1.0f, Px(1.0f))), knobR, Col(WithAlpha(p.shadow, Alpha(p.shadow) * 0.8f)), 0);
    dl->AddCircleFilled(knob, knobR, Col(IM_COL32(255, 255, 255, 255)), 0);
    if (labelSize.x > 0) SideLabel(label, labelPos);
    return pressed;
}

bool Checkbox(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    if (!SearchMatch(label)) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const float rowH = ImGui::GetFrameHeight();
    const float box = IM_ROUND(rowH * 0.66f);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 labelPos(pos.x + box + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y);
    const ImVec2 labelSize = SideLabelSize(label, labelPos.x);
    const ImRect total(pos, ImVec2(pos.x + box + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f),
                                   ImMax(pos.y + rowH, labelPos.y + labelSize.y + style.FramePadding.y)));
    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(total, id, &hovered, &held);
    if (pressed) { *v = !*v; ImGui::MarkItemEdited(id); }
    const float on = Animate(id, *v ? 1.0f : 0.0f, 20.0f);
    const float hov = Animate(id ^ kHoverKey, hovered ? 1.0f : 0.0f, 16.0f);
    const Palette& p = Colors();
    ImDrawList* dl = window->DrawList;
    const ImVec2 b0(pos.x, pos.y + IM_ROUND((rowH - box) * 0.5f));
    const ImVec2 b1(b0.x + box, b0.y + box);
    const float r = ImMax(2.0f, box * 0.24f);
    ImGui::RenderNavCursor(total, id);
    const ImU32 off = Mix(p.control, p.controlHover, hov);
    dl->AddRectFilled(b0, b1, Col(Mix(off, Mix(p.accent, p.accentHover, hov), on)), r);
    if (on < 0.999f)
        dl->AddRect(b0, b1, Col(WithAlpha(Mix(p.controlActive, p.textDim, 0.35f + 0.3f * hov), 1.0f - on)), r, ImMax(1.0f, Px(1.25f)));
    if (on > 0.001f) DrawGlyph(dl, lucide::check, ImVec2((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f), IconSize(), Col(WithAlpha(p.accentText, on)));
    if (labelSize.x > 0.0f) SideLabel(label, labelPos);
    return pressed;
}

bool Radio(const char* label, bool active) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const float rowH = ImGui::GetFrameHeight();
    const float d = IM_ROUND(rowH * 0.66f);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 labelPos(pos.x + d + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y);
    const ImVec2 labelSize = SideLabelSize(label, labelPos.x);
    const ImRect total(pos, ImVec2(pos.x + d + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f),
                                   ImMax(pos.y + rowH, labelPos.y + labelSize.y + style.FramePadding.y)));
    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(total, id, &hovered, &held);
    if (pressed) ImGui::MarkItemEdited(id);
    const float on = Animate(id, active ? 1.0f : 0.0f, 20.0f);
    const float hov = Animate(id ^ kHoverKey, hovered ? 1.0f : 0.0f, 16.0f);
    const Palette& p = Colors();
    ImDrawList* dl = window->DrawList;
    const ImVec2 c(pos.x + d * 0.5f, pos.y + rowH * 0.5f);
    const float r = d * 0.5f;
    ImGui::RenderNavCursor(total, id);
    const ImU32 off = Mix(p.control, p.controlHover, hov);
    dl->AddCircleFilled(c, r, Col(Mix(off, Mix(p.accent, p.accentHover, hov), on)), 0);
    if (on < 0.999f) dl->AddCircle(c, r, Col(WithAlpha(Mix(p.controlActive, p.textDim, 0.35f + 0.3f * hov), 1.0f - on)), 0, ImMax(1.0f, Px(1.25f)));
    if (on > 0.001f) dl->AddCircleFilled(c, r * 0.42f * on, Col(WithAlpha(p.accentText, on)), 0);
    if (labelSize.x > 0.0f) SideLabel(label, labelPos);
    return pressed;
}

void FocusRing() {
    ImGuiContext& g = *GImGui;
    const bool editing = ImGui::IsItemActive() && g.InputTextState.ID == ImGui::GetItemID();
    if (!editing && !(ImGui::IsItemFocused() && g.NavCursorVisible)) return;
    const float t = Ease(AnimateFrom(ImGui::GetItemID() ^ 0x7F4A7C15u, 0.0f, 1.0f, 20.0f));
    const float grow = Px(1.5f);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - grow, a.y - grow), ImVec2(b.x + grow, b.y + grow),
                                        Col(WithAlpha(Colors().accent, 0.75f * t)), ImGui::GetStyle().FrameRounding + grow, ImMax(1.0f, Px(1.5f)));
}

namespace {
// Sections are cards. The header's row and the content are drawn on the upper channel of a splitter; SectionEnd,
// knowing where the content ended, paints the card under them on the lower one. The content folds with a clip that
// grows or shrinks over a fifth of a second; the full height of each section's content is remembered from the last
// frame it was drawn in.
struct SectionFrame {
    ImGuiID id = 0;
    ImGuiWindow* window = nullptr;
    ImDrawListSplitter splitter;
    float t = 1.0f;
    float top = 0.0f, headerBottom = 0.0f, startY = 0.0f;
    float x0 = 0.0f, x1 = 0.0f;
    float inset = 0.0f;
    float workMaxX = 0.0f, contentMaxX = 0.0f;
    bool clipped = false;
};
SectionFrame g_sections[8];
int g_sectionDepth = 0;
ImGuiStorage g_sectionHeights;

// From the header's bottom to the card's bottom when the section is open: the gap, the content, the card's padding.
float SectionFullHeight(const SectionFrame& f, float contentH) {
    return (f.startY - f.headerBottom) + contentH + IM_ROUND(f.inset * 0.85f);
}
}

bool SectionHeader(const char* label, const char* id, bool defaultOpen, Icon icon) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    ImGui::PushID(id);
    const ImGuiID hid = window->GetID("##header");
    ImGuiStorage* storage = window->DC.StateStorage;
    bool open = storage->GetBool(hid, defaultOpen);
    if (g_searching) {
        // Filtering: the section shows open when its title matches or one of its widgets did on the last frame it was
        // drawn; a section that showed nothing stays out until the query changes.
        CloseSearchSection();
        g_searchSection = hid;
        g_sectionAll = Contains(label);
        if (!g_sectionAll && g_searchHits.GetInt(hid, 1) == 0) { ImGui::PopID(); return false; }
        open = true;
    }

    const float inset = CardInset();
    const float h = IM_ROUND(ImGui::GetFrameHeight() + Px(10.0f));
    const ImVec2 pos = window->DC.CursorPos;
    const float w = ImMax(ImGui::GetContentRegionAvail().x, h * 3.0f);
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(ImVec2(w, h), 0.0f);
    bool hovered = false, held = false, pressed = false;
    if (ImGui::ItemAdd(bb, hid)) pressed = ImGui::ButtonBehavior(bb, hid, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    if (pressed && !g_searching) { open = !open; storage->SetBool(hid, open); }
    const float t = AnimateLinear(hid ^ 0x51ED270Bu, open ? 1.0f : 0.0f, 0.2f);
    const float hov = Animate(hid ^ kHoverKey, (hovered || held) ? 1.0f : 0.0f, 16.0f);
    const bool expanded = t > 0.001f && g_sectionDepth < (int)IM_ARRAYSIZE(g_sections);

    ImDrawList* dl = window->DrawList;
    SectionFrame* f = expanded ? &g_sections[g_sectionDepth++] : nullptr;
    if (f) {
        f->splitter.Split(dl, 2);
        f->splitter.SetCurrentChannel(dl, 1);
    } else {
        DrawCard(dl, bb.Min, bb.Max);   // folded: the card is the header alone
    }
    if (hov > 0.001f)
        dl->AddRectFilled(ImVec2(bb.Min.x + 1.0f, bb.Min.y + 1.0f), ImVec2(bb.Max.x - 1.0f, bb.Max.y - (f ? 0.0f : 1.0f)),
                          Col(WithAlpha(p.controlHover, (held ? 0.9f : 0.55f) * hov)), CardRounding() - 1.0f,
                          f ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
    ImGui::RenderNavCursor(bb, hid);
    float x = bb.Min.x + inset;
    if (icon != Icon::None) {
        const float is = IconSize();
        DrawIcon(dl, icon, ImVec2(x + is * 0.5f, bb.GetCenter().y), is, Col(Mix(p.accentHover, p.accent, p.light)));
        x += is + IM_ROUND(style.ItemInnerSpacing.x * 1.1f);
    }
    const float chevronW = IconSize(0.62f);
    ImGui::PushFont(BoldFont(), 0.0f);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float titleMax = bb.Max.x - inset - chevronW - style.ItemInnerSpacing.x;   // a long title ends in an ellipsis before the chevron
    const float titleY = IM_ROUND(bb.Min.y + (bb.GetHeight() - labelSize.y) * 0.5f);
    if (titleMax > x)
        ImGui::RenderTextEllipsis(dl, ImVec2(x, titleY), ImVec2(titleMax, titleY + labelSize.y), titleMax, label, ImGui::FindRenderedTextEnd(label), &labelSize);
    ImGui::PopFont();
    DrawChevron(dl, ImVec2(bb.Max.x - inset - chevronW * 0.5f, bb.GetCenter().y), chevronW, Ease(t) * IM_PI, Col(Mix(p.textDim, p.text, hov)));
    ImGui::PopID();

    if (!f) return false;
    // The content: inset from the card's edges on both sides, starting just under the header's row.
    window->DC.CursorPos.y = bb.Max.y + Px(2.0f);
    f->id = hid; f->window = window; f->t = t;
    f->top = bb.Min.y; f->headerBottom = bb.Max.y; f->startY = window->DC.CursorPos.y;
    f->x0 = bb.Min.x; f->x1 = bb.Max.x;
    f->inset = inset;
    f->workMaxX = window->WorkRect.Max.x;
    f->contentMaxX = window->ContentRegionRect.Max.x;
    f->clipped = false;
    ImGui::Indent(inset);
    window->WorkRect.Max.x = ImMin(window->WorkRect.Max.x, bb.Max.x - inset);
    window->ContentRegionRect.Max.x = ImMin(window->ContentRegionRect.Max.x, bb.Max.x - inset);
    if (t < 0.999f) {   // folding: the content shows as far down as the card reaches
        const float shownBottom = bb.Max.y + SectionFullHeight(*f, g_sectionHeights.GetFloat(hid, 0.0f)) * Ease(t);
        ImGui::PushClipRect(ImVec2(window->ClipRect.Min.x, bb.Max.y), ImVec2(window->ClipRect.Max.x, shownBottom), true);
        f->clipped = true;
    }
    return true;
}

void SectionEnd() {
    if (g_sectionDepth <= 0) return;
    SectionFrame& f = g_sections[--g_sectionDepth];
    ImGuiWindow* window = f.window;
    const ImGuiStyle& style = ImGui::GetStyle();
    // The content ended one item spacing above the cursor.
    const float contentH = ImMax(0.0f, window->DC.CursorPos.y - style.ItemSpacing.y - f.startY);
    g_sectionHeights.SetFloat(f.id, contentH);
    if (f.clipped) ImGui::PopClipRect();
    ImGui::Unindent(f.inset);
    window->WorkRect.Max.x = f.workMaxX;
    window->ContentRegionRect.Max.x = f.contentMaxX;
    // Folding, the card's bottom moves between the header's and the content's; folded it is the header alone.
    const float bottom = f.headerBottom + SectionFullHeight(f, contentH) * Ease(f.t);
    ImDrawList* dl = window->DrawList;
    f.splitter.SetCurrentChannel(dl, 0);
    DrawCard(dl, ImVec2(f.x0, f.top), ImVec2(f.x1, bottom));
    f.splitter.Merge(dl);
    window->DC.CursorPos.y = bottom + style.ItemSpacing.y;
    window->DC.CursorMaxPos.y = bottom;
    window->DC.IdealMaxPos.y = ImMin(window->DC.IdealMaxPos.y, window->DC.CursorMaxPos.y);
    window->DC.PrevLineSize.y = 0.0f;
    window->DC.CurrLineSize.y = 0.0f;
}

namespace {
// Panels, like sections, draw their widgets on the upper channel of a splitter and the card under them on the
// lower one once PanelEnd knows how tall they came out.
struct PanelFrame {
    ImGuiWindow* window = nullptr;
    ImDrawListSplitter splitter;
    ImVec2 min, pad;
    float x1 = 0.0f;
    ImU32 stripe = 0;
    float workMaxX = 0.0f, contentMaxX = 0.0f;
};
PanelFrame g_panels[4];
int g_panelDepth = 0;
}

void PanelBegin(float width, const ImVec2& pad, ImU32 stripe) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    IM_ASSERT(g_panelDepth < (int)IM_ARRAYSIZE(g_panels));
    PanelFrame& f = g_panels[g_panelDepth++];
    f.window = window;
    f.min = window->DC.CursorPos;
    f.pad = pad;
    f.x1 = f.min.x + (width > 0.0f ? width : ImGui::GetContentRegionAvail().x);
    f.stripe = stripe;
    f.workMaxX = window->WorkRect.Max.x;
    f.contentMaxX = window->ContentRegionRect.Max.x;
    ImDrawList* dl = window->DrawList;
    f.splitter.Split(dl, 2);
    f.splitter.SetCurrentChannel(dl, 1);
    // Wrapped text and full-width widgets end at the panel's inner edge.
    window->WorkRect.Max.x = ImMin(window->WorkRect.Max.x, f.x1 - pad.x);
    window->ContentRegionRect.Max.x = ImMin(window->ContentRegionRect.Max.x, f.x1 - pad.x);
    ImGui::SetCursorScreenPos(ImVec2(f.min.x + pad.x + (stripe ? Px(4.0f) : 0.0f), f.min.y + pad.y));
    ImGui::BeginGroup();
}

void PanelEnd() {
    if (g_panelDepth <= 0) return;
    PanelFrame& f = g_panels[--g_panelDepth];
    ImGuiWindow* window = f.window;
    ImGui::EndGroup();
    const float bottom = IM_ROUND(ImGui::GetItemRectMax().y + f.pad.y);
    window->WorkRect.Max.x = f.workMaxX;
    window->ContentRegionRect.Max.x = f.contentMaxX;
    ImDrawList* dl = window->DrawList;
    f.splitter.SetCurrentChannel(dl, 0);
    const ImVec2 max(f.x1, bottom);
    DrawCard(dl, f.min, max);
    if (f.stripe & IM_COL32_A_MASK) {
        // The band follows the card's rounded corners: the card's shape, cut off a few pixels from its left edge.
        dl->PushClipRect(f.min, ImVec2(f.min.x + ImMax(3.0f, Px(4.0f)), bottom), true);
        dl->AddRectFilled(f.min, max, Col(f.stripe), CardRounding());
        dl->PopClipRect();
    }
    f.splitter.Merge(dl);
    ImGui::SetCursorScreenPos(f.min);
    ImGui::Dummy(ImVec2(f.x1 - f.min.x, bottom - f.min.y));
}

void SectionLabel(const char* text) {
    if (g_searching && !g_sectionAll) return;   // a group caption without its group would mislead
    const Palette& p = Colors();
    ImGui::Dummy(ImVec2(0.0f, Px(2.0f)));
    ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
    ImGui::PushFont(BoldFont(), ImGui::GetStyle().FontSizeBase * 0.86f);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    // A hairline runs on from the caption to the right edge.
    const float x0 = a.x + ts.x + ImGui::GetStyle().ItemInnerSpacing.x;
    const float x1 = a.x + ImGui::GetContentRegionAvail().x;
    if (x1 > x0) ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, IM_ROUND(a.y + ts.y * 0.55f)), ImVec2(x1, IM_ROUND(a.y + ts.y * 0.55f)), Col(p.cardBorder));
}

void Help(const char* text) {
    if (g_searchSkipped) return;   // it belongs to a widget that was left out
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    const float d = IconSize();
    // No room for the mark right of the control (a narrow sidebar, a wrapped label): the control itself shows the tip.
    if (window->DC.CursorPosPrevLine.x + ImGui::GetStyle().ItemSpacing.x + d > window->WorkRect.Max.x) { Tooltip(text); return; }
    ImGui::SameLine();
    const Palette& p = Colors();
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImRect bb(pos, ImVec2(pos.x + d, pos.y + lineH));
    const ImGuiID id = window->GetID(text);
    ImGui::ItemSize(ImVec2(d, lineH), 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return;
    const float hov = Animate(id ^ kHoverKey, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
    DrawGlyph(window->DrawList, lucide::circle_question_mark, ImVec2(pos.x + d * 0.5f, pos.y + lineH * 0.5f), d,
              Col(Mix(WithAlpha(p.textDim, 0.85f), p.accentHover, hov)));
    Tooltip(text);
}

void Hint(const char* text) {
    if (g_searching && !g_sectionAll) return;
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().textDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// Inline badges sit on the text baseline of their line, like text does: after a framed control (a toggle, a
// button) the line's baseline offset moves them down to the control's text, so a row never looks staggered.
void StatusDot(ImU32 color, const char* text) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    ImDrawList* dl = window->DrawList;
    const float h = ImGui::GetTextLineHeight();
    const float r = IM_ROUND(h * 0.22f);
    const ImVec2 pos = window->DC.CursorPos;
    const float y = pos.y + window->DC.CurrLineTextBaseOffset + h * 0.5f;
    dl->AddCircleFilled(ImVec2(pos.x + r + 1.0f, y), r + ImMax(1.0f, Px(2.0f)), Col(WithAlpha(color, 0.22f)), 0);
    dl->AddCircleFilled(ImVec2(pos.x + r + 1.0f, y), r, Col(color), 0);
    ImGui::Dummy(ImVec2(r * 2.0f + Px(4.0f), h));
    ImGui::SameLine(0.0f, Px(4.0f));
    ImGui::PushTextWrapPos(0.0f);   // a long line (a Japanese runtime note, a mirror error) wraps under itself, never past the edge
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

void Pill(const char* text, ImU32 bg, ImU32 fg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(Px(9.0f), Px(3.0f));
    const ImVec2 box(size.x + pad.x * 2.0f, size.y + pad.y * 2.0f);
    ImVec2 pos = window->DC.CursorPos;
    pos.y += ImMax(0.0f, window->DC.CurrLineTextBaseOffset - pad.y);
    const ImRect bb(pos, ImVec2(pos.x + box.x, pos.y + box.y));
    ImGui::ItemSize(box, pad.y);
    if (!ImGui::ItemAdd(bb, 0)) return;
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(bb.Min, bb.Max, Col(bg), box.y * 0.5f);
    dl->AddText(ImVec2(bb.Min.x + pad.x, bb.Min.y + pad.y), Col(fg), text);
}

void PillAfter(const char* text, ImU32 bg, ImU32 fg, float spacing) {
    if (g_searchSkipped) return;   // it belongs to a widget that was left out
    SameLineIfRoom(ImGui::CalcTextSize(text).x + Px(9.0f) * 2.0f, spacing);
    Pill(text, bg, fg);
}

bool ResetButton(const char* id, bool modified, float size, const char* tooltip) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const float shown = Animate(window->GetID(id) ^ kShownKey, modified ? 1.0f : 0.0f, 16.0f);
    if (shown < 0.01f) {
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + size, pos.y + size));
        ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
        ImGui::ItemAdd(bb, 0);
        return false;
    }
    // While it fades out it takes no press. The disabled flag is only ever added here, never cleared, so the icon of a
    // control in a disabled section stays disabled.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * shown);
    if (!modified) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    const bool pressed = IconButton(id, Icon::Reset, ImVec2(size, size), modified ? tooltip : nullptr, ButtonKind::Plain);
    if (!modified) ImGui::PopItemFlag();
    ImGui::PopStyleVar();
    return pressed && modified;
}

namespace {
// After a reset slider: the icon, then the label, over which the slider's tooltip shows as well.
bool ResetTail(const char* label, const char* tooltip, bool modified, float resetW) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID slider = ImGui::GetItemID();
    bool tip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    const bool reset = ResetButton("##reset", modified, resetW);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    TrailingLabel(label);
    tip = tip || ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
    if (tip && tooltip) TooltipShow(slider, tooltip);
    return reset;
}
}

// The label column. Labels are measured as they are drawn; the widest one so far sets the column, so the first
// frame after a new label appears (the Advanced sections open, the language changes) still cuts it and the next
// frame does not.
static float      s_labelEm = 0.0f;
static Lang       s_labelLang = Lang::English;

void LabelSeen(const char* label) {
    if (!label) return;
    const char* end = ImGui::FindRenderedTextEnd(label);   // a "##id" label has nothing to show
    if (end == label) return;
    s_labelEm = std::max(s_labelEm, ImGui::CalcTextSize(label, end).x / ImGui::GetFontSize());
}

void TrailingLabel(const char* label) {
    LabelSeen(label);
    ImGui::PushTextWrapPos(0.0f);   // a label longer than its column (the column is at most half the row) wraps under itself
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
}

void LabelAfterItem(const char* label) {
    ImGuiContext& g = *GImGui;
    const ImGuiLastItemData item = g.LastItemData;
    ImGui::SameLine(0.0f, g.Style.ItemInnerSpacing.x);
    TrailingLabel(label);
    const bool labelHovered = (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_HoveredRect) != 0;
    g.LastItemData = item;
    if (labelHovered) g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredRect;
}

float LabelColumn(float rowWidth) {
    if (I18n::Current() != s_labelLang) { s_labelLang = I18n::Current(); s_labelEm = 0.0f; }
    const float em = ImGui::GetFontSize();
    return std::min(std::max(7.5f, s_labelEm + 0.9f) * em, rowWidth * 0.5f);   // 0.9 em: the inner spacing and some air
}

// A row of buttons wraps instead of running past the edge: the next button stays on the line only when it fits
// in the room right of the last item.
void SameLineIfFits(const char* buttonLabel) {
    const ImGuiStyle& style = ImGui::GetStyle();
    SameLineIfRoom(ImGui::CalcTextSize(buttonLabel, ImGui::FindRenderedTextEnd(buttonLabel)).x + style.FramePadding.x * 2.0f, style.ItemSpacing.x);
}

void SameLineIfRoom(float width, float spacing) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->DC.CursorPosPrevLine.x + spacing + width <= window->WorkRect.Max.x) ImGui::SameLine(0.0f, spacing);
}

// Sliders -----------------------------------------------------------------------------------

namespace {
ImDrawListSplitter g_sliderSplit;

// The ImGui slider draws its grab and its text over a frame and a fill painted here underneath (on the lower channel
// of a splitter). The fill ends at the grab's centre, placed as SliderBehavior places it.
template <typename T, typename Fn>
bool FilledSlider(const T* v, T minV, T maxV, bool isInt, bool logarithmic, Fn&& slider) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return slider();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 pos = window->DC.CursorPos;
    const float w = ImGui::CalcItemWidth();
    const ImRect frame(pos, ImVec2(pos.x + w, pos.y + ImGui::GetFrameHeight()));
    ImDrawList* dl = window->DrawList;
    g_sliderSplit.Split(dl, 2);
    g_sliderSplit.SetCurrentChannel(dl, 1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 0, 0, 0));
    const bool changed = slider();
    ImGui::PopStyleColor(5);
    const ImGuiID id = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    g_sliderSplit.SetCurrentChannel(dl, 0);
    const Palette& p = Colors();
    const float hov = Animate(id ^ kHoverKey, (hovered || active) ? 1.0f : 0.0f, 16.0f);
    dl->AddRectFilled(frame.Min, frame.Max, Col(active ? p.controlActive : Mix(p.control, p.controlHover, hov)), style.FrameRounding);
    if (!ImGui::TempInputIsActive(id) && maxV > minV) {
        constexpr float kGrabPad = 2.0f;
        const float sliderSz = frame.GetWidth() - kGrabPad * 2.0f;
        float grabSz = style.GrabMinSize;
        if (isInt) grabSz = ImMax(sliderSz / (float)((double)maxV - (double)minV + 1.0), style.GrabMinSize);
        grabSz = ImMin(grabSz, sliderSz);
        // The fill ends where ImGui puts the grab: a logarithmic slider (the zoom) spaces its values by their ratio.
        const bool logScale = logarithmic && (double)minV > 0.0;
        const float frac = logScale ? ImSaturate((float)(std::log((double)*v / (double)minV) / std::log((double)maxV / (double)minV)))
                                    : ImSaturate((float)(((double)*v - (double)minV) / ((double)maxV - (double)minV)));
        const float x = frame.Min.x + kGrabPad + grabSz * 0.5f + (sliderSz - grabSz) * frac;
        dl->AddRectFilled(frame.Min, ImVec2(x, frame.Max.y), Col(WithAlpha(p.accent, 0.34f - 0.12f * p.light + 0.08f * hov)),
                          style.FrameRounding, ImDrawFlags_RoundCornersLeft);
        // The value's place: two short ticks at the end of the fill, above and below the figures (a grab block or a
        // line across them cut "1.00" in two when the value sat in the middle).
        const float lw = std::max(1.0f, Px(2.0f));
        const float lx = std::round(x - lw * 0.5f);
        const float tick = std::max(Px(3.0f), std::floor((frame.GetHeight() - ImGui::GetTextLineHeight()) * 0.5f) - Px(1.0f));
        const ImU32 tc = Col(active ? p.accentHover : Mix(p.accent, p.accentHover, hov));
        dl->AddRectFilled(ImVec2(lx, frame.Min.y), ImVec2(lx + lw, frame.Min.y + tick), tc);
        dl->AddRectFilled(ImVec2(lx, frame.Max.y - tick), ImVec2(lx + lw, frame.Max.y), tc);
    }
    g_sliderSplit.Merge(dl);
    return changed;
}
}

// ImGui clips a slider's text at its frame. Where it would be, the format is fitted first: a figure drops the words
// around it ("40 Mbit/s" -> "40"), a text drops a trailing part in brackets ("Off (every frame)" -> "Off"), and what
// still does not fit ends in an ellipsis. A figure keeps its conversion, so rounding and typing a value in work as before.
template <typename T>
const char* FitSliderFormat(const char* fmt, T v, char* out, size_t size) {
    const float room = ImGui::CalcItemWidth() - ImGui::GetStyle().FramePadding.x * 2.0f;
    char shown[128];
    ImFormatString(shown, sizeof(shown), fmt, v);
    if (ImGui::CalcTextSize(shown).x <= room) return fmt;
    const char* spec = ImParseFormatFindStart(fmt);
    if (spec[0] == '%') {
        ImStrncpy(out, spec, ImMin((size_t)(ImParseFormatFindEnd(spec) - spec) + 1, size));
        return out;
    }
    std::string text = shown;
    const size_t n = text.size();
    const bool bracket = n > 0 && (text.back() == ')' || (n >= 3 && text.compare(n - 3, 3, "\xEF\xBC\x89") == 0));
    size_t open = std::string::npos;
    if (bracket) {
        const size_t a = text.rfind('('), b = text.rfind("\xEF\xBC\x88");
        open = a == std::string::npos ? b : b == std::string::npos ? a : std::max(a, b);
    }
    if (open != std::string::npos && open > 0) {
        text.erase(open);
        while (!text.empty() && text.back() == ' ') text.pop_back();
    }
    if (ImGui::CalcTextSize(text.c_str()).x > room) {
        const char* ellipsis = "\xE2\x80\xA6";
        while (!text.empty() && ImGui::CalcTextSize((text + ellipsis).c_str()).x > room) {
            while (!text.empty() && ((unsigned char)text.back() & 0xC0) == 0x80) text.pop_back();   // a whole UTF-8 character
            if (!text.empty()) text.pop_back();
        }
        text += ellipsis;
    }
    std::string escaped;   // the text becomes the format
    for (char c : text) { escaped += c; if (c == '%') escaped += '%'; }
    ImStrncpy(out, escaped.c_str(), size);
    return out;
}

bool SliderFloatFill(const char* label, float* v, float minV, float maxV, const char* fmt, ImGuiSliderFlags flags) {
    const bool log = (flags & ImGuiSliderFlags_Logarithmic) != 0;
    char fitted[128];
    if (fmt) fmt = FitSliderFormat(fmt, (double)*v, fitted, sizeof(fitted));
    if (ImGui::FindRenderedTextEnd(label) == label)
        return FilledSlider(v, minV, maxV, false, log, [&] { return ImGui::SliderFloat(label, v, minV, maxV, fmt, flags); });
    ImGui::PushID(label);
    const bool changed = FilledSlider(v, minV, maxV, false, log, [&] { return ImGui::SliderFloat("##v", v, minV, maxV, fmt, flags); });
    ImGui::PopID();
    LabelAfterItem(label);
    return changed;
}

bool SliderIntFill(const char* label, int* v, int minV, int maxV, const char* fmt, ImGuiSliderFlags flags) {
    const bool log = (flags & ImGuiSliderFlags_Logarithmic) != 0;
    char fitted[128];
    if (fmt) fmt = FitSliderFormat(fmt, *v, fitted, sizeof(fitted));
    if (ImGui::FindRenderedTextEnd(label) == label)
        return FilledSlider(v, minV, maxV, true, log, [&] { return ImGui::SliderInt(label, v, minV, maxV, fmt, flags); });
    ImGui::PushID(label);
    const bool changed = FilledSlider(v, minV, maxV, true, log, [&] { return ImGui::SliderInt("##v", v, minV, maxV, fmt, flags); });
    ImGui::PopID();
    LabelAfterItem(label);
    return changed;
}

bool InputIntLabel(const char* label, int* v, int step, int stepFast) {
    // The -/+ buttons only while the field keeps room for six digits beside them.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttons = (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x) * 2.0f;
    if (ImGui::CalcItemWidth() - buttons < ImGui::CalcTextSize("000000").x + style.FramePadding.x * 2.0f) step = stepFast = 0;
    ImGui::PushID(label);
    const bool changed = ImGui::InputInt("##v", v, step, stepFast);
    ImGui::PopID();
    LabelAfterItem(label);
    return changed;
}

bool SliderReset(const char* label, float* v, float minV, float maxV, float def, const char* fmt, const char* tooltip) {
    if (!SearchMatch(label, tooltip)) return false;
    ImGui::PushID(label);
    const float resetW = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
    bool changed = SliderFloatFill("##s", v, minV, maxV, fmt, ImGuiSliderFlags_AlwaysClamp);
    if (ResetTail(label, tooltip, *v != def, resetW)) { *v = def; changed = true; }
    ImGui::PopID();
    return changed;
}

bool SliderIntReset(const char* label, int* v, int minV, int maxV, int def, const char* fmt, const char* tooltip) {
    if (!SearchMatch(label, tooltip)) return false;
    ImGui::PushID(label);
    const float resetW = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
    bool changed = SliderIntFill("##s", v, minV, maxV, fmt, ImGuiSliderFlags_AlwaysClamp);
    if (ResetTail(label, tooltip, *v != def, resetW)) { *v = def; changed = true; }
    ImGui::PopID();
    return changed;
}

// Segmented switch ----------------------------------------------------------------------------

namespace {
float SegmentWidth(const char* label, Icon icon) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float tw = ImGui::CalcTextSize(label, nullptr, true).x;
    const float iw = icon != Icon::None ? IconSize() : 0.0f;
    return IM_ROUND(style.FramePadding.x * 2.2f + iw + (iw > 0.0f && tw > 0.0f ? IconGap() : 0.0f) + tw);
}
}

float SegmentedWidth(const char* const* labels, int count, const Icon* icons) {
    float w = 0.0f;
    for (int i = 0; i < count; ++i) w += SegmentWidth(labels[i], icons ? icons[i] : Icon::None);
    return w;
}

bool Segmented(const char* id, const char* const* labels, int count, int* value, float width, const Icon* icons) {
    if (count <= 0) return false;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    const float h = ImGui::GetFrameHeight();
    auto segWidth = [&](int i) { return width > 0.0f ? width / (float)count : SegmentWidth(labels[i], icons ? icons[i] : Icon::None); };
    float w = 0.0f;
    for (int i = 0; i < count; ++i) w += segWidth(i);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::PushID(id);
    ImGui::ItemSize(total, style.FramePadding.y);
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(total.Min, total.Max, Col(p.control), style.FrameRounding);
    // The raised thumb slides to the chosen segment instead of jumping.
    float onX0 = 0.0f, onX1 = 0.0f;
    float runX = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float sw = segWidth(i);
        if (i == *value) { onX0 = runX; onX1 = runX + sw; }
        runX += sw;
    }
    const float inset = ImMax(2.0f, Px(3.0f));
    if (*value >= 0 && *value < count) {
        const float hx0 = Animate(window->GetID("##hl0"), onX0, 18.0f);
        const float hx1 = Animate(window->GetID("##hl1"), onX1, 18.0f);
        const ImVec2 t0(pos.x + hx0 + inset, pos.y + inset), t1(pos.x + hx1 - inset, pos.y + h - inset);
        const ImU32 thumb = Mix(Mix(p.controlActive, IM_COL32(255, 255, 255, 255), 0.05f), p.card, p.light);
        ShadowRing(dl, t0, t1, style.FrameRounding - 1.0f, Px(4.0f), Px(1.0f), Col(WithAlpha(p.shadow, Alpha(p.shadow) * 0.9f)));
        dl->AddRectFilled(t0, t1, Col(thumb), ImMax(0.0f, style.FrameRounding - 1.0f));
    }
    bool changed = false;
    float x = pos.x;
    for (int i = 0; i < count; ++i) {
        const float segW = segWidth(i);
        const ImRect bb(ImVec2(x, pos.y), ImVec2(x + segW, pos.y + h));
        const ImGuiID bid = window->GetID(i);
        bool hovered = false, held = false, pressed = false;
        if (ImGui::ItemAdd(bb, bid)) pressed = ImGui::ButtonBehavior(bb, bid, &hovered, &held);
        const bool on = (*value == i);
        const float hov = Animate(bid ^ kHoverKey, (hovered && !on) ? 1.0f : 0.0f, 16.0f);
        const float sel = Animate(bid ^ 0x2545F491u, on ? 1.0f : 0.0f, 18.0f);
        ImGui::RenderNavCursor(bb, bid);
        const ImU32 ink = Mix(Mix(p.textDim, p.text, hov), p.text, sel);
        const Icon icon = icons ? icons[i] : Icon::None;
        const ImVec2 ts = ImGui::CalcTextSize(labels[i], nullptr, true);
        const float iw = icon != Icon::None ? IconSize() : 0.0f;
        const float gap = (iw > 0.0f && ts.x > 0.0f) ? IconGap() : 0.0f;
        float cx = bb.Min.x + IM_ROUND((segW - (iw + gap + ts.x)) * 0.5f);
        if (iw > 0.0f) {
            DrawIcon(dl, icon, ImVec2(cx + iw * 0.5f, bb.GetCenter().y), iw, Col(Mix(ink, Mix(p.accentHover, p.accent, p.light), sel)));
            cx += iw + gap;
        }
        if (ts.x > 0.0f) dl->AddText(ImVec2(cx, bb.Min.y + IM_ROUND((h - ts.y) * 0.5f)), Col(ink), labels[i], ImGui::FindRenderedTextEnd(labels[i]));
        if (pressed && !on) { *value = i; changed = true; }
        x += segW;
    }
    ImGui::PopID();
    return changed;
}

void KeyValue(const char* key, const char* value) {
    if (!SearchMatch(key, value)) return;
    ImGui::TextDisabled("%s", key);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f + ImGui::GetCursorPosX() * 0.0f);
    ImGui::TextUnformatted(value);
}

} // namespace vdc::ui
