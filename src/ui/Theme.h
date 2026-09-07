// VRChat DLSS5 Cam - the flat visual theme, the motion helpers and the small custom widgets the interface is built from.
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
    float  light;                        // 0 = the dark theme, 1 = the light one (in between while switching)
};
const Palette& Colors();

void ApplyTheme(ImGuiStyle& style, float dpiScale);
// Moves between the dark (0) and the light (1) theme: blends the palette and rewrites the style colours. Cheap enough
// to call every frame while a switch animates.
void SetThemeLight(ImGuiStyle& style, float light);

ImU32 WithAlpha(ImU32 color, float alpha);
ImU32 Mix(ImU32 a, ImU32 b, float t);   // blend of two colours; a fully transparent side only lends its alpha
ImU32 Col(ImU32 color);                  // the colour dimmed by the current style alpha (inside BeginDisabled)

// Motion: one value per id, kept in the current window's storage and moved a little every frame ------------------
float Animate(ImGuiID id, float target, float speed = 14.0f);                  // exponential approach; the first call starts at the target
float AnimateFrom(ImGuiID id, float from, float target, float speed = 14.0f);  // the first call starts at "from"
float AnimateLinear(ImGuiID id, float target, float seconds);                  // constant speed; the whole way takes "seconds"
void AnimateSnap(ImGuiID id, float value);                                     // jump without motion
float Ease(float t);                                                           // smoothstep of 0..1

// Smooth scrolling for a window opened with ImGuiWindowFlags_NoScrollWithMouse: call right after Begin/BeginChild.
// The wheel moves a target by "step" per notch and the window glides there; scrollbar drags are followed.
void SmoothScroll(bool horizontal, float step);
void SmoothScrollTo(float target, bool horizontal = false);                    // glide to a position (SetScroll with motion)

// Popups and tooltips that fade in instead of appearing at once ---------------------------------------------------
bool BeginPopupFade(const char* strId, ImGuiWindowFlags flags = 0);            // pair with EndPopupFade() when true
void EndPopupFade();
bool BeginDropdown(const char* label, const char* preview, ImGuiComboFlags flags = 0);   // flat combo box; pair with EndDropdown()
void EndDropdown();
void Tooltip(const char* text);                                                // tooltip of the last item, fading in
void TooltipShow(ImGuiID key, const char* text);                               // shows it now; "key" tells one tooltip from another

// Icons drawn from lines and triangles, so they scale with the interface -----------------------------------------
enum class Icon { Play, Pause, StepBack, StepForward, ChevronDown, ChevronUp, ChevronLeft, ChevronRight,
                  Reset, Refresh, OpenExternal, Close, Lock, Undo, Redo };
void DrawIcon(ImDrawList* dl, Icon icon, const ImVec2& center, float size, ImU32 color);          // size: side of the icon's box
void DrawChevron(ImDrawList* dl, const ImVec2& center, float size, float angle, ImU32 color);    // 0 points down, turns clockwise

enum class ButtonKind { Flat, Accent, Ghost, Plain };

// Widgets ----------------------------------------------------------------------------------
bool Toggle(const char* label, bool* v);                                           // switch-style checkbox
bool SectionHeader(const char* label, const char* id, bool defaultOpen = true);    // flat collapsing header; true while the content shows
void SectionEnd();                                                                 // closes the content of a header that returned true
void SectionLabel(const char* text);                                               // caption + thin rule (not collapsible)
void Help(const char* text);                                                       // "?" marker with tooltip
void StatusDot(ImU32 color, const char* text);                                     // coloured dot + text
void Pill(const char* text, ImU32 bg, ImU32 fg);                                   // rounded badge
bool SliderReset(const char* label, float* v, float minV, float maxV, float def, const char* fmt, const char* tooltip);
bool SliderIntReset(const char* label, int* v, int minV, int maxV, int def, const char* fmt, const char* tooltip);
bool FlatButton(const char* label, const ImVec2& size = ImVec2(0, 0));             // the ordinary button
bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));           // the primary action
bool GhostButton(const char* label, const ImVec2& size = ImVec2(0, 0));            // outlined secondary action
bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));           // red, for removing things
bool IconButton(const char* id, Icon icon, const ImVec2& size = ImVec2(0, 0), const char* tooltip = nullptr,
                ButtonKind kind = ButtonKind::Flat);
bool ChevronButton(const char* id, float angle, const ImVec2& size = ImVec2(0, 0), const char* tooltip = nullptr,
                   ButtonKind kind = ButtonKind::Flat);
bool Segmented(const char* id, const char* const* labels, int count, int* value, float width = 0.0f);  // one-of-n switch
void KeyValue(const char* key, const char* value);                                 // two-column line
void Hint(const char* text);                                                       // wrapped dim paragraph

} // namespace vdc::ui
