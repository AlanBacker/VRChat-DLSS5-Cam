#include "ui/Theme.h"
#include "imgui_internal.h"
#include <cstdio>

namespace vdc::ui {

namespace {
ImVec4 C(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

Palette g_palette = {
    IM_COL32(79, 146, 255, 255),  IM_COL32(104, 164, 255, 255), IM_COL32(58, 122, 228, 255), IM_COL32(255, 255, 255, 255),
    IM_COL32(86, 208, 128, 255),  IM_COL32(255, 184, 64, 255),  IM_COL32(255, 92, 92, 255),  IM_COL32(136, 142, 156, 255),
    IM_COL32(24, 26, 31, 255),    IM_COL32(40, 43, 51, 255),    IM_COL32(10, 11, 14, 210),
    IM_COL32(230, 232, 238, 255), IM_COL32(148, 154, 168, 255),
    IM_COL32(36, 39, 47, 255),    IM_COL32(48, 52, 62, 255),    IM_COL32(60, 65, 78, 255),
    IM_COL32(8, 9, 12, 255),
    IM_COL32(52, 56, 66, 255),    IM_COL32(79, 146, 255, 110),  IM_COL32(240, 242, 246, 255),
    IM_COL32(79, 146, 255, 60),
    C(16, 17, 21),
};
}

const Palette& Colors() { return g_palette; }

ImU32 WithAlpha(ImU32 color, float alpha) {
    const ImU32 a = (ImU32)(ImClamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
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

    ImVec4* c = style.Colors;
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
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

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
    const float t = ImGui::GetCurrentContext()->LastActiveId == id ? ImSaturate(ImGui::GetCurrentContext()->LastActiveIdTimer / 0.12f) : 1.0f;
    const float anim = *v ? t : 1.0f - t;
    const Palette& p = Colors();
    ImU32 bg = *v ? (hovered ? p.accentHover : p.accent) : (hovered ? p.controlActive : p.controlHover);
    ImDrawList* dl = window->DrawList;
    const float radius = height * 0.5f;
    const float y0 = pos.y + (rowH - height) * 0.5f;
    dl->AddRectFilled(ImVec2(pos.x, y0), ImVec2(pos.x + width, y0 + height), bg, radius);
    const float knobX = pos.x + radius + (width - height) * anim;
    dl->AddCircleFilled(ImVec2(knobX, y0 + radius), radius - 3.0f, IM_COL32(255, 255, 255, *v ? 255 : 210));
    if (labelSize.x > 0)
        ImGui::RenderText(ImVec2(pos.x + width + style.ItemInnerSpacing.x, pos.y + (rowH - labelSize.y) * 0.5f), label);
    return pressed;
}

bool SectionHeader(const char* label, const char* id, bool defaultOpen) {
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 7));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, WithAlpha(Colors().controlHover, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, WithAlpha(Colors().controlActive, 0.7f));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanFullWidth;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.02f);
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    // A thin rule under the header keeps the sections apart without boxes.
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, a.y - 2.0f), ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y - 2.0f), Colors().panelBorder);
    ImGui::PopID();
    return open;
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
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, a.y + 1.0f), ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + 1.0f), p.panelBorder);
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
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
    dl->AddCircleFilled(ImVec2(pos.x + r + 1.0f, pos.y + h * 0.5f), r, color);
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
    dl->AddRectFilled(pos, ImVec2(pos.x + box.x, pos.y + box.y), bg, box.y * 0.5f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), fg, text);
    ImGui::Dummy(box);
}

namespace {
bool ResetButton(bool enabled, float size) {
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    const bool pressed = ImGui::Button("\xE2\x86\xBA", ImVec2(size, size));
    ImGui::PopStyleColor();
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
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
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
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ResetButton(*v != def, resetW)) { *v = def; changed = true; }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

bool AccentButton(const char* label, const ImVec2& size) {
    const Palette& p = Colors();
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.accentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, p.accentText);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool GhostButton(const char* label, const ImVec2& size) {
    const Palette& p = Colors();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.controlHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.controlActive);
    ImGui::PushStyleColor(ImGuiCol_Border, p.panelBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

bool Segmented(const char* id, const char* const* labels, int count, int* value, float width) {
    if (count <= 0) return false;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    const float h = ImGui::GetFrameHeight();
    float natural = 0.0f;
    for (int i = 0; i < count; ++i) natural += ImGui::CalcTextSize(labels[i]).x + style.FramePadding.x * 2.2f;
    const float w = width > 0.0f ? width : natural;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total(pos, ImVec2(pos.x + w, pos.y + h));
    ImGui::PushID(id);
    ImGui::ItemSize(total, 0.0f);
    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(total.Min, total.Max, p.control, style.FrameRounding);
    bool changed = false;
    float x = pos.x;
    for (int i = 0; i < count; ++i) {
        const float segW = width > 0.0f ? w / (float)count : ImGui::CalcTextSize(labels[i]).x + style.FramePadding.x * 2.2f;
        const ImRect bb(ImVec2(x, pos.y), ImVec2(x + segW, pos.y + h));
        const ImGuiID bid = window->GetID(i);
        bool hovered = false, held = false, pressed = false;
        if (ImGui::ItemAdd(bb, bid)) pressed = ImGui::ButtonBehavior(bb, bid, &hovered, &held);
        const bool on = (*value == i);
        const ImVec2 inner0(bb.Min.x + 2.0f, bb.Min.y + 2.0f), inner1(bb.Max.x - 2.0f, bb.Max.y - 2.0f);
        if (on) dl->AddRectFilled(inner0, inner1, p.accent, style.FrameRounding - 1.0f);
        else if (hovered) dl->AddRectFilled(inner0, inner1, p.controlHover, style.FrameRounding - 1.0f);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        dl->AddText(ImVec2(bb.Min.x + (segW - ts.x) * 0.5f, bb.Min.y + (h - ts.y) * 0.5f), on ? p.accentText : p.text, labels[i]);
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
