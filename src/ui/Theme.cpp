#include "ui/Theme.h"
#include "imgui_internal.h"
#include <cstdio>

namespace vdc::ui {

namespace {
Palette g_palette = {
    IM_COL32(88, 156, 255, 255),  IM_COL32(112, 172, 255, 255), IM_COL32(66, 132, 232, 255), IM_COL32(255, 255, 255, 255),
    IM_COL32(94, 214, 132, 255),  IM_COL32(255, 190, 70, 255),  IM_COL32(255, 96, 96, 255),  IM_COL32(150, 156, 168, 255),
    IM_COL32(26, 29, 36, 255),    IM_COL32(48, 52, 62, 255),    IM_COL32(12, 13, 17, 200),
};
ImVec4 C(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }
}

const Palette& Colors() { return g_palette; }

ImU32 WithAlpha(ImU32 color, float alpha) {
    const ImU32 a = (ImU32)(ImClamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

void ApplyTheme(ImGuiStyle& style, float dpiScale) {
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 10.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 7.0f;
    style.TabRounding = 7.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(8, 7);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 14.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(16, 4);
    style.CellPadding = ImVec2(6, 4);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = C(232, 234, 240);
    c[ImGuiCol_TextDisabled]         = C(140, 146, 158);
    c[ImGuiCol_WindowBg]             = C(17, 19, 24);
    c[ImGuiCol_ChildBg]              = C(24, 27, 34);
    c[ImGuiCol_PopupBg]              = C(28, 31, 39, 0.98f);
    c[ImGuiCol_Border]               = C(48, 52, 62);
    c[ImGuiCol_BorderShadow]         = C(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = C(36, 40, 50);
    c[ImGuiCol_FrameBgHovered]       = C(46, 51, 63);
    c[ImGuiCol_FrameBgActive]        = C(54, 60, 74);
    c[ImGuiCol_TitleBg]              = C(17, 19, 24);
    c[ImGuiCol_TitleBgActive]        = C(24, 27, 34);
    c[ImGuiCol_TitleBgCollapsed]     = C(17, 19, 24);
    c[ImGuiCol_MenuBarBg]            = C(24, 27, 34);
    c[ImGuiCol_ScrollbarBg]          = C(17, 19, 24, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = C(60, 65, 78);
    c[ImGuiCol_ScrollbarGrabHovered] = C(80, 86, 102);
    c[ImGuiCol_ScrollbarGrabActive]  = C(96, 104, 122);
    c[ImGuiCol_CheckMark]            = C(88, 156, 255);
    c[ImGuiCol_SliderGrab]           = C(88, 156, 255);
    c[ImGuiCol_SliderGrabActive]     = C(112, 172, 255);
    c[ImGuiCol_Button]               = C(40, 45, 56);
    c[ImGuiCol_ButtonHovered]        = C(54, 60, 74);
    c[ImGuiCol_ButtonActive]         = C(66, 74, 92);
    c[ImGuiCol_Header]               = C(36, 40, 50, 0.0f);
    c[ImGuiCol_HeaderHovered]        = C(46, 51, 63, 0.6f);
    c[ImGuiCol_HeaderActive]         = C(54, 60, 74, 0.8f);
    c[ImGuiCol_Separator]            = C(48, 52, 62);
    c[ImGuiCol_SeparatorHovered]     = C(88, 156, 255);
    c[ImGuiCol_SeparatorActive]      = C(112, 172, 255);
    c[ImGuiCol_ResizeGrip]           = C(48, 52, 62, 0.5f);
    c[ImGuiCol_ResizeGripHovered]    = C(88, 156, 255, 0.7f);
    c[ImGuiCol_ResizeGripActive]     = C(112, 172, 255);
    c[ImGuiCol_Tab]                  = C(28, 31, 39);
    c[ImGuiCol_TabHovered]           = C(54, 60, 74);
    c[ImGuiCol_TabSelected]          = C(40, 45, 56);
    c[ImGuiCol_TabSelectedOverline]  = C(88, 156, 255);
    c[ImGuiCol_TabDimmed]            = C(24, 27, 34);
    c[ImGuiCol_TabDimmedSelected]    = C(36, 40, 50);
    c[ImGuiCol_TabDimmedSelectedOverline] = C(48, 52, 62);
    c[ImGuiCol_PlotLines]            = C(88, 156, 255);
    c[ImGuiCol_PlotHistogram]        = C(94, 214, 132);
    c[ImGuiCol_TableHeaderBg]        = C(28, 31, 39);
    c[ImGuiCol_TableBorderStrong]    = C(48, 52, 62);
    c[ImGuiCol_TableBorderLight]     = C(36, 40, 50);
    c[ImGuiCol_TableRowBg]           = C(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = C(255, 255, 255, 0.03f);
    c[ImGuiCol_TextLink]             = C(112, 172, 255);
    c[ImGuiCol_TextSelectedBg]       = C(88, 156, 255, 0.35f);
    c[ImGuiCol_DragDropTarget]       = C(88, 156, 255);
    c[ImGuiCol_NavCursor]            = C(88, 156, 255, 0.8f);
    c[ImGuiCol_ModalWindowDimBg]     = C(0, 0, 0, 0.6f);
    c[ImGuiCol_InputTextCursor]      = C(232, 234, 240);
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

bool Toggle(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float width = height * 1.8f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImRect total(pos, ImVec2(pos.x + width + (labelSize.x > 0 ? style.ItemInnerSpacing.x + labelSize.x : 0), pos.y + height));
    const ImGuiID id = window->GetID(label);
    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id)) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(total, id, &hovered, &held);
    if (pressed) { *v = !*v; ImGui::MarkItemEdited(id); }
    const float t = ImGui::GetCurrentContext()->LastActiveId == id ? ImSaturate(ImGui::GetCurrentContext()->LastActiveIdTimer / 0.12f) : 1.0f;
    const float anim = *v ? t : 1.0f - t;
    const Palette& p = Colors();
    ImU32 bg = *v ? (hovered ? p.accentHover : p.accent) : (hovered ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImDrawList* dl = window->DrawList;
    const float radius = height * 0.5f;
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg, radius);
    const float knobX = pos.x + radius + (width - height) * anim;
    dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), radius - 3.0f, IM_COL32(255, 255, 255, *v ? 255 : 200));
    if (labelSize.x > 0)
        ImGui::RenderText(ImVec2(pos.x + width + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);
    return pressed;
}

bool SectionHeader(const char* label, const char* id, bool defaultOpen) {
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetColorU32(ImGuiCol_FrameBgActive));
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanFullWidth;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    ImGui::PopID();
    return open;
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

void StatusDot(ImU32 color, const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const float r = h * 0.28f;
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

bool SliderReset(const char* label, float* v, float minV, float maxV, float def, const char* fmt, const char* tooltip) {
    ImGui::PushID(label);
    const float resetW = ImGui::GetFrameHeight();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
    bool changed = ImGui::SliderFloat("##s", v, minV, maxV, fmt, ImGuiSliderFlags_AlwaysClamp);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::BeginDisabled(*v == def);
    if (ImGui::Button("R", ImVec2(resetW, resetW))) { *v = def; changed = true; }
    ImGui::EndDisabled();
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

void KeyValue(const char* key, const char* value) {
    ImGui::TextDisabled("%s", key);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f + ImGui::GetCursorPosX() * 0.0f);
    ImGui::TextUnformatted(value);
}

} // namespace vdc::ui
