#include "ui/Theme.h"
#include "imgui_internal.h"
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace vdc::ui {

namespace {
ImVec4 C(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

// The two palettes; g_palette holds the blend in use.
const Palette kDark = {
    IM_COL32(79, 146, 255, 255),  IM_COL32(104, 164, 255, 255), IM_COL32(58, 122, 228, 255), IM_COL32(255, 255, 255, 255),
    IM_COL32(86, 208, 128, 255),  IM_COL32(255, 184, 64, 255),  IM_COL32(255, 92, 92, 255),  IM_COL32(136, 142, 156, 255),
    IM_COL32(24, 26, 31, 255),    IM_COL32(40, 43, 51, 255),    IM_COL32(10, 11, 14, 210),
    IM_COL32(230, 232, 238, 255), IM_COL32(148, 154, 168, 255),
    IM_COL32(36, 39, 47, 255),    IM_COL32(48, 52, 62, 255),    IM_COL32(60, 65, 78, 255),
    IM_COL32(8, 9, 12, 255),
    IM_COL32(52, 56, 66, 255),    IM_COL32(79, 146, 255, 110),  IM_COL32(240, 242, 246, 255),
    IM_COL32(79, 146, 255, 60),
    C(16, 17, 21),
    0.0f,
};
const Palette kLight = {
    IM_COL32(46, 111, 224, 255),  IM_COL32(72, 132, 240, 255),  IM_COL32(36, 94, 200, 255),  IM_COL32(255, 255, 255, 255),
    IM_COL32(32, 160, 86, 255),   IM_COL32(200, 128, 12, 255),  IM_COL32(214, 52, 52, 255),  IM_COL32(120, 126, 140, 255),
    IM_COL32(255, 255, 255, 255), IM_COL32(222, 226, 233, 255), IM_COL32(255, 255, 255, 222),
    IM_COL32(28, 30, 36, 255),    IM_COL32(110, 116, 130, 255),
    IM_COL32(232, 235, 241, 255), IM_COL32(220, 224, 232, 255), IM_COL32(206, 211, 221, 255),
    IM_COL32(220, 223, 229, 255),
    IM_COL32(214, 218, 226, 255), IM_COL32(46, 111, 224, 110),  IM_COL32(52, 58, 70, 255),
    IM_COL32(46, 111, 224, 60),
    C(240, 242, 246),
    1.0f,
};
Palette g_palette = kDark;
float g_light = 0.0f;

ImU32 LerpCol(ImU32 a, ImU32 b, float t) {
    return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
}

// The ImGui colour table of one theme.
void ThemeColors(ImVec4* c, bool light) {
    if (!light) {
        c[ImGuiCol_Text]                 = C(230, 232, 238);
        c[ImGuiCol_TextDisabled]         = C(148, 154, 168);
        c[ImGuiCol_WindowBg]             = C(16, 17, 21);
        c[ImGuiCol_ChildBg]              = C(24, 26, 31);
        c[ImGuiCol_PopupBg]              = C(28, 30, 36, 0.98f);
        c[ImGuiCol_Border]               = C(40, 43, 51);
        c[ImGuiCol_BorderShadow]         = C(0, 0, 0, 0);
        c[ImGuiCol_FrameBg]              = C(36, 39, 47);
        c[ImGuiCol_FrameBgHovered]       = C(48, 52, 62);
        c[ImGuiCol_FrameBgActive]        = C(60, 65, 78);
        c[ImGuiCol_TitleBg]              = C(16, 17, 21);
        c[ImGuiCol_TitleBgActive]        = C(24, 26, 31);
        c[ImGuiCol_TitleBgCollapsed]     = C(16, 17, 21);
        c[ImGuiCol_MenuBarBg]            = C(24, 26, 31);
        c[ImGuiCol_ScrollbarBg]          = C(16, 17, 21, 0.0f);
        c[ImGuiCol_ScrollbarGrab]        = C(56, 60, 72);
        c[ImGuiCol_ScrollbarGrabHovered] = C(76, 82, 98);
        c[ImGuiCol_ScrollbarGrabActive]  = C(92, 100, 118);
        c[ImGuiCol_CheckMark]            = C(79, 146, 255);
        c[ImGuiCol_SliderGrab]           = C(79, 146, 255);
        c[ImGuiCol_SliderGrabActive]     = C(104, 164, 255);
        c[ImGuiCol_Button]               = C(36, 39, 47);
        c[ImGuiCol_ButtonHovered]        = C(48, 52, 62);
        c[ImGuiCol_ButtonActive]         = C(60, 65, 78);
        c[ImGuiCol_Header]               = C(36, 39, 47, 0.0f);
        c[ImGuiCol_HeaderHovered]        = C(48, 52, 62, 0.6f);
        c[ImGuiCol_HeaderActive]         = C(60, 65, 78, 0.8f);
        c[ImGuiCol_Separator]            = C(40, 43, 51);
        c[ImGuiCol_SeparatorHovered]     = C(79, 146, 255);
        c[ImGuiCol_SeparatorActive]      = C(104, 164, 255);
        c[ImGuiCol_ResizeGrip]           = C(40, 43, 51, 0.5f);
        c[ImGuiCol_ResizeGripHovered]    = C(79, 146, 255, 0.7f);
        c[ImGuiCol_ResizeGripActive]     = C(104, 164, 255);
        c[ImGuiCol_Tab]                  = C(24, 26, 31);
        c[ImGuiCol_TabHovered]           = C(48, 52, 62);
        c[ImGuiCol_TabSelected]          = C(36, 39, 47);
        c[ImGuiCol_TabSelectedOverline]  = C(79, 146, 255);
        c[ImGuiCol_TabDimmed]            = C(24, 26, 31);
        c[ImGuiCol_TabDimmedSelected]    = C(36, 39, 47);
        c[ImGuiCol_TabDimmedSelectedOverline] = C(40, 43, 51);
        c[ImGuiCol_PlotLines]            = C(79, 146, 255);
        c[ImGuiCol_PlotHistogram]        = C(86, 208, 128);
        c[ImGuiCol_TableHeaderBg]        = C(28, 30, 36);
        c[ImGuiCol_TableBorderStrong]    = C(40, 43, 51);
        c[ImGuiCol_TableBorderLight]     = C(32, 35, 42);
        c[ImGuiCol_TableRowBg]           = C(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt]        = C(255, 255, 255, 0.03f);
        c[ImGuiCol_TextLink]             = C(104, 164, 255);
        c[ImGuiCol_TextSelectedBg]       = C(79, 146, 255, 0.35f);
        c[ImGuiCol_DragDropTarget]       = C(79, 146, 255);
        c[ImGuiCol_NavCursor]            = C(79, 146, 255, 0.8f);
        c[ImGuiCol_ModalWindowDimBg]     = C(0, 0, 0, 0.6f);
        c[ImGuiCol_InputTextCursor]      = C(230, 232, 238);
    } else {
        c[ImGuiCol_Text]                 = C(28, 30, 36);
        c[ImGuiCol_TextDisabled]         = C(110, 116, 130);
        c[ImGuiCol_WindowBg]             = C(240, 242, 246);
        c[ImGuiCol_ChildBg]              = C(255, 255, 255);
        c[ImGuiCol_PopupBg]              = C(255, 255, 255, 0.98f);
        c[ImGuiCol_Border]               = C(222, 226, 233);
        c[ImGuiCol_BorderShadow]         = C(0, 0, 0, 0);
        c[ImGuiCol_FrameBg]              = C(232, 235, 241);
        c[ImGuiCol_FrameBgHovered]       = C(220, 224, 232);
        c[ImGuiCol_FrameBgActive]        = C(206, 211, 221);
        c[ImGuiCol_TitleBg]              = C(240, 242, 246);
        c[ImGuiCol_TitleBgActive]        = C(255, 255, 255);
        c[ImGuiCol_TitleBgCollapsed]     = C(240, 242, 246);
        c[ImGuiCol_MenuBarBg]            = C(255, 255, 255);
        c[ImGuiCol_ScrollbarBg]          = C(240, 242, 246, 0.0f);
        c[ImGuiCol_ScrollbarGrab]        = C(200, 205, 214);
        c[ImGuiCol_ScrollbarGrabHovered] = C(176, 182, 194);
        c[ImGuiCol_ScrollbarGrabActive]  = C(156, 163, 177);
        c[ImGuiCol_CheckMark]            = C(46, 111, 224);
        c[ImGuiCol_SliderGrab]           = C(46, 111, 224);
        c[ImGuiCol_SliderGrabActive]     = C(72, 132, 240);
        c[ImGuiCol_Button]               = C(232, 235, 241);
        c[ImGuiCol_ButtonHovered]        = C(220, 224, 232);
        c[ImGuiCol_ButtonActive]         = C(206, 211, 221);
        c[ImGuiCol_Header]               = C(232, 235, 241, 0.0f);
        c[ImGuiCol_HeaderHovered]        = C(220, 224, 232, 0.6f);
        c[ImGuiCol_HeaderActive]         = C(206, 211, 221, 0.8f);
        c[ImGuiCol_Separator]            = C(222, 226, 233);
        c[ImGuiCol_SeparatorHovered]     = C(46, 111, 224);
        c[ImGuiCol_SeparatorActive]      = C(72, 132, 240);
        c[ImGuiCol_ResizeGrip]           = C(222, 226, 233, 0.5f);
        c[ImGuiCol_ResizeGripHovered]    = C(46, 111, 224, 0.7f);
        c[ImGuiCol_ResizeGripActive]     = C(72, 132, 240);
        c[ImGuiCol_Tab]                  = C(255, 255, 255);
        c[ImGuiCol_TabHovered]           = C(220, 224, 232);
        c[ImGuiCol_TabSelected]          = C(232, 235, 241);
        c[ImGuiCol_TabSelectedOverline]  = C(46, 111, 224);
        c[ImGuiCol_TabDimmed]            = C(255, 255, 255);
        c[ImGuiCol_TabDimmedSelected]    = C(232, 235, 241);
        c[ImGuiCol_TabDimmedSelectedOverline] = C(222, 226, 233);
        c[ImGuiCol_PlotLines]            = C(46, 111, 224);
        c[ImGuiCol_PlotHistogram]        = C(32, 160, 86);
        c[ImGuiCol_TableHeaderBg]        = C(235, 238, 243);
        c[ImGuiCol_TableBorderStrong]    = C(222, 226, 233);
        c[ImGuiCol_TableBorderLight]     = C(232, 235, 241);
        c[ImGuiCol_TableRowBg]           = C(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt]        = C(0, 0, 0, 0.03f);
        c[ImGuiCol_TextLink]             = C(46, 111, 224);
        c[ImGuiCol_TextSelectedBg]       = C(46, 111, 224, 0.3f);
        c[ImGuiCol_DragDropTarget]       = C(46, 111, 224);
        c[ImGuiCol_NavCursor]            = C(46, 111, 224, 0.8f);
        c[ImGuiCol_ModalWindowDimBg]     = C(0, 0, 0, 0.35f);
        c[ImGuiCol_InputTextCursor]      = C(28, 30, 36);
    }
}

constexpr ImGuiID kHoverKey = 0x9E3779B9u;   // mixed into an item's id for its hover animation

float* MotionValue(ImGuiID id, float seed) { return ImGui::GetCurrentWindow()->DC.StateStorage->GetFloatRef(id, seed); }
float FrameStep() { return ImMin(ImGui::GetIO().DeltaTime, 0.05f); }
float Alpha(ImU32 c) { return ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f; }
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
    ImGui::StyleColorsDark(&style);
    // Flat: no window or frame borders, small even rounding, generous but regular spacing.
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 12.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(0, 4);
    style.CellPadding = ImVec2(6, 4);
    style.DisabledAlpha = 0.45f;

    SetThemeLight(style, g_light);
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

void SetThemeLight(ImGuiStyle& style, float light) {
    light = ImSaturate(light);
    g_light = light;
    if (light <= 0.0f || light >= 1.0f) {
        g_palette = light >= 1.0f ? kLight : kDark;
        ThemeColors(style.Colors, light >= 1.0f);
        return;
    }
    const Palette& a = kDark;
    const Palette& b = kLight;
    Palette& p = g_palette;
    p.accent = LerpCol(a.accent, b.accent, light); p.accentHover = LerpCol(a.accentHover, b.accentHover, light);
    p.accentActive = LerpCol(a.accentActive, b.accentActive, light); p.accentText = LerpCol(a.accentText, b.accentText, light);
    p.good = LerpCol(a.good, b.good, light); p.warn = LerpCol(a.warn, b.warn, light); p.bad = LerpCol(a.bad, b.bad, light);
    p.muted = LerpCol(a.muted, b.muted, light);
    p.panel = LerpCol(a.panel, b.panel, light); p.panelBorder = LerpCol(a.panelBorder, b.panelBorder, light);
    p.overlayBg = LerpCol(a.overlayBg, b.overlayBg, light);
    p.text = LerpCol(a.text, b.text, light); p.textDim = LerpCol(a.textDim, b.textDim, light);
    p.control = LerpCol(a.control, b.control, light); p.controlHover = LerpCol(a.controlHover, b.controlHover, light);
    p.controlActive = LerpCol(a.controlActive, b.controlActive, light);
    p.surface = LerpCol(a.surface, b.surface, light);
    p.track = LerpCol(a.track, b.track, light); p.rangeFill = LerpCol(a.rangeFill, b.rangeFill, light); p.knob = LerpCol(a.knob, b.knob, light);
    p.selection = LerpCol(a.selection, b.selection, light);
    p.window = ImLerp(a.window, b.window, light);
    p.light = light;
    ImVec4 dark[ImGuiCol_COUNT], lite[ImGuiCol_COUNT];
    ThemeColors(dark, false);
    ThemeColors(lite, true);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) style.Colors[i] = ImLerp(dark[i], lite[i], light);
}

// Motion ------------------------------------------------------------------------------------

float Animate(ImGuiID id, float target, float speed) { return AnimateFrom(id, target, target, speed); }

float AnimateFrom(ImGuiID id, float from, float target, float speed) {
    float* v = MotionValue(id, from);
    const float d = target - *v;
    if (ImFabs(d) < 0.002f) { *v = target; return target; }
    *v += d * (1.0f - std::exp(-speed * FrameStep()));
    return *v;
}

float AnimateLinear(ImGuiID id, float target, float seconds) {
    float* v = MotionValue(id, target);
    const float step = seconds > 0.0f ? FrameStep() / seconds : 1.0f;
    *v += ImClamp(target - *v, -step, step);
    if (ImFabs(target - *v) < 0.0005f) *v = target;
    return *v;
}

void AnimateSnap(ImGuiID id, float value) { *MotionValue(id, value) = value; }

float Ease(float t) { t = ImSaturate(t); return t * t * (3.0f - 2.0f * t); }

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
    if (wheel != 0.0f && !io.KeyCtrl && !io.KeyAlt && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
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

// Popups ------------------------------------------------------------------------------------

bool BeginPopupFade(const char* strId, ImGuiWindowFlags flags) {
    const ImGuiID id = ImGui::GetID(strId);
    const bool open = ImGui::IsPopupOpen(strId);
    if (!open) { AnimateSnap(id ^ kFadeKey, 0.0f); return false; }
    const float t = AnimateFrom(id ^ kFadeKey, 0.0f, 1.0f, 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Ease(t));
    if (!ImGui::BeginPopup(strId, flags)) { ImGui::PopStyleVar(); return false; }
    return true;
}

void EndPopupFade() {
    ImGui::EndPopup();
    ImGui::PopStyleVar();
}

bool BeginDropdown(const char* label, const char* preview, ImGuiComboFlags flags) {
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = g.Style;
    const Palette& p = Colors();
    const ImGuiID id = window->GetID(label);
    const ImGuiID popupId = ImHashStr("##ComboPopup", 0, id);
    const float arrowW = ImGui::GetFrameHeight();
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float w = ImGui::CalcItemWidth();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + labelSize.y + style.FramePadding.y * 2.0f));
    const ImRect total(bb.Min, ImVec2(bb.Max.x + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f), bb.Max.y));
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
    const float angle = Animate(id ^ kOpenKey, open ? IM_PI : 0.0f, 18.0f);
    DrawChevron(dl, ImVec2(bb.Max.x - arrowW * 0.5f, bb.GetCenter().y), arrowW * 0.34f, angle, Col(p.textDim));
    if (preview) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.text);
        ImGui::RenderTextClipped(ImVec2(bb.Min.x + style.FramePadding.x, bb.Min.y + style.FramePadding.y),
                                 ImVec2(bb.Max.x - arrowW, bb.Max.y - style.FramePadding.y), preview, nullptr, nullptr);
        ImGui::PopStyleColor();
    }
    if (labelSize.x > 0.0f) ImGui::RenderText(ImVec2(bb.Max.x + style.ItemInnerSpacing.x, bb.Min.y + style.FramePadding.y), label);
    if (!open) { AnimateSnap(id ^ kFadeKey, 0.0f); return false; }
    const float t = AnimateFrom(id ^ kFadeKey, 0.0f, 1.0f, 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * Ease(t));
    if (!ImGui::BeginComboPopup(popupId, bb, flags)) { ImGui::PopStyleVar(); return false; }
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
    if (key != lastKey || frame - lastFrame > 2) t = 0.0f;   // another item, or the tooltip was away for a while
    lastKey = key; lastFrame = frame;
    t += (1.0f - t) * (1.0f - std::exp(-22.0f * FrameStep()));
    if (t > 0.995f) t = 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Ease(t));
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
}

void Tooltip(const char* text) {
    if (!text || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    TooltipShow(ImGui::GetItemID(), text);
}

// Icons -------------------------------------------------------------------------------------

void DrawChevron(ImDrawList* dl, const ImVec2& c, float size, float angle, ImU32 col) {
    const float s = size * 0.5f;
    const float cs = std::cos(angle), sn = std::sin(angle);
    auto pt = [&](float x, float y) { return ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs); };
    ImVec2 pts[3] = { pt(-s, -s * 0.5f), pt(0.0f, s * 0.5f), pt(s, -s * 0.5f) };
    dl->AddPolyline(pts, 3, col, ImMax(1.5f, size * 0.16f));
}

namespace {
// Three quarters of a circle with an arrow head at its end: clockwise for "refresh", mirrored for "reset".
void DrawArcArrow(ImDrawList* dl, const ImVec2& c, float size, bool clockwise, ImU32 col) {
    const float r = size * 0.38f;
    const float thick = ImMax(1.5f, size * 0.14f);
    constexpr int n = 24;
    const float a0 = -IM_PI * 0.3f, sweep = IM_PI * 1.5f;
    ImVec2 pts[n + 1];
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + sweep * (float)i / (float)n;
        const float x = std::cos(a) * r;
        pts[i] = ImVec2(c.x + (clockwise ? x : -x), c.y + std::sin(a) * r);
    }
    dl->AddPolyline(pts, n + 1, col, thick);
    const float aEnd = a0 + sweep;
    ImVec2 dir(-std::sin(aEnd), std::cos(aEnd)), nrm(std::cos(aEnd), std::sin(aEnd));
    if (!clockwise) { dir.x = -dir.x; nrm.x = -nrm.x; }
    const ImVec2 tip = pts[n];
    const float h = size * 0.26f;
    dl->AddTriangleFilled(tip, ImVec2(tip.x - dir.x * h + nrm.x * h * 0.7f, tip.y - dir.y * h + nrm.y * h * 0.7f),
                          ImVec2(tip.x - dir.x * h - nrm.x * h * 0.7f, tip.y - dir.y * h - nrm.y * h * 0.7f), col);
}
}

void DrawIcon(ImDrawList* dl, Icon icon, const ImVec2& c, float size, ImU32 col) {
    const float s = size * 0.5f;
    const float thick = ImMax(1.5f, size * 0.13f);
    auto at = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    switch (icon) {
    case Icon::Play:
        dl->AddTriangleFilled(at(-0.62f, -0.85f), at(0.9f, 0.0f), at(-0.62f, 0.85f), col);
        break;
    case Icon::Pause:
        dl->AddRectFilled(at(-0.75f, -0.85f), at(-0.2f, 0.85f), col, size * 0.06f);
        dl->AddRectFilled(at(0.2f, -0.85f), at(0.75f, 0.85f), col, size * 0.06f);
        break;
    case Icon::StepBack:
        dl->AddRectFilled(at(-0.9f, -0.85f), at(-0.6f, 0.85f), col, size * 0.05f);
        dl->AddTriangleFilled(at(0.8f, -0.85f), at(-0.4f, 0.0f), at(0.8f, 0.85f), col);
        break;
    case Icon::StepForward:
        dl->AddRectFilled(at(0.6f, -0.85f), at(0.9f, 0.85f), col, size * 0.05f);
        dl->AddTriangleFilled(at(-0.8f, -0.85f), at(0.4f, 0.0f), at(-0.8f, 0.85f), col);
        break;
    case Icon::ChevronDown:  DrawChevron(dl, c, size, 0.0f, col); break;
    case Icon::ChevronUp:    DrawChevron(dl, c, size, IM_PI, col); break;
    case Icon::ChevronLeft:  DrawChevron(dl, c, size, IM_PI * 0.5f, col); break;
    case Icon::ChevronRight: DrawChevron(dl, c, size, -IM_PI * 0.5f, col); break;
    case Icon::Reset:   DrawArcArrow(dl, c, size, false, col); break;
    case Icon::Refresh: DrawArcArrow(dl, c, size, true, col); break;
    case Icon::OpenExternal: {
        ImVec2 box[5] = { at(0.15f, -0.8f), at(-0.8f, -0.8f), at(-0.8f, 0.8f), at(0.8f, 0.8f), at(0.8f, -0.15f) };
        dl->AddPolyline(box, 5, col, thick);
        dl->AddLine(at(-0.1f, 0.1f), at(0.8f, -0.8f), col, thick);
        ImVec2 head[3] = { at(0.25f, -0.8f), at(0.8f, -0.8f), at(0.8f, -0.25f) };
        dl->AddPolyline(head, 3, col, thick);
        break;
    }
    case Icon::Close:
        dl->AddLine(at(-0.6f, -0.6f), at(0.6f, 0.6f), col, thick);
        dl->AddLine(at(-0.6f, 0.6f), at(0.6f, -0.6f), col, thick);
        break;
    case Icon::Undo:
    case Icon::Redo: {
        // An arc from the lower right over the top to the left, ending in a head that points down; mirrored for redo.
        const bool redo = icon == Icon::Redo;
        const float r = size * 0.36f;
        constexpr int n = 18;
        const float a0 = IM_PI * 0.3f, a1 = -IM_PI * 0.92f;
        ImVec2 pts[n + 1];
        for (int i = 0; i <= n; ++i) {
            const float a = a0 + (a1 - a0) * (float)i / (float)n;
            const float x = std::cos(a) * r;
            pts[i] = ImVec2(c.x + (redo ? -x : x) + (redo ? -1.0f : 1.0f) * size * 0.04f, c.y + std::sin(a) * r + size * 0.08f);
        }
        dl->AddPolyline(pts, n + 1, col, thick);
        ImVec2 dir(std::sin(a1), -std::cos(a1)), nrm(std::cos(a1), std::sin(a1));
        if (redo) { dir.x = -dir.x; nrm.x = -nrm.x; }
        const float h = size * 0.26f;
        const ImVec2 tip(pts[n].x + dir.x * h * 0.45f, pts[n].y + dir.y * h * 0.45f);
        dl->AddTriangleFilled(ImVec2(tip.x + dir.x * h * 0.5f, tip.y + dir.y * h * 0.5f),
                              ImVec2(tip.x - dir.x * h * 0.5f + nrm.x * h * 0.7f, tip.y - dir.y * h * 0.5f + nrm.y * h * 0.7f),
                              ImVec2(tip.x - dir.x * h * 0.5f - nrm.x * h * 0.7f, tip.y - dir.y * h * 0.5f - nrm.y * h * 0.7f), col);
        break;
    }
    case Icon::Lock: {
        dl->AddRectFilled(at(-0.7f, -0.05f), at(0.7f, 0.85f), col, size * 0.1f);
        ImVec2 pts[14];
        pts[0] = at(-0.42f, -0.05f);
        for (int i = 0; i <= 11; ++i) {
            const float a = IM_PI + IM_PI * (float)i / 11.0f;
            pts[1 + i] = at(std::cos(a) * 0.42f, -0.15f + std::sin(a) * 0.42f);
        }
        pts[13] = at(0.42f, -0.05f);
        dl->AddPolyline(pts, 14, col, thick);
        break;
    }
    }
}

// Buttons -----------------------------------------------------------------------------------

namespace {
void ButtonColors(ButtonKind kind, ImU32& bg, ImU32& bgHover, ImU32& bgActive, ImU32& fg, ImU32& border) {
    const Palette& p = Colors();
    border = 0;
    switch (kind) {
    case ButtonKind::Accent: bg = p.accent; bgHover = p.accentHover; bgActive = p.accentActive; fg = p.accentText; break;
    case ButtonKind::Ghost:  bg = 0; bgHover = p.controlHover; bgActive = p.controlActive; fg = p.text; border = p.panelBorder; break;
    case ButtonKind::Plain:  bg = 0; bgHover = p.controlHover; bgActive = p.controlActive; fg = p.text; break;
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
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 size = ImGui::CalcItemSize(sizeArg, labelSize.x + style.FramePadding.x * 2.0f, labelSize.y + style.FramePadding.y * 2.0f);
    ImU32 bg, bgHover, bgActive, fg, border;
    ButtonColors(kind, bg, bgHover, bgActive, fg, border);
    ImRect bb;
    bool hovered = false;
    const bool pressed = ButtonFrame(id, size, bg, bgHover, bgActive, border, bb, hovered);
    if (bb.GetWidth() <= 0.0f) return false;
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::RenderTextClipped(ImVec2(bb.Min.x + style.FramePadding.x, bb.Min.y + style.FramePadding.y),
                             ImVec2(bb.Max.x - style.FramePadding.x, bb.Max.y - style.FramePadding.y),
                             label, nullptr, &labelSize, style.ButtonTextAlign, &bb);
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
    iconSize = ImMin(size.x, size.y) * 0.56f;
    fg = Col(fg);
    Tooltip(tooltip);
    return pressed;
}
}

bool FlatButton(const char* label, const ImVec2& size) { return TextButton(label, size, ButtonKind::Flat); }
bool AccentButton(const char* label, const ImVec2& size) { return TextButton(label, size, ButtonKind::Accent); }
bool GhostButton(const char* label, const ImVec2& size) { return TextButton(label, size, ButtonKind::Ghost); }

bool DangerButton(const char* label, const ImVec2& sizeArg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
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
    if (iconSize > 0.0f) DrawChevron(ImGui::GetWindowDrawList(), center, iconSize, angle, fg);
    return pressed;
}

// Widgets -----------------------------------------------------------------------------------

bool Toggle(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight() * 0.86f;
    const float width = height * 1.8f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float rowH = ImGui::GetFrameHeight();
    const ImRect total(pos, ImVec2(pos.x + width + (labelSize.x > 0 ? style.ItemInnerSpacing.x + labelSize.x : 0), pos.y + rowH));
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
    const ImU32 bg = Mix(Mix(p.controlHover, p.controlActive, hov), Mix(p.accent, p.accentHover, hov), on);
    ImDrawList* dl = window->DrawList;
    const float radius = height * 0.5f;
    const float y0 = pos.y + (rowH - height) * 0.5f;
    dl->AddRectFilled(ImVec2(pos.x, y0), ImVec2(pos.x + width, y0 + height), Col(bg), radius);
    const float knobX = pos.x + radius + (width - height) * Ease(on);
    dl->AddCircleFilled(ImVec2(knobX, y0 + radius), radius - 3.0f, Col(IM_COL32(255, 255, 255, (int)(210 + 45 * on))));
    if (labelSize.x > 0)
        ImGui::RenderText(ImVec2(pos.x + width + style.ItemInnerSpacing.x, pos.y + (rowH - labelSize.y) * 0.5f), label);
    return pressed;
}

namespace {
// Collapsing sections fold their content with a clip that grows or shrinks over a fifth of a second. The full
// height of each section's content is remembered from the last frame it was drawn in.
struct SectionFrame { ImGuiID id = 0; ImGuiWindow* window = nullptr; float t = 1.0f; float startY = 0.0f; bool clipped = false; };
SectionFrame g_sections[8];
int g_sectionDepth = 0;
ImGuiStorage g_sectionHeights;
}

bool SectionHeader(const char* label, const char* id, bool defaultOpen) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    ImGui::PushID(id);
    const ImGuiID hid = window->GetID("##header");
    ImGuiStorage* storage = window->DC.StateStorage;
    bool open = storage->GetBool(hid, defaultOpen);

    const ImVec2 pad(8.0f, 7.0f);
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.02f);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float h = labelSize.y + pad.y * 2.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const float w = ImMax(ImGui::GetContentRegionAvail().x, labelSize.x + h + pad.x * 2.0f);
    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::ItemSize(bb, pad.y);
    bool hovered = false, held = false, pressed = false;
    if (ImGui::ItemAdd(bb, hid)) pressed = ImGui::ButtonBehavior(bb, hid, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
    if (pressed) { open = !open; storage->SetBool(hid, open); }
    const float t = AnimateLinear(hid ^ 0x51ED270Bu, open ? 1.0f : 0.0f, 0.2f);
    const float hov = Animate(hid ^ kHoverKey, (hovered || held) ? 1.0f : 0.0f, 16.0f);

    ImDrawList* dl = window->DrawList;
    if (hov > 0.001f)
        dl->AddRectFilled(bb.Min, bb.Max, Col(WithAlpha(held ? p.controlActive : p.controlHover, (held ? 0.7f : 0.6f) * hov)), style.FrameRounding);
    const float chevron = labelSize.y * 0.6f;
    DrawChevron(dl, ImVec2(bb.Min.x + pad.x + chevron * 0.5f + 1.0f, bb.GetCenter().y), chevron,
                -IM_PI * 0.5f + Ease(t) * IM_PI * 0.5f, Col(p.textDim));
    ImGui::RenderText(ImVec2(bb.Min.x + pad.x + chevron + 9.0f, bb.Min.y + pad.y), label);
    ImGui::PopFont();
    // A thin rule under the header keeps the sections apart without boxes.
    const float ruleY = bb.Max.y + style.ItemSpacing.y - 2.0f;
    dl->AddLine(ImVec2(bb.Min.x, ruleY), ImVec2(bb.Min.x + w, ruleY), Col(p.panelBorder));
    ImGui::PopID();

    if (t <= 0.001f || g_sectionDepth >= (int)IM_ARRAYSIZE(g_sections)) return false;
    SectionFrame& f = g_sections[g_sectionDepth++];
    f.id = hid; f.window = window; f.t = t; f.startY = window->DC.CursorPos.y; f.clipped = false;
    if (t < 0.999f) {
        const float shownH = g_sectionHeights.GetFloat(hid, 0.0f) * Ease(t);
        ImGui::PushClipRect(ImVec2(window->ClipRect.Min.x, f.startY), ImVec2(window->ClipRect.Max.x, f.startY + shownH), true);
        f.clipped = true;
    }
    return true;
}

void SectionEnd() {
    if (g_sectionDepth <= 0) return;
    SectionFrame& f = g_sections[--g_sectionDepth];
    ImGuiWindow* window = f.window;
    const float fullH = window->DC.CursorPos.y - f.startY;
    g_sectionHeights.SetFloat(f.id, fullH);
    if (!f.clipped) return;
    ImGui::PopClipRect();
    const float y = f.startY + fullH * Ease(f.t);
    window->DC.CursorPos.y = y;
    window->DC.CursorMaxPos.y = ImMin(window->DC.CursorMaxPos.y, y);
    window->DC.IdealMaxPos.y = ImMin(window->DC.IdealMaxPos.y, y);
}

void SectionLabel(const char* text) {
    const Palette& p = Colors();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.86f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, a.y + 1.0f), ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + 1.0f), Col(p.panelBorder));
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    const Palette& p = Colors();
    const float lineH = ImGui::GetTextLineHeight();
    const float d = ImGui::GetFontSize() * 1.05f;
    const ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    const ImRect bb(pos, ImVec2(pos.x + d, pos.y + lineH));
    const ImGuiID id = window->GetID(text);
    ImGui::ItemSize(ImVec2(d, lineH), 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return;
    const float hov = Animate(id ^ kHoverKey, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
    ImDrawList* dl = window->DrawList;
    const ImVec2 c(pos.x + d * 0.5f, pos.y + lineH * 0.5f);
    const float r = d * 0.46f;
    const ImU32 ink = Col(Mix(p.textDim, p.text, hov));
    if (hov > 0.001f) dl->AddCircleFilled(c, r, Col(WithAlpha(p.controlHover, hov)));
    dl->AddCircle(c, r, ink, 0, 1.2f);
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * 0.78f;
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "?");
    dl->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ink, "?");
    Tooltip(text);
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().textDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void StatusDot(ImU32 color, const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const float r = h * 0.26f;
    dl->AddCircleFilled(ImVec2(pos.x + r + 1.0f, pos.y + h * 0.5f), r, Col(color));
    ImGui::Dummy(ImVec2(r * 2.0f + 4.0f, h));
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextUnformatted(text);
}

void Pill(const char* text, ImU32 bg, ImU32 fg) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(9.0f, 3.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 box(size.x + pad.x * 2.0f, size.y + pad.y * 2.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + box.x, pos.y + box.y), Col(bg), box.y * 0.5f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), Col(fg), text);
    ImGui::Dummy(box);
}

namespace {
bool ResetButton(bool enabled, float size) {
    ImGui::BeginDisabled(!enabled);
    const bool pressed = IconButton("##reset", Icon::Reset, ImVec2(size, size), nullptr, ButtonKind::Plain);
    ImGui::EndDisabled();
    return pressed;
}
}

bool SliderReset(const char* label, float* v, float minV, float maxV, float def, const char* fmt, const char* tooltip) {
    ImGui::PushID(label);
    const float resetW = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
    bool changed = ImGui::SliderFloat("##s", v, minV, maxV, fmt, ImGuiSliderFlags_AlwaysClamp);
    Tooltip(tooltip);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ResetButton(*v != def, resetW)) { *v = def; changed = true; }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool SliderIntReset(const char* label, int* v, int minV, int maxV, int def, const char* fmt, const char* tooltip) {
    ImGui::PushID(label);
    const float resetW = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
    bool changed = ImGui::SliderInt("##s", v, minV, maxV, fmt, ImGuiSliderFlags_AlwaysClamp);
    Tooltip(tooltip);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ResetButton(*v != def, resetW)) { *v = def; changed = true; }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool Segmented(const char* id, const char* const* labels, int count, int* value, float width) {
    if (count <= 0) return false;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    const float h = ImGui::GetFrameHeight();
    auto segWidth = [&](int i) { return width > 0.0f ? width / (float)count : ImGui::CalcTextSize(labels[i]).x + style.FramePadding.x * 2.2f; };
    float w = 0.0f;
    for (int i = 0; i < count; ++i) w += segWidth(i);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::PushID(id);
    ImGui::ItemSize(total, 0.0f);
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(total.Min, total.Max, Col(p.control), style.FrameRounding);
    // The highlight slides to the chosen segment instead of jumping.
    float onX0 = 0.0f, onX1 = 0.0f;
    float runX = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float sw = segWidth(i);
        if (i == *value) { onX0 = runX; onX1 = runX + sw; }
        runX += sw;
    }
    if (*value >= 0 && *value < count) {
        const float hx0 = Animate(window->GetID("##hl0"), onX0, 18.0f);
        const float hx1 = Animate(window->GetID("##hl1"), onX1, 18.0f);
        dl->AddRectFilled(ImVec2(pos.x + hx0 + 2.0f, pos.y + 2.0f), ImVec2(pos.x + hx1 - 2.0f, pos.y + h - 2.0f), Col(p.accent), style.FrameRounding - 1.0f);
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
        if (hov > 0.001f)
            dl->AddRectFilled(ImVec2(bb.Min.x + 2.0f, bb.Min.y + 2.0f), ImVec2(bb.Max.x - 2.0f, bb.Max.y - 2.0f), Col(WithAlpha(p.controlHover, hov)), style.FrameRounding - 1.0f);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        dl->AddText(ImVec2(bb.Min.x + (segW - ts.x) * 0.5f, bb.Min.y + (h - ts.y) * 0.5f), Col(on ? p.accentText : p.text), labels[i]);
        if (pressed && !on) { *value = i; changed = true; }
        x += segW;
    }
    ImGui::PopID();
    return changed;
}

void KeyValue(const char* key, const char* value) {
    ImGui::TextDisabled("%s", key);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f + ImGui::GetCursorPosX() * 0.0f);
    ImGui::TextUnformatted(value);
}

} // namespace vdc::ui
