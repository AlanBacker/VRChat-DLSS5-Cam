// VRChat DLSS5 Cam - visual theme and small custom widgets.
#pragma once
#include "imgui.h"

namespace vdc::ui {

struct Palette {
    ImU32 accent, accentHover, accentActive, accentText;
    ImU32 good, warn, bad, muted;
    ImU32 panel, panelBorder, overlayBg;
};
const Palette& Colors();

void ApplyTheme(ImGuiStyle& style, float dpiScale);

// Widgets ----------------------------------------------------------------------------------
bool Toggle(const char* label, bool* v);                                           // switch-style checkbox
bool SectionHeader(const char* label, const char* id, bool defaultOpen = true);    // styled collapsing header
void Help(const char* text);                                                       // (?) marker with tooltip
void StatusDot(ImU32 color, const char* text);                                     // coloured dot + text
void Pill(const char* text, ImU32 bg, ImU32 fg);                                   // rounded badge
bool SliderReset(const char* label, float* v, float minV, float maxV, float def, const char* fmt, const char* tooltip);
bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));
void KeyValue(const char* key, const char* value);                                 // two-column line
ImU32 WithAlpha(ImU32 color, float alpha);

} // namespace vdc::ui
