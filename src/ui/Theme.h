// VRChat DLSS5 Cam - the flat visual theme and the small custom widgets the interface is built from.
#pragma once
#include "imgui.h"

namespace vdc::ui {

struct Palette {
    ImU32 accent, accentHover, accentActive, accentText;
    ImU32 good, warn, bad, muted;
    ImU32 panel, panelBorder, overlayBg;
    ImU32 text, textDim;                 // body text and captions
    ImU32 control, controlHover, controlActive;   // flat buttons and frames
    ImU32 surface;                       // the preview background
    ImU32 track, rangeFill, knob;        // seek bar
    ImU32 selection;                     // selected library item
    ImVec4 window;                       // the clear colour behind everything
};
const Palette& Colors();

void ApplyTheme(ImGuiStyle& style, float dpiScale);

// Widgets ----------------------------------------------------------------------------------
bool Toggle(const char* label, bool* v);                                           // switch-style checkbox
bool SectionHeader(const char* label, const char* id, bool defaultOpen = true);    // flat collapsing header
void SectionLabel(const char* text);                                               // caption + thin rule (not collapsible)
void Help(const char* text);                                                       // (?) marker with tooltip
void StatusDot(ImU32 color, const char* text);                                     // coloured dot + text
void Pill(const char* text, ImU32 bg, ImU32 fg);                                   // rounded badge
bool SliderReset(const char* label, float* v, float minV, float maxV, float def, const char* fmt, const char* tooltip);
bool SliderIntReset(const char* label, int* v, int minV, int maxV, int def, const char* fmt, const char* tooltip);
bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));           // the primary action
bool GhostButton(const char* label, const ImVec2& size = ImVec2(0, 0));            // outlined secondary action
bool Segmented(const char* id, const char* const* labels, int count, int* value, float width = 0.0f);  // one-of-n switch
void KeyValue(const char* key, const char* value);                                 // two-column line
void Hint(const char* text);                                                       // wrapped dim paragraph
ImU32 WithAlpha(ImU32 color, float alpha);

} // namespace vdc::ui
