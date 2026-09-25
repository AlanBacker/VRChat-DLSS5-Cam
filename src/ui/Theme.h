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
    ImU32 card, cardBorder;              // the raised panels: sidebar sections, the library, the video controls
    ImU32 shadow;                        // the soft shadow under floating things (popups, toasts)
    ImVec4 window;                       // the clear colour behind everything
    float  light;                        // 0 = the dark theme, 1 = the light one (in between while switching)
};
const Palette& Colors();

void ApplyTheme(ImGuiStyle& style, float dpiScale);
float Dpi();                                                   // the display scale the style was built for (1 at 96 dpi)
void SetFonts(ImFont* bold, ImFont* mono, ImFont* icons);   // the faces the widgets use for titles, figures and icons (set every frame)
ImFont* BoldFont();
ImFont* MonoFont();
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
// Whether anything moved on this frame (a value still on its way, a scroll gliding, a fade): the interface keeps
// drawing at full rate while it does and may rest once everything has settled. Cleared by ResetAnimating() at the
// start of a frame; MarkAnimating() is for motion kept outside these helpers.
void ResetAnimating();
void MarkAnimating();
bool Animating();
// Fades and shifts what a window has drawn since vertex `fromVtx` (its draw list is edited in place): the bars
// around the preview dip their content in step with the preview's own dip around a change of the source.
void FadeDrawn(ImDrawList* dl, int fromVtx, float alpha, float dy);

// Smooth scrolling for a window opened with ImGuiWindowFlags_NoScrollWithMouse: call right after Begin/BeginChild.
// The wheel moves a target by "step" per notch and the window glides there; scrollbar drags are followed.
void SmoothScroll(bool horizontal, float step);
void SmoothScrollTo(float target, bool horizontal = false);                    // glide to a position (SetScroll with motion)

// Popups and tooltips that fade in instead of appearing at once ---------------------------------------------------
bool BeginPopupFade(const char* strId, ImGuiWindowFlags flags = 0);            // pair with EndPopupFade() when true
void EndPopupFade();
// A dialog: a popup centred on "center" that opens with a short rise (it fades in while it grows from a little
// smaller and settles a few pixels up into place) and, however it closes (a button, a click outside, Escape), sinks
// away the same way. Pair a true return with EndDialog().
bool BeginDialog(const char* strId, const ImVec2& center, ImGuiWindowFlags flags = 0);
void EndDialog();
void ScrollEdgeFade(ImU32 bg, float height);   // in a scrolled child, before EndChild: the rows cut off above or below fade into bg
bool BeginDropdown(const char* label, const char* preview, ImGuiComboFlags flags = 0);   // flat combo box; pair with EndDropdown()
void EndDropdown();
void Tooltip(const char* text);                                                // tooltip of the last item, fading in
void TooltipShow(ImGuiID key, const char* text);                               // shows it now; "key" tells one tooltip from another

// Icons: the Lucide line icons from the icon font (sharp at every size), the media controls drawn as filled shapes --
enum class Icon { Play, Pause, StepBack, StepForward, ChevronDown, ChevronUp, ChevronLeft, ChevronRight,
                  Reset, Refresh, OpenExternal, Close, Lock, Undo, Redo, Fullscreen, ExitFullscreen, History,
                  Help, Save, Edit, Plus, Search, RotateLeft, RotateRight, FlipH, FlipV, Crop,
                  Image, Film, Broadcast, Sparkle, Download, Layers, Grid, Eye, Plug, Gauge, Info, Folder, Check,
                  Camera, Warning, Stop, Sliders, CircleCheck, CircleX, ImagePlus, Languages, Trash, Keyboard, Monitor,
                  Wand, Settings, Compare, ZoomIn, ZoomOut, Terminal, Key, Upload, Copy, Images, Video, Cpu, Loader,
                  Import, Minus, Ellipsis, ListChecks, SquareCheck, ArrowRight, Sun, Moon, Bell, Scissors, Flag, Repeat,
                  FileImage, FileVideo, Mouse, Hand, Move, Focus, Aperture, Shield, Wifi, HardDrive, PanelLeft,
                  PanelRight, None };
void DrawIcon(ImDrawList* dl, Icon icon, const ImVec2& center, float size, ImU32 color);          // size: side of the icon's box
float IconSize(float scale = 1.0f);                                                              // a whole-pixel icon size that suits the current font
void DrawChevron(ImDrawList* dl, const ImVec2& center, float size, float angle, ImU32 color);    // 0 points down, turns clockwise
void DrawSpinner(ImDrawList* dl, const ImVec2& center, float size, ImU32 color);                 // a turning arc (keeps frames coming)
void DrawLogo(ImDrawList* dl, const ImVec2& min, float size, float alpha = 1.0f);                // the program's mark, drawn
// A soft shadow around a rectangle, drawn outside it only (so it may be painted after the rectangle's own fill).
void DrawShadow(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, float alpha = 1.0f);
// A raised card: soft shadow, card fill, hairline border (the sidebar sections, the video controls, the library).
void DrawCard(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float alpha = 1.0f);
// A card's rim without its fill: the shadow and the hairline, for a card whose inside is painted by its owner (the
// picture). Drawn after that inside.
void DrawCardEdge(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float alpha = 1.0f);
float CardRounding();                                                                            // corner radius of cards and panels
float CardInset();                                                                               // the padding inside a card
void WindowShadow();                                                                             // a soft shadow around the current popup, tooltip or floating window
// Paints the outside of a rectangle's rounded corners in "bg" (the colour behind it), so something drawn square on
// top of a rounded panel (the picture) reads as clipped to the panel.
void MaskCorners(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float rounding, ImU32 bg);
// A dropdown with an icon at the start of its box ("preview" may be null: the icon alone, for a narrow bar).
bool BeginDropdown(const char* label, const char* preview, Icon icon, ImGuiComboFlags flags = 0);

enum class ButtonKind { Flat, Accent, Ghost, Plain, Danger };

// Widgets ----------------------------------------------------------------------------------
bool Toggle(const char* label, bool* v);                                           // switch-style checkbox
bool Checkbox(const char* label, bool* v);                                         // flat box with a drawn check mark
bool Radio(const char* label, bool active);                                        // round choice mark; true when clicked
// A section of the sidebar: a card with an icon, a title and a fold chevron; true while its content shows (it is
// drawn inside the card, inset from its edges). Pair a true return with SectionEnd().
bool SectionHeader(const char* label, const char* id, bool defaultOpen = true, Icon icon = Icon::None);
void SectionEnd();                                                                 // closes the content of a header that returned true
// Widgets that come and go (the Advanced switch's) open and close the way a section folds: between RevealBegin and
// RevealEnd they are laid out in full but show only as far down as "t" (0..1) reaches, faded by it, and whatever
// follows moves with that edge. False when t is 0: nothing is drawn, and RevealEnd is not called.
bool RevealBegin(const char* id, float t);
void RevealEnd();
// A card around widgets whose height is known only once they are drawn: between PanelBegin and PanelEnd the
// widgets are laid out inside it, "pad" in from its edges ("width" 0: the rest of the line). A colour in "stripe"
// paints a band along its left edge (a notice).
void PanelBegin(float width, const ImVec2& pad, ImU32 stripe = 0);
void PanelEnd();
void FocusRing();                                                                  // an accent ring around the last item while it has the keyboard
void SectionLabel(const char* text);                                               // caption + thin rule (not collapsible)
void Help(const char* text);                                                       // "?" marker with tooltip
void StatusDot(ImU32 color, const char* text);                                     // coloured dot + text
void Pill(const char* text, ImU32 bg, ImU32 fg);                                   // rounded badge
void PillAfter(const char* text, ImU32 bg, ImU32 fg, float spacing);               // a badge right of the last item, or on the next line when it has no room there
// The label column: the room a control leaves for the label to its right. It is as wide as the widest trailing
// label drawn so far in the current language (a ratchet in font sizes, started over on a language change), never
// under 7.5 em and never more than half the row, so a long label (Japanese, English) is not cut at the edge.
void  LabelSeen(const char* label);                                                // every trailing label reports itself
void  TrailingLabel(const char* label);                                            // draws it and reports it
void  LabelAfterItem(const char* label);                                           // a trailing label that leaves the control the last item (its tip covers the label)
float LabelColumn(float rowWidth);                                                 // PushItemWidth(-LabelColumn(avail))
void  SameLineIfFits(const char* buttonLabel);                                     // SameLine() only when a button of this label has room left on the line
void  SameLineIfRoom(float width, float spacing);                                  // SameLine(0, spacing) only when "width" has room left on the line
// A slider, its reset icon and its trailing label. The icon is only there once the value differs from "def"; the
// tooltip covers the label too.
bool SliderReset(const char* label, float* v, float minV, float maxV, float def, const char* fmt, const char* tooltip);
bool SliderIntReset(const char* label, int* v, int minV, int maxV, int def, const char* fmt, const char* tooltip);
// The reset icon beside a control: shown while "modified", fading in and out, its room kept either way so the
// control beside it keeps its width.
bool ResetButton(const char* id, bool modified, float size, const char* tooltip = nullptr);
bool InputIntLabel(const char* label, int* v, int step, int stepFast);             // ImGui::InputInt with a trailing label that wraps
// ImGui sliders whose track fills with the accent up to the value (the look of every slider in the program).
bool SliderFloatFill(const char* label, float* v, float minV, float maxV, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
bool SliderIntFill(const char* label, int* v, int minV, int maxV, const char* fmt = "%d", ImGuiSliderFlags flags = 0);
bool FlatButton(const char* label, const ImVec2& size = ImVec2(0, 0));             // the ordinary button
bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));           // the primary action
bool GhostButton(const char* label, const ImVec2& size = ImVec2(0, 0));            // outlined secondary action
bool IconTextButton(const char* label, Icon icon, const ImVec2& size = ImVec2(0, 0), ButtonKind kind = ButtonKind::Flat);   // icon before the label
float IconTextButtonWidth(const char* label);                                      // the natural width of such a button
bool DangerButton(const char* label, const ImVec2& size = ImVec2(0, 0));           // red, for removing things
bool IconButton(const char* id, Icon icon, const ImVec2& size = ImVec2(0, 0), const char* tooltip = nullptr,
                ButtonKind kind = ButtonKind::Flat);
bool ChevronButton(const char* id, float angle, const ImVec2& size = ImVec2(0, 0), const char* tooltip = nullptr,
                   ButtonKind kind = ButtonKind::Flat);
bool Segmented(const char* id, const char* const* labels, int count, int* value, float width = 0.0f,
               const Icon* icons = nullptr);                                       // one-of-n switch, optionally with an icon per segment
float SegmentedWidth(const char* const* labels, int count, const Icon* icons = nullptr);   // its natural width
void KeyValue(const char* key, const char* value);                                 // two-column line

// Search: between SearchBegin and SearchEnd the labelled widgets that do not match the query are left out, and a
// section (SectionHeader) whose widgets all missed on the previous frame is left out whole. A section whose title
// matches shows everything in it. The match is a case-insensitive substring of the label or the tooltip.
void SearchBegin(const char* query);                                               // empty or null: no filtering
void SearchEnd();
bool Searching();                                                                  // a query is set
// Between SearchHold(true) and SearchHold(false) every widget shows whatever the query: the parts of a control that
// belong to it as a whole (the modifier keys of the capture hotkey, the size fields under "Custom resolution").
void SearchHold(bool hold);
bool SearchMatch(const char* label, const char* tooltip = nullptr);               // whether such a widget shows
bool SearchSkipped();                                                              // the last labelled widget was left out
int  SearchHits();                                                                 // matches so far in this frame
void Hint(const char* text);                                                       // wrapped dim paragraph
// A progress line: "text" at the left of a row and the share done at its right, over a slim rounded track that the
// accent fills, gliding to each new value. A negative fraction sweeps a segment along the track instead, for a step
// whose length is unknown. "width" 0: the rest of the row.
void ProgressLine(const char* id, const char* text, float fraction, float width = 0.0f);

} // namespace vdc::ui
