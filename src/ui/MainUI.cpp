// VRChat DLSS5 Cam - the main window. Flat layout: a top bar with the source switch and the main action, the preview
// in the middle with the video controls and the media library under it, a sidebar of plain sections on the right.
#include "ui/MainUI.h"
#include "ui/Theme.h"
#include "core/Capture.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/McpServer.h"
#include "core/Util.h"
#include "core/VideoSource.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace vdc::ui {

namespace {

struct HotkeyName { unsigned vk; const char* name; };
const HotkeyName kHotkeys[] = {
    {'A',"A"},{'B',"B"},{'C',"C"},{'D',"D"},{'E',"E"},{'F',"F"},{'G',"G"},{'H',"H"},{'I',"I"},{'J',"J"},{'K',"K"},{'L',"L"},{'M',"M"},
    {'N',"N"},{'O',"O"},{'P',"P"},{'Q',"Q"},{'R',"R"},{'S',"S"},{'T',"T"},{'U',"U"},{'V',"V"},{'W',"W"},{'X',"X"},{'Y',"Y"},{'Z',"Z"},
    {'0',"0"},{'1',"1"},{'2',"2"},{'3',"3"},{'4',"4"},{'5',"5"},{'6',"6"},{'7',"7"},{'8',"8"},{'9',"9"},
    {0x70,"F1"},{0x71,"F2"},{0x72,"F3"},{0x73,"F4"},{0x74,"F5"},{0x75,"F6"},{0x76,"F7"},{0x77,"F8"},{0x78,"F9"},{0x79,"F10"},{0x7A,"F11"},{0x7B,"F12"},
    {0x20,"Space"},{0x2D,"Insert"},{0x2C,"PrintScreen"},{0x13,"Pause"},{0x24,"Home"},{0x23,"End"},{0x21,"PageUp"},{0x22,"PageDown"},
    {0x60,"Num0"},{0x61,"Num1"},{0x62,"Num2"},{0x63,"Num3"},{0x64,"Num4"},{0x65,"Num5"},{0x66,"Num6"},{0x67,"Num7"},{0x68,"Num8"},{0x69,"Num9"},
};
const int kTimelapseChoices[] = { 0, 1, 2, 5, 10, 30, 60, 300 };

const char* HotkeyKeyName(unsigned vk) {
    for (const auto& h : kHotkeys) if (h.vk == vk) return h.name;
    return "?";
}

std::string FormatDuration(double seconds) {   // m:ss, h:mm:ss above an hour
    const int t = (int)(std::max(0.0, seconds) + 0.5);
    if (t >= 3600) return StrPrintf("%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
    return StrPrintf("%d:%02d", t / 60, t % 60);
}
std::string FormatClock(double seconds) {      // mm:ss.hh for the transport: a fixed width in the monospace font
    const double v = std::max(0.0, seconds);
    const int whole = (int)v;
    const int hundredths = std::min(99, (int)((v - whole) * 100.0 + 0.5));
    if (whole >= 3600) return StrPrintf("%d:%02d:%02d.%02d", whole / 3600, (whole / 60) % 60, whole % 60, hundredths);
    return StrPrintf("%02d:%02d.%02d", whole / 60, whole % 60, hundredths);
}
std::string FormatMsFixed(double ms) { return StrPrintf("%6.2f ms", ms); }   // constant width in the monospace font

// A length given at 96 dpi, in whole pixels at the current display scale.
float Px(float v) { return std::round(v * Dpi()); }

// The "Open..." button with, once a file is open, a close button beside it.
void OpenCloseRow(const char* openLabel, Icon icon, bool loaded, bool& open, bool& close) {
    const float closeW = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float openW = loaded ? std::max(Px(40.0f), ImGui::GetContentRegionAvail().x - closeW - spacing) : -FLT_MIN;
    if (IconTextButton(openLabel, icon, ImVec2(openW, 0))) open = true;
    if (!loaded) return;
    ImGui::SameLine(0.0f, spacing);
    if (IconButton("##closeMedia", Icon::Close, ImVec2(closeW, 0), TR(TipCloseMedia))) close = true;
}
constexpr float kZoomMin = 0.1f, kZoomMax = 8.0f;   // preview magnification limits, relative to the picture's pixels

// A dimmed label at the left and its value flush with the right edge of the row, in the monospace font: a
// justified two-column list, so the figures line up at the right whatever the language's label widths, and a
// figure that changes never pushes anything else around. A label with a tip carries its "?" mark right after it,
// as the controls do. A value with no room left after its label (a long label in a narrow sidebar) goes on the next
// line, still flush right.
void Readout(const Fonts* fonts, const char* label, const std::string& value, const char* tip = nullptr) {
    if (!SearchMatch(label, value.c_str())) return;
    const float x0 = ImGui::GetCursorPosX();
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("%s:", label);
    if (tip) Help(tip);
    const float labelEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX();
    if (fonts) ImGui::PushFont(fonts->Mono(), 0.0f);
    const float w = ImGui::CalcTextSize(value.c_str()).x;
    if (x0 + rowW - w >= labelEnd + ImGui::GetStyle().ItemSpacing.x) ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(x0, x0 + rowW - w));
    ImGui::TextDisabled("%s", value.c_str());
    if (fonts) ImGui::PopFont();
}

void SyncBuffer(char* buf, size_t size, const std::string& value, bool editing) {
    if (editing) return;
    if (std::strncmp(buf, value.c_str(), size) != 0) std::snprintf(buf, size, "%s", value.c_str());
}

bool ComboIds(const char* label, int* value, const char* const* items, int count, const char* tooltip = nullptr) {
    bool changed = false;
    bool shown = SearchMatch(label, tooltip);
    for (int i = 0; !shown && i < count; ++i) shown = SearchMatch(items[i]);   // the option names count too
    if (!shown) return false;
    if (*value < 0 || *value >= count) *value = 0;
    if (BeginDropdown(label, items[*value])) {
        for (int i = 0; i < count; ++i) {
            const bool selected = (*value == i);
            if (ImGui::Selectable(items[i], selected)) { *value = i; changed = true; }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        EndDropdown();
    }
    Tooltip(tooltip);
    return changed;
}

void Tip(const char* text) { if (!SearchSkipped()) Tooltip(text); }

// A button with an icon before its label while both fit the width it is given, the label alone when they do not
// (a narrow sidebar, a long translation): the words matter more than the picture.
bool ActionButton(const char* label, Icon icon, const ImVec2& size, ButtonKind kind = ButtonKind::Ghost) {
    const float w = size.x < 0.0f ? ImGui::GetContentRegionAvail().x + size.x : size.x;
    if (kind == ButtonKind::Plain || w <= 0.0f || IconTextButtonWidth(label) <= w) return IconTextButton(label, icon, size, kind);
    if (kind == ButtonKind::Accent) return AccentButton(label, size);
    if (kind == ButtonKind::Flat) return FlatButton(label, size);
    return GhostButton(label, size);
}

// Whether a switch of "count" equal segments across "width" leaves every segment room for its icon and its name.
bool SegmentsFit(const char* const* labels, int count, const Icon* icons, float width) {
    for (int i = 0; i < count; ++i)
        if (SegmentedWidth(labels + i, 1, icons ? icons + i : nullptr) * (float)count > width) return false;
    return true;
}

// The size of two buttons that share a row: half the row each while both names fit that, the full row each (one
// above the other) when one does not.
ImVec2 PairSize(const char* a, const char* b, float fullW) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float halfW = std::floor((fullW - style.ItemSpacing.x) * 0.5f);
    const float pad = style.FramePadding.x * 2.0f;
    const bool fit = ImGui::CalcTextSize(a, nullptr, true).x + pad <= halfW && ImGui::CalcTextSize(b, nullptr, true).x + pad <= halfW;
    return ImVec2(fit ? halfW : fullW, 0.0f);
}

// The hit width of the line that resizes the sidebar or the library, at the edge of their gutters.
constexpr float kBarGrip = 6.0f;   // at 96 dpi

// The gutter between two panels that folds one of them away: the window's background with a chevron in the
// middle, lit under the mouse (a rounded fill over the gutter and a brighter chevron). "angle" turns the chevron
// (0 points down).
void DrawGutter(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, float hover, float angle) {
    const Palette& p = Colors();
    if (hover > 0.01f)
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), Col(WithAlpha(p.controlHover, (p.light > 0.5f ? 0.9f : 0.7f) * hover)), Px(5.0f));
    DrawChevron(dl, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f), IconSize(0.55f), angle, Col(Mix(p.textDim, p.text, hover)));
}

// The padding inside the library's card.
ImVec2 LibraryPad() { return ImVec2(Px(14.0f), Px(10.0f)); }

// The height of the video controls under the picture: the seek bar and the row of buttons inside the card's padding.
float TransportHeight() {
    const float frameH = ImGui::GetFrameHeight();
    return std::round(frameH * 0.9f) + ImGui::GetStyle().ItemSpacing.y + frameH + Px(10.0f) * 2.0f;
}

// Text over the picture: a dark copy a pixel down and to the right keeps it readable on any frame.
void ShadowText(ImDrawList* dl, const ImVec2& pos, ImU32 col, const char* text) {
    const float o = std::max(1.0f, Px(1.0f));
    const int a = (int)(140.0f * (float)((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
    dl->AddText(ImVec2(pos.x + o, pos.y + o), IM_COL32(0, 0, 0, a), text);
    dl->AddText(pos, col, text);
}

// A coloured dot with a soft halo and one line of text, cut with an ellipsis at "maxX" (the status bar never wraps).
// "detail" follows the text in the dim colour while there is room for some of it.
void DotLine(ImDrawList* dl, const ImVec2& pos, float lineH, float maxX, ImU32 dot, ImU32 textCol, const char* text, const char* detail = nullptr) {
    const float r = std::round(lineH * 0.22f);
    const float halo = std::max(1.0f, Px(2.0f));
    const ImVec2 c(pos.x + halo + r, pos.y + lineH * 0.5f);
    dl->AddCircleFilled(c, r + halo, Col(WithAlpha(dot, 0.22f)), 0);
    dl->AddCircleFilled(c, r, Col(dot), 0);
    const float x = c.x + r + halo + Px(6.0f);
    if (maxX <= x) return;
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
    ImGui::RenderTextEllipsis(dl, ImVec2(x, pos.y), ImVec2(maxX, pos.y + lineH), maxX, text, nullptr, nullptr);
    ImGui::PopStyleColor();
    const float dx = x + ImGui::CalcTextSize(text).x + Px(10.0f);
    if (!detail || !*detail || maxX - dx < ImGui::GetFontSize() * 3.0f) return;
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().textDim);
    ImGui::RenderTextEllipsis(dl, ImVec2(dx, pos.y), ImVec2(maxX, pos.y + lineH), maxX, detail, nullptr, nullptr);
    ImGui::PopStyleColor();
}

// "2 min 05 s" style text of a duration estimate.
std::string FormatEstimate(double seconds) {
    const int t = (int)(std::max(0.0, seconds) + 0.5);
    if (t >= 3600) return StrPrintf(TR(EstHours), t / 3600, (t / 60) % 60);
    if (t >= 60) return StrPrintf(TR(EstMinutes), t / 60, t % 60);
    return StrPrintf(TR(EstSeconds), std::max(1, t));
}

bool IsHevc(const std::string& codec) { return codec.find("HEV") != std::string::npos || codec == "H265"; }

// "loops forever" / "plays once" / "plays N times" of an animated image.
std::string LoopText(int loops) {
    if (loops <= 0) return TR(LoopForever);
    if (loops == 1) return TR(LoopOnce);
    return StrPrintf(TR(LoopTimes), loops);
}

// The text of a library item's state badge (null: none).
const char* StateText(const LibraryItem& it) {
    if (it.probe == 2) return TR(StateUnreadable);
    switch (it.state) {
    case LibraryItem::Queued: return TR(StateQueued);
    case LibraryItem::Processing: return TR(Processing);
    case LibraryItem::Done: return TR(StateDone);
    case LibraryItem::Failed: return TR(StateFailed);
    default: return nullptr;
    }
}

// While a file is processed the settings are locked: a change half-way through would make a result that mixes two
// looks, and a still image being saved must not restart its passes.
bool SettingsLocked(const UiFrameInfo& info) {
    return info.videoProcessing || info.videoFinishing || info.batchRunning || info.capturePending > 0
        || (info.status && info.status->capturesInFlight > 0);
}

} // namespace

void MainUI::Toast(const std::string& text, bool error, bool success) {
    m_toasts.push_back({ text, ImGui::GetTime(), error, success });
    if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
}

// ------------------------------------------------------------------------------------------

void MainUI::UpdateShown(const UiFrameInfo& info) {
    const double now = ImGui::GetTime();
    if (m_shownTime >= 0.0 && now - m_shownTime < 0.5) return;
    m_shownTime = now;
    m_shown.fps = info.fps; m_shown.cpuMs = info.cpuMs; m_shown.uiGpuMs = info.uiGpuMs;
    m_shown.processingFps = info.processingFps; m_shown.senderFps = info.senderFps;
    if (info.status) {
        for (UINT i = 0; i < (UINT)GpuTimer::Count; ++i) m_shown.gpuMs[i] = info.status->gpuMs[i];
        m_shown.statAvgCost = info.status->statAvgCost; m_shown.statMaxCost = info.status->statMaxCost;
        m_shown.statAvgMotion = info.status->statAvgMotion;
        m_shown.processedFrames = (unsigned long long)info.status->processedFrames;
        m_shown.resets = (unsigned long long)info.status->resets;
        m_shown.depthMs = info.status->depthInferMs;
        m_shown.nrOutDelta = info.status->nrOutDelta;
    }
}

void MainUI::Draw(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    m_fonts = &fonts;
    SetFonts(fonts.Bold(), fonts.Mono(), fonts.Icons());
    ResetAnimating();
    m_presetList = info.presets;
    UpdateShown(info);
    ImGuiIO& io = ImGui::GetIO();
    // Theme: the palette crossfades when the choice (or Windows) changes.
    {
        const float target = (s.theme == 2 || (s.theme == 0 && info.systemLight)) ? 1.0f : 0.0f;
        if (m_themeLight < 0.0f) m_themeLight = target;
        else if (m_themeLight != target) {
            const float d = target - m_themeLight;
            m_themeLight = std::fabs(d) < 0.004f ? target : m_themeLight + d * (1.0f - std::exp(-9.0f * std::min(io.DeltaTime, 0.05f)));
        }
        if (m_themeLight != Colors().light) SetThemeLight(ImGui::GetStyle(), m_themeLight);
        if (m_themeLight != target) MarkAnimating();
    }
    if (!m_undoInit) { m_undoBase = { s.ParameterText(), LibrarySnapshot(info), std::string() }; m_undoInit = true; }
    // The update popup waits while the setup guide is up; the guide shows the same access controls as the popup
    // that a failed mirror check would open, so that one is not opened over it. Both also wait until the window has
    // come up after the start-up card (when the guide would open): a check that answers at once finds its result
    // within the first frames, and the window's own fade-in would cover the popup's rise.
    if (info.updateShow) m_updateDeferred = true;
    if (info.mirrorPrompt && !m_guideShowing) m_mirrorDeferred = true;
    if (m_guideShowing) m_mirrorDeferred = false;
    if (m_guideAutoDone && !m_guideShowing) {
        if (m_updateDeferred) { m_updateDeferred = false; m_updateOpen = true; }
        if (m_mirrorDeferred) { m_mirrorDeferred = false; m_mirrorOpen = true; }
    }
    if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) ApplyUndo(s, info, ev, io.KeyShift);
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) ApplyUndo(s, info, ev, true);
    }
    // F11 enters and leaves the fullscreen view, Esc leaves it (unless a popup or a text field takes the key).
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) RequestFullscreen();
        else if (info.fullscreen && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                 !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) RequestFullscreen();
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, info.fullscreen ? ImVec2(0, 0) : ImVec2(Px(12.0f), Px(10.0f)));
    if (info.fullscreen) ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 255));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##host", nullptr, flags);
    if (info.fullscreen) ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    if (info.fullscreen) {
        // The picture alone, over the whole screen; no bars, sidebar or library.
        DrawFullscreen(s, info, ev, fonts);
        ImGui::End();
        m_previewMin = vp->WorkPos;
        m_previewMax = ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
        DrawFades(s, info, ev);
        DrawToasts(fonts);
        TrackUndo(s, info);
        return;
    }

    DrawTopBar(s, info, ev, fonts);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float statusH = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bodyH = std::max(50.0f, avail.y - statusH - style.ItemSpacing.y);
    // The sidebar's width is the user's (dragged at the handle), kept within what the window can give.
    const float em = ImGui::GetFontSize();
    const float sidebarMin = em * 16.0f, sidebarMax = std::max(sidebarMin, avail.x * 0.6f);
    const float sidebarW = std::max(50.0f, std::min(std::clamp((s.sidebarWidth > 0.0f ? s.sidebarWidth : 24.0f) * em, sidebarMin, sidebarMax), avail.x * 0.6f));
    // The sidebar slides in and out behind the gutter at the edge of the preview; the preview takes the room it
    // frees while it moves.
    const float handleW = Px(16.0f);
    const float sideT = Ease(AnimateLinear(ImGui::GetID("##sidebarSlide"), s.sidebarVisible ? 1.0f : 0.0f, 0.22f));
    const float shownW = sidebarW * sideT;
    const float previewW = std::max(50.0f, avail.x - shownW - handleW);
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();
    m_previewMin = bodyOrigin;   // the fade's rectangle, narrowed to the picture in DrawPreview
    m_previewMax = ImVec2(bodyOrigin.x + previewW, bodyOrigin.y + bodyH);

    // The preview column (picture, video controls, library) paints its own cards on the window's background.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##preview", ImVec2(previewW, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    DrawPreview(s, info, ev, fonts);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    {
        // The gutter has two zones, so a click and a drag never share the same spot: the gutter itself folds the
        // sidebar (hand cursor, the chevron lights up), the thin line along its sidebar-side edge
        // resizes it (resize cursor, blue under the mouse).
        const Palette& p = Colors();
        const ImVec2 hpos = ImGui::GetCursorScreenPos();
        const bool canResize = s.sidebarVisible && sideT > 0.99f;
        const float gripW = canResize ? Px(kBarGrip) : 0.0f;
        ImGui::InvisibleButton("##sidebarFold", ImVec2(handleW - gripW, bodyH));
        const bool foldLit = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (foldLit) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemDeactivated() && ImGui::IsItemHovered()) { s.sidebarVisible = !s.sidebarVisible; ev.settingsChanged = true; }
        Tip(s.sidebarVisible ? TR(SidebarHide) : TR(SidebarShow));
        bool gripLit = false;
        if (gripW > 0.0f) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::InvisibleButton("##sidebarResize", ImVec2(gripW, bodyH));
            if (ImGui::IsItemActivated()) m_sidebarDragW = sidebarW;
            if (ImGui::IsItemActive()) {
                const float dx = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x;
                s.sidebarWidth = std::clamp(m_sidebarDragW - dx, sidebarMin, sidebarMax) / em;
            }
            if (ImGui::IsItemDeactivated()) ev.settingsChanged = true;
            gripLit = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (gripLit) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (!ImGui::IsItemActive()) Tip(TR(TipSidebarDrag));
        }
        const float hov = Animate(ImGui::GetID("##sidebarHandleHover"), foldLit ? 1.0f : 0.0f, 16.0f);
        const float grip = Animate(ImGui::GetID("##sidebarGripHover"), gripLit ? 1.0f : 0.0f, 16.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float foldW = handleW - gripW;
        DrawGutter(dl, hpos, ImVec2(foldW, bodyH), hov, IM_PI * 0.5f - IM_PI * sideT);
        if (canResize) {   // the line a drag moves: always drawn so it can be found, blue under the mouse
            const float w = std::max(1.0f, Px(1.0f)) + Px(2.0f) * grip;
            const float x = hpos.x + handleW - Px(3.0f);
            dl->AddRectFilled(ImVec2(x - w * 0.5f, hpos.y + CardRounding()), ImVec2(x + w * 0.5f, hpos.y + bodyH - CardRounding()),
                              Mix(p.cardBorder, p.accent, grip), w * 0.5f);
        }
    }

    // The Advanced switch's controls open and close with it (RevealBegin), in the sidebar, the status bar and the
    // window of a library item's own values.
    m_advShown = AnimateLinear(ImGui::GetID("##advanced"), s.showAdvanced ? 1.0f : 0.0f, 0.2f);
    if (shownW > 0.5f) {
        ImGui::SameLine(0.0f, 0.0f);
        // The clip reaches a little above the body, into the gap under the top bar, so the search field's focus ring
        // shows whole.
        ImGui::PushClipRect(ImVec2(bodyOrigin.x, bodyOrigin.y - Px(4.0f)), ImVec2(bodyOrigin.x + avail.x, bodyOrigin.y + bodyH), true);
        // While the settings are locked their banner holds the top of the column, outside the scrolled settings, so a
        // scrolled sidebar can neither cut it nor carry it away; the settings make room for it as it comes in. The
        // search row stays above the scrolled settings for the same reason, under the banner while there is one.
        const ImVec2 column = ImGui::GetCursorScreenPos();
        const bool locked = SettingsLocked(info);
        if (locked) m_lockText = info.mcpJob.empty() ? TR(LockedWhileBusy) : StrPrintf(TR(McpJobRunningFmt), info.mcpJob.c_str());
        const float lockT = Animate(ImGui::GetID("##lock"), locked ? 1.0f : 0.0f, 12.0f);
        // The cards reach the edges of the column; the side padding leaves room for their shadows only.
        const float pad = Px(6.0f);
        // The banner and the search row end where the cards do. They are laid out before the settings are drawn, with
        // the settings' scrollbar as it was on the last frame. When the Cancel button comes or goes (a video that is
        // finishing can no longer be cancelled), its room below the text opens or closes smoothly.
        const float barW = m_sidebarBarW;
        const LockLayout lock = LayOutLockBanner(m_lockText, info.batchRunning || info.videoProcessing, std::floor(sidebarW) - pad * 2.0f - barW);
        const float lockH = Animate(ImGui::GetID("##lockHeight"), lock.height, 12.0f);
        const float lockSpace = std::round((lockH + style.ItemSpacing.y) * lockT);
        ImGui::SetCursorScreenPos(ImVec2(column.x + pad, column.y + lockSpace));
        const int vtxSearch = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        const float settingsTop = std::floor(DrawSidebarSearch(s, ev, std::max(1.0f, sidebarW - pad * 2.0f - barW)) + style.ItemSpacing.y);
        ModeFadeContent(vtxSearch);
        ImGui::SetCursorScreenPos(ImVec2(column.x, settingsTop));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, 0.0f));
        ImGui::BeginChild("##sidebar", ImVec2(sidebarW, std::max(1.0f, column.y + bodyH - settingsTop)), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        m_sidebarBarW = ImGui::GetCurrentWindow()->ScrollbarSizes.x;   // for the banner and the search row on the next frame
        ImGui::SetScrollX(0.0f);   // the sidebar only ever scrolls vertically
        SmoothScroll(false, ImGui::GetFontSize() * 3.6f);
        if (m_openAbout) {   // after the setup guide: the About section opens and the sidebar glides to it
            m_openAbout = false;
            ImGui::PushID("about");
            ImGui::GetStateStorage()->SetBool(ImGui::GetID("##header"), true);
            ImGui::PopID();
        }
        if (m_scrollToAbout > 0.0) {
            if (ImGui::GetTime() < m_scrollToAbout) { if (m_aboutY >= 0.0f) SmoothScrollTo(m_aboutY); }
            else m_scrollToAbout = -1.0;
        }
        const int vtx0 = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        DrawSidebar(s, info, ev, fonts);
        ModeFadeContent(vtx0);
        ImGui::EndChild();
        if (lockSpace > 0.0f) {   // the layout goes on below the settings
            const ImVec2 next = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(column);
            DrawLockBanner(info, ev, lock, lockT, lockSpace, lockH, sidebarW, barW);
            ImGui::SetCursorScreenPos(next);
        }
        ImGui::PopClipRect();
    }

    DrawStatusBar(s, info, ev, fonts);
    DrawUpdatePopup(s, info, ev, fonts);
    DrawMirrorPopup(s, info, ev, fonts);
    DrawSetupGuide(s, info, ev, fonts);
    ImGui::End();

    if (s.showLog) DrawLogWindow(s, ev, fonts);
    DrawItemParams(s, info, ev, fonts);
    DrawFades(s, info, ev);
    DrawToasts(fonts);
    TrackUndo(s, info);
}

// Fullscreen: the picture over the whole screen. The transport bar of a video and the exit button show while the
// mouse moves and fade away after a few seconds of rest (the keys keep working).
void MainUI::DrawFullscreen(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 region = ImGui::GetContentRegionAvail();
    if (region.x < 8.0f || region.y < 8.0f) return;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || ImGui::IsAnyMouseDown()) m_fullscreenMouseTime = now;
    const bool transport = s.sourceMode == SourceVideo && info.videoLoaded;
    // The video controls float over the bottom of the picture as a card, no wider than a comfortable reach.
    const float inset = Px(16.0f);
    const float transportH = TransportHeight();
    const float barW = std::min(region.x - inset * 2.0f, ImGui::GetFontSize() * 60.0f);
    const ImVec2 barPos(origin.x + std::floor((region.x - barW) * 0.5f), origin.y + region.y - transportH - inset);
    const bool overBar = transport && ImGui::IsMousePosValid() && io.MousePos.y >= barPos.y - Px(24.0f);
    const bool wantControls = now - m_fullscreenMouseTime < 2.5 || overBar || m_seekDragging || ImGui::IsAnyItemActive();
    m_fullscreenControls = Ease(AnimateLinear(ImGui::GetID("##fullscreenControls"), wantControls ? 1.0f : 0.0f, 0.3f));
    m_toastBottom = origin.y + region.y - inset - (transport ? transportH + inset : 0.0f);

    DrawPicture(s, info, ev, fonts, origin, region);
    if (transport) {
        if (m_fullscreenControls > 0.02f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * m_fullscreenControls);
            DrawTransport(s, info, ev, fonts, barPos, ImVec2(barW, transportH));
            ImGui::PopStyleVar();
        } else {
            VideoKeys(info, ev);
        }
    }
    if (!wantControls && m_fullscreenControls < 0.02f) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
}

// The fullscreen switch in the top right corner of the picture (F11 does the same; Esc leaves).
void MainUI::FullscreenButton(const UiFrameInfo& info, UiEvents& ev, const ImVec2& origin, const ImVec2& region) {
    const float alpha = info.fullscreen ? m_fullscreenControls : 1.0f;
    if (alpha < 0.02f) return;
    const float bsz = ImGui::GetFrameHeight();
    if (region.x < bsz * 3.0f || region.y < bsz * 3.0f) return;
    const ImVec2 pos(origin.x + region.x - bsz - Px(12.0f), origin.y + Px(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + bsz, pos.y + bsz), ImGui::GetColorU32(Colors().overlayBg), ImGui::GetStyle().FrameRounding);
    ImGui::SetCursorScreenPos(pos);
    if (IconButton("##fullscreen", info.fullscreen ? Icon::ExitFullscreen : Icon::Fullscreen, ImVec2(bsz, bsz),
                   info.fullscreen ? TR(TipExitFullscreen) : TR(TipFullscreen), ButtonKind::Plain)) RequestFullscreen();
    ImGui::PopStyleVar();
}

// Undo ---------------------------------------------------------------------------------------

std::vector<LibrarySnapshotItem> MainUI::LibrarySnapshot(const UiFrameInfo& info) {
    std::vector<LibrarySnapshotItem> out;
    if (!info.library) return out;
    out.reserve(info.library->size());
    for (const LibraryItem& item : *info.library) {
        LibrarySnapshotItem e;
        e.path = item.path;
        e.useOwn = item.useOwn;
        if (item.useOwn && item.own) e.own = item.own->ParameterText();
        e.inSec = item.inSec; e.outSec = item.outSec;
        e.transform = item.transform;
        out.push_back(std::move(e));
    }
    return out;
}

// The files and their own values count; the processing range is carried along without making a step of its own.
bool MainUI::SameLibrary(const std::vector<LibrarySnapshotItem>& a, const std::vector<LibrarySnapshotItem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].path != b[i].path || a[i].useOwn != b[i].useOwn || a[i].own != b[i].own || a[i].transform != b[i].transform) return false;
    }
    return true;
}

void MainUI::TrackUndo(const Settings& s, const UiFrameInfo& info, bool force) {
    if (m_undoHold) { m_undoHold = false; return; }   // the library of an undo is restored after this frame
    if (ImGui::IsAnyItemActive()) return;   // a slider is held or a field is being typed in: one step per edit
    // Comparing the parameter text and the library costs too much to do on every frame; a few times per second
    // catches every change (an undo checks at once).
    const double now = ImGui::GetTime();
    if (!force && m_undoCheckTime >= 0.0 && now - m_undoCheckTime < 0.25) return;
    m_undoCheckTime = now;
    std::string cur = s.ParameterText();
    std::vector<LibrarySnapshotItem> lib = LibrarySnapshot(info);
    if (cur == m_undoBase.params && SameLibrary(lib, m_undoBase.library)) return;
    UndoStep next{ std::move(cur), std::move(lib), std::string() };
    next.label = StepLabel(m_undoBase, next);
    m_undo.push_back(std::move(m_undoBase));
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_undoBase = std::move(next);
}

void MainUI::ApplyUndo(Settings& s, const UiFrameInfo& info, UiEvents& ev, bool redo) {
    TrackUndo(s, info, true);   // a change not yet recorded becomes a step first
    std::vector<UndoStep>& from = redo ? m_redo : m_undo;
    std::vector<UndoStep>& to = redo ? m_undo : m_redo;
    if (from.empty()) return;
    // The recorded current state is moved rather than a fresh snapshot: several steps may be applied in one frame,
    // before the library restore of the first has landed.
    to.push_back(m_undoBase);
    UndoStep step = std::move(from.back());
    from.pop_back();
    const bool nrWas = s.nrEnabled, dlaaWas = s.dlaaEnabled;
    s.ApplyText(step.params);
    s.Clamp();
    if (!SameLibrary(step.library, to.back().library)) { ev.libraryRestore = true; ev.libraryRestoreItems = step.library; m_undoHold = true; }
    m_undoBase = std::move(step);
    ev.settingsChanged = true;
    ev.nrChanged = true;
    ev.dlaaChanged = true;
    if (nrWas != s.nrEnabled || dlaaWas != s.dlaaEnabled) ev.resetHistory = true;
    ImGui::ClearActiveID();
}

std::vector<std::string> MainUI::HistoryLabels(int& current) const {
    std::vector<std::string> out;
    for (const UndoStep& st : m_undo) out.push_back(st.label);
    current = (int)out.size();
    out.push_back(m_undoBase.label);
    for (size_t i = m_redo.size(); i-- > 0;) out.push_back(m_redo[i].label);
    return out;
}

// To an entry of the history list: index 0 is the oldest kept state, m_undo.size() the current one, later ones
// the states undone from.
void MainUI::GoToHistory(Settings& s, const UiFrameInfo& info, UiEvents& ev, int index) {
    const int cur = (int)m_undo.size();
    for (int i = cur; i > index && !m_undo.empty(); --i) ApplyUndo(s, info, ev, false);
    for (int i = cur; i < index && !m_redo.empty(); ++i) ApplyUndo(s, info, ev, true);
}

// A short name for the change from one state to the next: the file added or removed, or the value that changed.
std::string MainUI::StepLabel(const UndoStep& from, const UndoStep& to) {
    auto fileName = [](const std::wstring& path) {
        const size_t k = path.find_last_of(L"\\/");
        return WideToUtf8(k == std::wstring::npos ? path : path.substr(k + 1));
    };
    if (!SameLibrary(from.library, to.library)) {
        std::vector<const LibrarySnapshotItem*> added, removed;
        for (const auto& b : to.library) {
            bool found = false;
            for (const auto& a : from.library) if (a.path == b.path) { found = true; break; }
            if (!found) added.push_back(&b);
        }
        for (const auto& a : from.library) {
            bool found = false;
            for (const auto& b : to.library) if (a.path == b.path) { found = true; break; }
            if (!found) removed.push_back(&a);
        }
        if (added.size() == 1 && removed.empty()) return StrPrintf(TR(HistoryAdded), fileName(added[0]->path).c_str());
        if (removed.size() == 1 && added.empty()) return StrPrintf(TR(HistoryRemoved), fileName(removed[0]->path).c_str());
        if (!added.empty() && added.size() >= removed.size()) return StrPrintf(TR(HistoryAddedMany), (int)added.size());
        if (!removed.empty()) return StrPrintf(TR(HistoryRemovedMany), (int)removed.size());
        for (size_t i = 0; i < to.library.size() && i < from.library.size(); ++i) {
            const LibrarySnapshotItem& a = from.library[i];
            const LibrarySnapshotItem& b = to.library[i];
            if (a.path != b.path) continue;
            if (a.useOwn != b.useOwn || a.own != b.own) return StrPrintf(TR(HistoryOwnParams), fileName(b.path).c_str());
            if (a.transform != b.transform) {
                const SourceTransform& x = a.transform, & y = b.transform;
                const char* what = y.Identity() ? TR(HistoryOriented)
                                 : x.rotate != y.rotate ? TR(HistoryTurned)
                                 : (x.flipH != y.flipH || x.flipV != y.flipV) ? TR(HistoryMirrored) : TR(HistoryCropped);
                return StrPrintf(what, fileName(b.path).c_str());
            }
        }
        return TR(SecLibrary);
    }
    // Values: the "key=value" lines that differ.
    auto lines = [](const std::string& text) {
        std::vector<std::pair<std::string, std::string>> out;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            const std::string line = text.substr(pos, eol - pos);
            pos = eol + 1;
            const size_t eq = line.find('=');
            if (eq != std::string::npos) out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
        }
        return out;
    };
    const auto before = lines(from.params), after = lines(to.params);
    std::string firstKey, firstValue;
    int changed = 0;
    for (const auto& kv : after) {
        std::string old;
        bool had = false;
        for (const auto& b : before) if (b.first == kv.first) { old = b.second; had = true; break; }
        if (had && old == kv.second) continue;
        if (changed++ == 0) { firstKey = kv.first; firstValue = kv.second; }
    }
    if (changed == 0) return "\xE2\x80\xA6";
    // The live picture's orientation, in the words a file's uses.
    if (firstKey == "spoutRotate" || firstKey == "spoutFlipH" || firstKey == "spoutFlipV") {
        auto value = [&](const char* k) { for (const auto& kv : after) if (kv.first == k) return kv.second; return std::string("0"); };
        const bool asItComes = value("spoutRotate") == "0" && value("spoutFlipH") == "0" && value("spoutFlipV") == "0";
        return StrPrintf(asItComes ? TR(HistoryOriented) : firstKey == "spoutRotate" ? TR(HistoryTurned) : TR(HistoryMirrored), TR(TopSpout));
    }
    // The key's name in the interface and a readable value.
    const char* label = nullptr;
    bool isBool = false;
#define VDC_KEY(k, name, flag) if (firstKey == k) { label = TR(name); isBool = flag; }
    VDC_KEY("nrEnabled", NrEnable, true) VDC_KEY("nrCaptureOnly", NrCaptureOnly, true)
    VDC_KEY("nrStyle", Style, false) VDC_KEY("nrIntensity", Intensity, false)
    VDC_KEY("nrGlobalTone", GlobalTone, false) VDC_KEY("nrLocalTone", LocalTone, false) VDC_KEY("nrLocalStructure", LocalStructure, false)
    VDC_KEY("nrSkinStructure", SkinStructure, false) VDC_KEY("nrAutoMask", AutoMask, true) VDC_KEY("nrUiCorrection", UiCorrection, true)
    VDC_KEY("upscaleMode", UpscaleMethod, false) VDC_KEY("nrInputExposure", InputExposure, false) VDC_KEY("nrToneTransfer", ToneTransfer, false)
    VDC_KEY("nrColorStrength", ColorStrength, false) VDC_KEY("nrShadowGain", ShadowGain, false) VDC_KEY("nrHighlightGain", HighlightGain, false)
    VDC_KEY("nrScaleMode", NrScaleMode, false) VDC_KEY("nrInputScale", NrScaleModePercent, false) VDC_KEY("nrMaxLongEdge", NrScaleModeFixed, false) VDC_KEY("hdrPaperWhite", PaperWhite, false) VDC_KEY("hdrHighlightCompression", HighlightCompression, false)
    VDC_KEY("motionMode", MotionSource, false) VDC_KEY("depthMode", DepthSource, false) VDC_KEY("searchRadius", SearchRadius, false)
    VDC_KEY("motionConfidence", MotionConfidence, false) VDC_KEY("nvofGrid", NvofGrid, false) VDC_KEY("nvofPerf", NvofPerf, false)
    VDC_KEY("nvofBidirectional", NvofBidirectional, true) VDC_KEY("flowBidirectional", FlowBidirectional, true)
    VDC_KEY("depthInterval", DepthInterval, false) VDC_KEY("depthLongSide", DepthResolution, false)
    VDC_KEY("autoReset", AutoReset, true) VDC_KEY("cutThreshold", CutThreshold, false) VDC_KEY("dlaaEnabled", DlaaEnable, true)
    VDC_KEY("dlaaPreset", DlaaPreset, false) VDC_KEY("compareMode", Compare, false) VDC_KEY("wipePosition", CompareWipe, false)
    VDC_KEY("checkerboard", Checkerboard, true) VDC_KEY("fitMode", FitWindowLabel, false) VDC_KEY("vsync", Vsync, true)
    VDC_KEY("processRateLimit", RateLimit, false) VDC_KEY("showOverlay", Overlay, true) VDC_KEY("keepAlpha", KeepAlpha, true)
    VDC_KEY("saveOriginal", SaveOriginal, true) VDC_KEY("timelapseSeconds", Timelapse, false) VDC_KEY("videoMatchSource", MatchSource, true)
    VDC_KEY("videoOutput", VideoOutput, false) VDC_KEY("videoBitrateMbps", Bitrate, false) VDC_KEY("videoKeepAudio", KeepAudio, true)
    VDC_KEY("webpQuality", WebpQuality, false)
    VDC_KEY("videoHardwareDecode", HardwareDecode, true) VDC_KEY("customResolution", CustomResolution, true) VDC_KEY("customWidth", Width, false)
    VDC_KEY("customHeight", Height, false) VDC_KEY("keepAspect", KeepAspect, true)
    VDC_KEY("captureName", CaptureName, false) VDC_KEY("outputName", OutputName, false)
#undef VDC_KEY
    std::string value = firstValue;
    if (isBool) value = (value == "1" || value == "true") ? TR(HistoryOn) : TR(HistoryOff);
    else if (firstKey == "nrSkinStructure" && value.compare(0, 2, "-1") == 0) value = TR(HistoryDefault);
    else if (value.find('.') != std::string::npos) {
        // Two decimals, trailing zeros dropped.
        const double d = atof(value.c_str());
        value = StrPrintf("%.2f", d);
        while (value.size() > 1 && value.back() == '0') value.pop_back();
        if (!value.empty() && value.back() == '.') value.pop_back();
    }
    std::string text = std::string(label ? label : firstKey.c_str()) + ": " + value;
    if (changed > 1) text = StrPrintf(TR(HistoryMore), text.c_str(), changed - 1);
    return text;
}

// The history list: every kept state in order, the current one marked, the undone ones dimmed after it. A click
// takes the settings and the library to that state; the later ones stay until a new change is made.
void MainUI::DrawHistory(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& anchor) {
    if (ImGui::IsPopupOpen("##history")) ImGui::SetNextWindowPos(ImVec2(anchor.x, anchor.y + Px(6.0f)), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    if (!BeginPopupFade("##history")) return;
    const Palette& p = Colors();
    ImGui::PushFont(fonts.Bold(), 0.0f);
    ImGui::TextUnformatted(TR(SecHistory));
    ImGui::PopFont();
    ImGui::Separator();
    const int undoN = (int)m_undo.size(), redoN = (int)m_redo.size(), total = undoN + 1 + redoN;
    const float rowH = ImGui::GetFrameHeight();
    const float listH = rowH * (float)std::min(total, 14) + 6.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::BeginChild("##historyList", ImVec2(ImGui::GetFontSize() * 20.0f, listH), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    int jump = -1;
    for (int i = 0; i < total; ++i) {
        const UndoStep& st = i < undoN ? m_undo[(size_t)i] : i == undoN ? m_undoBase : m_redo[(size_t)(redoN - 1 - (i - undoN - 1))];
        const bool current = i == undoN, later = i > undoN;
        ImGui::PushID(i);
        if (later) ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
        const char* label = st.label.empty() ? TR(HistoryInitial) : st.label.c_str();
        if (ImGui::Selectable(label, current, ImGuiSelectableFlags_None, ImVec2(0.0f, rowH)) && !current) jump = i;
        if (later) ImGui::PopStyleColor();
        if (current && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY(0.5f);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    EndPopupFade();
    if (jump >= 0) GoToHistory(s, info, ev, jump);
}

// The update popup: what is new, the notes, and the one button that downloads, swaps the files and restarts.
void MainUI::DrawUpdatePopup(Settings& /*s*/, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    if (m_updateOpen) { ImGui::OpenPopup("##update"); m_updateOpen = false; }
    // Centred on every frame from its own size, so it stays put when the progress bar appears.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!BeginDialog("##update", ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f))) return;
    const Palette& p = Colors();
    const float em = ImGui::GetFontSize();
    const float w = em * 28.0f;
    const int st = info.updateState;
    const bool busy = st == UpDownloading || st == UpExtracting || st == UpRestarting;
    ImGui::PushFont(fonts.Bold(), ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextUnformatted(info.updateEdition ? TR(UpdateEditionTitle) : info.updateDowngrade ? TR(UpdateDowngradeTitle) : TR(UpdateTitle));
    ImGui::PopFont();
    // Every release found is labelled, so a full release reaching the pre-release channel reads as one.
    ImGui::SameLine(0.0f, Px(10.0f));
    if (info.updatePrerelease) Pill(TR(ChannelPreview), WithAlpha(p.warn, 0.2f), p.warn);
    else Pill(TR(ReleaseFull), WithAlpha(p.good, 0.2f), p.good);
    if (info.updateEdition) {
        const char* thisEdition = APP_EDITION_AMD ? TR(EditionAmd) : TR(EditionGeforce);
        const char* otherEdition = APP_EDITION_AMD ? TR(EditionGeforce) : TR(EditionAmd);
        ImGui::TextUnformatted(StrPrintf(TR(UpdateEditionFmt), otherEdition, info.updateVersion.c_str(), thisEdition, info.appVersion.c_str()).c_str());
    } else if (info.updateDowngrade) {
        ImGui::PushTextWrapPos(w);
        ImGui::TextUnformatted(StrPrintf(TR(UpdateDowngradeFmt), info.updateVersion.c_str(), info.appVersion.c_str()).c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextUnformatted(StrPrintf(TR(UpdateVersionFmt), info.updateVersion.c_str(), info.appVersion.c_str()).c_str());
    }
    if (info.prerelease && !info.updatePrerelease && !info.updateEdition && !info.updateDowngrade) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.textDim); ImGui::TextWrapped("%s", TR(UpdateFullNote)); ImGui::PopStyleColor();
    }
    if (!info.updateDate.empty()) ImGui::TextDisabled("%s", StrPrintf(TR(UpdatePublished), info.updateDate.c_str()).c_str());
    if (!info.updateNotes.empty()) {
        ImGui::Spacing();
        const float textH = ImGui::CalcTextSize(info.updateNotes.c_str(), nullptr, false, w - Px(20.0f)).y;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, p.surface);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(10.0f), Px(8.0f)));
        ImGui::BeginChild("##notes", ImVec2(w, std::min(em * 14.0f, textH + Px(20.0f))), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PushTextWrapPos(w - Px(20.0f));
        ImGui::TextUnformatted(info.updateNotes.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }
    ImGui::Spacing();
    if (st == UpDownloading) {
        // A site that does not say the size: the amount so far, over a sweeping line.
        const bool sized = info.updateTotalMb > 0.0;
        ProgressLine("##updateBar", (sized ? StrPrintf(TR(UpdateDownloading), info.updateDownloadedMb, info.updateTotalMb)
                                           : StrPrintf(TR(UpdateDownloadingMb), info.updateDownloadedMb)).c_str(),
                     sized ? info.updateProgress : -1.0f, w);
    } else if (st == UpExtracting) {
        ProgressLine("##updateBar", TR(UpdateExtracting), -1.0f, w);
    } else if (st == UpRestarting) {
        ImGui::TextDisabled("%s", TR(UpdateRestarting));
    } else if (st == UpFailed) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::PushTextWrapPos(w);
        ImGui::TextUnformatted(info.updateWritable ? StrPrintf(TR(UpdateFailed), info.updateError.c_str()).c_str() : TR(UpdateNotWritable));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushTextWrapPos(w);
        Hint(TR(UpdateApplyHint));
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    ImGui::BeginDisabled(busy || !info.updateHasAsset || !info.updateWritable);
    if (ActionButton(info.updateDowngrade ? TR(UpdateDowngradeNow) : TR(UpdateNow), Icon::Download, ImVec2(em * 9.0f, 0.0f), ButtonKind::Accent)) ev.updateStart = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ActionButton(TR(UpdatePage), Icon::OpenExternal, ImVec2(em * 9.0f, 0.0f))) ev.updateOpenPage = true;
    ImGui::SameLine();
    if (st == UpDownloading || st == UpExtracting) {
        if (FlatButton(TR(Cancel), ImVec2(em * 7.0f, 0.0f))) ev.updateCancel = true;
    } else {
        ImGui::BeginDisabled(st == UpRestarting);
        if (FlatButton(TR(UpdateLater), ImVec2(em * 7.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
    }
    EndDialog();
}

namespace {
// "https://hk.gh-proxy.com/" -> "hk.gh-proxy.com": the sites are shown by their host name.
std::string HostOf(const std::string& url) {
    std::string s = url;
    const size_t scheme = s.find("://");
    if (scheme != std::string::npos) s.erase(0, scheme + 3);
    const size_t slash = s.find('/');
    if (slash != std::string::npos) s.erase(slash);
    return s;
}

// One item of the guide's pages: a bold title, a dim paragraph and an accent bar along its left.
void GuideItem(const Fonts& fonts, const char* title, const char* text) {
    const Palette& p = Colors();
    const float em = ImGui::GetFontSize();
    ImGui::Indent(em * 0.9f);
    const ImVec2 c = ImGui::GetCursorScreenPos();
    ImGui::PushFont(fonts.Bold(), 0.0f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    const ImVec2 e = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(c.x - em * 0.6f, c.y + Px(2.0f)), ImVec2(c.x - em * 0.6f + Px(3.0f), e.y - Px(3.0f)), WithAlpha(p.accent, 0.75f), Px(1.5f));
    ImGui::Unindent(em * 0.9f);
    ImGui::Spacing();
}
} // namespace

// How GitHub is reached: directly, through the fastest of the built-in mirror sites, or through a site of the
// user's own. Shared by the About section, the setup guide and the popup that a failed mirror check opens.
void MainUI::MirrorControls(Settings& s, const UiFrameInfo& info, UiEvents& ev, float width, bool why) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    if (why) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(TR(MirrorWhy));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    const char* modes[] = { TR(MirrorDirect), TR(MirrorAuto), TR(MirrorCustom) };
    if (SegmentsFit(modes, 3, nullptr, width)) {
        // One switch across the width when the three names fit it, one choice per line when they do not.
        int mode = std::clamp(s.githubMirror, 0, 2);
        if (Segmented("##mirrorMode", modes, 3, &mode, width) && mode != s.githubMirror) { s.githubMirror = mode; ev.settingsChanged = true; }
    } else {
        for (int i = 0; i < 3; ++i) {
            ImGui::PushID(i);
            if (Radio(modes[i], s.githubMirror == i) && s.githubMirror != i) { s.githubMirror = i; ev.settingsChanged = true; }
            ImGui::PopID();
        }
    }
    // What follows the mode choice changes with it (the site box, the test button, the measured list, the site in
    // use), so it fades in, drifting up a little, like a page of the guide: at its first appearance, on a change of
    // mode, when a test starts and when its list arrives.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int vtx0 = dl->VtxBuffer.Size;
    const int state = s.githubMirror * 8 + (info.mirrorProbing ? 1 : 0) + (info.mirrors.empty() ? 0 : 2) + (info.mirrorInUse.empty() ? 0 : 4);
    const double now = ImGui::GetTime();
    if (state != m_mirrorBlockState) { m_mirrorBlockState = state; m_mirrorBlockTime = now; }
    if (s.githubMirror == 2) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", s.githubMirrorCustom.c_str());
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputTextWithHint("##mirrorCustom", "https://", buf, sizeof(buf))) { s.githubMirrorCustom = buf; ev.settingsChanged = true; }
        FocusRing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        Hint(TR(MirrorCustomHint));
        ImGui::PopTextWrapPos();
    }
    if (s.githubMirror == 1) {
        ImGui::BeginDisabled(info.mirrorProbing || info.updaterBusy);
        if (ActionButton(TR(MirrorTest), Icon::Gauge, ImVec2(width, 0.0f))) ev.mirrorProbe = true;
        ImGui::EndDisabled();
        if (info.mirrorProbing) {
            ImGui::TextDisabled("%s", TR(MirrorTesting));
        } else if (info.mirrors.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            Hint(TR(MirrorNotMeasured));
            ImGui::PopTextWrapPos();
        } else {
            for (size_t i = 0; i < info.mirrors.size(); ++i) {
                const UiFrameInfo::MirrorRow& m = info.mirrors[i];
                const std::string host = HostOf(m.url);
                if (m.ok) StatusDot(i == 0 ? p.good : p.textDim, StrPrintf("%s  %.2f %s", host.c_str(), m.seconds, TR(Seconds)).c_str());
                else StatusDot(p.bad, StrPrintf("%s  %s", host.c_str(), m.error.rfind("HTTP ", 0) == 0 ? m.error.c_str() : TR(MirrorNoAnswer)).c_str());   // "HTTP 404" says more than "no answer"
                if (i == 0 && m.ok) PillAfter(TR(MirrorFastest), WithAlpha(p.good, 0.18f), p.good, style.ItemInnerSpacing.x);
            }
        }
    }
    if (s.githubMirror != 0 && !info.mirrorInUse.empty()) ImGui::TextDisabled("%s", StrPrintf(TR(MirrorInUse), HostOf(info.mirrorInUse).c_str()).c_str());
    {
        const float ft = (float)std::min(1.0, (now - m_mirrorBlockTime) / 0.28);
        if (ft < 1.0f) FadeDrawn(dl, vtx0, Ease(ft), (1.0f - Ease(ft)) * ImGui::GetFontSize() * 0.4f);
    }
}

// No mirror site answered the update check: the user picks a site of their own, gives the sites up, or waits.
void MainUI::DrawMirrorPopup(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    if (m_mirrorOpen) { ImGui::OpenPopup("##mirror"); m_mirrorOpen = false; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!BeginDialog("##mirror", ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f))) return;
    const Palette& p = Colors();
    const float em = ImGui::GetFontSize();
    const float w = em * 28.0f;
    ImGui::Dummy(ImVec2(w, 0.0f));   // fixes the width from the first frame, so the paragraphs wrap at it
    ImGui::PushFont(fonts.Bold(), ImGui::GetStyle().FontSizeBase * 1.15f);
    ImGui::TextUnformatted(TR(MirrorFailTitle));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(w);
    ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
    if (s.githubMirror == 2) ImGui::TextUnformatted(StrPrintf(TR(MirrorFailCustom), info.updateError.c_str()).c_str());
    else ImGui::TextUnformatted(TR(MirrorFailText));
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    MirrorControls(s, info, ev, w, false);
    ImGui::Separator();
    if (AccentButton(TR(MirrorCheckAgain), ImVec2(em * 9.0f, 0.0f))) { ev.updateCheckNow = true; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (FlatButton(TR(UpdateLater), ImVec2(em * 7.0f, 0.0f))) ImGui::CloseCurrentPopup();
    EndDialog();
}

// A pulsing accent ring around the last item while the setup guide's pointers are lit (Spotlight is called after
// the Setup guide and Documentation buttons and the top bar's ? button).
void MainUI::Spotlight(bool foreground) {
    const double now = ImGui::GetTime();
    if (m_spotUntil < 0.0 || now < m_spotAt || now >= m_spotUntil) return;
    // One flash: the ring comes up quickly, then fades while it widens a little.
    const float p = (float)((now - m_spotAt) / (m_spotUntil - m_spotAt));
    const float a = p < 0.15f ? p / 0.15f : 1.0f - Ease((p - 0.15f) / 0.85f);
    const float grow = Px(3.0f) * (1.0f + p);
    const ImVec2 a0 = ImGui::GetItemRectMin(), a1 = ImGui::GetItemRectMax();
    ImDrawList* dl = foreground ? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
    dl->AddRect(ImVec2(a0.x - grow, a0.y - grow), ImVec2(a1.x + grow, a1.y + grow), WithAlpha(Colors().accent, a), ImGui::GetStyle().FrameRounding + grow, std::max(1.0f, Px(2.0f)));
}

void MainUI::CloseGuide(Settings& s, UiEvents& ev, bool point) {
    s.setupGuideSeen = 1;
    ev.settingsChanged = true;
    ev.guideClosed = true;
    m_guideShowing = false;
    if (m_updateDeferred) { m_updateDeferred = false; m_updateOpen = true; }
    if (!point) return;
    // The places the last page names light up: the sidebar shows, the About section opens and the sidebar glides
    // to it, and a ring flashes once around the Setup guide and Documentation buttons and the ? button in the top bar.
    s.sidebarVisible = true;
    m_searchBuf[0] = 0;
    m_openAbout = true;
    m_scrollToAbout = ImGui::GetTime() + 0.8;
    m_spotAt = ImGui::GetTime() + 0.9;   // once the sidebar has glided to the About section
    m_spotUntil = m_spotAt + 1.4;
}

// The setup guide: shown once at the first start (also the first start after the update that brought it), and
// again from the About section. Language and theme, the GitHub access, what the program does, what the settings
// do, and where to find things afterwards.
void MainUI::DrawSetupGuide(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const double now = ImGui::GetTime();
    if (!m_guideAutoDone && info.windowShown && m_startFade >= 0.0 && now - m_startFade > 0.7) {
        m_guideAutoDone = true;
        if (!s.setupGuideSeen) m_guideOpen = true;
    }
    if (m_guideOpen) { ImGui::OpenPopup("##guide"); m_guideOpen = false; m_guidePage = 0; m_guidePageTime = now; }
    // Closed by a click outside or Escape: counts as seen, without the pointers.
    if (m_guideShowing && !ImGui::IsPopupOpen("##guide")) CloseGuide(s, ev, false);
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!BeginDialog("##guide", ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f))) return;
    m_guideShowing = true;
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float em = ImGui::GetFontSize();
    const float w = std::max(em * 20.0f, std::min(em * 36.0f, vp->WorkSize.x - em * 4.0f));
    const float pageH = std::max(em * 12.0f, std::min(em * 28.0f, vp->WorkSize.y - em * 11.0f));   // the longest page (the settings) whole in all four languages
    constexpr int kPages = 5;

    ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.15f);
    ImGui::TextUnformatted(TR(GuideTitle));
    ImGui::PopFont();
    {
        const std::string step = StrPrintf(TR(GuideStepFmt), m_guidePage + 1, kPages);
        ImGui::SameLine(w - ImGui::CalcTextSize(step.c_str()).x);
        ImGui::TextDisabled("%s", step.c_str());
    }
    ImGui::Spacing();

    // The page, in a child of fixed height so the window keeps its size; the content fades in on a page change.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##guidePage", ImVec2(w, pageH), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGui::PopStyleColor();
    const int vtx0 = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    auto title = [&](const char* text) {
        ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.3f);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
        ImGui::Spacing();
    };
    const float inner = ImGui::GetContentRegionAvail().x;
    switch (m_guidePage) {
    case 0: {
        title(TR(GuideWelcome));
        Hint(TR(GuideWelcomeText));
        ImGui::Spacing(); ImGui::Spacing();
        const char* items[] = { TR(LangAuto), "English", "简体中文", "日本語", "한국어" };
        ImGui::SetNextItemWidth(inner * 0.55f);
        if (ComboIds("##guideLang", &s.language, items, 5)) { ev.languageChanged = true; ev.settingsChanged = true; }
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR(Language));
        Hint(TR(GuideLanguageHint));
        ImGui::Spacing();
        const char* themes[] = { TR(ThemeSystem), TR(ThemeDark), TR(ThemeLight) };
        const Icon themeIcons[] = { Icon::Monitor, Icon::Moon, Icon::Sun };
        if (SegmentsFit(themes, 3, nullptr, inner * 0.55f)) {
            const bool withIcons = SegmentsFit(themes, 3, themeIcons, inner * 0.55f);
            if (Segmented("##guideTheme", themes, 3, &s.theme, inner * 0.55f, withIcons ? themeIcons : nullptr)) ev.settingsChanged = true;
        } else {
            ImGui::SetNextItemWidth(inner * 0.55f);
            if (ComboIds("##guideTheme", &s.theme, themes, 3)) ev.settingsChanged = true;
        }
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR(Theme));
        Hint(TR(GuideThemeHint));
        break;
    }
    case 1:
        title(TR(GithubAccess));
        MirrorControls(s, info, ev, inner, true);
        break;
    case 2:
        title(TR(GuidePageHow));
        GuideItem(fonts, TR(GuideHowLiveT), TR(GuideHowLive));
        GuideItem(fonts, TR(GuideHowFilesT), TR(GuideHowFiles));
        GuideItem(fonts, TR(GuideHowLayoutT), TR(GuideHowLayout));
        break;
    case 3:
        title(TR(GuidePageSettings));
        GuideItem(fonts, TR(SecNeural), TR(GuideSetNeural));
        GuideItem(fonts, TR(GuideSetDetailT), TR(GuideSetDetail));
        GuideItem(fonts, TR(GuideSetBlendT), TR(GuideSetBlend));
        GuideItem(fonts, TR(GuideSetGuidanceT), TR(GuideSetGuidance));
        ImGui::PushStyleColor(ImGuiCol_Text, p.accentHover);
        ImGui::TextWrapped("%s", TR(GuideSetTip));
        ImGui::PopStyleColor();
        break;
    default:
        title(TR(GuidePageWhere));
        GuideItem(fonts, TR(GuideWhereGuideT), TR(GuideWhereGuide));
        GuideItem(fonts, TR(GuideWhereDocsT), TR(GuideWhereDocs));
        GuideItem(fonts, TR(GuideWhereSearchT), TR(GuideWhereSearch));
        ImGui::TextDisabled("%s", TR(GuideWhereLight));
        ImGui::Spacing(); ImGui::Spacing();
        ImGui::TextWrapped("%s", TR(GuideStar));
        ImGui::Spacing();
        if (AccentButton(TR(GuideStarButton), ImVec2(em * 10.0f, 0.0f))) ev.openProjectPage = true;
        ImGui::SameLine();
        if (GhostButton(TR(GuideBoothButton), ImVec2(em * 10.0f, 0.0f))) ev.openBooth = true;
        break;
    }
    {
        const float ft = m_guidePageTime >= 0.0 ? (float)std::min(1.0, (now - m_guidePageTime) / 0.28) : 1.0f;
        if (ft < 1.0f) FadeDrawn(ImGui::GetWindowDrawList(), vtx0, Ease(ft), (1.0f - Ease(ft)) * em * 0.4f);
    }
    ScrollEdgeFade(ImGui::GetColorU32(ImGuiCol_PopupBg), em * 2.5f);   // a small window: the page scrolls
    ImGui::EndChild();

    // Step dots, then Skip at the left and Back / Next (Finish) at the right.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c0 = ImGui::GetCursorScreenPos();
        const float gap = Px(14.0f);
        float x = c0.x + (w - gap * kPages) * 0.5f + gap * 0.5f;
        const float y = c0.y + em * 0.55f;
        for (int i = 0; i < kPages; ++i) {
            const float t = Animate(ImGui::GetID(StrPrintf("##guideDot%d", i).c_str()), i == m_guidePage ? 1.0f : 0.0f, 14.0f);
            dl->AddCircleFilled(ImVec2(x, y), Dpi() * (3.0f + 1.2f * t), Mix(WithAlpha(p.textDim, 0.4f), p.accent, t));
            x += gap;
        }
        ImGui::Dummy(ImVec2(w, em * 1.1f));
    }
    ImGui::Separator();
    const bool last = m_guidePage == kPages - 1;
    const float bw = em * 7.0f;
    if (!last) { if (FlatButton(TR(GuideSkip), ImVec2(bw, 0.0f))) { CloseGuide(s, ev, true); ImGui::CloseCurrentPopup(); } }
    else ImGui::Dummy(ImVec2(bw, ImGui::GetFrameHeight()));
    ImGui::SameLine(w - bw * 2.0f - style.ItemSpacing.x);
    ImGui::BeginDisabled(m_guidePage == 0);
    if (GhostButton(TR(GuideBack), ImVec2(bw, 0.0f))) { --m_guidePage; m_guidePageTime = now; }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (AccentButton(last ? TR(GuideFinish) : TR(GuideNext), ImVec2(bw, 0.0f))) {
        if (last) { CloseGuide(s, ev, true); ImGui::CloseCurrentPopup(); }
        else { ++m_guidePage; m_guidePageTime = now; }
    }
    EndDialog();
}

void MainUI::ResetView(bool animate) {
    m_zoomTarget = 1.0f;
    m_panHome = animate;
    if (!animate) { m_zoom = 1.0f; m_pan = ImVec2(0, 0); }
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawTopBar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    const float frameH = ImGui::GetFrameHeight();
    const float rowH = frameH * 1.35f;
    const ImVec2 pad(Px(12.0f), Px(7.0f));
    const float barH = rowH + pad.y * 2.0f;
    // One card with every element centred on its middle line. The bar never scrolls: its parts are measured first
    // and the optional ones (the wide language box, undo and redo, the status badges, the name, the mark) are
    // dropped when the window is too narrow for all of them.
    const ImVec2 card0 = ImGui::GetCursorScreenPos();
    DrawCard(ImGui::GetWindowDrawList(), card0, ImVec2(card0.x + ImGui::GetContentRegionAvail().x, card0.y + barH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
    ImGui::BeginChild("##top", ImVec2(0, barH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::SetScrollX(0.0f);
    ImGui::SetScrollY(0.0f);
    const float top = pad.y;
    auto centred = [&](float itemH) { return top + (rowH - itemH) * 0.5f; };
    // Text items add the line's baseline offset themselves (a badge before them leaves one): taken off here.
    auto centredText = [&](float itemH) { return centred(itemH) - ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset; };
    const bool imageMode = s.sourceMode == SourceImage;
    const bool videoMode = s.sourceMode == SourceVideo;
    const bool busy = info.videoProcessing || info.batchRunning;   // the main button cancels the run

    // Left part: the program's mark and name, the source switch.
    const float logoS = std::round(frameH * 0.95f);
    const float titleSizeBase = style.FontSizeBase * 1.12f;
    ImGui::PushFont(fonts.Bold(), titleSizeBase);
    const ImVec2 titleSize = ImGui::CalcTextSize(TR(AppTitle));
    ImGui::PopFont();
    const char* modes[] = { TR(TopSpout), TR(TopImage), TR(TopVideo) };
    const Icon modeIcons[] = { Icon::Broadcast, Icon::Image, Icon::Film };
    const float switchW = SegmentedWidth(modes, 3, modeIcons);

    // Status badge.
    const bool haveSenders = info.senders && !info.senders->empty();
    const std::string batchBadge = !info.mcpJob.empty() ? std::string(TR(McpJobBadge))
                                 : info.batchRunning ? StrPrintf(TR(BatchRunning), std::min(info.batchIndex + 1, info.batchCount), info.batchCount) : std::string();
    // A loaded picture or video has no badge: the switch shows the mode and the status bar names the file.
    const char* badge = info.batchRunning ? batchBadge.c_str()
                      : videoMode ? (info.videoProcessing ? TR(VideoProcessing) : info.videoLoaded ? nullptr : TR(NoVideo))
                      : imageMode ? (info.imageLoaded ? nullptr : TR(NoImage))
                      : info.sourceConnected ? TR(StatusConnected) : haveSenders ? TR(StatusWaiting) : TR(StatusNoSpout);
    const ImU32 badgeFg = info.batchRunning ? p.accentHover
                        : videoMode ? (info.videoProcessing ? p.warn : info.videoLoaded ? p.good : p.muted)
                        : imageMode ? (info.imageLoaded ? p.good : p.muted)
                        : info.sourceConnected ? p.good : haveSenders ? p.warn : p.muted;
    const ImU32 badgeBg = WithAlpha(badgeFg, badgeFg == p.muted ? 0.2f : 0.16f);
    const bool nrBadge = info.status && info.status->nrActive;
    const float textH = ImGui::GetTextLineHeight();
    const float pillPad = Px(9.0f) * 2.0f;   // Pill()'s padding on both sides
    const float pillH = textH + Px(3.0f) * 2.0f;
    const float badgesW = (badge ? ImGui::CalcTextSize(badge).x + pillPad : 0.0f)
                        + (nrBadge ? ImGui::CalcTextSize("DLSS 5").x + pillPad + (badge ? Px(6.0f) : 0.0f) : 0.0f);

    // Right part: undo, redo and the history, the documentation, the language, the main action.
    const char* actionText = busy ? TR(Cancel) : videoMode ? TR(ProcessVideo) : imageMode ? TR(ProcessAndSave) : TR(Capture);
    const Icon actionIcon = busy ? Icon::Stop : videoMode ? Icon::Film : imageMode ? Icon::Sparkle : Icon::Camera;
    const float actionW = IconTextButtonWidth(actionText) + Px(10.0f);

    const float availW = ImGui::GetWindowWidth() - pad.x * 2.0f;
    const float gap = Px(16.0f);
    // The language box fits the chosen name after its icon; the icon alone when the bar is narrow.
    const char* langItems[] = { TR(LangAuto), "English", "简体中文", "日本語", "한국어" };
    const int langSel = (s.language >= 0 && s.language < 5) ? s.language : 0;
    const float langIconW = IconSize() + style.FramePadding.x * 2.0f + frameH;
    float langW = std::max(ImGui::GetFontSize() * 6.0f, langIconW + style.ItemInnerSpacing.x + ImGui::CalcTextSize(langItems[langSel]).x);
    bool langCompact = false;
    const float undoW = frameH * 3.0f + style.ItemInnerSpacing.x * 2.0f;   // undo, redo, history
    bool showBadges = true, showTitle = true, showUndo = true, showLogo = true;
    auto rightW = [&]() { return actionW + langW + style.ItemSpacing.x + frameH + style.ItemSpacing.x + (showUndo ? undoW + style.ItemSpacing.x : 0.0f); };
    auto leftW = [&]() {
        return (showLogo ? logoS + (showTitle ? Px(10.0f) + titleSize.x + Px(20.0f) : Px(14.0f)) : 0.0f) + switchW
             + (showBadges && badgesW > 0.0f ? Px(12.0f) + badgesW : 0.0f);
    };
    if (leftW() + gap + rightW() > availW) { langW = langIconW; langCompact = true; }
    if (leftW() + gap + rightW() > availW) showUndo = false;
    if (leftW() + gap + rightW() > availW) showBadges = false;
    if (leftW() + gap + rightW() > availW) showTitle = false;
    if (leftW() + gap + rightW() > availW) showLogo = false;

    if (showLogo) {
        ImGui::SetCursorPosY(centred(logoS));
        DrawLogo(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), logoS);
        ImGui::Dummy(ImVec2(logoS, logoS));
        if (showTitle) {
            ImGui::SameLine(0.0f, Px(10.0f));
            ImGui::SetCursorPosY(centredText(titleSize.y));
            ImGui::PushFont(fonts.Bold(), titleSizeBase);
            ImGui::TextUnformatted(TR(AppTitle));
            ImGui::PopFont();
            ImGui::SameLine(0.0f, Px(20.0f));
        } else {
            ImGui::SameLine(0.0f, Px(14.0f));
        }
    }
    ImGui::SetCursorPosY(centred(frameH));
    ImGui::BeginDisabled(busy);
    {
        // The switch shows the mode that is coming; it is applied at the bottom of the fade (DrawFades).
        int mode = m_modePending >= 0 ? m_modePending : s.sourceMode;
        if (Segmented("##source", modes, 3, &mode, 0.0f, modeIcons) && mode != s.sourceMode && m_modePending < 0) {
            m_modePending = mode;
            m_modeFadeStart = ImGui::GetTime();
        }
    }
    ImGui::EndDisabled();
    Tip(TR(SourceModeHint));
    if (showBadges && badgesW > 0.0f) {
        ImGui::SameLine(0.0f, Px(12.0f));
        if (badge) {
            ImGui::SetCursorPosY(centred(pillH));
            Pill(badge, badgeBg, badgeFg);
        }
        if (nrBadge) {
            if (badge) ImGui::SameLine(0.0f, Px(6.0f));
            ImGui::SetCursorPosY(centred(pillH));
            Pill("DLSS 5", WithAlpha(p.accent, 0.18f), Mix(p.accentHover, p.accent, p.light));
        }
    }
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

    ImGui::SameLine(std::max(leftEnd + gap, ImGui::GetWindowWidth() - pad.x - rightW()));
    if (showUndo) {
        ImGui::SetCursorPosY(centred(frameH));
        ImGui::BeginDisabled(m_undo.empty());
        if (IconButton("##undo", Icon::Undo, ImVec2(frameH, frameH), TR(TipUndo), ButtonKind::Plain)) ApplyUndo(s, info, ev, false);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::SetCursorPosY(centred(frameH));
        ImGui::BeginDisabled(m_redo.empty());
        if (IconButton("##redo", Icon::Redo, ImVec2(frameH, frameH), TR(TipRedo), ButtonKind::Plain)) ApplyUndo(s, info, ev, true);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::SetCursorPosY(centred(frameH));
        if (IconButton("##historyBtn", Icon::History, ImVec2(frameH, frameH), TR(TipHistory), ButtonKind::Plain)) ImGui::OpenPopup("##history");
        DrawHistory(s, info, ev, fonts, ImGui::GetItemRectMax());
        ImGui::SameLine();
    }
    ImGui::SetCursorPosY(centred(frameH));
    if (IconButton("##docs", Icon::Help, ImVec2(frameH, frameH), TR(TipDocs), ButtonKind::Plain)) ev.openDocs = true;
    Spotlight(true);
    ImGui::SameLine();
    ImGui::SetCursorPosY(centred(frameH));
    ImGui::SetNextItemWidth(langW);
    if (BeginDropdown("##lang", langCompact ? nullptr : langItems[langSel], Icon::Languages)) {
        for (int i = 0; i < 5; ++i) {
            const bool selected = langSel == i;
            if (ImGui::Selectable(langItems[i], selected) && s.language != i) { s.language = i; ev.languageChanged = true; ev.settingsChanged = true; }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        EndDropdown();
    }
    Tooltip(TR(Language));
    ImGui::SameLine();
    ImGui::SetCursorPosY(centred(frameH));
    if (IconTextButton(actionText, actionIcon, ImVec2(actionW, 0), ButtonKind::Accent)) {
        if (info.batchRunning) ev.batchCancel = true;
        else if (info.videoProcessing) ev.cancelVideo = true;
        else ev.captureNow = true;
    }
    if (!busy) Tooltip(StrPrintf("%s (%s)", videoMode ? TR(VideoHint) : imageMode ? TR(ImageHint) : TR(CaptureHint), info.hotkeyText.c_str()).c_str());
    ImGui::EndChild();
}

// ------------------------------------------------------------------------------------------

// The lock banner's layout in a banner `width` wide: the text and the Cancel button side by side while both fit, else
// the button under the text, at the right, so neither a narrow sidebar nor a longer language cuts the text; a text too
// long for a row of its own wraps.
MainUI::LockLayout MainUI::LayOutLockBanner(const std::string& text, bool cancellable, float width) {
    LockLayout l;
    const float frameH = ImGui::GetFrameHeight();
    const float inner = Px(5.0f);
    const float textW = ImGui::CalcTextSize(text.c_str()).x;
    const float rowW = width - (Px(12.0f) + IconSize() + Px(8.0f)) - inner;   // the text's room in a row of its own, after the lock icon
    l.cancelW = cancellable ? IconTextButtonWidth(TR(Cancel)) : 0.0f;
    l.buttonBelow = cancellable && textW > rowW - l.cancelW - inner;
    if (textW > rowW) l.wrapW = std::max(1.0f, rowW);
    l.textH = l.wrapW > 0.0f ? ImGui::CalcTextSize(text.c_str(), nullptr, false, l.wrapW).y - ImGui::GetTextLineHeight() + frameH : frameH;
    l.height = inner * 2.0f + l.textH + (l.buttonBelow ? inner + frameH : 0.0f);
    return l;
}

// The banner over the locked settings: what is running, and the button that stops it. It has a strip of its own at the
// top of the sidebar column (`space` high, the settings below it) and slides down from the strip's top edge as it
// fades in (t). `height` is the banner's, gliding towards the layout's when the button's room opens or closes.
void MainUI::DrawLockBanner(const UiFrameInfo& info, UiEvents& ev, const LockLayout& lock, float t, float space, float height, float width, float barW) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(6.0f), 0.0f));
    ImGui::BeginChild("##sidebarLock", ImVec2(width, space), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int vtx0 = dl->VtxBuffer.Size;
    const float frameH = ImGui::GetFrameHeight();
    const float inner = Px(5.0f);
    const float rowH = frameH + inner * 2.0f;   // the first row: the lock icon, the text's first line and a button beside it
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const ImVec2 b0(top.x, top.y + space - (height + style.ItemSpacing.y));
    const ImVec2 b1(b0.x + ImGui::GetContentRegionAvail().x - barW, b0.y + height);
    dl->AddRectFilled(b0, b1, WithAlpha(p.warn, 0.13f * t), CardRounding());
    dl->AddRect(b0, b1, WithAlpha(p.warn, 0.35f * t), CardRounding(), 1.0f);
    const float iconS = IconSize();
    DrawIcon(dl, Icon::Lock, ImVec2(b0.x + Px(12.0f) + iconS * 0.5f, b0.y + rowH * 0.5f), iconS, WithAlpha(p.warn, t));
    const bool cancellable = info.batchRunning || info.videoProcessing;
    const bool beside = cancellable && !lock.buttonBelow;
    const float textX = b0.x + Px(12.0f) + iconS + Px(8.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(p.warn, t));
    ImGui::PushClipRect(b0, ImVec2(b1.x - (beside ? lock.cancelW + inner * 2.0f : inner), b1.y), true);
    ImGui::SetCursorScreenPos(ImVec2(textX, b0.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
    if (lock.wrapW > 0.0f) ImGui::PushTextWrapPos(textX + lock.wrapW - ImGui::GetWindowPos().x);
    ImGui::TextUnformatted(m_lockText.c_str());
    if (lock.wrapW > 0.0f) ImGui::PopTextWrapPos();
    ImGui::PopClipRect();
    ImGui::PopStyleColor();
    if (cancellable) {
        if (beside) {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(b1.x - lock.cancelW - inner, b0.y + inner));
        } else {   // under the text; the banner clips it while its room opens
            ImGui::PushClipRect(b0, b1, true);
            ImGui::SetCursorScreenPos(ImVec2(b1.x - lock.cancelW - inner, b0.y + inner + lock.textH + inner));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * t);
        if (IconTextButton(TR(Cancel), Icon::Stop, ImVec2(lock.cancelW, frameH), ButtonKind::Accent)) { if (info.batchRunning) ev.batchCancel = true; else ev.cancelVideo = true; }
        ImGui::PopStyleVar();
        if (!beside) ImGui::PopClipRect();
    }
    ModeFadeContent(vtx0);
    ImGui::EndChild();
}

// The search field, and the Advanced switch at the end of its row (on a row of its own when the sidebar is too narrow
// for both), drawn above the scrolled settings so a scroll can neither cut nor carry them away: while the field holds
// text only the matching controls show, in their sections (Theme's Search*); the switch decides how much of every
// section is shown. rowW ends where the cards do; the result is the row's bottom edge.
float MainUI::DrawSidebarSearch(Settings& s, UiEvents& ev, float rowW) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID("sidebarSearch");
    const float frameH = ImGui::GetFrameHeight();
    const float iconS = IconSize();
    const float iconW = iconS + style.ItemInnerSpacing.x;
    const bool hasQuery = m_searchBuf[0] != 0;
    const float clearW = hasQuery ? frameH : 0.0f;
    const float toggleW = std::round(std::round(frameH * 0.72f) * 1.8f) + style.ItemInnerSpacing.x + ImGui::CalcTextSize(TR(Advanced)).x;
    const float fieldMin = ImGui::GetFontSize() * 9.0f;
    const bool sameRow = rowW - toggleW - style.ItemSpacing.x * 1.5f >= fieldMin;
    const float fieldW = sameRow ? rowW - toggleW - style.ItemSpacing.x * 1.5f : rowW;
    const ImVec2 f0 = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemWidth(fieldW);
    if (hasQuery) ImGui::SetNextItemAllowOverlap();   // the clear mark inside it takes the mouse
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x + iconW, style.FramePadding.y));
    ImGui::InputTextWithHint("##search", TR(SearchHint), m_searchBuf, sizeof(m_searchBuf));
    ImGui::PopStyleVar();
    FocusRing();
    if (ImGui::IsItemDeactivated() && ImGui::IsKeyDown(ImGuiKey_Escape)) m_searchBuf[0] = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawIcon(dl, Icon::Search, ImVec2(f0.x + style.FramePadding.x + iconS * 0.5f, f0.y + frameH * 0.5f), iconS, p.textDim);
    if (hasQuery) {
        // The clear mark sits inside the field, at its right end.
        const ImVec2 keep = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(f0.x + fieldW - clearW, f0.y));
        if (IconButton("##searchClear", Icon::Close, ImVec2(clearW, frameH), TR(SearchClear), ButtonKind::Plain)) m_searchBuf[0] = 0;
        ImGui::SetCursorScreenPos(keep);
    }
    // The switch at the end of the field's row, or under the field. Placed by hand: the row is drawn in the host
    // window, on the line the preview column started, so the line break after the field lands below the body.
    ImGui::SetCursorScreenPos(sameRow ? ImVec2(f0.x + rowW - toggleW, f0.y) : ImVec2(f0.x, std::floor(f0.y + frameH + style.ItemSpacing.y)));
    if (Toggle(TR(Advanced), &s.showAdvanced)) ev.settingsChanged = true;
    const float bottom = std::max(f0.y + frameH, ImGui::GetItemRectMax().y);
    Tip(TR(TipAdvanced));
    ImGui::PopID();
    return bottom;
}

void MainUI::DrawSidebar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const Palette& p = Colors();
    const bool locked = SettingsLocked(info);   // the banner over the column says so (DrawLockBanner)
    ImGui::Dummy(ImVec2(0.0f, Px(2.0f)));   // under the search row (DrawSidebarSearch)
    SearchBegin(m_searchBuf);
    m_adv = Searching() ? 1.0f : m_advShown;   // a search looks through the expert controls too
    ImGui::BeginDisabled(locked);
    ImGui::PushItemWidth(-LabelColumn(ImGui::GetContentRegionAvail().x));
    if (SectionHeader(TR(SecSource), "source", true, Icon::Import)) { BlockSource(s, info, ev); SectionEnd(); }
    if (SectionHeader(TR(SecNeural), "neural", true, Icon::Sparkle)) { BlockNeural(s, info, ev); SectionEnd(); }
    if (SectionHeader(TR(SecCapture), "save", true, Icon::Save)) { BlockSave(s, info, ev); SectionEnd(); }
    // The expert sections open and close with the Advanced switch; Display sits under DLAA, before the internals.
    if (RevealBegin("##advSections", m_adv)) {
        if (SectionHeader(TR(SecGuidance), "guidance", false, Icon::Layers)) { BlockGuidance(s, info, ev); SectionEnd(); }
#if !APP_EDITION_AMD   // DLAA is NVIDIA-only
        if (SectionHeader(TR(SecDlaa), "dlaa", false, Icon::Grid)) { BlockDlaa(s, info, ev); SectionEnd(); }
#endif
        RevealEnd();
    }
    if (SectionHeader(TR(SecDisplay), "view", false, Icon::Monitor)) { BlockView(s, info, ev); SectionEnd(); }
    if (SectionHeader(TR(SecMcp), "mcp", false, Icon::Plug)) { BlockMcp(s, info, ev); SectionEnd(); }
    if (RevealBegin("##advInternals", m_adv)) {
        if (SectionHeader(TR(SecInternals), "internals", false, Icon::Cpu)) { BlockInternals(s, info, ev); SectionEnd(); }
        RevealEnd();
    }
    m_aboutY = ImGui::GetCursorPosY() - ImGui::GetCurrentWindow()->WindowPadding.y;   // where the sidebar glides to after the setup guide
    if (SectionHeader(TR(SecAbout), "about", false, Icon::Info)) { BlockAbout(s, info, ev, fonts); SectionEnd(); }
    const bool nothingFound = Searching() && SearchHits() == 0;
    SearchEnd();
    if (nothingFound) {
        // Nothing matched: a quiet card-less note in the middle of the column.
        ImGui::Dummy(ImVec2(0.0f, Px(12.0f)));
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 c(ImGui::GetCursorScreenPos().x + w * 0.5f, ImGui::GetCursorScreenPos().y + IconSize(1.4f) * 0.5f);
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Search, c, IconSize(1.4f), WithAlpha(p.textDim, 0.7f));
        ImGui::Dummy(ImVec2(0.0f, IconSize(1.4f) + Px(4.0f)));
        const ImVec2 ts = ImGui::CalcTextSize(TR(SearchNoResults), nullptr, false, w);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (w - ts.x) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(w, ts.x + 1.0f));
        ImGui::TextDisabled("%s", TR(SearchNoResults));
        ImGui::PopTextWrapPos();
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, Px(10.0f)));   // the last card's shadow, when scrolled to the end
}

// How long the current file would take with the settings as they are, from the measured cost of a frame.
std::string MainUI::EstimateText(const Settings& s, const UiFrameInfo& info) const {
    if (info.costSecPerFrame <= 0.0 || info.costPixels <= 0.0) return TR(EstimateNone);
    auto frameCost = [&](double pixels) { return info.costSecPerFrame * std::max(pixels, 1.0) / info.costPixels; };
    double seconds = -1.0;
    if (s.sourceMode == SourceVideo && info.videoLoaded) {
        const double in = std::max(0.0, info.videoIn);
        const double out = info.videoOut > in ? info.videoOut : info.videoDurationSeconds;
        const double frames = std::max(0.0, out - in) * (info.videoFps > 0.0 ? info.videoFps : 30.0);
        seconds = frames * frameCost((double)info.videoWidth * info.videoHeight) * 1.05 + 0.5;
    } else if (s.sourceMode == SourceImage && info.imageLoaded) {
        const double pixels = (double)info.imageWidth * info.imageHeight;
        seconds = 32.0 * frameCost(pixels) + pixels / 1e6 * info.pngSecPerMegapixel + 0.2;
    }
    if (seconds < 0.0) return TR(EstimateNone);
    return "\xE2\x89\x88 " + FormatEstimate(seconds);
}

// Seconds left in the running batch: the rest of the current file at its measured rate plus the queued files at
// the estimated cost of a frame.
double MainUI::BatchRemaining(const UiFrameInfo& info) const {
    if (!info.batchRunning || !info.library) return -1.0;
    double total = 0.0;
    bool known = false;
    if (info.videoProcessing && info.videoFrame >= 5 && info.videoElapsed > 0.5) {
        const double perFrame = info.videoElapsed / (double)info.videoFrame;
        total += (double)(std::max(info.videoFrames, info.videoFrame) - info.videoFrame) * perFrame;
        known = true;
    }
    if (info.costSecPerFrame > 0.0 && info.costPixels > 0.0) {
        for (const auto& it : *info.library) {
            if (it.state != LibraryItem::Queued || it.probe != 1) continue;
            const double cost = info.costSecPerFrame * std::max(1.0, (double)it.width * it.height) / info.costPixels;
            if (it.isVideo) {
                const double in = std::max(0.0, it.inSec);
                const double out = it.outSec > in ? it.outSec : it.duration;
                total += std::max(0.0, out - in) * (it.fps > 0.0 ? it.fps : 30.0) * cost * 1.05 + 0.5;
            } else {
                total += 32.0 * cost + (double)it.width * it.height / 1e6 * info.pngSecPerMegapixel + 0.2;
            }
            known = true;
        }
    }
    return known ? total : -1.0;
}

void MainUI::BlockSource(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const bool busy = info.videoProcessing || info.batchRunning;
    if (s.sourceMode == SourceVideo) {
        ImGui::BeginDisabled(busy);
        OpenCloseRow(TR(OpenVideo), Icon::FileVideo, info.videoLoaded, ev.openVideo, ev.closeMedia);
        ImGui::EndDisabled();
        if (info.videoLoaded) {
            StatusDot(p.good, StrPrintf("%s  %ux%u  %.3g %s  %s", info.videoName.c_str(), info.videoWidth, info.videoHeight, info.videoFps, TR(Fps),
                                        FormatDuration(info.videoDurationSeconds).c_str()).c_str());
            if (info.videoAnimation != 0) {
                // An animated image: its format, frame count, loop count and transparency.
                const char* kDot = "  \xC2\xB7  ";
                std::string line = info.videoCodec;
                if (info.videoAnimation == 3 && info.videoLossless) line += " (lossless)";
                line += kDot + StrPrintf(TR(AnimFrames), (unsigned long long)info.videoFrames);
                line += kDot + LoopText(info.videoLoopCount);
                if (info.videoHasAlpha) line += kDot + std::string(TR(Transparent));
                ImGui::TextDisabled("%s", line.c_str());
            } else {
                ImGui::TextDisabled("%s: %s (%s)  \xC2\xB7  %s", TR(VideoDecoder), info.videoCodec.c_str(),
                                    info.videoHardwareDecode ? TR(HwLabel) : TR(SwLabel), info.videoHasAudio ? TR(Audio) : TR(NoAudio));
            }
            if (info.videoProcessing) {
                const unsigned long long total = std::max(info.videoFrames, info.videoFrame);
                const float frac = total ? (float)((double)info.videoFrame / (double)total) : 0.0f;
                const std::string label = StrPrintf(TR(FrameOf), (unsigned long long)info.videoFrame, total);
                ProgressLine("##videoBar", label.c_str(), frac);
                const double rate = info.videoElapsed > 0.5 ? (double)info.videoFrame / info.videoElapsed : 0.0;
                const double remaining = (rate > 0.0 && total > info.videoFrame) ? (double)(total - info.videoFrame) / rate : 0.0;
                if (info.videoFinishing) ImGui::TextDisabled("%s", TR(VideoFinishing));
                else ImGui::TextDisabled("%5.1f %s  \xC2\xB7  %s %s", rate, TR(Fps), FormatDuration(remaining).c_str(), TR(Remaining));
            } else {
                ImGui::TextDisabled("%s", info.videoPlaying ? TR(Playing) : info.imageConverging ? TR(Processing) : TR(Converged));
            }
        } else {
            StatusDot(p.muted, TR(NoVideo));
            Hint(TR(VideoHint));
        }
        ImGui::Spacing();
        {
            ImGui::BeginDisabled(busy);
            // The output follows the opened file by default: same codec, average bitrate and frame rate.
            if (Toggle(TR(MatchSource), &s.videoMatchSource)) ev.settingsChanged = true;
            Help(TR(TipMatchSource));
            if (s.videoMatchSource && info.videoLoaded && info.videoAnimation != 0) {
                // An animated image comes back in its own format; only a lossy WebP has a quality to choose.
                const bool webp = info.videoAnimation == 3;
                const bool lossless = info.videoLossless || (webp && s.webpQuality >= 100);
                const std::string fmt = webp && lossless ? info.videoCodec + " (lossless)" : info.videoCodec;
                const std::string loop = LoopText(info.videoLoopCount);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                ImGui::TextDisabled(TR(MatchedAnim), fmt.c_str(), loop.c_str());
                ImGui::PopTextWrapPos();
                if (webp && !info.videoLossless) {
                    if (SliderIntReset(TR(WebpQuality), &s.webpQuality, 50, 100, 90, "%d", TR(TipWebpQuality))) ev.settingsChanged = true;
                }
            } else if (s.videoMatchSource && RunningUnderWine()) {
                // Proton: a video comes back as WebP whatever its codec (no MP4 writer); the quality is the WebP one.
                Hint(TR(ProtonNoMp4));
                if (SliderIntReset(TR(WebpQuality), &s.webpQuality, 50, 100, 90, "%d", TR(TipWebpQuality))) ev.settingsChanged = true;
            } else if (s.videoMatchSource) {
                if (info.videoLoaded) {
                    const std::string rate = info.videoBitrateKbps > 0 ? StrPrintf("%.1f Mbit/s", info.videoBitrateKbps / 1000.0) : std::string(TR(BitrateUnknown));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                    ImGui::TextDisabled(TR(MatchedSpecs), IsHevc(info.videoCodec) ? TR(VideoOutputHevc) : TR(VideoOutputH264), rate.c_str(), info.videoFps);
                    ImGui::PopTextWrapPos();
                }
                if (Toggle(TR(KeepAudio), &s.videoKeepAudio)) ev.settingsChanged = true;
                Help(TR(TipKeepAudio));
            } else {
                const char* outputs[] = { TR(VideoOutputH264), TR(VideoOutputHevc), TR(VideoOutputPng), TR(VideoOutputGif), TR(VideoOutputApng), TR(VideoOutputWebP) };
                if (RunningUnderWine()) {
                    // Proton: the two MP4 entries are left out (no MP4 writer); the setting keeps its Windows numbering.
                    int choice = std::clamp(s.videoOutput - 2, 0, 3);
                    if (ComboIds(TR(VideoOutput), &choice, outputs + 2, 4, TR(ProtonNoMp4))) { s.videoOutput = choice + 2; ev.settingsChanged = true; }
                } else if (ComboIds(TR(VideoOutput), &s.videoOutput, outputs, 6, TR(TipVideoOutput))) ev.settingsChanged = true;
                if (s.videoOutput == 0 || s.videoOutput == 1) {
                    if (SliderIntReset(TR(Bitrate), &s.videoBitrateMbps, 5, 200, 40, "%d Mbit/s", TR(TipBitrate))) ev.settingsChanged = true;
                    if (Toggle(TR(KeepAudio), &s.videoKeepAudio)) ev.settingsChanged = true;
                    Help(TR(TipKeepAudio));
                } else if (s.videoOutput == 5) {
                    if (SliderIntReset(TR(WebpQuality), &s.webpQuality, 50, 100, 90, "%d", TR(TipWebpQuality))) ev.settingsChanged = true;
                }
            }
            if (info.videoAnimation == 0 && RevealBegin("##advDecode", m_adv)) {
                if (Toggle(TR(HardwareDecode), &s.videoHardwareDecode)) ev.settingsChanged = true;
                Help(TR(TipHardwareDecode));
                RevealEnd();
            }
            ImGui::EndDisabled();
        }
    } else if (s.sourceMode == SourceImage) {
        OpenCloseRow(TR(OpenImage), Icon::FileImage, info.imageLoaded, ev.openImage, ev.closeMedia);
        if (info.imageLoaded) {
            StatusDot(p.good, StrPrintf("%s  %ux%u", info.imageName.c_str(), info.imageOrigWidth, info.imageOrigHeight).c_str());
            if (info.imageWidth != info.imageOrigWidth || info.imageHeight != info.imageOrigHeight)
                ImGui::TextDisabled("%s (%ux%u)", TR(ImageDownscaled), info.imageWidth, info.imageHeight);
            // Always one line here: a line that only appears while the picture converges would shift everything
            // below it each time a setting changes.
            ImGui::TextDisabled("%s", info.imageConverging ? TR(Processing) : TR(Converged));
        } else {
            StatusDot(p.muted, TR(NoImage));
            Hint(TR(ImageHint));
        }
    } else {
        // Sender selection.
        {
            const std::string preview = s.senderName.empty() ? std::string(TR(SenderAuto)) : s.senderName;
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
            if (BeginDropdown("##sender", preview.c_str())) {
                if (ImGui::Selectable(TR(SenderAuto), s.senderName.empty())) { s.senderName.clear(); ev.senderChanged = true; ev.settingsChanged = true; }
                if (info.senders) {
                    for (const auto& name : *info.senders) {
                        if (ImGui::Selectable(name.c_str(), name == s.senderName)) { s.senderName = name; ev.senderChanged = true; ev.settingsChanged = true; }
                    }
                    if (info.senders->empty()) ImGui::TextDisabled("%s", TR(NoSenders));
                }
                EndDropdown();
            }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (IconButton("##refresh", Icon::Refresh, ImVec2(ImGui::GetFrameHeight(), 0), TR(Refresh))) ev.refreshSenders = true;
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            TrailingLabel(TR(Sender));
        }
        if (info.status && info.sourceConnected) {
            StatusDot(p.good, StrPrintf("%s  %ux%u  %s  %3.0f %s", info.senderName.c_str(), info.status->srcWidth, info.status->srcHeight,
                                        info.sourceFormat.c_str(), m_shown.senderFps, TR(Fps)).c_str());
        } else if (RunningUnderWine()) {
            // Proton: no Spout sender can exist (docs/LINUX.md)
            StatusDot(p.muted, TR(StatusNoSpout));
            Hint(TR(ProtonNoSpout));
        } else {
            // The same words as the badge in the top bar: waiting while a sender is there, else none found.
            const bool senders = info.senders && !info.senders->empty();
            StatusDot(senders ? p.warn : p.muted, senders ? TR(StatusWaiting) : TR(StatusNoSpout));
            Hint(TR(HowToEnable));
        }
    }
    ImGui::Spacing();
    // Output resolution: off = the source size; larger than the source = upscaling (DLSS super resolution or
    // resampling), smaller = downscaling.
    if (Toggle(TR(CustomResolution), &s.customResolution)) ev.settingsChanged = true;
    Help(TR(CustomResolutionHint));
    if (s.customResolution) {
        SearchHold(true);   // the block shows as a whole under its switch
        ImGui::Indent(Px(6.0f));
        LabelSeen(TR(Width));
        if (InputIntLabel(TR(Width), &s.customWidth, 2, 64)) { s.Clamp(); ev.settingsChanged = true; }
        FocusRing();
        ImGui::BeginDisabled(s.keepAspect);
        LabelSeen(TR(Height));
        if (InputIntLabel(TR(Height), &s.customHeight, 2, 64)) { s.Clamp(); ev.settingsChanged = true; }
        FocusRing();
        ImGui::EndDisabled();
        if (Checkbox(TR(KeepAspect), &s.keepAspect)) ev.settingsChanged = true;
        {
            // The common sizes as small buttons after a caption, wrapping to the next line when the row is full;
            // the one in use is marked.
            const ImGuiStyle& sty = ImGui::GetStyle();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(sty.FramePadding.x * 0.8f, std::max(1.0f, Px(3.0f))));
            const float rowEnd = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", TR(Presets));
            struct { const char* n; int w, h; } presets[] = { {"720p",1280,720}, {"1080p",1920,1080}, {"1440p",2560,1440}, {"4K",3840,2160}, {"8K",7680,4320} };
            for (auto& pr : presets) {
                const float w = ImGui::CalcTextSize(pr.n).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                if (ImGui::GetItemRectMax().x + sty.ItemInnerSpacing.x + w <= rowEnd) ImGui::SameLine(0.0f, sty.ItemInnerSpacing.x);
                const bool on = s.customWidth == pr.w && (s.keepAspect || s.customHeight == pr.h);
                if (on ? GhostButton(pr.n) : FlatButton(pr.n)) { s.customWidth = pr.w; s.customHeight = pr.h; ev.settingsChanged = true; }
            }
            ImGui::PopStyleVar();
        }
        // Upscaling: the output is larger than the source. The method, a plain warning about the cost (the neural
        // pass and everything after it work on the large picture) and what is in effect right now.
        const PipelineStatus* st = info.status;
        const UINT srcW = st ? st->srcWidth : 0, srcH = st ? st->srcHeight : 0;
        UINT outW = (UINT)s.customWidth, outH = (UINT)s.customHeight;
        if (s.keepAspect && srcW && srcH) outH = (UINT)std::lround((double)outW * srcH / srcW);
        const bool upscaling = srcW && srcH && (outW > srcW || outH > srcH);
        if (upscaling) {
            const bool dlss = st->ngxInitialized && st->dlssAvailable;
            const char* methods[] = { TR(UpscaleDlss), TR(UpscaleResample) };
            if (ComboIds(TR(UpscaleMethod), &s.upscaleMode, methods, 2, TR(TipUpscaleMethod))) ev.settingsChanged = true;
            if (!dlss && s.upscaleMode == 0) Hint(TR(UpscaleNoDlss));
            const double factor = (double)outW * outH / ((double)srcW * srcH);
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", StrPrintf(TR(UpscaleWarning), factor).c_str());
            ImGui::PopStyleColor();
            if (!m_upscaleWarned) { m_upscaleWarned = true; Toast(TR(UpscaleToast)); }
            if (st->upscaleMode == 1) {
                ImGui::TextDisabled("%s", StrPrintf(TR(UpscaleStatusDlss), st->srQualityName.c_str(), st->inWidth, st->inHeight, st->outWidth, st->outHeight).c_str());
            } else if (st->upscaleMode == 2) {
                ImGui::TextDisabled("%s", st->dlaaActive ? TR(UpscaleStatusDlaa) : TR(UpscaleStatusResample));
                if (s.upscaleMode == 0 && st->dlaaFailed && !st->dlaaError.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
                    ImGui::TextWrapped("%s", st->dlaaError.c_str());
                    ImGui::PopStyleColor();
                }
            }
            if (st->workWidth && (st->workWidth != st->outWidth || st->workHeight != st->outHeight))
                Hint(StrPrintf(TR(UpscaleStatusCapped), st->workWidth, st->workHeight, st->outWidth, st->outHeight).c_str());
        } else {
            m_upscaleWarned = false;   // the next upscale gets the notice again
        }
        ImGui::Unindent(Px(6.0f));
        SearchHold(false);
    } else {
        m_upscaleWarned = false;
    }
    if (s.sourceMode == SourceSpout) Hint(TR(VrchatResHint));
    // HDR source controls: only meaningful for floating-point (scene-linear) Spout textures.
    if (info.sourceIsHdr && s.sourceMode == SourceSpout) {
        ImGui::Spacing();
        SectionLabel(TR(HdrSource));
        ImGui::Indent(Px(6.0f));
        bool ch = false;
        ch |= SliderReset(TR(PaperWhite), &s.hdrPaperWhite, 0.1f, 8.0f, 1.0f, "%.2f", TR(TipPaperWhite));
        ch |= SliderReset(TR(HighlightCompression), &s.hdrHighlightCompression, 0.0f, 1.0f, 1.0f, "%.2f", TR(TipHighlightCompression));
        if (ch) ev.settingsChanged = true;
        Hint(TR(HdrSourceHint));
        ImGui::Unindent(Px(6.0f));
    }
    ImGui::Spacing();
}

void MainUI::BlockNeural(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    if (Toggle(TR(NrEnable), &s.nrEnabled)) { ev.nrChanged = true; ev.settingsChanged = true; }
    if (st) {
        if (st->nrActive) PillAfter(TR(Active), WithAlpha(p.good, 0.18f), p.good, Px(10.0f));
        else if (st->nrFailed) PillAfter(TR(Failed), WithAlpha(p.bad, 0.18f), p.bad, Px(10.0f));
        else if (st->nrStandby) PillAfter(TR(Standby), WithAlpha(p.warn, 0.18f), p.warn, Px(10.0f));
        else PillAfter(TR(Inactive), WithAlpha(p.muted, 0.2f), p.muted, Px(10.0f));
    }
    Hint(TR(NrHint));
    if (s.sourceMode == SourceSpout && RevealBegin("##advCaptureOnly", m_adv)) {
        if (Toggle(TR(NrCaptureOnly), &s.nrCaptureOnly)) ev.settingsChanged = true;
        Help(TR(TipNrCaptureOnly));
        RevealEnd();
    }

    // Runtime.
    ImGui::Spacing();
    // The runtime rows differ by edition: the NVIDIA runtime file the GeForce edition hosts, or the FSR host that
    // DLSS-NR-on-AMD attaches to in the Radeon edition. The route is the edition's; there is nothing to choose.
    if (EditionRoute() == RouteFsrHost) BlockFsrHost(s, info, ev);
    else BlockNgxRuntime(s, info, ev);
    ImGui::Spacing();
    EffectControls(s, ev, m_adv, s.nrEnabled, st);
    if (st && RevealBegin("##advNeuralReadouts", m_adv)) {
        Readout(m_fonts, TR(GpuTime), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Neural]));
        Readout(m_fonts, TR(Frames), StrPrintf("%llu", m_shown.processedFrames));
        Readout(m_fonts, TR(NrPassSize), st->nrActive && st->nrPassWidth ? StrPrintf("%ux%u", st->nrPassWidth, st->nrPassHeight) : std::string("-"));
        Readout(m_fonts, TR(NrOutputCheck), st->nrActive && m_shown.nrOutDelta >= 0.0f ? StrPrintf("%5.3f", m_shown.nrOutDelta) : std::string("-"), TR(TipNrOutputCheck));
        RevealEnd();
    }
    ImGui::Spacing();
}

// The effect controls of the DLSS 5 pass: shared by the sidebar and by the window of a library item's own values.
void MainUI::EffectControls(Settings& s, UiEvents& ev, float advanced, bool enabled, const PipelineStatus* st) {
    ImGui::BeginDisabled(!enabled);
    {
        DrawPresetRow(s, ev);
        const char* styles[] = { TR(StyleDefault), TR(StyleNatural), TR(StyleCinematic) };
        if (ComboIds(TR(Style), &s.nrStyle, styles, 3, TR(TipStyle))) { ev.nrChanged = true; ev.settingsChanged = true; }
    }
    bool ch = false;
    // 0..2: up to 1 goes to the runtime (which stops there); above 1 the composite pass amplifies the matching part
    // of the change the network made (see the tooltips).
    ch |= SliderReset(TR(Intensity), &s.nrIntensity, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipIntensity));
    if (RevealBegin("##basicHint", 1.0f - advanced)) { Hint(TR(NrStrengthHint)); RevealEnd(); }
    bool blend = false;
    if (RevealBegin("##advEffect", advanced)) {
        ch |= SliderReset(TR(GlobalTone), &s.nrGlobalTone, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipGlobalTone));
        ch |= SliderReset(TR(LocalTone), &s.nrLocalTone, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipLocalTone));
        ch |= SliderReset(TR(LocalStructure), &s.nrLocalStructure, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipLocalStructure));
        if (SearchMatch(TR(SkinStructure), TR(TipSkinStructure))) {
            // The box and its slider are one entry, found by the slider's name: the box's own label is only
            // "Runtime default", and its caption would otherwise stand alone in a search.
            SearchHold(true);
            bool useDefault = s.nrSkinStructure < 0.0f;
            ImGui::PushID("skin");
            if (Checkbox(TR(UseDefault), &useDefault)) { s.nrSkinStructure = useDefault ? -1.0f : 1.0f; ch = true; }
            const std::string what = StrPrintf("(%s)", TR(SkinStructure));   // beside the box while it fits, else below it
            SameLineIfRoom(ImGui::CalcTextSize(what.c_str()).x, ImGui::GetStyle().ItemSpacing.x);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", what.c_str());
            ImGui::PopTextWrapPos();
            if (!useDefault) ch |= SliderReset(TR(SkinStructure), &s.nrSkinStructure, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipSkinStructure));
            else Tip(TR(TipSkinStructure));
            ImGui::PopID();
            SearchHold(false);
        }
        if (Toggle(TR(AutoMask), &s.nrAutoMask)) ch = true;
        Help(TR(TipAutoMask));
        if (Toggle(TR(UiCorrection), &s.nrUiCorrection)) ch = true;
        Help(TR(TipUiCorrection));
        {
            // Neural pass resolution, one list: the full picture, a cap on the long edge, or a percentage; a reduced
            // pass has its change upsampled onto the full picture. A value from the settings file or the command
            // line that is not on the list gets its own entry.
            static const int kNrEdges[] = { 3840, 2880, 2160, 1440, 1080, 720 };
            static const int kNrPercents[] = { 75, 50, 25 };
            std::vector<std::string> labels;
            labels.emplace_back(TR(NrScaleFull));
            int sel = 0;
            for (int e : kNrEdges) { if (s.nrScaleMode == 1 && s.nrMaxLongEdge == e) sel = (int)labels.size(); labels.emplace_back(StrPrintf(TR(NrScaleEdgeFmt), e)); }
            for (int pc : kNrPercents) { if (s.nrScaleMode == 0 && s.nrInputScale == pc) sel = (int)labels.size(); labels.emplace_back(StrPrintf(TR(NrScalePercentFmt), pc)); }
            if (sel == 0 && !(s.nrScaleMode == 0 && s.nrInputScale >= 100)) {
                sel = (int)labels.size();
                labels.emplace_back(s.nrScaleMode == 1 ? StrPrintf(TR(NrScaleEdgeFmt), s.nrMaxLongEdge) : StrPrintf(TR(NrScalePercentFmt), s.nrInputScale));
            }
            std::vector<const char*> items;
            for (const std::string& l : labels) items.push_back(l.c_str());
            if (ComboIds(TR(NrScaleMode), &sel, items.data(), (int)items.size(), TR(TipNrScaleMode))) {
                if (sel == 0) { s.nrScaleMode = 0; s.nrInputScale = 100; }
                else if (sel <= 6) { s.nrScaleMode = 1; s.nrMaxLongEdge = kNrEdges[sel - 1]; }
                else if (sel <= 9) { s.nrScaleMode = 0; s.nrInputScale = kNrPercents[sel - 7]; }
                ch = true;
            }
            if (st && s.nrEnabled && st->nrPassCapped && st->nrPassWidth)
                Hint(StrPrintf(TR(NrPassCapped), st->nrPassWidth, st->nrPassHeight).c_str());
        }
        ImGui::Spacing();
        SectionLabel(TR(OutputBlend));
        // The exposure changes what the network sees (neural pass re-run); the strengths only change the composite.
        ch |= SliderReset(TR(InputExposure), &s.nrInputExposure, 0.25f, 4.0f, 1.0f, "%.2fx", TR(TipInputExposure));
        blend |= SliderReset(TR(ToneTransfer), &s.nrToneTransfer, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipToneTransfer));
        blend |= SliderReset(TR(ColorStrength), &s.nrColorStrength, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipColorStrength));
        blend |= SliderReset(TR(ShadowGain), &s.nrShadowGain, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipShadowGain));
        blend |= SliderReset(TR(HighlightGain), &s.nrHighlightGain, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipHighlightGain));
        RevealEnd();
    }
    ImGui::EndDisabled();
    if (blend) ev.settingsChanged = true;
    if (ch) { ev.nrChanged = true; ev.settingsChanged = true; }
    ImGui::Spacing();
    if (GhostButton(TR(ResetHistory))) ev.resetHistory = true;
    SameLineIfFits(TR(ResetDefaults));
    if (GhostButton(TR(ResetDefaults))) {
        s.nrPreset = 0; s.nrStyle = 0; s.nrIntensity = 1.0f; s.nrGlobalTone = 1.0f; s.nrLocalTone = 1.0f;
        s.nrLocalStructure = 1.0f; s.nrSkinStructure = -1.0f; s.nrAutoMask = false; s.nrUiCorrection = false;
        s.nrInputExposure = 1.0f; s.nrToneTransfer = 1.0f; s.nrColorStrength = 1.0f;
        s.nrShadowGain = 1.0f; s.nrHighlightGain = 1.0f; s.nrInputScale = 100; s.nrScaleMode = 0; s.nrMaxLongEdge = 2160;
        ev.nrChanged = true; ev.settingsChanged = true;
    }
}

void MainUI::BlockSave(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const bool busy = info.videoProcessing || info.batchRunning;
    ImGui::BeginDisabled(busy);
    if (IconTextButton(s.sourceMode == SourceVideo ? TR(ProcessVideo) : s.sourceMode == SourceImage ? TR(ProcessAndSave) : TR(Capture),
                       s.sourceMode == SourceVideo ? Icon::Film : s.sourceMode == SourceImage ? Icon::Sparkle : Icon::Camera, ImVec2(-FLT_MIN, 0), ButtonKind::Accent))
        ev.captureNow = true;
    ImGui::EndDisabled();
    Tip(s.sourceMode == SourceVideo ? TR(VideoHint) : s.sourceMode == SourceImage ? TR(ImageHint) : TR(CaptureHint));
    Hint(TR(OutputHint));
    if (s.sourceMode != SourceSpout) {
        // What the run would take on this card, so a slow card can be judged before the wait.
        Readout(m_fonts, TR(Estimate), EstimateText(s, info), TR(TipEstimate));
    }
    if (SearchMatch(TR(CaptureFolder), TR(OpenFolder))) {
        SyncBuffer(m_folderBuf, sizeof(m_folderBuf), s.captureFolder, m_folderEditing);
        const float btnW = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - btnW * 2.0f - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
        const std::string hint = WideToUtf8(info.captureFolder);
        ImGui::InputTextWithHint("##folder", hint.c_str(), m_folderBuf, sizeof(m_folderBuf));
        FocusRing();
        m_folderEditing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { s.captureFolder = m_folderBuf; ev.settingsChanged = true; }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (IconButton("##browsefolder", Icon::Folder, ImVec2(btnW, 0), TR(Browse))) ev.browseFolder = true;
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (IconButton("##openfolder", Icon::OpenExternal, ImVec2(btnW, 0), TR(OpenFolder))) ev.openCaptureFolder = true;
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        TrailingLabel(TR(CaptureFolder));
    }
    {
        // The saved files' names, from a template; an empty box means the default it shows. Live captures and
        // processed files have their own (a capture has no source name to build on), the mode says which is shown.
        const bool live = s.sourceMode == SourceSpout;
        std::string& tmpl = live ? s.captureName : s.outputName;
        SyncBuffer(m_nameBuf, sizeof(m_nameBuf), tmpl, m_nameEditing);
        const std::string hint = WideToUtf8(live ? Capture::kDefaultCaptureName : Capture::kDefaultOutputName);
        ImGui::InputTextWithHint("##filename", hint.c_str(), m_nameBuf, sizeof(m_nameBuf));
        FocusRing();
        m_nameEditing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { tmpl = m_nameBuf; ev.settingsChanged = true; }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        TrailingLabel(live ? TR(CaptureName) : TR(OutputName));
        Help(TR(TipFileName));
    }
    if (Toggle(TR(SaveOriginal), &s.saveOriginal)) ev.settingsChanged = true;
    if (RevealBegin("##advKeepAlpha", m_adv)) {
        if (Toggle(TR(KeepAlpha), &s.keepAlpha)) ev.settingsChanged = true;
        Help(TR(TipKeepAlpha));
        RevealEnd();
    }
    if (s.sourceMode == SourceSpout) {
        ImGui::Spacing();
        // Hotkey.
        if (Toggle(TR(Hotkey), &s.hotkeyEnabled)) { ev.hotkeyChanged = true; ev.settingsChanged = true; }
        Help(TR(TipHotkey));
        if (s.hotkeyEnabled && !SearchSkipped()) {
            SearchHold(true);
            ImGui::Indent(Px(6.0f));
            bool ctrl = (s.hotkeyModifiers & 0x0002) != 0, alt = (s.hotkeyModifiers & 0x0001) != 0, shift = (s.hotkeyModifiers & 0x0004) != 0, win = (s.hotkeyModifiers & 0x0008) != 0;
            bool hc = false;
            hc |= Checkbox("Ctrl", &ctrl); SameLineIfFits("Shift  ");
            hc |= Checkbox("Alt", &alt); SameLineIfFits("Shift  ");
            hc |= Checkbox("Shift", &shift); SameLineIfFits("Shift  ");
            hc |= Checkbox("Win", &win);
            if (hc) s.hotkeyModifiers = (ctrl ? 0x0002u : 0u) | (alt ? 0x0001u : 0u) | (shift ? 0x0004u : 0u) | (win ? 0x0008u : 0u);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            if (BeginDropdown(TR(Key), HotkeyKeyName(s.hotkeyKey))) {
                for (const auto& h : kHotkeys) {
                    if (ImGui::Selectable(h.name, h.vk == s.hotkeyKey)) { s.hotkeyKey = h.vk; hc = true; }
                }
                EndDropdown();
            }
            if (hc) { ev.hotkeyChanged = true; ev.settingsChanged = true; }
            ImGui::Unindent(Px(6.0f));
            SearchHold(false);
        }
        // Timelapse.
        {
            int idx = 0;
            for (int i = 0; i < (int)IM_ARRAYSIZE(kTimelapseChoices); ++i) if (kTimelapseChoices[i] == s.timelapseSeconds) idx = i;
            std::string labels[IM_ARRAYSIZE(kTimelapseChoices)];
            const char* items[IM_ARRAYSIZE(kTimelapseChoices)];
            for (int i = 0; i < (int)IM_ARRAYSIZE(kTimelapseChoices); ++i) {
                labels[i] = (kTimelapseChoices[i] == 0) ? std::string(TR(TimelapseOff)) : StrPrintf("%d %s", kTimelapseChoices[i], TR(Seconds));
                items[i] = labels[i].c_str();
            }
            if (ComboIds(TR(Timelapse), &idx, items, IM_ARRAYSIZE(kTimelapseChoices), TR(TipTimelapse))) { s.timelapseSeconds = kTimelapseChoices[idx]; ev.settingsChanged = true; }
        }
    }
    if (!info.lastCapture.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, info.lastCaptureOk ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : p.bad);
        ImGui::TextWrapped("%s: %s", TR(LastCapture), info.lastCapture.c_str());
        ImGui::PopStyleColor();
    }
    // Always one line: a count that only appears during a capture would shift the sections below it.
    if (info.capturePending) ImGui::TextDisabled("%zu %s", info.capturePending, TR(Pending));
    else ImGui::TextDisabled(" ");
    ImGui::Spacing();
}

void MainUI::BlockView(Settings& s, const UiFrameInfo& /*info*/, UiEvents& ev) {
    if (SearchMatch(TR(Theme), TR(TipTheme))) {
        const char* themes[] = { TR(ThemeSystem), TR(ThemeDark), TR(ThemeLight) };
        const Icon themeIcons[] = { Icon::Monitor, Icon::Moon, Icon::Sun };
        // Icons only while every segment has room for its icon and its word; a list when even the words do not fit.
        const float w = ImGui::CalcItemWidth();
        if (SegmentsFit(themes, 3, nullptr, w)) {
            const bool withIcons = SegmentsFit(themes, 3, themeIcons, w);
            if (Segmented("##theme", themes, 3, &s.theme, w, withIcons ? themeIcons : nullptr)) ev.settingsChanged = true;
            Tip(TR(TipTheme));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            TrailingLabel(TR(Theme));
        } else if (ComboIds(TR(Theme), &s.theme, themes, 3, TR(TipTheme))) {
            ev.settingsChanged = true;
        }
    }
    {
        const char* items[] = { TR(CompareOutput), TR(CompareOriginal), TR(CompareWipe), TR(CompareMotion), TR(CompareDepth) };
        if (ComboIds(TR(Compare), &s.compareMode, items, 5)) ev.settingsChanged = true;
    }
    if (s.compareMode == CompareWipe && !SearchSkipped()) {
        if (SliderFloatFill("##wipe", &s.wipePosition, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
    }
    {
        const char* items[] = { TR(FitWindowLabel), TR(OneToOne) };
        if (ComboIds("##fit", &s.fitMode, items, 2)) { ev.settingsChanged = true; ResetView(false); }
        // Manual magnification on top of the fit, shown relative to the picture's pixels. The wheel over the preview
        // does the same; the button returns to the fitted view.
        if (SearchMatch(TR(Zoom), TR(TipZoom))) {
            ImGui::PushID("zoom");
            const ImGuiStyle& style = ImGui::GetStyle();
            const float resetW = ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
            float pct = m_zoomTarget * m_baseScale * 100.0f;
            if (SliderFloatFill("##z", &pct, m_zoomFloor * 100.0f, kZoomMax * 100.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
                m_zoom = m_zoomTarget = pct / (100.0f * std::max(m_baseScale, 1e-6f));
                m_panHome = false;
            }
            Tip(TR(TipZoom));
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            const bool moved = std::fabs(m_zoomTarget - 1.0f) >= 1e-4f || m_pan.x != 0.0f || m_pan.y != 0.0f;
            if (ResetButton("##resetview", moved, resetW, TR(ResetView))) ResetView(true);
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            TrailingLabel(TR(Zoom));
            ImGui::PopID();
        }
    }
    if (Toggle(TR(Checkerboard), &s.checkerboard)) ev.settingsChanged = true;
    if (Toggle(TR(ShowLibrary), &s.libraryVisible)) ev.settingsChanged = true;
    if (Toggle(TR(Overlay), &s.showOverlay)) ev.settingsChanged = true;
    if (Toggle(TR(ShowLog), &s.showLog)) ev.settingsChanged = true;
    if (Toggle(TR(ReopenLast), &s.reopenLast)) ev.settingsChanged = true;
    Help(TR(TipReopenLast));
    if (RevealBegin("##advView", m_adv)) {
        if (Toggle(TR(Vsync), &s.vsync)) ev.settingsChanged = true;
        if (SliderIntReset(TR(RateLimit), &s.processRateLimit, 0, 240, 0, s.processRateLimit > 0 ? "%d fps" : TR(RateLimitOff), TR(TipRateLimit)))
            ev.settingsChanged = true;
        RevealEnd();
    }
    ImGui::Spacing();
}

// MCP: a server that lets a client work the program through the same actions
// as this interface. The switch, the port, whether it may change anything, its state and the client configuration.
void MainUI::BlockMcp(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float btnW = ImGui::GetFrameHeight();
    auto ageText = [](double age) {
        if (age < 60.0) return StrPrintf("%.0f s", age);
        if (age < 3600.0) return StrPrintf("%.0f min", age / 60.0);
        if (age < 86400.0) return StrPrintf("%.1f h", age / 3600.0);
        return StrPrintf("%.0f d", age / 86400.0);
    };
    Hint(TR(McpHint));
    if (Toggle(TR(McpEnable), &s.mcpEnabled)) ev.settingsChanged = true;
    Help(TR(TipMcp));
    {
        const char* items[] = { TR(McpReachLocal), TR(McpReachNetwork) };
        if (ComboIds(TR(McpReach), &s.mcpBind, items, 2, TR(TipMcpReach))) ev.settingsChanged = true;
    }
    if (s.mcpBind == 1 && !Searching()) {
        if (info.mcpKeys.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", TR(McpNetworkNoKeys));
            ImGui::PopStyleColor();
        }
        if (ActionButton(TR(McpFirewall), Icon::Shield, ImVec2(fullW, 0.0f))) ev.mcpFirewall = true;
        Tooltip(TR(TipMcpFirewall));
    }
    if (SearchMatch(TR(McpPort), TR(TipMcpPort))) {
        LabelSeen(TR(McpPort));
        if (InputIntLabel(TR(McpPort), &s.mcpPort, 1, 100)) { s.Clamp(); ev.settingsChanged = true; }
        Tip(TR(TipMcpPort));
    }
    if (Toggle(TR(McpReadOnly), &s.mcpReadOnly)) ev.settingsChanged = true;
    Help(TR(TipMcpReadOnly));
    if (SearchMatch(TR(McpCopyConfig), TR(TipMcpCopyConfig))) {   // shows with the section's title or its buttons
        // The state: a badge and the address, then how much the clients have done.
        ImGui::Spacing();
        if (info.mcpRunning) {
            Pill(TR(McpOn), WithAlpha(p.good, 0.2f), p.good);
            ImGui::SameLine(0.0f, Px(6.0f));
            ImGui::TextWrapped("%s", info.mcpUrl.c_str());
            for (const std::string& a : info.mcpAddresses) ImGui::TextDisabled("%s", a.c_str());
            if (info.mcpSession && !s.mcpEnabled) { ImGui::PushTextWrapPos(0.0f); ImGui::TextDisabled("%s", TR(McpSessionNote)); ImGui::PopTextWrapPos(); }
        } else if (!info.mcpError.empty()) {
            Pill(TR(McpOff), WithAlpha(p.bad, 0.2f), p.bad);
            ImGui::SameLine(0.0f, Px(6.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
            ImGui::TextWrapped("%s", StrPrintf(TR(McpStartFailedFmt), info.mcpError.c_str()).c_str());
            ImGui::PopStyleColor();
        } else {
            Pill(TR(McpOff), WithAlpha(p.textDim, 0.2f), p.textDim);
            ImGui::SameLine(0.0f, Px(6.0f));
            ImGui::TextDisabled("%s", info.mcpUrl.c_str());
        }
        if (info.mcpCalls > 0) ImGui::TextDisabled("%s", StrPrintf(TR(McpCallsFmt), info.mcpCalls, info.mcpLastTool.c_str(), ageText(info.mcpLastAge).c_str()).c_str());
        else if (info.mcpRunning) ImGui::TextDisabled("%s", TR(McpNoCalls));
        ImGui::Spacing();
        if (ActionButton(TR(McpCopyConfig), Icon::Copy, ImVec2(fullW, 0.0f))) { ImGui::SetClipboardText(info.mcpConfig.c_str()); Toast(TR(McpCopied)); }
        Tooltip(TR(TipMcpCopyConfig));
        const ImVec2 pair = PairSize(TR(McpCopyUrl), TR(McpOpenPage), fullW);
        if (ActionButton(TR(McpCopyUrl), Icon::Copy, pair)) { ImGui::SetClipboardText(info.mcpUrl.c_str()); Toast(TR(McpUrlCopied)); }
        if (pair.x < fullW) ImGui::SameLine(0.0f, style.ItemSpacing.x);
        ImGui::BeginDisabled(!info.mcpRunning);
        if (ActionButton(TR(McpOpenPage), Icon::OpenExternal, pair)) ev.mcpOpenPage = true;
        ImGui::EndDisabled();
        if (ActionButton(TR(Documentation), Icon::Help, ImVec2(fullW, 0.0f))) ev.mcpOpenDocs = true;
    }
    // The keys: one row each, then the field for a new one. A key's secret is copied when it is made and on demand.
    if (SearchMatch(TR(McpKeys), TR(TipMcpRole))) {
        SearchHold(true);   // the list and the row for a new key show as a whole
        ImGui::Spacing();
        ImGui::TextDisabled("%s", TR(McpKeys));
        Help(TR(TipMcpRole));
        if (info.mcpKeys.empty()) { ImGui::PushTextWrapPos(0.0f); ImGui::TextDisabled("%s", TR(McpNoKeys)); ImGui::PopTextWrapPos(); }
        for (const McpKeyView& k : info.mcpKeys) {
            ImGui::PushID(k.name.c_str());
            const char* role = k.role == McpRoleAdmin ? TR(McpRoleAdmin) : k.role == McpRoleJobs ? TR(McpRoleJobs) : TR(McpRoleViewer);
            const ImU32 rc = k.role == McpRoleAdmin ? p.accent : k.role == McpRoleJobs ? p.good : p.textDim;
            const float rowY = ImGui::GetCursorPosY();
            if (IconButton("##copy", Icon::Copy, ImVec2(btnW, 0), TR(McpKeyCopy), ButtonKind::Plain)) { ev.mcpKeyCopy = true; ev.mcpKeyName = k.name; }
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            if (IconButton("##remove", Icon::Trash, ImVec2(btnW, 0), TR(McpKeyRemove), ButtonKind::Plain)) { ev.mcpKeyRemove = true; ev.mcpKeyName = k.name; }
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
            ImGui::SetCursorPosY(rowY + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextUnformatted(k.name.c_str());
            ImGui::SameLine(0.0f, Px(6.0f));
            Pill(role, WithAlpha(rc, 0.2f), rc);
            if (ImGui::IsItemHovered()) Tooltip(TR(TipMcpRole));
            ImGui::SameLine(0.0f, Px(6.0f));
            if (k.lastAge < 0.0) ImGui::TextDisabled("%s", TR(McpKeyNever));
            else ImGui::TextDisabled("%s", StrPrintf(TR(McpKeyUsedFmt), ageText(k.lastAge).c_str(), k.calls).c_str());
            ImGui::PopID();
        }
        {
            // One row while the name keeps room for a few words, else the name has a row of its own above the role and the button.
            const char* roles[] = { TR(McpRoleViewer), TR(McpRoleJobs), TR(McpRoleAdmin) };
            const float roleW = std::floor(fullW * 0.3f);
            const float addW = IconTextButtonWidth(TR(McpAddKey));
            const float nameW = fullW - roleW - addW - style.ItemInnerSpacing.x * 2.0f;
            const bool oneRow = nameW >= ImGui::GetFontSize() * 7.0f;
            ImGui::SetNextItemWidth(oneRow ? nameW : fullW);
            ImGui::InputTextWithHint("##keyname", TR(McpKeyNameHint), m_mcpKeyBuf, sizeof(m_mcpKeyBuf));
            FocusRing();
            const bool enter = ImGui::IsItemDeactivatedAfterEdit() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            if (oneRow) ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(oneRow ? roleW : fullW - addW - style.ItemInnerSpacing.x);
            ComboIds("##keyrole", &m_mcpKeyRole, roles, 3);
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::BeginDisabled(m_mcpKeyBuf[0] == 0);
            if (IconTextButton(TR(McpAddKey), Icon::Plus, ImVec2(addW, 0)) || (enter && m_mcpKeyBuf[0])) {
                ev.mcpKeyAdd = true; ev.mcpKeyName = m_mcpKeyBuf; ev.mcpKeyRole = m_mcpKeyRole;
                m_mcpKeyBuf[0] = 0;
            }
            ImGui::EndDisabled();
            Tooltip(TR(TipMcpAddKey));
        }
        SearchHold(false);
    }
    // The jobs the clients sent: counts, the folder, and how long the results stay.
    if (SearchMatch(TR(McpJobs), TR(TipMcpKeepHours))) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", TR(McpJobs));
        ImGui::SameLine(0.0f, Px(6.0f));
        ImGui::TextDisabled("%s", StrPrintf(TR(McpJobsFmt), info.mcpJobsQueued, info.mcpJobsRunning, info.mcpJobsDone).c_str());
        if (!info.mcpJob.empty()) PillAfter(info.mcpJob.c_str(), WithAlpha(p.warn, 0.2f), p.warn, Px(6.0f));
        LabelSeen(TR(McpKeepHours));
        if (InputIntLabel(TR(McpKeepHours), &s.mcpKeepHours, 1, 24)) { s.Clamp(); ev.settingsChanged = true; }
        Tip(TR(TipMcpKeepHours));
        if (ActionButton(TR(McpOpenJobs), Icon::Folder, ImVec2(fullW, 0.0f))) ev.mcpOpenJobs = true;
    }
    if (RevealBegin("##advMcp", m_adv)) {
        if (SearchMatch(TR(McpQueueMax), TR(TipMcpQueueMax))) {
            LabelSeen(TR(McpQueueMax));
            if (InputIntLabel(TR(McpQueueMax), &s.mcpQueueMax, 1, 10)) { s.Clamp(); ev.settingsChanged = true; }
            Tip(TR(TipMcpQueueMax));
        }
        if (SearchMatch(TR(McpQueuePerKey), TR(TipMcpQueuePerKey))) {
            LabelSeen(TR(McpQueuePerKey));
            if (InputIntLabel(TR(McpQueuePerKey), &s.mcpQueuePerKey, 1, 5)) { s.Clamp(); ev.settingsChanged = true; }
            Tip(TR(TipMcpQueuePerKey));
        }
        if (SearchMatch(TR(McpUploadMax), TR(TipMcpUploadMax))) {
            LabelSeen(TR(McpUploadMax));
            if (InputIntLabel(TR(McpUploadMax), &s.mcpUploadMaxMb, 64, 1024)) { s.Clamp(); ev.settingsChanged = true; }
            Tip(TR(TipMcpUploadMax));
        }
        if (Toggle(TR(McpLocalNoKey), &s.mcpLocalNoKey)) ev.settingsChanged = true;
        Help(TR(TipMcpLocalNoKey));
        if (SearchMatch(TR(McpJobFolder), TR(TipMcpJobFolder))) {
            SyncBuffer(m_mcpJobFolderBuf, sizeof(m_mcpJobFolderBuf), s.mcpJobFolder, m_mcpJobFolderEditing);
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
            ImGui::InputTextWithHint("##mcpjobfolder", TR(TipMcpJobFolder), m_mcpJobFolderBuf, sizeof(m_mcpJobFolderBuf));
            FocusRing();
            m_mcpJobFolderEditing = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) { s.mcpJobFolder = m_mcpJobFolderBuf; ev.settingsChanged = true; }
            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            TrailingLabel(TR(McpJobFolder));
            Tip(TR(TipMcpJobFolder));
        }
        RevealEnd();
    }
    ImGui::Spacing();
}

void MainUI::BlockGuidance(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    // A picture or a paused video has no motion to measure: say so rather than reporting the flow as unavailable.
    const bool still = (s.sourceMode == SourceImage) || (s.sourceMode == SourceVideo && !info.videoPlaying && !info.videoProcessing);
    {
        const char* items[] = { TR(MotionZero), TR(MotionCompute), TR(MotionNvof), TR(MotionFsr) };
        // The FSR flow is this program's own and deserves a word of explanation when it is selected.
        if (ComboIds(TR(MotionSource), &s.motionMode, items, 4, s.motionMode == MotionFsrFlow ? TR(TipFsrFlow) : TR(TipMotion))) ev.settingsChanged = true;
    }
    if (s.motionMode == MotionCompute) {
        if (SliderIntReset(TR(SearchRadius), &s.searchRadius, 2, 12, 7, "%d px", TR(TipSearchRadius))) ev.settingsChanged = true;
    } else if (s.motionMode == MotionNvOpticalFlow) {
        int grid = (s.nvofGrid == 4) ? 0 : (s.nvofGrid == 1) ? 2 : 1;
        const char* grids[] = { "4 px", "2 px", "1 px" };
        if (ComboIds(TR(NvofGrid), &grid, grids, 3, TR(TipNvof))) { s.nvofGrid = (grid == 0) ? 4 : (grid == 2) ? 1 : 2; ev.settingsChanged = true; }
        int perf = (s.nvofPerf == 5) ? 0 : (s.nvofPerf == 20) ? 2 : 1;
        const char* perfs[] = { TR(PerfSlow), TR(PerfMedium), TR(PerfFast) };
        if (ComboIds(TR(NvofPerf), &perf, perfs, 3)) { s.nvofPerf = (perf == 0) ? 5 : (perf == 2) ? 20 : 10; ev.settingsChanged = true; }
        if (Toggle(TR(NvofBidirectional), &s.nvofBidirectional)) ev.settingsChanged = true;
        Help(TR(TipNvofBidirectional));
        if (st) {
            if (still && !st->nvofReady) StatusDot(p.muted, StrPrintf("%s: %s", TR(Nvof), TR(StaticPreview)).c_str());
            else if (st->nvofReady) StatusDot(p.good, StrPrintf("%s: %s (%u px%s)", TR(Nvof), TR(Available), st->nvofGrid, st->nvofBidirectional ? " \xE2\x87\x84" : "").c_str());
            else if (!st->nvofAvailable && info.adapter && !info.adapter->IsNvidia()) StatusDot(p.muted, StrPrintf("%s: %s", TR(Nvof), TR(NvofNoEngine)).c_str());
            // No source yet: the engine is created with the first frame, so this is no failure either.
            else if (st->srcWidth == 0 && st->nvofError.empty()) StatusDot(p.muted, StrPrintf("%s: %s", TR(Nvof), TR(DepthWaitingSource)).c_str());
            else if (!st->nvofAvailable) StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), TR(NotAvailable)).c_str());
            else StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), st->nvofError.empty() ? TR(NotAvailable) : st->nvofError.c_str()).c_str());
        }
    }
    if (s.motionMode == MotionFsrFlow) {
        if (SliderIntReset(TR(SearchRadius), &s.searchRadius, 2, 12, 7, "%d px", TR(TipFlowRadius))) ev.settingsChanged = true;
        if (Toggle(TR(FlowBidirectional), &s.flowBidirectional)) ev.settingsChanged = true;
        Help(TR(TipFlowBidirectional));
        if (st) {
            if (still) StatusDot(p.muted, StrPrintf("%s: %s", TR(Nvof), TR(StaticPreview)).c_str());
            else if (st->srcWidth == 0) StatusDot(p.muted, StrPrintf("%s: %s", TR(Nvof), TR(DepthWaitingSource)).c_str());
            else StatusDot(p.good, StrPrintf("%s: %s (%d %s%s)", TR(Nvof), TR(Available), st->flowLevels, TR(FlowLevels),
                                             s.flowBidirectional ? " \xE2\x87\x84" : "").c_str());
        }
    }
    if (s.motionMode != MotionZero && SliderReset(TR(MotionConfidence), &s.motionConfidence, 0.0f, 1.0f, 0.35f, "%.2f", TR(TipConfidence)))
        ev.settingsChanged = true;
    {
        // Display order puts the estimated depth first; the enum keeps the 0.1.x numbering.
        const char* items[] = { TR(DepthEstimated), TR(DepthFlat), TR(DepthGradient), TR(DepthZero) };
        int sel = (s.depthMode == DepthEstimated) ? 0 : std::clamp(s.depthMode, 0, 2) + 1;
        if (ComboIds(TR(DepthSource), &sel, items, 4, TR(TipDepth))) { s.depthMode = (sel == 0) ? DepthEstimated : sel - 1; ev.settingsChanged = true; }
    }
    if (s.depthMode == DepthEstimated) {
        if (st && st->depthParked) {
            StatusDot(p.muted, StrPrintf("%s: %s", TR(DepthStatus), TR(DepthParked)).c_str());
        } else if (st) {
            switch (st->depthState) {
            case (int)DepthEstimatorState::Ready:
                StatusDot(p.good, StrPrintf("%s: %s  %s %ux%u  %s %5.1f ms", TR(DepthStatus), TR(DepthReady), st->depthBackend.c_str(),
                                            st->depthInferW, st->depthInferH, TR(Inference), m_shown.depthMs).c_str());
                break;
            case (int)DepthEstimatorState::Initializing:
                StatusDot(p.warn, StrPrintf("%s: %s", TR(DepthStatus), TR(DepthInitializing)).c_str());
                break;
            case (int)DepthEstimatorState::Unavailable:
                if (st->depthMessage.empty()) {
                    // The estimator has not started yet (it starts with the first frame and reports a missing model
                    // or runtime itself), so this is no failure.
                    StatusDot(p.muted, StrPrintf("%s: %s", TR(DepthStatus), TR(DepthWaitingSource)).c_str());
                    break;
                }
                [[fallthrough]];
            default:
                StatusDot(p.warn, StrPrintf("%s: %s", TR(DepthStatus), TR(DepthUnavailable)).c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
                if (!st->depthModelExists) ImGui::TextWrapped("%s", TR(DepthModelMissing));
                else if (!st->depthMessage.empty()) ImGui::TextWrapped("%s", st->depthMessage.c_str());
                ImGui::PopStyleColor();
                break;
            }
        }
        if (SliderIntReset(TR(DepthInterval), &s.depthInterval, 1, 10, 4, "%d", TR(TipDepthInterval))) ev.settingsChanged = true;
        {
            static const int kSides[] = { 252, 336, 420, 518 };
            const char* sides[] = { "252 px", "336 px", "420 px", "518 px" };
            int res = 1;
            for (int i = 0; i < 4; ++i) if (s.depthLongSide == kSides[i]) res = i;
            if (ComboIds(TR(DepthResolution), &res, sides, 4, TR(TipDepthResolution))) { s.depthLongSide = kSides[res]; ev.settingsChanged = true; }
        }
        if (SearchMatch(TR(DepthModel), TR(Reload))) {
            // The model file: the field, then browse and reload as icon buttons on the same row.
            SyncBuffer(m_depthModelBuf, sizeof(m_depthModelBuf), s.depthModelPath, m_depthModelEditing);
            const float btnW = ImGui::GetFrameHeight();
            const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - (btnW + gap) * 2.0f);
            const std::string hint = st ? WideToUtf8(st->depthModelPath) : std::string();
            ImGui::InputTextWithHint("##depthmodel", hint.c_str(), m_depthModelBuf, sizeof(m_depthModelBuf));
            FocusRing();
            m_depthModelEditing = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) { s.depthModelPath = m_depthModelBuf; ev.settingsChanged = true; }
            ImGui::SameLine(0.0f, gap);
            if (IconButton("##browsedepth", Icon::Folder, ImVec2(btnW, 0), TR(Browse))) ev.browseDepthModel = true;
            ImGui::SameLine(0.0f, gap);
            if (IconButton("##reloaddepth", Icon::Refresh, ImVec2(btnW, 0), TR(Reload))) ev.reloadDepth = true;
            ImGui::SameLine(0.0f, gap);
            TrailingLabel(TR(DepthModel));
        }
    }
    if (Toggle(TR(AutoReset), &s.autoReset)) ev.settingsChanged = true;
    Help(TR(TipAutoReset));
    if (s.autoReset && SliderReset(TR(CutThreshold), &s.cutThreshold, 0.01f, 0.5f, 0.10f, "%.2f", TR(TipCutThreshold)))
        ev.settingsChanged = true;
    if (st) {
        Readout(m_fonts, TR(FrameCost), StrPrintf("%5.3f  max %5.3f", m_shown.statAvgCost, m_shown.statMaxCost));
        Readout(m_fonts, "|mv|", StrPrintf("%5.2f px", m_shown.statAvgMotion));
        Readout(m_fonts, TR(Resets), StrPrintf("%llu", m_shown.resets));
    }
    ImGui::Spacing();
}

// The NVIDIA runtime rows of the DLSS 5 section: which build is loaded, what failed, the path of the file.
void MainUI::BlockNgxRuntime(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    if (st && st->nrRuntimeLoaded) {
        // The bundled build in use is named after the version (or the file next to the executable).
        if (info.nrRuntimeBuild) StatusDot(p.good, StrPrintf("%s: %s %s \xC2\xB7 %s", TR(Runtime), TR(Loaded), st->nrRuntimeVersion.c_str(), info.nrRuntimeBuild).c_str());
        else StatusDot(p.good, StrPrintf("%s: %s %s", TR(Runtime), TR(Loaded), st->nrRuntimeVersion.c_str()).c_str());
    } else if (st && st->nrRuntimeIdle) {
        StatusDot(p.muted, StrPrintf("%s: %s %s", TR(Runtime), st->nrRuntimeVersion.c_str(), TR(RuntimeIdle)).c_str());
    } else {
        StatusDot(p.warn, StrPrintf("%s: %s", TR(Runtime), TR(NotLoaded)).c_str());
        if (!info.nrRuntimeExists) {
            if (info.adapter && !info.adapter->IsNvidia()) {
                // The bundled builds are not even tried on another vendor's card: say what would run here.
                ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
                ImGui::TextWrapped("%s", TR(NrOtherVendorHint));
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
                ImGui::TextWrapped("%s", TR(RuntimeMissing));
                ImGui::PopStyleColor();
                ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
                ImGui::TextWrapped("%s", TR(RuntimeVariants));
                ImGui::PopStyleColor();
            }
        }
    }
    // A failed feature, or a failed load (the runtime is neither loaded nor resting).
    if (st && !st->nrError.empty() && (st->nrFailed || (!st->nrRuntimeLoaded && !st->nrRuntimeIdle))) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("%s", st->nrError.c_str());
        ImGui::PopStyleColor();
        // The RTX 50 build only carries Blackwell code: say so on an older card instead of leaving a bare NGX code.
        // The build under runtimes\universal\ is the one for those cards, so nothing is added when it is loaded.
        const int gen = info.adapter ? info.adapter->RtxGeneration() : 0;
        const bool universal = st->nrRuntimePath.find(L"\\universal\\") != std::wstring::npos;
        if (gen >= 2 && gen <= 4 && !universal && (st->nrRuntimeVersion.empty() || st->nrRuntimeVersion.rfind("310.8", 0) == 0)) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", TR(NrArchHint));
            ImGui::PopStyleColor();
        }
    }
    if (info.nrRuntimeExhausted) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
        ImGui::TextWrapped("%s", TR(NrAllBuildsFailed));
        ImGui::PopStyleColor();
    }
    if (st && st->nrActive && (st->nrOutState == 2 || (st->nrOutState == 3 && s.nrIntensity > 0.05f))) {
        // The runtime reports success but the output check found a black or unchanged picture.
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("%s", I18n::T(st->nrOutState == 2 ? Str::NrOutBlack : Str::NrOutSame));
        ImGui::PopStyleColor();
    }
    if (SearchMatch(TR(RuntimePath), TR(Reload))) {
        SyncBuffer(m_runtimeBuf, sizeof(m_runtimeBuf), s.nrDllPath, m_runtimeEditing);
        const float btnW = ImGui::GetFrameHeight();
        const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - (btnW + gap) * 2.0f);
        const std::string hint = WideToUtf8(info.nrRuntimePath);
        ImGui::InputTextWithHint("##nrpath", hint.c_str(), m_runtimeBuf, sizeof(m_runtimeBuf));
        FocusRing();
        m_runtimeEditing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { s.nrDllPath = m_runtimeBuf; ev.reloadRuntime = true; ev.settingsChanged = true; }
        ImGui::SameLine(0.0f, gap);
        if (IconButton("##browseruntime", Icon::Folder, ImVec2(btnW, 0), TR(Browse))) ev.browseRuntime = true;
        ImGui::SameLine(0.0f, gap);
        if (IconButton("##reloadruntime", Icon::Refresh, ImVec2(btnW, 0), TR(Reload))) ev.reloadRuntime = true;
        ImGui::SameLine(0.0f, gap);
        TrailingLabel(TR(RuntimePath));
        if (info.nrSetPathMissing) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", TR(RuntimePathMissing));
            ImGui::PopStyleColor();
        }
    }
#if !APP_EDITION_AMD
    // The GeForce edition on a Radeon card: the Radeon edition is the one that runs the FSR host.
    if (info.adapter && info.adapter->IsAmd()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
        ImGui::TextWrapped("%s", TR(EditionHintGeforceOnAmd));
        ImGui::PopStyleColor();
        if (FlatButton(TR(EditionGetAmd))) ev.editionSwitch = true;
        Tip(TR(TipEditionSwitch));
    }
#endif
}

// The FSR host rows: the FSR runtime the app hosts, whether DLSS-NR-on-AMD is attached to the process, and the
// download of its installer (the Radeon edition's one-click install). The installer is that project's own program
// under its own terms; nothing of it is part of this application.
void MainUI::BlockFsrHost(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    const bool fsrLoaded = st && st->nrFsrLoaded;
    const bool portLoaded = st && !st->nrAmdPort.empty();
    if (fsrLoaded) {
        StatusDot(p.good, StrPrintf("%s: %s FSR %s \xC2\xB7 %s", TR(Runtime), TR(Loaded), st->nrFsrVersion.c_str(), TR(FsrHostName)).c_str());
    } else {
        StatusDot(p.warn, StrPrintf("%s: %s \xC2\xB7 %s", TR(Runtime), TR(NotLoaded), TR(FsrHostName)).c_str());
        if (!info.fsrDllExists) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", TR(FsrDllMissing));
            ImGui::PopStyleColor();
#if !APP_EDITION_AMD
            if (FlatButton(TR(EditionGetAmd))) ev.editionSwitch = true;
            Tip(TR(TipEditionSwitch));
#endif
        } else if (st && !st->nrFsrError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
            ImGui::TextWrapped("%s", st->nrFsrError.c_str());
            ImGui::PopStyleColor();
        }
    }
    // The port: loaded, with its release when that is known and whether a newer one is out; or not loaded, with
    // what that means here.
    const PortSetup::Status* ps = info.portSetup;
    const bool latestKnown = ps && !ps->tag.empty();
    const bool newer = latestKnown && !info.portInstalledTag.empty() && ps->tag != info.portInstalledTag;
    if (portLoaded) {
        if (info.portInstalledTag.empty()) {
            StatusDot(p.good, StrPrintf(TR(AmdPortDetected), st->nrAmdPort.c_str()).c_str());
            if (latestKnown) {
                ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
                ImGui::TextWrapped("%s", StrPrintf(TR(AmdPortVersionUnknown), ps->tag.c_str(), ps->date.c_str()).c_str());
                ImGui::PopStyleColor();
            }
        } else {
            StatusDot(p.good, StrPrintf(latestKnown && !newer ? TR(AmdPortLoadedCurrent) : TR(AmdPortLoadedVersion),
                                        info.portInstalledTag.c_str(), st->nrAmdPort.c_str()).c_str());
        }
        if (newer) StatusDot(p.accent, StrPrintf(TR(AmdPortNewer), ps->tag.c_str(), ps->date.c_str()).c_str());
    } else {
        StatusDot(p.warn, TR(AmdPortNotFound));
        if (info.portWeightsExist && !info.portRestartHint) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", TR(AmdPortWeightsNotLoaded));
            ImGui::PopStyleColor();
        } else if (!info.portRestartHint && info.fsrDllExists) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
            ImGui::TextWrapped("%s", TR(AmdPortOffer));
            ImGui::PopStyleColor();
        }
    }
    if (st && st->nrFailed && !st->nrError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("%s", st->nrError.c_str());
        ImGui::PopStyleColor();
    }
    if (st && st->nrActive && (st->nrOutState == 2 || (st->nrOutState == 3 && s.nrIntensity > 0.05f))) {
        // The FSR pass ran, but the picture came back black or unchanged: without the port attached, an unchanged
        // picture is the expected outcome (FSR at native size with zero motion is close to a copy).
        ImGui::PushStyleColor(ImGuiCol_Text, st->nrOutState == 2 ? p.bad : p.warn);
        ImGui::TextWrapped("%s", st->nrOutState == 2 ? TR(NrOutBlack) : (portLoaded ? TR(NrOutSame) : TR(FsrNoEffect)));
        ImGui::PopStyleColor();
    }
    if (info.fsrDllExists) PortActions(info, ev, portLoaded, false);
    if (IconTextButton(TR(Reload), Icon::Refresh)) ev.reloadRuntime = true;   // on its own line: the port's buttons fill the sidebar's width
    ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
    ImGui::TextWrapped("%s", TR(AmdPortRequirements));
    ImGui::PopStyleColor();
#if APP_EDITION_AMD
    // The Radeon edition on a GeForce card: the GeForce edition is the one with the runtime builds.
    if (info.adapter && info.adapter->IsNvidia()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
        ImGui::TextWrapped("%s", TR(EditionHintAmdOnGeforce));
        ImGui::PopStyleColor();
        if (FlatButton(TR(EditionGetGeforce))) ev.editionSwitch = true;
        Tip(TR(TipEditionSwitch));
    }
#endif
}

// The installer's state, the restart after it, and the buttons: install (the first press is the consent), update to
// a newer release, or the installer again (update or removal); its licence and its release page. Shared by the DLSS 5
// section and the start-up card.
void MainUI::PortActions(const UiFrameInfo& info, UiEvents& ev, bool portLoaded, bool card) {
    const Palette& p = Colors();
    const PortSetup::Status* ps = info.portSetup;
    const bool busy = ps && (ps->state == PortSetup::State::Checking || ps->state == PortSetup::State::Downloading || ps->state == PortSetup::State::Installing ||
                            ps->state == PortSetup::State::Launched);
    const bool latestKnown = ps && !ps->tag.empty();
    const bool newer = latestKnown && !info.portInstalledTag.empty() && ps->tag != info.portInstalledTag;
    if (ps) {
        switch (ps->state) {
            case PortSetup::State::Checking: StatusDot(p.muted, TR(AmdPortChecking)); break;
            case PortSetup::State::Downloading: {
                std::string text = StrPrintf(TR(AmdPortDownloading), ps->downloadedMb, ps->totalMb);
                if (!ps->mirrorInUse.empty()) text += " \xC2\xB7 " + ps->mirrorInUse;   // through a mirror site
                StatusDot(p.accent, text.c_str());
                break;
            }
            case PortSetup::State::Installing: StatusDot(p.accent, TR(AmdPortInstalling)); break;
            case PortSetup::State::Launched: StatusDot(p.accent, TR(AmdPortInstallerRunning)); break;
            case PortSetup::State::Failed:
                ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
                ImGui::TextWrapped("%s: %s", TR(AmdPortFailed), ps->error.c_str());
                ImGui::PopStyleColor();
                break;
            default: break;
        }
    }
    if (info.portRestartHint) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.good);
        if (info.portRestartIn >= 0) ImGui::TextWrapped("%s", StrPrintf(TR(AmdPortRestartIn), info.portRestartIn).c_str());
        else ImGui::TextWrapped("%s", TR(AmdPortRestartHint));
        ImGui::PopStyleColor();
        if (AccentButton(TR(RestartNow))) ev.restartApp = true;
        if (info.portRestartIn >= 0) { SameLineIfFits(TR(Cancel)); if (FlatButton(TR(Cancel))) ev.portRestartCancel = true; }
        return;
    }
    ImGui::BeginDisabled(busy);
    if (!portLoaded && !info.portWeightsExist) {
        if (card ? AccentButton(TR(AmdPortInstall)) : FlatButton(TR(AmdPortInstall))) ev.portInstall = true;
        Tip(TR(TipAmdPortInstall));
    } else if (newer) {
        if (AccentButton(StrPrintf(TR(AmdPortUpdateTo), ps->tag.c_str()).c_str())) ev.portInstall = true;
        Tip(TR(TipAmdPortInstall));
    } else {
        if (FlatButton(TR(AmdPortRerun))) ev.portInstall = true;
        Tip(TR(TipAmdPortRerun));
    }
    ImGui::EndDisabled();
    SameLineIfFits(TR(AmdPortLicense));
    if (FlatButton(TR(AmdPortLicense))) ev.portOpenLicense = true;
    SameLineIfFits(TR(AmdPortPage));
    if (FlatButton(TR(AmdPortPage))) ev.portOpenPage = true;
}

void MainUI::BlockDlaa(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    const bool available = st && st->ngxInitialized && st->dlssAvailable;
    const bool inSr = st && st->upscaleMode == 1;   // DLSS super resolution in effect: it includes the anti-aliasing
    ImGui::BeginDisabled(!available || inSr);
    if (Toggle(TR(DlaaEnable), &s.dlaaEnabled)) { ev.dlaaChanged = true; ev.settingsChanged = true; }
    ImGui::EndDisabled();
    if (st) {
        if (st->dlaaActive) PillAfter(TR(Active), WithAlpha(p.good, 0.18f), p.good, Px(12.0f));
        else if (st->dlaaFailed) PillAfter(TR(Failed), WithAlpha(p.bad, 0.18f), p.bad, Px(12.0f));
        else if (!available) PillAfter(TR(Unsupported), WithAlpha(p.muted, 0.2f), p.muted, Px(12.0f));
        else PillAfter(TR(Inactive), WithAlpha(p.muted, 0.2f), p.muted, Px(12.0f));
    }
    Hint(inSr ? TR(DlaaInSr) : (st && st->dlaaTooLarge) ? TR(DlaaTooLarge) : TR(DlaaHint));
    if (st && st->dlaaFailed && !st->dlaaError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("%s", st->dlaaError.c_str());
        ImGui::PopStyleColor();
    }
    {
        // Every preset the NGX interface names, bound to the value the runtime receives as it is: which letters a
        // runtime build honours is its own business (the tip says what the bundled one does), and a swapped-in
        // runtime may differ. The value is never re-mapped here: an earlier build turned any letter outside its
        // list into K before the runtime saw it, which looked like the preset could not be chosen.
        static const char* presets[] = { "Default", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O" };
        ImGui::BeginDisabled(!available);
        if (ComboIds(TR(DlaaPreset), &s.dlaaPreset, presets, 16, TR(TipDlaaPreset))) { ev.dlaaChanged = true; ev.settingsChanged = true; }
        ImGui::EndDisabled();
    }
    if (info.status) Readout(m_fonts, TR(GpuTime), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Dlaa]));
    ImGui::Spacing();
}

void MainUI::BlockInternals(Settings& /*s*/, const UiFrameInfo& info, UiEvents& /*ev*/) {
    if (!info.status) return;
    ImGui::TextDisabled("%s:", TR(Timers));
    const struct { GpuTimer t; const char* name; } timers[] = {
        { GpuTimer::Convert, TR(TmConvert) }, { GpuTimer::Guidance, TR(TmGuidance) }, { GpuTimer::OpticalFlow, TR(TmOpticalFlow) },
        { GpuTimer::Dlaa, TR(TmDlaa) }, { GpuTimer::Neural, TR(TmNeural) }, { GpuTimer::Composite, TR(TmComposite) },
    };
    // The name at the left, the figure flush right in the monospace font, so the figures line up at the decimal point.
    // The figures' room is fixed: a figure that changes never moves anything, and a name longer than what is left wraps.
    ImFont* mono = m_fonts ? m_fonts->Mono() : nullptr;
    ImGui::PushFont(mono, 0.0f);
    const float valueW = ImGui::CalcTextSize("000.00 ms").x;
    ImGui::PopFont();
    const float x0 = ImGui::GetCursorPosX();
    const float rowW = ImGui::GetContentRegionAvail().x;
    const float nameEnd = std::max(x0 + rowW - valueW - ImGui::GetStyle().ItemSpacing.x, x0 + 1.0f);
    auto row = [&](const char* name, double ms) {
        const float y = ImGui::GetCursorPosY();
        ImGui::PushTextWrapPos(nameEnd);
        ImGui::TextDisabled("%s", name);
        ImGui::PopTextWrapPos();
        const float next = ImGui::GetCursorPosY();
        const std::string value = FormatMsFixed(ms);
        ImGui::PushFont(mono, 0.0f);
        ImGui::SetCursorPos(ImVec2(std::max(x0, x0 + rowW - ImGui::CalcTextSize(value.c_str()).x), y));
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopFont();
        ImGui::SetCursorPosY(std::max(next, ImGui::GetCursorPosY()));
    };
    for (const auto& t : timers) row(t.name, m_shown.gpuMs[(UINT)t.t]);
    row(TR(TmUi), m_shown.uiGpuMs);
    row(StrPrintf("%s CPU", TR(UiFps)).c_str(), m_shown.cpuMs);
    ImGui::Spacing();
}

void MainUI::BlockAbout(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    {
        // The program's mark, then its name and version (and the badges) centred on it.
        const float logoS = std::round(ImGui::GetFrameHeight() * 1.15f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        DrawLogo(ImGui::GetWindowDrawList(), at, logoS);
        ImGui::Dummy(ImVec2(logoS, logoS));
        ImGui::SameLine(0.0f, Px(10.0f));
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, at.y + std::round((logoS - ImGui::GetTextLineHeight()) * 0.5f)));
        ImGui::PushFont(fonts.Bold(), 0.0f);
        ImGui::Text("%s %s", TR(AppTitle), info.appVersion.c_str());
        ImGui::PopFont();
    }
    if (info.prerelease) PillAfter(TR(Prerelease), WithAlpha(Colors().accent, 0.2f), Colors().accentHover, Px(8.0f));
    if (APP_EDITION_AMD) PillAfter(TR(EditionAmd), WithAlpha(Colors().accent, 0.2f), Colors().accentHover, Px(8.0f));
    Hint(TR(AboutText));
    if (info.adapter) {
        ImGui::TextDisabled("%s:", TR(Gpu)); ImGui::SameLine(); ImGui::TextWrapped("%s", WideToUtf8(info.adapter->name).c_str());
        const std::wstring& drv = info.adapter->nvidiaDriverVersion.empty() ? info.adapter->driverVersion : info.adapter->nvidiaDriverVersion;
        ImGui::TextDisabled("%s:", TR(Driver)); ImGui::SameLine(); ImGui::TextUnformatted(WideToUtf8(drv).c_str());
    }
    if (info.status) {
        ImGui::TextDisabled("%s:", TR(NgxStatus)); ImGui::SameLine(); ImGui::TextUnformatted(info.status->ngxStatus.c_str());
        ImGui::TextDisabled("%s:", TR(Nvof)); ImGui::SameLine();
        ImGui::TextUnformatted(info.status->nvofAvailable ? TR(Available) : (info.adapter && !info.adapter->IsNvidia()) ? TR(NvofNoEngine) : TR(NotAvailable));
    }
    // Updates: whether to look at every start, which channel, a check by hand and the result of the last one.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImGui::Spacing();
    if (Toggle(TR(UpdateAuto), &s.updateCheck)) ev.settingsChanged = true;
    {
        const char* channels[] = { TR(ChannelStable), TR(ChannelPreview) };
        if (ComboIds("##updateChannel", &s.updateChannel, channels, 2)) { ev.settingsChanged = true; ev.updateCheckNow = true; }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(TR(UpdateChannel));
        Help(TR(TipUpdateChannel));
    }
    // GitHub access: directly, or through a mirror site where GitHub is slow or unreachable.
    ImGui::Spacing();
    ImGui::TextUnformatted(TR(GithubAccess));
    Help(TR(MirrorWhy));
    MirrorControls(s, info, ev, fullW, false);
    ImGui::Spacing();
    {
        const int st = info.updateState;
        const bool busy = st == UpChecking || st == UpDownloading || st == UpExtracting || st == UpRestarting;
        const char* updateNow = info.updateDowngrade ? TR(UpdateDowngradeNow) : TR(UpdateNow);
        const ImVec2 pair = st == UpAvailable ? PairSize(TR(UpdateCheckNow), updateNow, fullW) : ImVec2(fullW, 0.0f);
        ImGui::BeginDisabled(busy);
        if (ActionButton(TR(UpdateCheckNow), Icon::Refresh, pair)) ev.updateCheckNow = true;
        ImGui::EndDisabled();
        if (st == UpAvailable) {
            if (pair.x < fullW) ImGui::SameLine(0.0f, style.ItemSpacing.x);
            if (ActionButton(updateNow, Icon::Download, pair, ButtonKind::Accent)) m_updateOpen = true;
        }
        const Palette& p = Colors();
        if (st == UpChecking) ImGui::TextDisabled("%s", TR(UpdateChecking));
        else if (st == UpUpToDate) ImGui::TextDisabled("%s", StrPrintf(TR(UpdateUpToDate), info.appVersion.c_str()).c_str());
        else if (st == UpAvailable) {
            if (info.updatePrerelease) Pill(TR(ChannelPreview), WithAlpha(p.warn, 0.2f), p.warn);
            else Pill(TR(ReleaseFull), WithAlpha(p.good, 0.2f), p.good);
            ImGui::SameLine(0.0f, Px(6.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, p.accentHover); ImGui::TextWrapped("%s", StrPrintf(info.updateDowngrade ? TR(UpdateDowngradeFmt) : TR(UpdateVersionFmt), info.updateVersion.c_str(), info.appVersion.c_str()).c_str()); ImGui::PopStyleColor();
        }
        else if (st == UpFailed) { ImGui::PushStyleColor(ImGuiCol_Text, p.bad); ImGui::TextWrapped("%s", info.updateWritable ? StrPrintf(TR(UpdateCheckFailed), info.updateError.c_str()).c_str() : TR(UpdateNotWritable)); ImGui::PopStyleColor(); }
    }
    ImGui::Spacing();
    // Rows of two equal buttons (a pair whose names do not fit half the row goes one above the other) and the reset
    // across the full width, all the same height.
    const ImVec2 row1 = PairSize(TR(OpenLogFile), TR(OpenSettingsFolder), fullW);
    if (ActionButton(TR(OpenLogFile), Icon::Terminal, row1)) ev.openLogFile = true;
    if (row1.x < fullW) ImGui::SameLine(0.0f, style.ItemSpacing.x);
    if (ActionButton(TR(OpenSettingsFolder), Icon::Folder, row1)) ev.openSettingsFolder = true;
    const ImVec2 row2 = PairSize(TR(Documentation), TR(ProjectPage), fullW);
    if (ActionButton(TR(Documentation), Icon::Help, row2)) ev.openDocs = true;
    Spotlight(false);
    if (row2.x < fullW) ImGui::SameLine(0.0f, style.ItemSpacing.x);
    if (ActionButton(TR(ProjectPage), Icon::OpenExternal, row2)) ev.openProjectPage = true;
    const ImVec2 row3 = PairSize(TR(GuideTitle), TR(Licenses), fullW);
    if (ActionButton(TR(GuideTitle), Icon::Wand, row3)) m_guideOpen = true;
    Spotlight(false);
    if (row3.x < fullW) ImGui::SameLine(0.0f, style.ItemSpacing.x);
    if (ActionButton(TR(Licenses), Icon::Shield, row3)) ev.openLicenses = true;
    if (ActionButton(TR(ResetAllSettings), Icon::Reset, ImVec2(fullW, 0.0f))) ImGui::OpenPopup("##resetall");
    if (BeginPopupFade("##resetall")) {
        const float bw = ImGui::GetFontSize() * 7.5f;
        ImGui::TextUnformatted(TR(ResetAllSettings));
        ImGui::Separator();
        if (DangerButton(TR(Ok), ImVec2(bw, 0))) { ev.resetDefaults = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (FlatButton(TR(Cancel), ImVec2(bw, 0))) ImGui::CloseCurrentPopup();
        EndPopupFade();
    }
    ImGui::Spacing();
}

// ------------------------------------------------------------------------------------------
// Preview: the picture, the video controls under it and the media library at the bottom.

void MainUI::DrawPreview(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 region = ImGui::GetContentRegionAvail();
    if (region.x < 8.0f || region.y < 8.0f) return;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float frameH = ImGui::GetFrameHeight();
    const float em = ImGui::GetFontSize();
    const float lineH = ImGui::GetTextLineHeight();
    const bool transport = s.sourceMode == SourceVideo && info.videoLoaded;
    const float gap = Px(8.0f);                 // between the picture and the video controls
    const float transportH = transport ? TransportHeight() : 0.0f;
    const float transportRoom = transport ? transportH + gap : 0.0f;
    const float barH = Px(16.0f);               // the gutter above the library: the sidebar's, laid flat
    // The library's height is the user's (dragged at the line under its gutter); the thumbnails take what is left
    // after the header, the names under them and the scrollbar. The picture keeps at least a third of the column.
    const ImVec2 libPad = LibraryPad();
    const float fixedH = libPad.y * 2.0f + frameH + style.ItemSpacing.y + Px(4.0f) * 2.0f + Px(6.0f) + lineH * 2.0f + style.ScrollbarSize;
    const float libraryClosedH = libPad.y * 2.0f + frameH;
    const float libraryMinH = fixedH + em * 3.0f;
    const float pictureMin = std::max(Px(120.0f), region.y * 0.35f);
    const float libraryMaxH = std::max(libraryMinH, std::min((region.y - transportH) * 0.7f, region.y - transportRoom - barH - pictureMin));
    const float libraryWantH = std::clamp(s.libraryHeight > 0.0f ? s.libraryHeight * em : fixedH + em * 4.5f, libraryMinH, libraryMaxH);
    const float libraryOpenH = std::max(libraryClosedH, std::min(libraryWantH, region.y - transportRoom - barH - Px(60.0f)));
    m_thumbH = std::max(Px(16.0f), libraryOpenH - fixedH);
    m_libraryFold = Ease(AnimateLinear(ImGui::GetID("##libraryFold"), s.libraryVisible ? 1.0f : 0.0f, 0.2f));
    // An empty library is its header row alone (the header and the welcome page say how to fill it): it opens to its
    // height with the first file, and its gutter has nothing to fold until then.
    const bool libraryEmpty = !info.library || info.library->empty();
    m_libraryFill = Ease(AnimateLinear(ImGui::GetID("##libraryFill"), libraryEmpty ? 0.0f : 1.0f, 0.2f));
    const float libraryH = libraryClosedH + (libraryOpenH - libraryClosedH) * m_libraryFold * m_libraryFill;
    const float pictureH = std::max(Px(40.0f), region.y - transportRoom - barH - libraryH);

    // The mode fade covers the picture's card; the video controls and the library dip their own content with it.
    m_previewMin = origin;
    m_previewMax = ImVec2(origin.x + region.x, origin.y + pictureH);

    DrawPicture(s, info, ev, fonts, origin, ImVec2(region.x, pictureH));
    float y = origin.y + pictureH;
    if (transport) {
        y += gap;
        DrawTransport(s, info, ev, fonts, ImVec2(origin.x, y), ImVec2(region.x, transportH));
        y += transportH;
    }
    {
        // The gutter above the library, with the same two zones as the sidebar's: the gutter folds the library, the
        // line along its lower edge (the one a drag moves) changes its height.
        const Palette& p = Colors();
        const ImVec2 bpos(origin.x, y);
        const bool canResize = s.libraryVisible && m_libraryFold > 0.99f && m_libraryFill > 0.99f;
        const float gripH = canResize ? Px(kBarGrip) : 0.0f;
        ImGui::SetCursorScreenPos(bpos);
        ImGui::BeginDisabled(libraryEmpty);
        ImGui::InvisibleButton("##libraryFold", ImVec2(region.x, barH - gripH));
        ImGui::EndDisabled();
        const bool foldLit = !libraryEmpty && (ImGui::IsItemHovered() || ImGui::IsItemActive());
        if (foldLit) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (!libraryEmpty && ImGui::IsItemDeactivated() && ImGui::IsItemHovered()) { s.libraryVisible = !s.libraryVisible; ev.settingsChanged = true; }
        if (!libraryEmpty) Tip(s.libraryVisible ? TR(HideLibrary) : TR(ShowLibrary));
        bool gripLit = false;
        if (gripH > 0.0f) {
            ImGui::SetCursorScreenPos(ImVec2(bpos.x, bpos.y + barH - gripH));
            ImGui::InvisibleButton("##libraryResize", ImVec2(region.x, gripH));
            if (ImGui::IsItemActivated()) m_libraryDragH = libraryOpenH;
            if (ImGui::IsItemActive()) {
                const float dy = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y;
                s.libraryHeight = std::clamp(m_libraryDragH - dy, libraryMinH, libraryMaxH) / em;
            }
            if (ImGui::IsItemDeactivated()) ev.settingsChanged = true;
            gripLit = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (gripLit) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (!ImGui::IsItemActive()) Tip(TR(TipLibraryResize));
        }
        const float hov = Animate(ImGui::GetID("##libraryBarHover"), foldLit ? 1.0f : 0.0f, 16.0f);
        const float grip = Animate(ImGui::GetID("##libraryGripHover"), gripLit ? 1.0f : 0.0f, 16.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (m_libraryFill > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * m_libraryFill);
            DrawGutter(dl, bpos, ImVec2(region.x, barH - gripH), hov, IM_PI * (1.0f - m_libraryFold));
            ImGui::PopStyleVar();
        }
        if (canResize) {   // the line a drag moves: always drawn so it can be found, blue under the mouse
            const float h = std::max(1.0f, Px(1.0f)) + Px(2.0f) * grip;
            const float ly = bpos.y + barH - Px(3.0f);
            dl->AddRectFilled(ImVec2(bpos.x + CardRounding(), ly - h * 0.5f), ImVec2(bpos.x + region.x - CardRounding(), ly + h * 0.5f),
                              Mix(p.cardBorder, p.accent, grip), h * 0.5f);
        }
        y += barH;
    }
    const float remaining = origin.y + region.y - y;
    if (remaining <= 8.0f) return;
    DrawLibrary(s, info, ev, fonts, ImVec2(origin.x, y), ImVec2(region.x, remaining));
}

void MainUI::DrawPicture(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& origin, const ImVec2& region) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const double now = ImGui::GetTime();
    ImGui::SetCursorScreenPos(origin);
    // In the window the picture lies on a card in the preview's background colour; fullscreen it fills the screen.
    const bool framed = !info.fullscreen;
    const float rounding = framed ? CardRounding() : 0.0f;
    const ImVec2 rmax(origin.x + region.x, origin.y + region.y);
    if (framed) dl->AddRectFilled(origin, rmax, p.surface, rounding);
    // The card's rim goes on last, over the picture: the corners masked in the window's colour, then the hairline and
    // the soft shadow outside (drawn with the clipping lifted).
    auto drawRim = [&]() {
        if (!framed) return;
        dl->PushClipRectFullScreen();
        MaskCorners(dl, origin, rmax, rounding, ImGui::ColorConvertFloat4ToU32(p.window));
        DrawCardEdge(dl, origin, rmax);
        dl->PopClipRect();
    };

    if (!info.hasDisplay || !info.displayTexture || !info.displayWidth || !info.displayHeight) {
        const bool loadedSomething = (s.sourceMode == SourceVideo && info.videoLoaded) || (s.sourceMode == SourceImage && info.imageLoaded)
                                  || (s.sourceMode == SourceSpout && info.sourceConnected);
        if (loadedSomething) {
            // Something is open but no frame has come yet: a spinner while one is on its way (a seek, the first
            // passes), plain words once it has taken long.
            AnimateSnap(ImGui::GetID("##welcomeFade"), 0.0f);
            if (m_loadingSince < 0.0) m_loadingSince = now;
            const bool seeking = s.sourceMode == SourceVideo && (info.videoSeeking || m_seekTarget >= 0.0);
            const bool waiting = seeking || now - m_loadingSince < 10.0;
            const char* text = seeking ? TR(Seeking) : waiting ? TR(PreparingPicture) : TR(NoDisplay);
            const float wrap = std::max(Px(80.0f), std::min(region.x - Px(40.0f), ImGui::GetFontSize() * 28.0f));
            const ImVec2 ts = ImGui::CalcTextSize(text, nullptr, false, wrap);
            const float spin = waiting ? IconSize(1.6f) : 0.0f;
            const float spinGap = waiting ? Px(10.0f) : 0.0f;
            const float top = origin.y + std::floor((region.y - spin - spinGap - ts.y) * 0.5f);
            dl->PushClipRect(origin, rmax, true);
            if (waiting) DrawSpinner(dl, ImVec2(origin.x + std::round(region.x * 0.5f), top + spin * 0.5f), spin, p.accent);
            dl->AddText(nullptr, 0.0f, ImVec2(origin.x + std::floor((region.x - ts.x) * 0.5f), top + spin + spinGap), p.textDim, text, nullptr, wrap);
            dl->PopClipRect();
            ImGui::Dummy(region);
            drawRim();
            FullscreenButton(info, ev, origin, region);
            return;
        }
        m_loadingSince = -1.0;
        DrawWelcome(s, info, ev, fonts, origin, region);
        drawRim();
        return;
    }
    AnimateSnap(ImGui::GetID("##welcomeFade"), 0.0f);   // the welcome fades in again the next time nothing is open
    m_loadingSince = -1.0;

    // Image rectangle: fitted to the view or 1:1, times the manual magnification (wheel, slider; double-click resets).
    // In the wipe compare the display holds the original and the output side by side; each half is the picture.
    const float texW = (float)(info.displayWide ? info.displayWidth / 2 : info.displayWidth), texH = (float)info.displayHeight;
    const float fitScale = std::min(region.x / texW, region.y / texH);   // the whole picture within the view
    m_baseScale = (s.fitMode == FitWindow) ? fitScale : 1.0f;
    // The magnification may always come down to the fitted view: a portrait 8K picture fits a small window only
    // below kZoomMin, and a floor at kZoomMin left it too large for the view with nothing to zoom out to.
    m_zoomFloor = std::min(kZoomMin, fitScale);
    const float zoomMin = m_zoomFloor / m_baseScale, zoomMax = kZoomMax / m_baseScale;
    m_zoomTarget = std::clamp(m_zoomTarget, zoomMin, zoomMax);
    m_zoom = std::clamp(m_zoom, zoomMin, zoomMax);
    const ImVec2 centre(origin.x + region.x * 0.5f, origin.y + region.y * 0.5f);
    auto imageSize = [&]() { return ImVec2(texW * m_baseScale * m_zoom, texH * m_baseScale * m_zoom); };
    auto imagePos = [&](const ImVec2& size) {
        return ImVec2(origin.x + (region.x - size.x) * 0.5f + m_pan.x, origin.y + (region.y - size.y) * 0.5f + m_pan.y);
    };

    ImGui::SetNextItemAllowOverlap();   // the corner button and the fullscreen transport bar sit on the canvas
    ImGui::InvisibleButton("##canvas", region, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    // Pan with the left or middle button, zoom with the wheel around the cursor, double-click to return to the fit.
    // The wipe handle, when it is being dragged, takes priority.
    const bool leftFree = !m_cropEditing;   // while a crop is adjusted the left button belongs to its rectangle
    if (!m_wipeDragging) {
        if (active && ((leftFree && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
            m_pan.x += io.MouseDelta.x; m_pan.y += io.MouseDelta.y;
        }
        if (hovered && io.MouseWheel != 0.0f) {
            float t = std::clamp(m_zoomTarget * (io.MouseWheel > 0 ? 1.25f : 0.8f), zoomMin, zoomMax);
            if (std::fabs(t - 1.0f) < 0.06f) t = 1.0f;   // snaps back to the fitted view
            m_zoomTarget = t;
            m_zoomAnchor = io.MousePos;   // the picture point under the cursor stays put while the zoom moves
            m_panHome = false;
        }
        if (hovered && leftFree && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { ResetView(true); m_zoomAnchor = centre; }
        if (active && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) m_panHome = false;
    }
    // The zoom glides toward its target; the pan follows so the anchor point keeps its place on the screen.
    if (m_zoom != m_zoomTarget) {
        const float old = m_zoom;
        const float d = m_zoomTarget - m_zoom;
        m_zoom = std::fabs(d) < 0.0015f * m_zoom ? m_zoomTarget : m_zoom + d * (1.0f - std::exp(-20.0f * std::min(io.DeltaTime, 0.05f)));
        const float k = m_zoom / old;
        m_pan.x = (m_pan.x + centre.x - m_zoomAnchor.x) * k + m_zoomAnchor.x - centre.x;
        m_pan.y = (m_pan.y + centre.y - m_zoomAnchor.y) * k + m_zoomAnchor.y - centre.y;
        MarkAnimating();
    }
    if (m_panHome) {
        const float f = std::exp(-20.0f * std::min(io.DeltaTime, 0.05f));
        m_pan.x *= f; m_pan.y *= f;
        if (std::fabs(m_pan.x) < 0.5f && std::fabs(m_pan.y) < 0.5f) { m_pan = ImVec2(0, 0); m_panHome = false; }
        MarkAnimating();
    }
    // The picture stays within reach: centred while it is smaller than the view, and never leaving a gap on a side
    // once it is larger.
    const ImVec2 imgSize = imageSize();
    const float slackX = std::max(0.0f, (imgSize.x - region.x) * 0.5f), slackY = std::max(0.0f, (imgSize.y - region.y) * 0.5f);
    m_pan.x = std::clamp(m_pan.x, -slackX, slackX);
    m_pan.y = std::clamp(m_pan.y, -slackY, slackY);
    const ImVec2 imgPos = imagePos(imgSize);
    const ImVec2 imgMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);

    // Wipe handle. The split is drawn here from the two halves of the display, so it follows the mouse at the
    // interface's rate whatever the processing rate is.
    if (s.compareMode == CompareWipe) {
        const float wipeX = imgPos.x + imgSize.x * s.wipePosition;
        const bool nearHandle = hovered && leftFree && std::fabs(io.MousePos.x - wipeX) < Px(8.0f) && io.MousePos.y >= imgPos.y && io.MousePos.y <= imgMax.y;
        if (nearHandle && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_wipeDragging = true;
        if (m_wipeDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_wipeDragging = false;
        if (nearHandle || m_wipeDragging) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (m_wipeDragging && imgSize.x > 0.0f) {
            const float v = std::clamp((io.MousePos.x - imgPos.x) / imgSize.x, 0.0f, 1.0f);
            if (v != s.wipePosition) { s.wipePosition = v; ev.settingsChanged = true; }
        }
    } else {
        m_wipeDragging = false;
    }
    if (active && leftFree && !m_wipeDragging && (slackX > 0.0f || slackY > 0.0f)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    dl->PushClipRect(origin, rmax, true);
    const ImTextureRef tex(info.displayTexture);
    if (s.compareMode == CompareWipe && info.displayWide) {
        const float wipeX = imgPos.x + imgSize.x * s.wipePosition;
        dl->PushClipRect(imgPos, ImVec2(wipeX, imgMax.y), true);
        dl->AddImage(tex, imgPos, imgMax, ImVec2(0, 0), ImVec2(0.5f, 1), IM_COL32_WHITE);
        dl->PopClipRect();
        dl->PushClipRect(ImVec2(wipeX, imgPos.y), imgMax, true);
        dl->AddImage(tex, imgPos, imgMax, ImVec2(0.5f, 0), ImVec2(1, 1), IM_COL32_WHITE);
        dl->PopClipRect();
    } else {
        dl->AddImage(tex, imgPos, imgMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    }
    if (s.compareMode == CompareWipe) {
        // The split line, its handle kept within the part of the picture that shows, and the names of the two sides
        // at the bottom corners of that part.
        const float wipeX = std::round(imgPos.x + imgSize.x * s.wipePosition);
        const float visL = std::max(imgPos.x, origin.x), visR = std::min(imgMax.x, rmax.x);
        const float visT = std::max(imgPos.y, origin.y), visB = std::min(imgMax.y, rmax.y);
        if (visB > visT) {
            dl->AddLine(ImVec2(wipeX, visT), ImVec2(wipeX, visB), IM_COL32(255, 255, 255, 220), std::max(1.0f, Px(2.0f)));
            if (visB - visT > Px(24.0f)) {
                const float hy = std::clamp(imgPos.y + imgSize.y * 0.5f, visT + Px(12.0f), visB - Px(12.0f));
                dl->AddCircleFilled(ImVec2(wipeX, hy), Px(9.0f), IM_COL32(255, 255, 255, 235));
                dl->AddCircleFilled(ImVec2(wipeX, hy), Px(6.0f), p.accent);
            }
            ImGui::PushFont(fonts.Bold(), 0.0f);
            const float ty = visB - ImGui::GetFontSize() - Px(8.0f);
            ShadowText(dl, ImVec2(visL + Px(10.0f), ty), IM_COL32(255, 255, 255, 220), TR(CompareOriginal));
            const float ow = ImGui::CalcTextSize(TR(Output)).x;
            ShadowText(dl, ImVec2(visR - ow - Px(10.0f), ty), IM_COL32(255, 255, 255, 220), TR(Output));
            ImGui::PopFont();
        }
    }
    // Overlay.
    if (s.showOverlay && info.status) {
        const PipelineStatus& st = *info.status;
        // Four lines of readouts. On a preview too narrow for a line it wraps between its readouts, so none is cut
        // at the edge of the picture.
        std::vector<std::string> parts[4];
        const char* seps[4] = { "  ", "  ", "   ", "   " };
        parts[0] = { StrPrintf("%s %ux%u  \xE2\x86\x92", TR(Source), st.srcWidth, st.srcHeight), StrPrintf("%s %ux%u", TR(Output), st.outWidth, st.outHeight) };
        if (st.nrActive) parts[1] = { StrPrintf("DLSS 5: %s", TR(Active)), StrPrintf("(%s %d, %.2f)", TR(Style), s.nrStyle, s.nrIntensity) };
        else parts[1] = { StrPrintf("DLSS 5: %s", st.nrFailed ? TR(Failed) : TR(Bypass)) };
        const char* motion = st.motionModeActive == MotionNvOpticalFlow ? "NVOF"
                           : st.motionModeActive == MotionFsrFlow ? "FSR"
                           : st.motionModeActive == MotionCompute ? TR(MotionCompute) : TR(MotionZero);
        const char* depth = st.depthModeActive == DepthEstimated ? "Depth Anything V2" : st.depthModeActive == DepthGradient ? TR(DepthGradient)
                          : st.depthModeActive == DepthZero ? TR(DepthZero) : TR(DepthFlat);
        parts[2] = { StrPrintf("%s: %s", TR(MotionSource), motion), StrPrintf("%s: %s", TR(DepthSource), depth) };
        if (st.upscaleMode == 1) parts[2].push_back("+DLSS SR");
        else if (st.dlaaActive) parts[2].push_back("+DLAA");
        if (st.sceneCut) parts[2].push_back(StrPrintf("[%s]", TR(SceneCut)));
        parts[3] = { StrPrintf("GPU %s", FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Frame]).c_str()) };
        if (s.sourceMode != SourceImage) parts[3].push_back(StrPrintf("%s %3.0f %s", TR(ProcessingFps), m_shown.processingFps, TR(Fps)));
        parts[3].push_back(StrPrintf("%s %3.0f %s", TR(UiFps), m_shown.fps, TR(Fps)));
        if (s.sourceMode == SourceImage) parts[3].push_back(info.imageName);
        else if (s.sourceMode == SourceVideo) parts[3].push_back(info.videoName);
        else parts[3].push_back(StrPrintf("%s %3.0f %s", TR(SenderFps), m_shown.senderFps, TR(Fps)));
        // Clear of the buttons in the top right corner (fullscreen, the magnification): below them on a preview too
        // narrow for both on one line.
        const float fh = ImGui::GetFrameHeight();
        float buttonsLeft = origin.x + region.x - Px(12.0f) - fh;
        if (s.fitMode == FitOneToOne || std::fabs(m_zoom - 1.0f) > 1e-3f) {
            ImGui::PushFont(fonts.Mono(), 0.0f);
            buttonsLeft -= Px(8.0f) + ImGui::CalcTextSize(StrPrintf("%s 800%%", TR(Zoom)).c_str()).x + Px(12.0f) * 2.0f;
            ImGui::PopFont();
        }
        ImGui::PushFont(fonts.Mono(), style.FontSizeBase * 0.92f);
        const float lh = ImGui::GetFontSize() + Px(3.0f);
        const ImVec2 pad(Px(10.0f), Px(8.0f));
        const float maxW = std::max(Px(40.0f), region.x - Px(24.0f) - pad.x * 2.0f);
        struct Row { std::string text; float w; bool good; };
        std::vector<Row> rows;
        for (int i = 0; i < 4; ++i) {
            const float sepW = ImGui::CalcTextSize(seps[i]).x;
            bool first = true;
            for (const std::string& part : parts[i]) {
                if (part.empty()) continue;
                const float pw = ImGui::CalcTextSize(part.c_str()).x;
                if (!first && rows.back().w + sepW + pw <= maxW) {
                    rows.back().text += seps[i];
                    rows.back().text += part;
                    rows.back().w += sepW + pw;
                } else {
                    rows.push_back({ part, pw, i == 1 && st.nrActive });
                }
                first = false;
            }
        }
        for (Row& r : rows) {   // a single part wider than the preview (a long file name) ends in an ellipsis
            if (r.w <= maxW) continue;
            const char* ellipsis = "\xE2\x80\xA6";
            while (!r.text.empty() && ImGui::CalcTextSize((r.text + ellipsis).c_str()).x > maxW) {
                while (!r.text.empty() && ((unsigned char)r.text.back() & 0xC0) == 0x80) r.text.pop_back();
                if (!r.text.empty()) r.text.pop_back();
            }
            r.text += ellipsis;
            r.w = ImGui::CalcTextSize(r.text.c_str()).x;
        }
        float w = 0.0f;
        for (const Row& r : rows) w = std::max(w, r.w);
        ImVec2 bpos(origin.x + Px(12.0f), origin.y + Px(12.0f));
        if (bpos.x + w + pad.x * 2.0f > buttonsLeft - Px(8.0f)) bpos.y += fh + Px(8.0f);
        // On a low preview only the lines with room above the picture tool row (where it was drawn last frame) show.
        float bottom = origin.y + region.y - Px(8.0f);
        if (m_toolRowMax.y > m_toolRowMin.y && m_toolRowMin.y > bpos.y && m_toolRowMin.x < bpos.x + w + pad.x * 2.0f && m_toolRowMax.x > bpos.x)
            bottom = std::min(bottom, m_toolRowMin.y - Px(6.0f));
        const float fit = std::floor((bottom - bpos.y - pad.y * 2.0f + Px(3.0f)) / lh);
        if ((float)rows.size() > fit) {
            rows.resize((size_t)std::max(0.0f, fit));
            w = 0.0f;
            for (const Row& r : rows) w = std::max(w, r.w);
        }
        if (!rows.empty())
            dl->AddRectFilled(bpos, ImVec2(bpos.x + w + pad.x * 2.0f, bpos.y + lh * (float)rows.size() - Px(3.0f) + pad.y * 2.0f), p.overlayBg, Px(6.0f));
        for (size_t i = 0; i < rows.size(); ++i)
            dl->AddText(ImVec2(bpos.x + pad.x, bpos.y + pad.y + lh * (float)i), rows[i].good ? p.good : p.text, rows[i].text.c_str());
        ImGui::PopFont();
    }
    if (s.fitMode == FitOneToOne || std::fabs(m_zoom - 1.0f) > 1e-3f) {
        // The magnification left of the fullscreen button, in a box as wide as the widest figure so it keeps still.
        ImGui::PushFont(fonts.Mono(), 0.0f);
        const std::string z = StrPrintf("%s %3.0f%%", TR(Zoom), m_baseScale * m_zoom * 100.0f);
        const float boxW = ImGui::CalcTextSize(StrPrintf("%s 800%%", TR(Zoom)).c_str()).x + Px(12.0f) * 2.0f;
        const float fh = ImGui::GetFrameHeight();
        const float right = rmax.x - Px(12.0f) - fh - Px(8.0f);
        const ImVec2 b0(right - boxW, origin.y + Px(12.0f));
        dl->AddRectFilled(b0, ImVec2(right, b0.y + fh), p.overlayBg, style.FrameRounding);
        dl->AddText(ImVec2(b0.x + Px(12.0f), b0.y + std::round((fh - ImGui::GetFontSize()) * 0.5f)), p.text, z.c_str());
        ImGui::PopFont();
    }
    // A video frame that is (almost) black, such as the fade-in at the start of a film: say so, or the user takes the
    // dark preview for a failure.
    if (s.sourceMode == SourceVideo && info.videoLoaded && !info.videoPlaying && !info.videoProcessing && info.videoPreviewLuma < 0.02f) {
        const float wrap = std::max(Px(80.0f), std::min(region.x - Px(40.0f), ImGui::GetFontSize() * 28.0f));
        const ImVec2 ts = ImGui::CalcTextSize(TR(DarkFrameHint), nullptr, false, wrap);
        const ImVec2 pad(Px(12.0f), Px(8.0f));
        const ImVec2 bpos(origin.x + std::floor((region.x - ts.x) * 0.5f) - pad.x, origin.y + std::floor((region.y - ts.y) * 0.5f) - pad.y);
        dl->AddRectFilled(bpos, ImVec2(bpos.x + ts.x + pad.x * 2.0f, bpos.y + ts.y + pad.y * 2.0f), p.overlayBg, Px(8.0f));
        dl->AddText(nullptr, 0.0f, ImVec2(bpos.x + pad.x, bpos.y + pad.y), p.text, TR(DarkFrameHint), nullptr, wrap);
    }
    dl->PopClipRect();
    DrawTransformTools(s, info, ev, origin, region, imgPos, imgSize, hovered);
    drawRim();   // after the tools: the crop shading and a tucked tab are cut at the rounded corners too
    FullscreenButton(info, ev, origin, region);
}

// Nothing open yet: the three steps, a tile for each way in (live from VRChat, a picture, a video) and a line on
// dropping files. On the FSR host route without DLSS-NR-on-AMD its installation (or, in the GeForce edition, the
// Radeon edition) comes first.
void MainUI::DrawWelcome(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& origin, const ImVec2& region) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fade = AnimateFrom(ImGui::GetID("##welcomeFade"), 0.0f, 1.0f, 5.0f);
    const float inset = Px(4.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + inset, origin.y + inset));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginChild("##welcome", ImVec2(std::max(1.0f, region.x - inset * 2.0f), std::max(1.0f, region.y - inset * 2.0f)),
                                        ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (open) {
        SmoothScroll(false, ImGui::GetFontSize() * 3.6f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int vtx0 = dl->VtxBuffer.Size;
        const float em = ImGui::GetFontSize();
        const float lineH = ImGui::GetTextLineHeight();
        const float frameH = ImGui::GetFrameHeight();
        const float availW = ImGui::GetContentRegionAvail().x;
        const float side = Px(24.0f);
        const float colW = std::max(Px(120.0f), std::min(availW - side * 2.0f, em * 46.0f));
        const float x0 = std::max(side, std::floor((availW - colW) * 0.5f));
        const float winH = ImGui::GetWindowHeight();
        const float top = m_welcomeH > 0.0f ? std::max(side, std::floor((winH - m_welcomeH) * 0.5f)) : side;
        ImGui::SetCursorPos(ImVec2(x0, top));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(x0 + colW);

        // Title and the line under it.
        ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.45f);
        ImGui::TextUnformatted(TR(WelcomeTitle));
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
        ImGui::TextUnformatted(TR(WelcomeLead));
        ImGui::PopStyleColor();

        // The three steps, the first one lit: a numbered circle and a name, an arrow between them; a step that does
        // not fit after the arrow starts a new row.
        ImGui::Dummy(ImVec2(0.0f, Px(6.0f)));
        {
            const char* steps[3] = { TR(StepOne), TR(StepTwo), TR(StepThree) };
            const float d = std::round(lineH * 1.25f);
            const float arrowW = Px(28.0f);
            const float labelGap = Px(8.0f);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            float x = start.x, y = start.y;
            for (int i = 0; i < 3; ++i) {
                const float w = d + labelGap + ImGui::CalcTextSize(steps[i]).x;
                if (i > 0) {
                    if (x + arrowW + w <= start.x + colW) {
                        DrawChevron(dl, ImVec2(x + arrowW * 0.5f, y + d * 0.5f), IconSize(0.5f), -IM_PI * 0.5f, WithAlpha(p.textDim, 0.7f));
                        x += arrowW;
                    } else {
                        x = start.x;
                        y += d + Px(6.0f);
                    }
                }
                const ImVec2 c(x + d * 0.5f, y + d * 0.5f);
                if (i == 0) dl->AddCircleFilled(c, d * 0.5f, p.accent, 0);
                else dl->AddCircle(c, d * 0.5f - 0.5f, WithAlpha(p.textDim, 0.7f), 0, std::max(1.0f, Px(1.25f)));
                const char digit[2] = { (char)('1' + i), 0 };
                ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 0.9f);
                const ImVec2 ds = ImGui::CalcTextSize(digit);
                dl->AddText(ImVec2(std::round(c.x - ds.x * 0.5f), std::round(c.y - ds.y * 0.5f)), i == 0 ? p.accentText : p.textDim, digit);
                ImGui::PopFont();
                dl->AddText(ImVec2(x + d + labelGap, std::round(y + (d - lineH) * 0.5f)), i == 0 ? p.text : p.textDim, steps[i]);
                x += w;
            }
            ImGui::SetCursorScreenPos(start);
            ImGui::Dummy(ImVec2(colW, y + d - start.y));
        }

        // On the FSR host route without DLSS-NR-on-AMD: its installation first, in a card with a warning stripe.
        const bool portStep = EditionRoute() == RouteFsrHost && !(info.status && !info.status->nrAmdPort.empty());
        if (portStep) {
            ImGui::Dummy(ImVec2(0.0f, Px(4.0f)));
            PanelBegin(colW, ImVec2(Px(16.0f), Px(14.0f)), p.warn);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.08f);
            ImGui::TextUnformatted(TR(AmdWelcomeTitle));
            ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
            if (!info.fsrDllExists) ImGui::TextUnformatted(TR(FsrDllMissing));
            else if (info.portWeightsExist && !info.portRestartHint) ImGui::TextUnformatted(TR(AmdPortWeightsNotLoaded));
            else if (!info.portRestartHint) ImGui::TextUnformatted(TR(AmdPortOffer));
            ImGui::PopStyleColor();
            ImGui::Spacing();
            if (info.fsrDllExists) PortActions(info, ev, false, true);
#if !APP_EDITION_AMD
            else { if (AccentButton(TR(EditionGetAmd))) ev.editionSwitch = true; Tip(TR(TipEditionSwitch)); }
#endif
            ImGui::PopTextWrapPos();
            PanelEnd();
        }

        // A tile for each way in: live from VRChat, a picture, a video. Side by side when three fit, stacked (the
        // icon at the left of the text) when they do not.
        ImGui::Dummy(ImVec2(0.0f, Px(4.0f)));
        {
            const bool wine = RunningUnderWine();
            const bool busy = info.batchRunning || info.videoProcessing;
            const int shownMode = m_modePending >= 0 ? m_modePending : s.sourceMode;
            struct Tile { Icon icon; int mode; const char* title; const char* text; };
            const Tile tiles[3] = {
                { Icon::Broadcast, SourceSpout, TR(WelcomeLiveTitle), wine ? TR(ProtonNoSpout) : TR(WelcomeLiveText) },
                { Icon::Image, SourceImage, TR(WelcomeImageTitle), TR(WelcomeImageText) },
                { Icon::Film, SourceVideo, TR(WelcomeVideoTitle), TR(WelcomeVideoText) },
            };
            const float gapT = Px(12.0f);
            const ImVec2 tpad(Px(16.0f), Px(14.0f));
            const float chip = std::round(frameH * 1.35f);
            const bool across = (colW - gapT * 2.0f) / 3.0f >= em * 10.5f;
            const float tileW = across ? std::floor((colW - gapT * 2.0f) / 3.0f) : colW;
            const float textW = across ? tileW - tpad.x * 2.0f : tileW - tpad.x * 2.0f - chip - Px(14.0f);
            // Whether a sender is there, under the live tile's text: a dot and the words, wrapped when they are long.
            const bool liveLine = shownMode == SourceSpout && !wine;
            const bool senders = info.senders && !info.senders->empty();
            const char* liveText = senders ? TR(StatusWaiting) : TR(StatusNoSpout);
            const float dotR = std::round(lineH * 0.22f), dotHalo = std::max(1.0f, Px(2.0f));
            const float dotW = (dotR + dotHalo) * 2.0f + Px(6.0f);
            const float liveW = std::max(1.0f, textW - dotW);
            const float liveH = liveLine ? ImGui::CalcTextSize(liveText, nullptr, false, liveW).y : 0.0f;
            float titleH[3], bodyH[3], tileH[3];
            float maxAcross = 0.0f;
            for (int i = 0; i < 3; ++i) {
                ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.05f);
                titleH[i] = ImGui::CalcTextSize(tiles[i].title, nullptr, false, textW).y;
                ImGui::PopFont();
                bodyH[i] = titleH[i] + Px(4.0f) + ImGui::CalcTextSize(tiles[i].text, nullptr, false, textW).y;
                if (i == 0 && liveLine) bodyH[i] += liveH + Px(6.0f);
                maxAcross = std::max(maxAcross, tpad.y * 2.0f + chip + Px(12.0f) + bodyH[i]);
            }
            for (int i = 0; i < 3; ++i) tileH[i] = across ? maxAcross : tpad.y * 2.0f + std::max(chip, bodyH[i]);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            float blockH = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const Tile& t = tiles[i];
                const ImVec2 a = across ? ImVec2(start.x + (tileW + gapT) * (float)i, start.y) : ImVec2(start.x, start.y + blockH);
                const ImVec2 b(a.x + tileW, a.y + tileH[i]);
                blockH = across ? tileH[i] : blockH + tileH[i] + (i < 2 ? gapT : 0.0f);
                const bool current = shownMode == t.mode;
                const bool clickable = !(i == 0 && (wine || current || busy));
                const float dim = (i == 0 && wine) ? 0.6f : 1.0f;
                ImGui::PushID(i);
                ImGui::SetCursorScreenPos(a);
                const bool pressed = ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH[i])) && clickable;
                const bool hovered = clickable && ImGui::IsItemHovered();
                if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const float hov = Animate(ImGui::GetID("##tileHover"), hovered ? 1.0f : 0.0f, 16.0f);
                ImGui::PopID();
                DrawCard(dl, a, b);
                if (hov > 0.001f) dl->AddRectFilled(a, b, WithAlpha(p.accent, 0.05f * hov), CardRounding());
                const float ring = std::max(current ? 0.9f : 0.0f, 0.8f * hov);
                if (ring > 0.001f) dl->AddRect(a, b, WithAlpha(p.accent, ring), CardRounding(), Px(1.5f));
                const ImVec2 c0(a.x + tpad.x, a.y + tpad.y);
                dl->AddRectFilled(c0, ImVec2(c0.x + chip, c0.y + chip), WithAlpha(p.accent, (0.22f - 0.10f * p.light) * dim), Px(8.0f));
                DrawIcon(dl, t.icon, ImVec2(c0.x + chip * 0.5f, c0.y + chip * 0.5f), IconSize(1.2f), WithAlpha(Mix(p.accentHover, p.accent, p.light), dim));
                const float bodyTop = across ? c0.y + chip + Px(12.0f) : c0.y + std::floor((std::max(chip, bodyH[i]) - bodyH[i]) * 0.5f);
                const float tx = across ? c0.x : c0.x + chip + Px(14.0f);
                ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.05f);
                dl->AddText(nullptr, 0.0f, ImVec2(tx, bodyTop), WithAlpha(p.text, dim), t.title, nullptr, textW);
                ImGui::PopFont();
                dl->AddText(nullptr, 0.0f, ImVec2(tx, bodyTop + titleH[i] + Px(4.0f)), WithAlpha(p.textDim, dim), t.text, nullptr, textW);
                if (i == 0 && liveLine) {
                    const float ly = bodyTop + bodyH[i] - liveH;
                    const ImU32 dot = senders ? p.warn : p.muted;
                    const ImVec2 dc(tx + dotHalo + dotR, ly + lineH * 0.5f);
                    dl->AddCircleFilled(dc, dotR + dotHalo, Col(WithAlpha(dot, 0.22f)), 0);
                    dl->AddCircleFilled(dc, dotR, Col(dot), 0);
                    dl->AddText(nullptr, 0.0f, ImVec2(tx + dotW, ly), p.textDim, liveText, nullptr, liveW);
                }
                if (pressed) {
                    if (t.mode == SourceSpout) {
                        if (m_modePending < 0 && s.sourceMode != SourceSpout) { m_modePending = SourceSpout; m_modeFadeStart = ImGui::GetTime(); }
                    } else if (t.mode == SourceImage) {
                        ev.openImage = true;   // opening the file switches the mode
                    } else {
                        ev.openVideo = true;
                    }
                }
            }
            ImGui::SetCursorScreenPos(start);
            ImGui::Dummy(ImVec2(colW, blockH));
        }

        // Dropping files, and the library's add button beside the words (under them when the row is narrow).
        ImGui::Dummy(ImVec2(0.0f, Px(4.0f)));
        {
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            const float is = IconSize();
            const float textX = is + Px(10.0f);
            const char* addLabel = TR(AddFiles);
            const float btnW = IconTextButtonWidth(addLabel);
            const bool beside = colW - textX - btnW - Px(16.0f) >= em * 14.0f;
            const float textW = beside ? colW - textX - btnW - Px(16.0f) : colW - textX;
            const ImVec2 ts = ImGui::CalcTextSize(TR(WelcomeDrop), nullptr, false, textW);
            const float rowH = beside ? std::max(ts.y, frameH) : ts.y + Px(10.0f) + frameH;
            const float textY = beside ? r0.y + std::floor((rowH - ts.y) * 0.5f) : r0.y;
            const float btnY = beside ? r0.y + std::floor((rowH - frameH) * 0.5f) : r0.y + ts.y + Px(10.0f);
            DrawIcon(dl, Icon::Import, ImVec2(r0.x + is * 0.5f, textY + lineH * 0.5f), is, p.textDim);
            dl->AddText(nullptr, 0.0f, ImVec2(r0.x + textX, textY), p.textDim, TR(WelcomeDrop), nullptr, textW);
            ImGui::SetCursorScreenPos(ImVec2(beside ? r0.x + colW - btnW : r0.x + textX, btnY));
            if (IconTextButton(addLabel, Icon::Images, ImVec2(0, 0), ButtonKind::Ghost)) ev.libraryAddFiles = true;
            ImGui::SetCursorScreenPos(r0);
            ImGui::Dummy(ImVec2(colW, rowH));
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        m_welcomeH = ImGui::GetItemRectSize().y;
        ImGui::Dummy(ImVec2(0.0f, side));
        FadeDrawn(dl, vtx0, fade, (1.0f - fade) * Px(12.0f));
        if (info.fullscreen) FullscreenButton(info, ev, ImGui::GetWindowPos(), ImGui::GetWindowSize());   // a way out that is not a key
    }
    ImGui::EndChild();
}

// Keyboard control of a video: space, arrows (shift: ten frames), I / O, Home / End. Not while typing or while a
// popup is open. Shared by the transport bar and the fullscreen view, whose bar may have faded out.
void MainUI::VideoKeys(const UiFrameInfo& info, UiEvents& ev) {
    ImGuiIO& io = ImGui::GetIO();
    const bool busy = info.videoProcessing || info.batchRunning;
    if (busy || io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    const double frame = info.videoFps > 0.0 ? 1.0 / info.videoFps : 1.0 / 30.0;
    const double duration = std::max(info.videoDurationSeconds, frame);
    auto sendSeek = [&](double t) {
        t = std::clamp(t, 0.0, std::max(0.0, duration - frame * 0.5));
        ev.videoSeek = true; ev.videoSeekTo = t;
        m_seekTarget = t; m_seekSentTime = ImGui::GetTime();
    };
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) ev.videoPlayToggle = true;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) ev.videoStep -= io.KeyShift ? 10 : 1;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) ev.videoStep += io.KeyShift ? 10 : 1;
    if (ImGui::IsKeyPressed(ImGuiKey_I, false)) ev.videoSetIn = true;
    if (ImGui::IsKeyPressed(ImGuiKey_O, false)) ev.videoSetOut = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) sendSeek(0.0);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) sendSeek(duration - frame);
}

// The video controls: seek bar with the in/out range and a hover picture, play/pause, frame steps, range buttons.
void MainUI::DrawTransport(Settings& /*s*/, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    {
        // The card goes on the parent's list with the clipping lifted, so its shadow shows around it. Fullscreen it
        // floats over the picture with a deeper shadow.
        ImDrawList* host = ImGui::GetWindowDrawList();
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        host->PushClipRectFullScreen();
        if (info.fullscreen) DrawShadow(host, pos, max, CardRounding(), 1.0f);
        DrawCard(host, pos, max, info.fullscreen ? 1.0f : 1.0f - Ease(m_modeFade));
        host->PopClipRect();
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Px(14.0f), Px(10.0f)));
    const bool open = ImGui::BeginChild("##transport", size, ImGuiChildFlags_AlwaysUseWindowPadding,
                                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) { ImGui::EndChild(); VideoKeys(info, ev); return; }
    const int vtx0 = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    const bool busy = info.videoProcessing || info.batchRunning;
    const double frame = info.videoFps > 0.0 ? 1.0 / info.videoFps : 1.0 / 30.0;
    const double duration = std::max(info.videoDurationSeconds, frame);
    const float frameH = ImGui::GetFrameHeight();
    const float lineH = ImGui::GetTextLineHeight();

    auto sendSeek = [&](double t) {
        t = std::clamp(t, 0.0, std::max(0.0, duration - frame * 0.5));
        ev.videoSeek = true; ev.videoSeekTo = t;
        m_seekTarget = t; m_seekSentTime = now;
    };
    // A sent seek stays on the bar until the processing thread reports a position near it (or gives up).
    if (m_seekTarget >= 0.0 && !m_seekDragging) {
        if (!info.videoSeeking && std::fabs(info.videoPosition - m_seekTarget) <= frame * 1.5) m_seekTarget = -1.0;
        else if (!info.videoSeeking && info.videoPlaying && now - m_seekSentTime > 0.5) m_seekTarget = -1.0;
        else if (now - m_seekSentTime > 4.0) m_seekTarget = -1.0;
    }
    const double shownPos = m_seekDragging ? m_seekDragTime : (m_seekTarget >= 0.0 ? m_seekTarget : info.videoPosition);

    VideoKeys(info, ev);

    // Seek bar.
    const float barH = std::round(frameH * 0.9f);
    const ImVec2 barPos = ImGui::GetCursorScreenPos();
    const float barW = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##seek", ImVec2(barW, barH));
    const bool barHovered = ImGui::IsItemHovered();
    const bool barActive = ImGui::IsItemActive();
    auto timeAt = [&](float x) { return std::clamp((double)((x - barPos.x) / std::max(barW, 1.0f)), 0.0, 1.0) * duration; };
    auto xAt = [&](double t) { return barPos.x + (float)std::clamp(t / duration, 0.0, 1.0) * barW; };
    if (!busy) {
        if (barActive) {
            if (!m_seekDragging) { m_seekDragging = true; m_seekSentTime = -1.0; }
            m_seekDragTime = timeAt(io.MousePos.x);
            if (now - m_seekSentTime >= 0.12) sendSeek(m_seekDragTime);
        } else if (m_seekDragging) {
            m_seekDragging = false;
            sendSeek(m_seekDragTime);
        }
        if (barHovered && io.MouseWheel != 0.0f) ev.videoStep += io.MouseWheel > 0 ? 1 : -1;
    } else {
        m_seekDragging = false;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = barPos.y + barH * 0.5f;
    const float trackH = std::max(Px(4.0f), barH * 0.28f);
    dl->AddRectFilled(ImVec2(barPos.x, cy - trackH * 0.5f), ImVec2(barPos.x + barW, cy + trackH * 0.5f), Col(p.track), trackH * 0.5f);
    const double inSec = std::max(0.0, info.videoIn);
    const double outSec = info.videoOut > inSec ? info.videoOut : duration;
    const bool ranged = inSec > 0.0 || info.videoOut > 0.0;
    if (ranged)
        dl->AddRectFilled(ImVec2(xAt(inSec), cy - trackH * 0.9f), ImVec2(xAt(outSec), cy + trackH * 0.9f), Col(p.rangeFill), trackH * 0.5f);
    if (busy && info.videoProcessing) {
        const unsigned long long total = std::max(info.videoFrames, info.videoFrame);
        const double frac = total ? (double)info.videoFrame / (double)total : 0.0;
        const double from = info.batchRunning ? 0.0 : inSec, to = info.batchRunning ? duration : outSec;
        dl->AddRectFilled(ImVec2(xAt(from), cy - trackH * 0.5f), ImVec2(xAt(from + (to - from) * frac), cy + trackH * 0.5f), Col(p.warn), trackH * 0.5f);
    } else {
        dl->AddRectFilled(ImVec2(barPos.x, cy - trackH * 0.5f), ImVec2(xAt(shownPos), cy + trackH * 0.5f), Col(p.accent), trackH * 0.5f);
        if (ranged) {
            const float mh = barH * 0.42f, mw = std::max(1.0f, 1.5f * Dpi());
            dl->AddRectFilled(ImVec2(xAt(inSec) - mw, cy - mh), ImVec2(xAt(inSec) + mw, cy + mh), Col(p.knob));
            if (info.videoOut > 0.0) dl->AddRectFilled(ImVec2(xAt(outSec) - mw, cy - mh), ImVec2(xAt(outSec) + mw, cy + mh), Col(p.knob));
        }
        const float grow = Animate(ImGui::GetID("##knob"), (barHovered || m_seekDragging) ? 1.0f : 0.0f, 18.0f);
        const float r = barH * (0.27f + 0.07f * grow);
        dl->AddCircleFilled(ImVec2(xAt(shownPos), cy), r, Col(p.knob));
    }

    // Hover: the frame under the cursor, from the storyboard or the exact frame the scanner decoded for this spot.
    const bool showHover = barHovered && !busy && !m_seekDragging;
    const float hoverT = showHover ? AnimateFrom(ImGui::GetID("##hovercard"), 0.0f, 1.0f, 22.0f) : 0.0f;
    if (!showHover) AnimateSnap(ImGui::GetID("##hovercard"), 0.0f);
    if (showHover) {
        const double t = timeAt(io.MousePos.x);
        ev.videoHover = true; ev.videoHoverTime = t;
        int cell = -1; double best = 1e9;
        if (info.atlas && info.storyCells && info.storyTimes && info.storyReady) {
            for (size_t i = 0; i < info.storyCells->size() && i < info.storyTimes->size() && i < info.storyReady->size(); ++i) {
                if (!(*info.storyReady)[i] || !info.atlas->Filled((*info.storyCells)[i])) continue;
                const double d = std::fabs((*info.storyTimes)[i] - t);
                if (d < best) { best = d; cell = (*info.storyCells)[i]; }
            }
            if (info.hoverCell >= 0 && info.hoverCellTime >= 0.0 && info.atlas->Filled(info.hoverCell) && std::fabs(info.hoverCellTime - t) <= best)
                cell = info.hoverCell;
        }
        // A card above the bar with the picture and the time; it rises and fades in.
        const float w = std::min(ImGui::GetFontSize() * 13.0f, barW);
        const float inset = Px(4.0f);
        const float imgH = std::round((w - inset * 2.0f) * 9.0f / 16.0f);
        const float boxH = (cell >= 0 ? imgH + inset : 0.0f) + lineH + Px(10.0f);
        const float x = std::round(std::clamp(io.MousePos.x - w * 0.5f, barPos.x, barPos.x + barW - w));
        const float lift = (1.0f - hoverT) * Px(6.0f);
        const float y0 = std::round(barPos.y - boxH - Px(10.0f) + lift);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const ImVec2 b0(x, y0), b1(x + w, y0 + boxH);
        const float a = hoverT * style.Alpha;
        DrawShadow(fg, b0, b1, Px(8.0f), hoverT);
        fg->AddRectFilled(b0, b1, WithAlpha(p.card, a), Px(8.0f));
        fg->AddRect(b0, b1, WithAlpha(p.cardBorder, a), Px(8.0f), 1.0f);
        if (cell >= 0) {
            float u0, v0, u1, v1;
            info.atlas->Uv(cell, u0, v0, u1, v1);
            fg->AddImageRounded(ImTextureRef((ImTextureID)info.atlas->TextureHandle()), ImVec2(x + inset, y0 + inset), ImVec2(x + w - inset, y0 + inset + imgH),
                                ImVec2(u0, v0), ImVec2(u1, v1), WithAlpha(IM_COL32_WHITE, a), Px(4.0f));
        }
        ImGui::PushFont(fonts.Mono(), 0.0f);
        const std::string tt = FormatClock(t);
        const ImVec2 ts = ImGui::CalcTextSize(tt.c_str());
        fg->AddText(ImVec2(std::round(x + (w - ts.x) * 0.5f), y0 + boxH - lineH - Px(5.0f)), WithAlpha(p.text, a), tt.c_str());
        ImGui::PopFont();
        fg->AddLine(ImVec2(io.MousePos.x, barPos.y), ImVec2(io.MousePos.x, barPos.y + barH), WithAlpha(p.knob, 0.5f * a));
    }

    // Controls row: frame back, play/pause, frame forward and the clock at the left; the range at the right, folded
    // into a menu when the row is too narrow for its buttons.
    const float stepW = std::round(frameH * 1.5f), playW = std::round(frameH * 2.1f);
    ImGui::BeginDisabled(busy);
    if (IconButton("##back", Icon::StepBack, ImVec2(stepW, frameH), TR(PrevFrame))) ev.videoStep -= 1;
    ImGui::SameLine(0.0f, Px(4.0f));
    if (IconButton("##play", info.videoPlaying ? Icon::Pause : Icon::Play, ImVec2(playW, frameH), info.videoPlaying ? TR(Pause) : TR(Play), ButtonKind::Accent))
        ev.videoPlayToggle = true;
    ImGui::SameLine(0.0f, Px(4.0f));
    if (IconButton("##fwd", Icon::StepForward, ImVec2(stepW, frameH), TR(NextFrame))) ev.videoStep += 1;
    ImGui::EndDisabled();
    // The widths the row can take, widest first; the first level that fits is used. The "Seeking" room is always
    // counted, so the level does not flip while a seek runs.
    const float contentRight = ImGui::GetWindowWidth() - style.WindowPadding.x;   // window-local
    const float clockX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + Px(12.0f);
    const bool processing = busy && info.videoProcessing;
    const unsigned long long frames = std::max(info.videoFrames, info.videoFrame);
    ImGui::PushFont(fonts.Mono(), 0.0f);
    const std::string clock = processing ? StrPrintf(TR(FrameOf), (unsigned long long)info.videoFrame, frames) : FormatClock(shownPos);
    const std::string total = StrPrintf(" / %s", FormatClock(duration).c_str());
    const float clockW = processing ? ImGui::CalcTextSize(StrPrintf(TR(FrameOf), frames, frames).c_str()).x : ImGui::CalcTextSize(clock.c_str()).x;
    const float totalW = ImGui::CalcTextSize(total.c_str()).x;
    ImGui::PopFont();
    const float seekW = Px(10.0f) + ImGui::CalcTextSize(TR(Seeking)).x;
    auto bw = [&](const char* t) { return ImGui::CalcTextSize(t, nullptr, true).x + style.FramePadding.x * 2.0f; };
    const std::string rangeText = ranged ? StrPrintf("%s %s \xE2\x80\x93 %s", TR(RangeLabel), FormatDuration(inSec).c_str(), FormatDuration(outSec).c_str()) : std::string();
    const float rangeW = ranged ? ImGui::CalcTextSize(rangeText.c_str()).x + Px(12.0f) : 0.0f;
    const float helpW = style.ItemSpacing.x + IconSize();
    const float btnsW = bw(TR(SetIn)) + bw(TR(SetOut)) + bw(TR(WholeVideo)) + style.ItemSpacing.x * 2.0f;
    const float left0 = clockX + clockW + totalW + seekW, left4 = clockX + clockW;
    const float leftW[5] = { left0, left0, left0, left0, left4 };
    const float rightW[5] = { rangeW + btnsW + helpW, btnsW + helpW, btnsW, frameH, frameH };
    int level = 4;
    for (int i = 0; i < 5; ++i) if (leftW[i] + Px(16.0f) + rightW[i] <= contentRight) { level = i; break; }

    ImGui::SameLine(0.0f, Px(12.0f));
    ImGui::PushFont(fonts.Mono(), 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(clock.c_str());
    if (!processing && level < 4) { ImGui::SameLine(0.0f, 0.0f); ImGui::TextDisabled("%s", total.c_str()); }
    ImGui::PopFont();
    if (level < 4 && !busy && (info.videoSeeking || m_seekTarget >= 0.0)) {
        ImGui::SameLine(0.0f, Px(10.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR(Seeking));
    }
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    ImGui::SameLine(std::max(leftEnd + Px(16.0f), contentRight - rightW[level]));
    if (level <= 2) {
        if (level == 0 && ranged) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", rangeText.c_str());
            ImGui::SameLine(0.0f, Px(12.0f));
        }
        ImGui::BeginDisabled(busy);
        if (GhostButton(TR(SetIn))) ev.videoSetIn = true;
        ImGui::SameLine();
        if (GhostButton(TR(SetOut))) ev.videoSetOut = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(!ranged);
        if (GhostButton(TR(WholeVideo))) ev.videoClearRange = true;
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (level <= 1) Help(TR(RangeHint));
    } else {
        ImGui::BeginDisabled(busy);
        const ImVec2 bpos = ImGui::GetCursorScreenPos();
        if (IconButton("##range", Icon::Scissors, ImVec2(frameH, frameH), TR(RangeLabel), ButtonKind::Ghost)) ImGui::OpenPopup("##rangeMenu");
        if (ranged) ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(bpos.x + frameH - Px(5.0f), bpos.y + Px(5.0f)), Px(3.0f), Col(p.accent), 0);
        ImGui::EndDisabled();
        if (BeginPopupFade("##rangeMenu")) {
            if (ImGui::MenuItem(TR(SetIn), "I")) ev.videoSetIn = true;
            if (ImGui::MenuItem(TR(SetOut), "O")) ev.videoSetOut = true;
            if (ImGui::MenuItem(TR(WholeVideo), nullptr, false, ranged)) ev.videoClearRange = true;
            if (ranged) { ImGui::Separator(); ImGui::TextDisabled("%s", rangeText.c_str()); }
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextDisabled("%s", TR(RangeHint));
            ImGui::PopTextWrapPos();
            EndPopupFade();
        }
    }
    if (!info.fullscreen) ModeFadeContent(vtx0);
    ImGui::EndChild();
}

// The media library: a strip of thumbnails under the preview. A click previews a file, a drag across the cards
// selects them for processing (Ctrl adds, Shift extends), the right button opens a card's menu.
void MainUI::DrawLibrary(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    {
        // The card on the parent's list with the clipping lifted, so its shadow shows around it.
        ImDrawList* host = ImGui::GetWindowDrawList();
        host->PushClipRectFullScreen();
        DrawCard(host, pos, ImVec2(pos.x + size.x, pos.y + size.y));
        host->PopClipRect();
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, LibraryPad());
    const bool open = ImGui::BeginChild("##library", size, ImGuiChildFlags_AlwaysUseWindowPadding,
                                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) { ImGui::EndChild(); return; }
    const int vtxRow = ImGui::GetWindowDrawList()->VtxBuffer.Size;   // the header, faded around a mode switch
    std::vector<LibraryItem>* lib = info.library;
    const int count = lib ? (int)lib->size() : 0;
    int selected = 0;
    if (lib) for (const auto& it : *lib) if (it.selected && it.probe != 2) ++selected;
    const bool running = info.batchRunning;
    const bool busy = running || info.videoProcessing;
    const float frameH = ImGui::GetFrameHeight();
    const float fold = m_libraryFold;

    // Header line: the name, the counts and the actions at the right end. A narrow row folds the actions step by
    // step: the "?" goes, the two add buttons become one, "Process all" and "Delete" move into the "more" menu, the
    // counts go, and "Process selected" keeps only its icon.
    std::string caption = count ? StrPrintf(count == 1 ? TR(LibraryCountOne) : TR(LibraryCount), count, selected) : std::string(TR(BatchEmpty));
    if (running) {
        const double left = BatchRemaining(info);
        if (left >= 0.0) caption += "  \xC2\xB7  " + StrPrintf(TR(RemainingFmt), ("\xE2\x89\x88 " + FormatEstimate(left)).c_str());
    }
    const char* addF = TR(AddFiles);
    const char* addD = TR(AddFolder);
    const char* procSel = TR(ProcessSelected);
    const char* procAll = TR(BatchStart);
    const char* del = TR(Delete);
    const char* cancel = TR(Cancel);
    const float sp = style.ItemSpacing.x;
    const bool showHelp = count > 0 && !running;
    ImGui::PushFont(fonts.Bold(), 0.0f);
    const float titleW = ImGui::CalcTextSize(TR(SecLibrary)).x;
    ImGui::PopFont();
    const float captionW = Px(10.0f) + ImGui::CalcTextSize(caption.c_str()).x;
    const float helpW = showHelp ? sp + IconSize() : 0.0f;
    float leftW[6], rightW[6];
    for (int l = 0; l < 6; ++l) {
        leftW[l] = titleW + (l < 4 ? captionW : 0.0f) + (l < 1 ? helpW : 0.0f);
        if (running) { rightW[l] = IconTextButtonWidth(cancel); continue; }
        float w = (l < 2) ? IconTextButtonWidth(addF) + sp + IconTextButtonWidth(addD) + sp : frameH + sp;
        w += (l < 5) ? IconTextButtonWidth(procSel) + sp : frameH + sp;
        if (l < 3) w += IconTextButtonWidth(procAll) + sp + IconTextButtonWidth(del) + sp;
        rightW[l] = w + frameH;   // the "more" button
    }
    const float availW = ImGui::GetContentRegionAvail().x;
    int level = 5;
    for (int l = 0; l < 6; ++l) if (leftW[l] + Px(12.0f) + rightW[l] <= availW) { level = l; break; }
    // An empty library offers the two add buttons alone: on a short row the caption goes first, then the buttons
    // become one.
    const bool empty = count == 0 && !running;
    const float addBothW = IconTextButtonWidth(addF) + sp + IconTextButtonWidth(addD);
    const bool showCaption = empty ? titleW + captionW + Px(12.0f) + addBothW <= availW : level < 4;
    const bool fullAdd = empty ? showCaption || titleW + Px(12.0f) + addBothW <= availW : level < 2;
    const float rightWidth = empty ? (fullAdd ? addBothW : frameH) : rightW[level];

    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(fonts.Bold(), 0.0f);
    ImGui::TextUnformatted(TR(SecLibrary));
    ImGui::PopFont();
    if (showCaption) {
        ImGui::SameLine(0.0f, Px(10.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", caption.c_str());
    }
    if (level < 1 && showHelp) Help(TR(TipLibrarySelect));
    {
        const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        const float rightX = ImGui::GetWindowWidth() - style.WindowPadding.x - rightWidth;
        ImGui::SameLine(std::max(leftEnd + Px(12.0f), rightX));
        if (running) {
            if (IconTextButton(cancel, Icon::Stop, ImVec2(0, 0), ButtonKind::Flat)) ev.batchCancel = true;
        } else {
            if (fullAdd) {
                if (IconTextButton(addF, Icon::ImagePlus, ImVec2(0, 0), ButtonKind::Ghost)) ev.libraryAddFiles = true;
                ImGui::SameLine();
                if (IconTextButton(addD, Icon::Folder, ImVec2(0, 0), ButtonKind::Ghost)) ev.libraryAddFolder = true;
            } else {
                if (IconButton("##libAdd", Icon::Plus, ImVec2(0, 0), addF, ButtonKind::Ghost)) ImGui::OpenPopup("##libAddMenu");
                if (BeginPopupFade("##libAddMenu")) {
                    if (ImGui::MenuItem(addF)) ev.libraryAddFiles = true;
                    if (ImGui::MenuItem(addD)) ev.libraryAddFolder = true;
                    EndPopupFade();
                }
            }
        }
        if (!running && !empty) {
            ImGui::SameLine();
            ImGui::BeginDisabled(selected == 0 || busy);
            const bool process = level < 5 ? IconTextButton(procSel, Icon::Wand, ImVec2(0, 0), ButtonKind::Accent)
                                           : IconButton("##procSel", Icon::Wand, ImVec2(0, 0), procSel, ButtonKind::Accent);
            if (process) ev.libraryProcessSelected = true;
            ImGui::EndDisabled();
            if (level < 3) {
                ImGui::SameLine();
                ImGui::BeginDisabled(count == 0 || busy);
                if (IconTextButton(procAll, Icon::ListChecks, ImVec2(0, 0), ButtonKind::Flat)) ev.libraryProcessAll = true;
                ImGui::EndDisabled();
                ImGui::SameLine();
                // Red only while something is selected. It drops the items from the library; the files stay.
                ImGui::BeginDisabled(selected == 0);
                if (IconTextButton(del, Icon::Trash, ImVec2(0, 0), selected ? ButtonKind::Danger : ButtonKind::Ghost)) ev.libraryDeleteSelected = true;
                Tip(TR(TipDelete));
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (IconButton("##libMore", Icon::Ellipsis, ImVec2(0, 0), TR(More), ButtonKind::Plain)) ImGui::OpenPopup("##libMenu");
            if (BeginPopupFade("##libMenu")) {
                if (level >= 3) {
                    if (ImGui::MenuItem(procAll, nullptr, false, count > 0 && !busy)) ev.libraryProcessAll = true;
                    if (ImGui::MenuItem(del, "Del", false, selected > 0)) ev.libraryDeleteSelected = true;
                    ImGui::Separator();
                }
                if (ImGui::MenuItem(TR(SelectAll), "Ctrl+A", false, count > 0) && lib) for (auto& it : *lib) it.selected = it.probe != 2;
                if (ImGui::MenuItem(TR(SelectNone), nullptr, false, selected > 0) && lib) for (auto& it : *lib) it.selected = false;
                EndPopupFade();
            }
        }
    }
    if (fold * m_libraryFill <= 0.001f) { m_libDrag = false; ModeFadeContent(vtxRow); ImGui::EndChild(); return; }
    ModeFadeContent(vtxRow);

    // The strip. The cards keep a margin inside it so the plate around a selected card is not cut at its edges.
    const float thumbH = m_thumbH > 0.0f ? m_thumbH : ImGui::GetFontSize() * 4.5f;
    const float cardW = std::floor(thumbH * 16.0f / 9.0f);
    const float lineH = ImGui::GetTextLineHeight();
    const float cardH = thumbH + Px(6.0f) + lineH * 2.0f;
    const float margin = Px(4.0f);
    const float stripW = ImGui::GetContentRegionAvail().x + margin * 2.0f;
    const float stripH = std::max(cardH + style.ScrollbarSize + margin * 2.0f, ImGui::GetContentRegionAvail().y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - margin);
    const bool stripOpen = ImGui::BeginChild("##strip", ImVec2(stripW, stripH), ImGuiChildFlags_None,
                                             ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!stripOpen) { ImGui::EndChild(); ImGui::EndChild(); return; }
    const int vtxStrip = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    ImGuiIO& io = ImGui::GetIO();
    SmoothScroll(true, cardW + style.ItemSpacing.x);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (count == 0) {
        // Nothing yet: a tray icon and the hint in the middle of the strip (the icon goes when the room is short).
        m_libDrag = false;
        const ImVec2 o = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
        const float is = IconSize(1.3f);
        const float wrap = std::max(Px(80.0f), std::min(wsz.x - Px(40.0f), ImGui::GetFontSize() * 40.0f));
        const ImVec2 ts = ImGui::CalcTextSize(TR(LibraryHint), nullptr, false, wrap);
        const bool icon = wsz.y >= ts.y + is + Px(8.0f) * 2.0f;
        const float lift = icon ? is + Px(8.0f) : 0.0f;
        const float top = o.y + std::max(0.0f, std::floor((wsz.y - lift - ts.y) * 0.5f));
        if (icon) DrawIcon(dl, Icon::Import, ImVec2(o.x + std::round(wsz.x * 0.5f), top + is * 0.5f), is, p.textDim);
        dl->AddText(nullptr, 0.0f, ImVec2(o.x + std::floor((wsz.x - ts.x) * 0.5f), top + lift), p.textDim, TR(LibraryHint), nullptr, wrap);
        ModeFadeContent(vtxStrip);
        ImGui::EndChild();
        ImGui::EndChild();
        return;
    }
    const ImTextureRef atlasTex = (info.atlas && info.atlas->Ready()) ? ImTextureRef((ImTextureID)info.atlas->TextureHandle()) : ImTextureRef();
    ImGui::SetCursorPos(ImVec2(margin, margin));
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const bool stripHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool menuOpen = ImGui::IsPopupOpen("##libctx");   // judged here: inside a card's PushID the name would hash differently
    // Keys while the cursor is on the strip (or it was clicked last): Ctrl+A selects every readable file, Delete
    // drops the selected ones from the library.
    const bool keysHere = (stripHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) && !running && !io.WantTextInput && !io.KeyAlt;
    if (keysHere && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        for (auto& it : *lib) it.selected = it.probe != 2;
    if (keysHere && !io.KeyCtrl && selected > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) ev.libraryDeleteSelected = true;
    // Selection drags are kept in content coordinates, so the rectangle stays put while the strip scrolls.
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 scroll(ImGui::GetScrollX(), ImGui::GetScrollY());
    auto toContent = [&](const ImVec2& v) { return ImVec2(v.x - winPos.x + scroll.x, v.y - winPos.y + scroll.y); };
    auto toScreen = [&](const ImVec2& v) { return ImVec2(v.x + winPos.x - scroll.x, v.y + winPos.y - scroll.y); };
    ImRect marquee;
    const bool dragging = m_libDrag && m_libDragMoved;
    if (dragging) {
        const ImVec2 a = toScreen(m_libDragStart), b = io.MousePos;
        marquee = ImRect(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)));
    }
    bool overControl = false;   // the cursor is on a card's box or its remove button
    unsigned underCursor = 0;   // the card under the cursor
    unsigned menuCard = 0;
    const bool anySelected = selected > 0;
    const float tr = Px(6.0f);   // the picture's corners
    for (size_t i = 0; i < lib->size(); ++i) {
        LibraryItem& it = (*lib)[i];
        ImGui::PushID((int)it.id);
        const ImVec2 c0(rowStart.x + (float)i * (cardW + style.ItemSpacing.x), rowStart.y);
        const ImVec2 c1(c0.x + cardW, c0.y + cardH);
        const ImVec2 t1(c0.x + cardW, c0.y + thumbH);
        ImGui::SetCursorScreenPos(c0);
        ImGui::Dummy(ImVec2(cardW, cardH));
        // Hover is judged on the card's rectangle: the box and the remove button sit on it and must not end it.
        const bool hovered = stripHovered && !dragging && ImGui::IsMouseHoveringRect(c0, c1);
        if (hovered) underCursor = it.id;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) menuCard = it.id;
        bool onControl = false;   // the cursor is on this card's box or remove button
        if (dragging && it.probe != 2) {
            const bool inRect = marquee.Overlaps(ImRect(c0, c1));
            const bool kept = io.KeyCtrl && std::find(m_libDragKeep.begin(), m_libDragKeep.end(), it.id) != m_libDragKeep.end();
            it.selected = inRect || kept;
        }
        const float hov = Animate(ImGui::GetID("##hover"), hovered ? 1.0f : 0.0f, 16.0f);
        const float sel = Animate(ImGui::GetID("##selected"), it.selected ? 1.0f : 0.0f, 16.0f);
        const bool current = (s.sourceMode == SourceVideo && it.isVideo && !info.videoPath.empty() && it.path == info.videoPath)
                          || (s.sourceMode == SourceImage && !it.isVideo && !info.imagePath.empty() && it.path == info.imagePath);
        // A plate behind a selected card (or the one under the mouse), the picture on a rounded tile, its marks.
        const float pl = Px(4.0f);
        if (sel > 0.001f) dl->AddRectFilled(ImVec2(c0.x - pl, c0.y - pl), ImVec2(c1.x + pl, c1.y + pl), WithAlpha(p.accent, (0.2f - 0.06f * p.light) * sel), Px(8.0f));
        else if (hov > 0.001f) dl->AddRectFilled(ImVec2(c0.x - pl, c0.y - pl), ImVec2(c1.x + pl, c1.y + pl), WithAlpha(p.controlHover, 0.6f * hov), Px(8.0f));
        dl->AddRectFilled(c0, t1, p.control, tr);
        const ImVec2 tc((c0.x + t1.x) * 0.5f, (c0.y + t1.y) * 0.5f);
        if (info.atlas && it.thumbCell >= 0 && info.atlas->Filled(it.thumbCell)) {
            float u0, v0, u1, v1;
            info.atlas->Uv(it.thumbCell, u0, v0, u1, v1);
            dl->AddImageRounded(atlasTex, c0, t1, ImVec2(u0, v0), ImVec2(u1, v1), IM_COL32_WHITE, tr);
        } else if (it.probe == 2) {
            DrawIcon(dl, Icon::Warning, tc, IconSize(1.2f), p.bad);
        } else {
            DrawIcon(dl, it.isVideo ? Icon::Film : Icon::Image, tc, IconSize(1.2f), WithAlpha(p.textDim, 0.6f));
        }
        if (it.state == LibraryItem::Queued) dl->AddRectFilled(c0, t1, IM_COL32(0, 0, 0, 110), tr);
        if (it.state == LibraryItem::Processing) {
            const float ph = Px(4.0f);
            dl->AddRectFilled(ImVec2(c0.x, t1.y - ph), t1, IM_COL32(0, 0, 0, 160), tr, ImDrawFlags_RoundCornersBottom);
            dl->AddRectFilled(ImVec2(c0.x, t1.y - ph), ImVec2(c0.x + cardW * std::clamp(it.progress, 0.02f, 1.0f), t1.y), p.accent, tr, ImDrawFlags_RoundCornersBottom);
        }
        if (hov > 0.001f) dl->AddRectFilled(c0, t1, WithAlpha(IM_COL32(255, 255, 255, 255), 0.05f * hov), tr);
        if (current) dl->AddRect(ImVec2(c0.x - Px(1.0f), c0.y - Px(1.0f)), ImVec2(t1.x + Px(1.0f), t1.y + Px(1.0f)), p.accent, Px(7.0f), Px(2.0f));
        else if (sel > 0.001f) dl->AddRect(c0, t1, WithAlpha(p.accent, sel), tr, std::max(1.0f, 1.5f * Dpi()));
        // State at the top right of the picture: a dark pill with a coloured dot.
        if (const char* st = StateText(it)) {
            const ImU32 col = (it.state == LibraryItem::Done) ? p.good : (it.state == LibraryItem::Failed || it.probe == 2) ? p.bad
                            : (it.state == LibraryItem::Processing) ? p.accentHover : p.warn;
            ImGui::PushFont(nullptr, style.FontSizeBase * 0.82f);
            const ImVec2 ss = ImGui::CalcTextSize(st);
            const float d = std::max(4.0f, std::round(ss.y * 0.4f));
            const ImVec2 pad(Px(6.0f), Px(2.0f));
            const float w = pad.x + d + Px(5.0f) + ss.x + pad.x;
            const ImVec2 b0(t1.x - w - Px(4.0f), c0.y + Px(4.0f));
            const ImVec2 b1(b0.x + w, b0.y + ss.y + pad.y * 2.0f);
            dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 160), (b1.y - b0.y) * 0.5f);
            dl->AddCircleFilled(ImVec2(b0.x + pad.x + d * 0.5f, (b0.y + b1.y) * 0.5f), d * 0.5f, col, 0);
            dl->AddText(ImVec2(b0.x + pad.x + d + Px(5.0f), b0.y + pad.y), IM_COL32(255, 255, 255, 235), st);
            ImGui::PopFont();
        }
        // A file with its own values carries a small mark at the bottom left of the picture.
        if (it.useOwn && it.own) {
            ImGui::PushFont(nullptr, style.FontSizeBase * 0.78f);
            const ImVec2 os = ImGui::CalcTextSize(TR(OwnBadge));
            const ImVec2 pad(Px(6.0f), Px(2.0f));
            const ImVec2 b0(c0.x + Px(4.0f), t1.y - os.y - pad.y * 2.0f - Px(4.0f));
            dl->AddRectFilled(b0, ImVec2(b0.x + os.x + pad.x * 2.0f, b0.y + os.y + pad.y * 2.0f), WithAlpha(p.accent, 0.85f), Px(4.0f));
            dl->AddText(ImVec2(b0.x + pad.x, b0.y + pad.y), p.accentText, TR(OwnBadge));
            ImGui::PopFont();
        }
        // Name and details.
        const float textY = t1.y + Px(3.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, p.text);
        ImGui::RenderTextEllipsis(dl, ImVec2(c0.x, textY), ImVec2(c0.x + cardW, textY + lineH), c0.x + cardW, it.name.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
        std::string detail;
        if (it.probe == 1) {
            detail = it.isVideo ? StrPrintf("%ux%u  %s", it.width, it.height, FormatDuration(it.duration).c_str()) : StrPrintf("%ux%u", it.width, it.height);
            if (it.isVideo && (it.inSec > 0.0 || it.outSec > 0.0))
                detail += StrPrintf("  [%s\xE2\x80\x93%s]", FormatDuration(it.inSec).c_str(), FormatDuration(it.outSec > 0.0 ? it.outSec : it.duration).c_str());
        } else if (it.probe == 2) {
            detail = TR(StateUnreadable);
        } else {
            detail = "\xE2\x80\xA6";
        }
        ImGui::PushFont(nullptr, style.FontSizeBase * 0.88f);
        ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
        ImGui::RenderTextEllipsis(dl, ImVec2(c0.x, textY + lineH), ImVec2(c0.x + cardW, textY + lineH * 2.0f), c0.x + cardW, detail.c_str(), nullptr, nullptr);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        // The selection box at the top left of the picture: under the mouse, on a selected card, and on every card
        // while any is selected.
        if (it.probe != 2) {
            const float boxA = std::max({ hov, sel, anySelected ? 1.0f : 0.0f });
            if (boxA > 0.01f) {
                const float box = std::round(frameH * 0.66f);
                ImGui::SetCursorScreenPos(ImVec2(c0.x + Px(6.0f), c0.y + Px(6.0f) - std::round((frameH - box) * 0.5f)));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * boxA);
                ImGui::BeginDisabled(running);
                if (Checkbox("##sel", &it.selected)) m_lastClicked = it.id;
                ImGui::EndDisabled();
                ImGui::PopStyleVar();
                onControl |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            }
        }
        // The remove button at the bottom right of the picture, under the mouse: white on a dark square in both themes.
        if (hov > 0.01f && !running && it.state != LibraryItem::Processing) {
            const float bs = std::round(frameH * 0.8f);
            const ImVec2 b0(t1.x - bs - Px(4.0f), t1.y - bs - Px(4.0f));
            ImGui::SetCursorScreenPos(b0);
            const bool remove = ImGui::InvisibleButton("##remove", ImVec2(bs, bs));
            const bool overRemove = ImGui::IsItemHovered();
            dl->AddRectFilled(b0, ImVec2(b0.x + bs, b0.y + bs), WithAlpha(IM_COL32(0, 0, 0, 255), (overRemove ? 0.75f : 0.55f) * hov), Px(4.0f));
            DrawIcon(dl, Icon::Close, ImVec2(b0.x + bs * 0.5f, b0.y + bs * 0.5f), IconSize(0.8f), WithAlpha(IM_COL32(255, 255, 255, 255), hov));
            Tooltip(TR(Remove));
            if (remove) ev.libraryRemove = it.id;
            onControl |= overRemove;
        }
        overControl |= onControl;
        if (hovered && !dragging && !menuOpen && !onControl) {
            if (it.probe == 2 && !it.error.empty()) TooltipShow(it.id, it.error.c_str());
            else if (it.state == LibraryItem::Failed && !it.error.empty()) TooltipShow(it.id, it.error.c_str());
            else if (it.state == LibraryItem::Done && !it.outName.empty()) TooltipShow(it.id, StrPrintf("%s: %s", TR(Saved), it.outName.c_str()).c_str());
            else TooltipShow(it.id, it.name.c_str());
        }
        // A double click opens the file in the preview.
        if (hovered && !onControl && !running && !menuOpen && it.probe != 2 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ev.libraryPreview = it.id;
            m_lastClicked = it.id;
            m_libDrag = false;
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + (float)count * (cardW + style.ItemSpacing.x) - style.ItemSpacing.x, rowStart.y));
    ImGui::Dummy(ImVec2(margin, cardH));
    // Presses on the strip: a click selects the card alone (Ctrl toggles it, Shift extends from the last one), a drag
    // selects a range; a double click (above) opens the file in the preview.
    if (!m_libDrag && stripHovered && !overControl && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !menuOpen) {
        m_libDrag = true;
        m_libDragMoved = false;
        m_libDragStart = toContent(io.MousePos);
        m_libDragKeep.clear();
        for (const auto& it : *lib) if (it.selected) m_libDragKeep.push_back(it.id);
    }
    if (m_libDrag) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 a = toScreen(m_libDragStart);
            if (!m_libDragMoved && !running && (std::fabs(io.MousePos.x - a.x) > Px(4.0f) || std::fabs(io.MousePos.y - a.y) > Px(4.0f))) m_libDragMoved = true;
        } else {
            if (!m_libDragMoved && underCursor && !running) {
                LibraryItem* item = nullptr;
                for (auto& it : *lib) if (it.id == underCursor) item = &it;
                if (item && item->probe != 2) {
                    if (io.KeyCtrl) {
                        item->selected = !item->selected;
                        m_lastClicked = item->id;
                    } else if (io.KeyShift && m_lastClicked) {
                        size_t from = lib->size(), to = lib->size();
                        for (size_t i = 0; i < lib->size(); ++i) { if ((*lib)[i].id == m_lastClicked) from = i; if ((*lib)[i].id == item->id) to = i; }
                        if (from < lib->size() && to < lib->size()) {
                            for (size_t i = std::min(from, to); i <= std::max(from, to); ++i) if ((*lib)[i].probe != 2) (*lib)[i].selected = true;
                        } else {
                            item->selected = true;
                        }
                    } else {
                        for (auto& o : *lib) o.selected = o.id == item->id && o.probe != 2;
                        m_lastClicked = item->id;
                    }
                }
            } else if (!m_libDragMoved && !underCursor && !running && !io.KeyCtrl && !io.KeyShift) {
                for (auto& o : *lib) o.selected = false;   // a click on the empty part of the strip, as in Explorer
            }
            m_libDrag = false;
            m_libDragMoved = false;
        }
    }
    if (dragging) {
        dl->AddRectFilled(marquee.Min, marquee.Max, WithAlpha(p.accent, 0.14f), Px(3.0f));
        dl->AddRect(marquee.Min, marquee.Max, WithAlpha(p.accent, 0.8f), Px(3.0f));
    }
    if (menuCard) { m_ctxItem = menuCard; m_libDrag = false; ImGui::OpenPopup("##libctx"); }
    DrawLibraryMenu(s, info, ev);
    ModeFadeContent(vtxStrip);
    ImGui::EndChild();
    ImGui::EndChild();
}

// The menu of a card: the file in Explorer, its own values (for every selected file when the card is one of
// several selected), out of the library.
void MainUI::DrawLibraryMenu(Settings& /*s*/, const UiFrameInfo& info, UiEvents& ev) {
    if (!BeginPopupFade("##libctx")) return;
    LibraryItem* item = nullptr;
    if (info.library) for (auto& it : *info.library) if (it.id == m_ctxItem) item = &it;
    if (!item) { ImGui::CloseCurrentPopup(); EndPopupFade(); return; }
    const bool running = info.batchRunning;
    int selectedCount = 0;
    if (item->selected) for (const auto& it : *info.library) if (it.selected && it.probe != 2) ++selectedCount;
    const bool group = item->selected && selectedCount > 1;
    ImGui::TextDisabled("%s", item->name.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem(TR(LocateFile))) ev.libraryLocate = item->id;
    ImGui::BeginDisabled(running || item->probe == 2);
    if (group && ImGui::MenuItem(StrPrintf(TR(ItemParamsMany), selectedCount).c_str(), nullptr, item->useOwn)) {
        m_paramsItems.clear();
        m_paramsItems.push_back(item->id);   // the clicked file leads: its values are shown and copied to the others
        for (const auto& it : *info.library) if (it.selected && it.probe != 2 && it.id != item->id) m_paramsItems.push_back(it.id);
        ev.libraryPreview = item->id;
    }
    if (ImGui::MenuItem(TR(ItemParams), nullptr, item->useOwn)) { m_paramsItems.assign(1, item->id); ev.libraryPreview = item->id; }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(running || item->state == LibraryItem::Processing);
    if (ImGui::MenuItem(TR(RemoveFromLibrary))) ev.libraryRemove = item->id;
    ImGui::EndDisabled();
    EndPopupFade();
}

// A floating window with the DLSS 5 controls of one library item, or of several selected at once. Their values
// replace the sidebar's for those files, in the preview and when they are processed. With several files the first
// one is edited and every change is copied to the others.
void MainUI::DrawItemParams(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& /*fonts*/) {
    if (m_paramsItems.empty()) return;
    std::vector<LibraryItem*> items;
    if (info.library) {
        for (unsigned id : m_paramsItems)
            for (auto& it : *info.library) if (it.id == id && it.probe != 2) { items.push_back(&it); break; }
    }
    if (items.empty()) { m_paramsItems.clear(); return; }
    LibraryItem* lead = items.front();
    const int n = (int)items.size();
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 27.0f, ImGui::GetFontSize() * 32.0f), ImGuiCond_FirstUseEver);
    const std::string title = (n > 1 ? StrPrintf(TR(OwnParamsMany), n) : StrPrintf(TR(OwnParamsTitle), lead->name.c_str())) + "###itemparams";
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoScrollWithMouse)) {
        WindowShadow();
        SmoothScroll(false, ImGui::GetFontSize() * 3.6f);
        ImGui::PushItemWidth(-LabelColumn(ImGui::GetContentRegionAvail().x));
        const bool locked = info.batchRunning || info.videoProcessing || info.videoFinishing;
        ImGui::BeginDisabled(locked);
        bool changed = false;
        bool useOwn = lead->useOwn;
        if (Toggle(n > 1 ? TR(UseOwnParamsMany) : TR(UseOwnParams), &useOwn)) {
            if (useOwn && !lead->own) lead->own = std::make_shared<Settings>(s);
            for (LibraryItem* it : items) {
                it->useOwn = useOwn;
                if (useOwn && it != lead) it->own = std::make_shared<Settings>(*lead->own);
            }
            changed = true;
        }
        Hint(n > 1 ? TR(OwnParamsManyHint) : TR(OwnParamsHint));
        if (n > 1) {
            std::string names;
            for (int i = 0; i < n && i < 8; ++i) { if (i) names += ", "; names += items[i]->name; }
            if (n > 8) names += ", \xE2\x80\xA6";
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", names.c_str());
            ImGui::PopStyleColor();
        }
        if (lead->useOwn && lead->own) {
            ImGui::Spacing();
            UiEvents sub;
            EffectControls(*lead->own, sub, m_advShown, s.nrEnabled, nullptr);
            if (sub.nrChanged || sub.settingsChanged) {
                changed = true;
                for (LibraryItem* it : items) {
                    if (it == lead) continue;
                    if (!it->own) it->own = std::make_shared<Settings>(*lead->own);
                    else it->own->CopyEffects(*lead->own);
                    it->useOwn = true;
                }
            }
            if (sub.resetHistory) ev.resetHistory = true;
        }
        ImGui::EndDisabled();
        if (changed) ev.itemParamsChanged = true;
        ImGui::PopItemWidth();
    }
    ImGui::End();
    if (!open) m_paramsItems.clear();
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawStatusBar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& /*fonts*/) {
    const Palette& p = Colors();
    const bool open = ImGui::BeginChild("##status", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) { ImGui::EndChild(); return; }
    ImGui::SetScrollX(0.0f);
    ImGui::SetScrollY(0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int vtx0 = dl->VtxBuffer.Size;
    const ImVec2 wpos = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
    const float frameH = ImGui::GetFrameHeight();
    m_toastBottom = wpos.y - Px(6.0f);   // notices rise from just above the bar
    const float rowY = wpos.y + std::floor((wsz.y - frameH) * 0.5f);
    const float x0 = wpos.x, x1 = wpos.x + wsz.x;
    // What the program is doing, at the left: the batch, the video run, or the source and its size.
    std::string line, detail;
    ImU32 dot = p.muted;
    const char* kDot = "  \xC2\xB7  ";
    auto leftText = [&](double seconds) { return kDot + StrPrintf(TR(RemainingFmt), ("\xE2\x89\x88 " + FormatEstimate(seconds)).c_str()); };
    if (info.batchRunning) {
        line = StrPrintf(TR(BatchRunning), std::min(info.batchIndex + 1, info.batchCount), info.batchCount);
        detail = info.batchItemName;
        const double left = BatchRemaining(info);
        if (left >= 0.0) detail += leftText(left);
        dot = p.accentHover;
    } else if (s.sourceMode == SourceVideo) {
        if (info.videoProcessing) {
            const unsigned long long total = std::max(info.videoFrames, info.videoFrame);
            line = info.videoName;
            detail = StrPrintf(TR(FrameOf), (unsigned long long)info.videoFrame, total);
            if (info.videoFrame >= 5 && info.videoElapsed > 0.5 && !info.videoFinishing)
                detail += leftText((double)(total - info.videoFrame) * info.videoElapsed / (double)info.videoFrame);
            dot = p.warn;
        } else if (info.videoLoaded) {
            line = info.videoName;
            detail = StrPrintf("%ux%u%s%.0f %s", info.videoWidth, info.videoHeight, kDot, info.videoFps, TR(Fps));
            dot = p.good;
        } else {
            line = TR(NoVideo);
        }
    } else if (s.sourceMode == SourceImage) {
        if (info.imageLoaded) { line = info.imageName; detail = StrPrintf("%ux%u", info.imageWidth, info.imageHeight); dot = p.good; }
        else line = TR(NoImage);
    } else if (info.sourceConnected && info.status) {
        line = info.senderName;
        detail = StrPrintf("%ux%u%s%.0f %s", info.status->srcWidth, info.status->srcHeight, kDot, m_shown.senderFps, TR(Fps));
        dot = p.good;
    } else {
        line = info.senders && !info.senders->empty() ? TR(StatusWaiting) : TR(StatusNoSpout);
    }
    // The last file saved at the right end, with a button that shows it in Explorer (or why the capture failed).
    float right = x1;
    if (info.lastSavedKnown && !info.lastSaved.empty()) {
        const std::string text = StrPrintf("%s: %s", info.lastSavedOk ? TR(Saved) : TR(CaptureFailed), info.lastSaved.c_str());
        const float btnW = info.lastSavedOk ? frameH : 0.0f;
        const float tw = std::min(ImGui::CalcTextSize(text.c_str()).x, std::max(0.0f, (x1 - x0) * 0.45f - btnW));
        if (tw > ImGui::GetFontSize() * 4.0f) {
            float bx = x1;
            if (info.lastSavedOk) {
                bx = x1 - frameH;
                ImGui::SetCursorScreenPos(ImVec2(bx, rowY));
                if (IconButton("##reveal", Icon::Folder, ImVec2(frameH, frameH), TR(LocateFile), ButtonKind::Plain)) ev.revealLastSaved = true;
                bx -= Px(4.0f);
            }
            const float th = ImGui::GetTextLineHeight();
            const float ty = rowY + std::floor((frameH - th) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, info.lastSavedOk ? p.textDim : p.bad);
            ImGui::RenderTextEllipsis(dl, ImVec2(bx - tw, ty), ImVec2(bx, ty + th), bx, text.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
            right = bx - tw - Px(24.0f);
        }
    }
    const float lineH = ImGui::GetTextLineHeight();
    const float ty = rowY + std::floor((frameH - lineH) * 0.5f);
    if (m_advShown > 0.001f && info.status) {
        // The pass timings, for those who asked for the details. Each figure sits right-aligned in a slot as wide as
        // "000.00 ms" (the interface font's digits all have one width), so nothing moves when a number changes.
        const double ms[4] = { m_shown.gpuMs[(UINT)GpuTimer::Guidance] + m_shown.gpuMs[(UINT)GpuTimer::OpticalFlow],
                               m_shown.gpuMs[(UINT)GpuTimer::Neural], m_shown.gpuMs[(UINT)GpuTimer::Composite], m_shown.uiGpuMs };
        const char* names[4] = { TR(TmGuidance), TR(TmNeural), TR(TmComposite), TR(TmUi) };
        const float slotW = ImGui::CalcTextSize("000.00 ms").x;
        const float nameGap = Px(5.0f), sepW = Px(20.0f);
        float tw = 0.0f;
        for (int i = 0; i < 4; ++i) tw += ImGui::CalcTextSize(names[i]).x + nameGap + slotW + (i < 3 ? sepW : 0.0f);
        if (right - tw - Px(24.0f) - x0 >= ImGui::GetFontSize() * 16.0f) {
            const int vtxTimes = dl->VtxBuffer.Size;
            float x = right - tw;
            for (int i = 0; i < 4; ++i) {
                dl->AddText(ImVec2(x, ty), p.textDim, names[i]);
                x += ImGui::CalcTextSize(names[i]).x + nameGap;
                const std::string v = StrPrintf("%.2f ms", ms[i]);
                dl->AddText(ImVec2(x + slotW - ImGui::CalcTextSize(v.c_str()).x, ty), Mix(p.textDim, p.text, 0.6f), v.c_str());
                x += slotW;
                if (i < 3) {
                    dl->AddCircleFilled(ImVec2(std::round(x + sepW * 0.5f), ty + lineH * 0.5f), std::max(1.0f, Px(1.5f)), WithAlpha(p.textDim, 0.7f), 0);
                    x += sepW;
                }
            }
            FadeDrawn(dl, vtxTimes, m_advShown, 0.0f);   // they come and go with the Advanced switch
            right -= tw + Px(24.0f);
        }
    }
    DotLine(dl, ImVec2(x0, ty), lineH, right, dot, p.text, line.c_str(), detail.c_str());
    ModeFadeContent(vtx0);
    ImGui::EndChild();
}

void MainUI::DrawLogWindow(Settings& s, UiEvents& ev, const Fonts& fonts) {
    ImGui::SetNextWindowSize(ImVec2(Px(760.0f), Px(420.0f)), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR(LogTitle), &s.showLog)) {
        WindowShadow();
        if (FlatButton(TR(OpenLogFile))) ev.openLogFile = true;
        ImGui::SameLine();
        if (FlatButton(TR(Clear))) { m_logCache.clear(); m_logGeneration = Log::Generation(); }
        if (Log::Generation() != m_logGeneration) { m_logCache = Log::Snapshot(); m_logGeneration = Log::Generation(); }
        ImGui::BeginChild("##logtext", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollWithMouse);
        SmoothScroll(false, ImGui::GetFontSize() * 3.6f);
        ImGui::PushFont(fonts.Mono(), ImGui::GetStyle().FontSizeBase * 0.92f);
        const Palette& p = Colors();
        for (const auto& e : m_logCache) {
            const ImU32 col = e.level == LogLevel::Error ? p.bad : e.level == LogLevel::Warn ? p.warn : ImGui::GetColorU32(ImGuiCol_Text);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextWrapped("%s  %s", e.time.c_str(), e.text.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
        ImGui::EndChild();
    }
    ImGui::End();
    if (!s.showLog) ev.settingsChanged = true;
}

void MainUI::DrawToasts(const Fonts& fonts) {
    const double now = ImGui::GetTime();
    auto lifetime = [](const ToastItem& t) { return t.error ? 7.0 : 4.5; };   // an error stays longer
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [&](const ToastItem& t) { return now - t.time > lifetime(t); }), m_toasts.end());
    if (m_toasts.empty()) return;
    const Palette& p = Colors();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGui::PushFont(fonts.Ui(), 0.0f);
    const float wrap = std::min(vp->WorkSize.x * 0.5f, ImGui::GetFontSize() * 36.0f);
    const float is = IconSize();
    const float lineH = ImGui::GetTextLineHeight();
    const ImVec2 pad(Px(14.0f), Px(10.0f));
    const float rightX = vp->WorkPos.x + vp->WorkSize.x - Px(16.0f);
    // Cards stacked upward from just above the status bar (or the fullscreen video controls), newest at the bottom.
    float y = m_toastBottom > 0.0f ? m_toastBottom : vp->WorkPos.y + vp->WorkSize.y - Px(16.0f);
    for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
        const double age = now - it->time;
        const double life = lifetime(*it);
        const float alpha = (float)std::clamp(std::min(age / 0.2, (life - age) / 0.6), 0.0, 1.0);
        const float slide = (1.0f - Ease((float)std::clamp(age / 0.25, 0.0, 1.0))) * Px(24.0f);
        const ImVec2 ts = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, wrap);
        const ImVec2 size(pad.x + is + Px(10.0f) + ts.x + pad.x, std::max(ts.y, is) + pad.y * 2.0f);
        const ImVec2 pos(std::round(rightX - size.x + slide), std::round(y - size.y));
        const ImVec2 max(pos.x + size.x, pos.y + size.y);
        DrawShadow(dl, pos, max, Px(8.0f), alpha);
        dl->AddRectFilled(pos, max, WithAlpha(p.card, alpha), Px(8.0f));
        dl->AddRect(pos, max, WithAlpha(p.cardBorder, alpha), Px(8.0f), 1.0f);
        const Icon icon = it->error ? Icon::CircleX : it->success ? Icon::CircleCheck : Icon::Info;
        const ImU32 iconCol = it->error ? p.bad : it->success ? p.good : p.accent;
        DrawIcon(dl, icon, ImVec2(pos.x + pad.x + is * 0.5f, pos.y + pad.y + lineH * 0.5f), is, WithAlpha(iconCol, alpha));
        dl->AddText(nullptr, 0.0f, ImVec2(pos.x + pad.x + is + Px(10.0f), pos.y + pad.y), WithAlpha(p.text, alpha), it->text.c_str(), nullptr, wrap);
        y = pos.y - Px(8.0f);
    }
    ImGui::PopFont();
}

// Presets ------------------------------------------------------------------------------------

// The user's presets of the effect values: a list to apply one, with overwrite, rename and delete on each row, and
// a button that saves the current values under a new name. The list shows the preset whose values are the current
// ones, or "Custom".
void MainUI::DrawPresetRow(Settings& s, UiEvents& ev) {
    if (!SearchMatch(TR(Preset), TR(TipPresets))) return;
    const ImGuiStyle& style = ImGui::GetStyle();
    static const std::vector<UserPreset> kNone;
    const std::vector<UserPreset>& presets = m_presetList ? *m_presetList : kNone;
    const std::string current = s.EffectText();
    int match = -1;
    for (size_t i = 0; i < presets.size() && match < 0; ++i) if (presets[i].text == current) match = (int)i;
    const float frameH = ImGui::GetFrameHeight();
    auto apply = [&](const std::string& text) {
        Settings tmp = s;
        tmp.ApplyText(text);
        s.CopyEffects(tmp);
        ev.nrChanged = true; ev.settingsChanged = true;
    };
    ImGui::PushID("presets");
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - frameH - style.ItemInnerSpacing.x);
    const char* preview = match >= 0 ? presets[match].name.c_str() : presets.empty() ? TR(PresetNone) : TR(PresetCustom);
    if (BeginDropdown("##list", preview)) {
        if (presets.empty()) ImGui::TextDisabled("%s", TR(PresetEmptyHint));
        const float toolsW = frameH * 3.0f + style.ItemInnerSpacing.x * 2.0f;
        const float rowW = std::max(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 14.0f);
        for (int i = 0; i < (int)presets.size(); ++i) {
            const UserPreset& pr = presets[i];
            ImGui::PushID(i);
            if (m_presetEdit == i) {
                // The name is typed in place; Enter keeps it, Esc leaves it as it was.
                ImGui::SetNextItemWidth(rowW - frameH - style.ItemInnerSpacing.x);
                if (m_presetFocus) { ImGui::SetKeyboardFocusHere(); m_presetFocus = false; }
                if (ImGui::InputText("##rename", m_presetBuf, sizeof(m_presetBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    ev.presetRename = i; ev.presetName = m_presetBuf; m_presetEdit = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_presetEdit = -1;
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (IconButton("##ok", Icon::Check, ImVec2(frameH, frameH), TR(Ok), ButtonKind::Accent)) { ev.presetRename = i; ev.presetName = m_presetBuf; m_presetEdit = -1; }
            } else {
                if (ImGui::Selectable(pr.name.c_str(), match == i, ImGuiSelectableFlags_None, ImVec2(rowW - toolsW - style.ItemSpacing.x, 0.0f))) apply(pr.text);
                ImGui::SameLine(0.0f, style.ItemSpacing.x);
                if (IconButton("##over", Icon::Save, ImVec2(frameH, frameH), TR(PresetOverwrite), ButtonKind::Plain)) { ev.presetSave = true; ev.presetName = pr.name; }
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (IconButton("##ren", Icon::Edit, ImVec2(frameH, frameH), TR(PresetRename), ButtonKind::Plain)) {
                    m_presetEdit = i; m_presetFocus = true;
                    std::snprintf(m_presetBuf, sizeof(m_presetBuf), "%s", pr.name.c_str());
                }
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
                if (IconButton("##del", Icon::Trash, ImVec2(frameH, frameH), TR(PresetDelete), ButtonKind::Plain)) ev.presetDelete = i;
            }
            ImGui::PopID();
        }
        EndDropdown();
    } else {
        m_presetEdit = -1;
    }
    Tooltip(TR(TipPresets));
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (IconButton("##add", Icon::Plus, ImVec2(frameH, frameH), TR(PresetSaveAs), ButtonKind::Plain)) {
        m_presetBuf[0] = 0;
        m_presetFocus = true;
        ImGui::OpenPopup("##presetname");
    }
    if (BeginPopupFade("##presetname")) {
        ImGui::TextUnformatted(TR(PresetName));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        if (m_presetFocus) { ImGui::SetKeyboardFocusHere(); m_presetFocus = false; }
        bool save = ImGui::InputText("##name", m_presetBuf, sizeof(m_presetBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        ImGui::BeginDisabled(m_presetBuf[0] == 0);
        if (AccentButton(TR(Save), ImVec2(ImGui::GetFontSize() * 7.5f, 0))) save = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (FlatButton(TR(Cancel), ImVec2(ImGui::GetFontSize() * 7.5f, 0))) ImGui::CloseCurrentPopup();
        if (save && m_presetBuf[0]) { ev.presetSave = true; ev.presetName = m_presetBuf; ImGui::CloseCurrentPopup(); }
        EndPopupFade();
    }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    TrailingLabel(TR(Preset));
    ImGui::PopID();
}

// Turn, mirror, crop -------------------------------------------------------------------------

namespace {
// The picture the passes see is the raw one mirrored, then turned, then cropped (the Convert pass). The buttons act
// on the picture as it is shown, so a turn carries the crop and the mirrors along with it.
void TurnTransform(SourceTransform& x, bool right) {
    x.rotate = (x.rotate + (right ? 1 : 3)) & 3;
    const float cx = x.cropX, cy = x.cropY, cw = x.cropW, ch = x.cropH;
    if (right) { x.cropX = 1.0f - cy - ch; x.cropY = cx; } else { x.cropX = cy; x.cropY = 1.0f - cx - cw; }
    x.cropW = ch; x.cropH = cw;
}
void MirrorTransform(SourceTransform& x, bool horizontal) {
    // Mirroring the shown picture is a mirror of the raw one about the other axis after a quarter turn.
    const bool quarter = (x.rotate & 1) != 0;
    if (horizontal != quarter) x.flipH = !x.flipH; else x.flipV = !x.flipV;
    if (horizontal) x.cropX = 1.0f - x.cropX - x.cropW; else x.cropY = 1.0f - x.cropY - x.cropH;
}
}

// A translucent row of tools at the bottom of the picture for the library file that is shown: turn left and right,
// mirror, crop and back to the file as it comes. Cropping darkens the outside of a rectangle with eight handles on
// the full picture; Apply (Enter) keeps it, Cancel (Esc) drops it. The live picture has the same row without the
// crop; its turn and mirrors are settings, so the preview, the captures and the timelapse all follow them.
void MainUI::DrawTransformTools(Settings& s, const UiFrameInfo& info, UiEvents& ev, const ImVec2& origin, const ImVec2& region,
                                const ImVec2& imgPos, const ImVec2& imgSize, bool canvasHovered) {
    LibraryItem* item = nullptr;
    if (info.shownItem && info.library) for (auto& it : *info.library) if (it.id == info.shownItem) { item = &it; break; }
    const bool live = !item && s.sourceMode == SourceSpout && info.sourceConnected;
    const bool locked = info.batchRunning || info.videoProcessing || info.videoFinishing;
    const float bsz = ImGui::GetFrameHeight();
    m_toolRowMin = m_toolRowMax = ImVec2(0.0f, 0.0f);
    if ((!item && !live) || info.fullscreen || locked || region.x < bsz * 10.0f || region.y < bsz * 4.0f) { m_cropEditing = false; m_cropHandle = -1; return; }
    if (m_cropEditing && (!item || m_cropItem != item->id)) m_cropEditing = false;
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gap = Px(4.0f), pad = Px(6.0f);

    if (m_cropEditing) {
        ev.cropEditing = true;
        ev.cropPreview = item->transform;
        SourceTransform& c = m_cropWork;
        auto toScreen = [&](float nx, float ny) { return ImVec2(imgPos.x + imgSize.x * nx, imgPos.y + imgSize.y * ny); };
        const ImVec2 r0 = toScreen(c.cropX, c.cropY), r1 = toScreen(c.cropX + c.cropW, c.cropY + c.cropH);
        // Corners, then the middles of the edges, clockwise from the top left.
        const ImVec2 handles[8] = { r0, ImVec2(r1.x, r0.y), r1, ImVec2(r0.x, r1.y),
                                    ImVec2((r0.x + r1.x) * 0.5f, r0.y), ImVec2(r1.x, (r0.y + r1.y) * 0.5f),
                                    ImVec2((r0.x + r1.x) * 0.5f, r1.y), ImVec2(r0.x, (r0.y + r1.y) * 0.5f) };
        int over = -1;
        if (canvasHovered) {
            for (int i = 0; i < 8 && over < 0; ++i)
                if (std::fabs(io.MousePos.x - handles[i].x) <= Px(9.0f) && std::fabs(io.MousePos.y - handles[i].y) <= Px(9.0f)) over = i;
            if (over < 0 && io.MousePos.x > r0.x && io.MousePos.x < r1.x && io.MousePos.y > r0.y && io.MousePos.y < r1.y) over = 8;
        }
        if (over >= 0 && m_cropHandle < 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { m_cropHandle = over; m_cropDragStart = io.MousePos; m_cropDragBase = c; }
        if (m_cropHandle >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_cropHandle = -1;
        const int lit = m_cropHandle >= 0 ? m_cropHandle : over;
        if (lit >= 0) {
            static const ImGuiMouseCursor cursors[9] = { ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNESW, ImGuiMouseCursor_ResizeNWSE, ImGuiMouseCursor_ResizeNESW,
                                                         ImGuiMouseCursor_ResizeNS, ImGuiMouseCursor_ResizeEW, ImGuiMouseCursor_ResizeNS, ImGuiMouseCursor_ResizeEW,
                                                         ImGuiMouseCursor_ResizeAll };
            ImGui::SetMouseCursor(cursors[lit]);
        }
        if (m_cropHandle >= 0 && imgSize.x > 0.0f && imgSize.y > 0.0f) {
            const float dx = (io.MousePos.x - m_cropDragStart.x) / imgSize.x, dy = (io.MousePos.y - m_cropDragStart.y) / imgSize.y;
            const SourceTransform& b = m_cropDragBase;
            constexpr float kMin = 0.02f;   // the smallest side of the rectangle
            float x0 = b.cropX, y0 = b.cropY, x1 = b.cropX + b.cropW, y1 = b.cropY + b.cropH;
            const int h = m_cropHandle;
            if (h == 8) {
                x0 = std::clamp(b.cropX + dx, 0.0f, 1.0f - b.cropW); y0 = std::clamp(b.cropY + dy, 0.0f, 1.0f - b.cropH);
                x1 = x0 + b.cropW; y1 = y0 + b.cropH;
            } else {
                if (h == 0 || h == 3 || h == 7) x0 = std::clamp(b.cropX + dx, 0.0f, x1 - kMin);
                if (h == 1 || h == 2 || h == 5) x1 = std::clamp(b.cropX + b.cropW + dx, x0 + kMin, 1.0f);
                if (h == 0 || h == 1 || h == 4) y0 = std::clamp(b.cropY + dy, 0.0f, y1 - kMin);
                if (h == 2 || h == 3 || h == 6) y1 = std::clamp(b.cropY + b.cropH + dy, y0 + kMin, 1.0f);
            }
            c.cropX = x0; c.cropY = y0; c.cropW = x1 - x0; c.cropH = y1 - y0;
        }
        // The outside darkened, the rectangle with its thirds, the handles.
        dl->PushClipRect(origin, ImVec2(origin.x + region.x, origin.y + region.y), true);
        const ImU32 shade = IM_COL32(0, 0, 0, 120);
        const ImVec2 imgMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);
        dl->AddRectFilled(imgPos, ImVec2(imgMax.x, r0.y), shade);
        dl->AddRectFilled(ImVec2(imgPos.x, r1.y), imgMax, shade);
        dl->AddRectFilled(ImVec2(imgPos.x, r0.y), ImVec2(r0.x, r1.y), shade);
        dl->AddRectFilled(ImVec2(r1.x, r0.y), ImVec2(imgMax.x, r1.y), shade);
        const ImU32 thin = IM_COL32(255, 255, 255, 90);
        for (int i = 1; i < 3; ++i) {
            const float fx = r0.x + (r1.x - r0.x) * (float)i / 3.0f, fy = r0.y + (r1.y - r0.y) * (float)i / 3.0f;
            dl->AddLine(ImVec2(fx, r0.y), ImVec2(fx, r1.y), thin);
            dl->AddLine(ImVec2(r0.x, fy), ImVec2(r1.x, fy), thin);
        }
        dl->AddRect(r0, r1, IM_COL32(255, 255, 255, 230), 0.0f, std::max(1.0f, 1.5f * Dpi()));
        for (int i = 0; i < 8; ++i)
            dl->AddRectFilled(ImVec2(handles[i].x - Px(5.0f), handles[i].y - Px(5.0f)), ImVec2(handles[i].x + Px(5.0f), handles[i].y + Px(5.0f)),
                              i == lit ? p.accent : IM_COL32(255, 255, 255, 235), Px(2.0f));
        dl->PopClipRect();
        // Apply and cancel in a pill at the bottom, with a line on how the rectangle is moved above it.
        const float applyW = ImGui::CalcTextSize(TR(CropApply)).x + style.FramePadding.x * 2.0f + Px(16.0f);
        const float cancelW = ImGui::CalcTextSize(TR(Cancel)).x + style.FramePadding.x * 2.0f + Px(16.0f);
        const float pillW = applyW + cancelW + gap + pad * 2.0f, pillH = bsz + pad * 2.0f;
        const ImVec2 pill(origin.x + (region.x - pillW) * 0.5f, origin.y + region.y - pillH - Px(12.0f));
        const ImVec2 hintSize = ImGui::CalcTextSize(TR(CropHint));
        if (hintSize.x + Px(24.0f) < region.x) {
            const ImVec2 h0(origin.x + (region.x - hintSize.x) * 0.5f - Px(8.0f), pill.y - hintSize.y - Px(14.0f));
            dl->AddRectFilled(h0, ImVec2(h0.x + hintSize.x + Px(16.0f), h0.y + hintSize.y + Px(8.0f)), p.overlayBg, Px(6.0f));
            dl->AddText(ImVec2(h0.x + Px(8.0f), h0.y + Px(4.0f)), p.text, TR(CropHint));
        }
        dl->AddRectFilled(pill, ImVec2(pill.x + pillW, pill.y + pillH), p.overlayBg, style.FrameRounding + pad);
        ImGui::SetCursorScreenPos(ImVec2(pill.x + pad, pill.y + pad));
        bool apply = AccentButton(TR(CropApply), ImVec2(applyW, bsz));
        ImGui::SameLine(0.0f, gap);
        bool cancel = FlatButton(TR(Cancel), ImVec2(cancelW, bsz));
        if (!io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) apply = true;
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) cancel = true;
        }
        if (apply) item->transform = m_cropWork;
        if (apply || cancel) { m_cropEditing = false; m_cropHandle = -1; }
        return;
    }

    // The tool row: a translucent pill that comes forward under the mouse. The grip at its left end moves it, and
    // it tucks away at an edge of the picture, dropped past that edge or with the arrow at its right end; a small
    // tab at the edge brings it back. Its place (fractions of the picture area) and the edge are kept in the settings.
    const float pi = 3.14159265358979f;
    const float gripW = std::round(bsz * 0.5f), tuckW = std::round(bsz * 0.75f);
    const int nBtn = item ? 6 : 5;
    const float pillW = gripW + tuckW + nBtn * bsz + (nBtn + 1) * gap + pad * 2.0f, pillH = bsz + pad * 2.0f;
    const ImVec2 lo(origin.x + pillW * 0.5f + Px(4.0f), origin.y + pillH * 0.5f + Px(4.0f));                 // the range of the centre
    const ImVec2 hi(origin.x + region.x - pillW * 0.5f - Px(4.0f), origin.y + region.y - pillH * 0.5f - Px(12.0f));
    auto inside = [&](const ImVec2& c) { return ImVec2(std::max(lo.x, std::min(c.x, hi.x)), std::max(lo.y, std::min(c.y, hi.y))); };
    ImVec2 centre = (s.toolRowX < 0.0f || s.toolRowY < 0.0f) ? ImVec2(origin.x + region.x * 0.5f, hi.y)
                                                              : inside(ImVec2(origin.x + s.toolRowX * region.x, origin.y + s.toolRowY * region.y));
    int edgeUnder = 0;   // while it is dragged: the edge the row has been taken past (1 left, 2 right, 3 top, 4 bottom)
    if (m_toolRowDragging) {
        const ImVec2 want(io.MousePos.x - m_toolRowDragOffset.x, io.MousePos.y - m_toolRowDragOffset.y);
        const float out[4] = { origin.x - want.x, want.x - (origin.x + region.x), origin.y - want.y, want.y - (origin.y + region.y) };
        float most = 0.0f;
        for (int i = 0; i < 4; ++i) if (out[i] > most) { most = out[i]; edgeUnder = i + 1; }
        centre = inside(want);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {   // dropped: past an edge, it tucks away there
            m_toolRowDragging = false;
            s.toolRowX = (centre.x - origin.x) / region.x; s.toolRowY = (centre.y - origin.y) / region.y;
            s.toolRowDock = edgeUnder;
            ev.settingsChanged = true;
            edgeUnder = 0;
        }
    }
    if (edgeUnder) {   // the edge the row would tuck away at lights up
        const ImU32 glow = WithAlpha(p.accent, 0.7f);
        const float t = Px(3.0f);
        switch (edgeUnder) {
        case 1:  dl->AddRectFilled(origin, ImVec2(origin.x + t, origin.y + region.y), glow); break;
        case 2:  dl->AddRectFilled(ImVec2(origin.x + region.x - t, origin.y), ImVec2(origin.x + region.x, origin.y + region.y), glow); break;
        case 3:  dl->AddRectFilled(origin, ImVec2(origin.x + region.x, origin.y + t), glow); break;
        default: dl->AddRectFilled(ImVec2(origin.x, origin.y + region.y - t), ImVec2(origin.x + region.x, origin.y + region.y), glow); break;
        }
    }

    if (s.toolRowDock != 0) {
        // Tucked away: a tab at the edge, where the row was, with an arrow pointing into the picture.
        const int dock = std::clamp(s.toolRowDock, 1, 4);
        const bool upright = dock <= 2;   // along the left or the right edge
        const float tabL = std::round(bsz * 2.2f), tabT = std::round(bsz * 0.6f);
        const float along = upright ? std::max(origin.y + Px(4.0f), std::min(centre.y - tabL * 0.5f, origin.y + region.y - tabL - Px(4.0f)))
                                    : std::max(origin.x + Px(4.0f), std::min(centre.x - tabL * 0.5f, origin.x + region.x - tabL - Px(4.0f)));
        ImVec2 a, b;
        ImDrawFlags corners = ImDrawFlags_RoundCornersTop;
        float angle = pi;
        switch (dock) {
        case 1:  a = ImVec2(origin.x, along); b = ImVec2(origin.x + tabT, along + tabL); corners = ImDrawFlags_RoundCornersRight; angle = -pi * 0.5f; break;
        case 2:  a = ImVec2(origin.x + region.x - tabT, along); b = ImVec2(origin.x + region.x, along + tabL); corners = ImDrawFlags_RoundCornersLeft; angle = pi * 0.5f; break;
        case 3:  a = ImVec2(along, origin.y); b = ImVec2(along + tabL, origin.y + tabT); corners = ImDrawFlags_RoundCornersBottom; angle = 0.0f; break;
        default: a = ImVec2(along, origin.y + region.y - tabT); b = ImVec2(along + tabL, origin.y + region.y); break;
        }
        ImGui::SetCursorScreenPos(a);
        const bool show = ImGui::InvisibleButton("##toolRowTab", ImVec2(b.x - a.x, b.y - a.y));
        const bool hot = ImGui::IsItemHovered();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        Tooltip(TR(TipToolRowShow));
        const float tabLift = Animate(ImGui::GetID("##toolRowTabLift"), hot ? 1.0f : 0.0f, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * (0.55f + 0.45f * tabLift));
        dl->AddRectFilled(a, b, ImGui::GetColorU32(p.overlayBg), Px(6.0f), corners);
        DrawChevron(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), bsz * 0.36f, angle, ImGui::GetColorU32(p.text));
        ImGui::PopStyleVar();
        if (show) { s.toolRowDock = 0; ev.settingsChanged = true; }
        return;
    }

    const ImVec2 pill(centre.x - pillW * 0.5f, centre.y - pillH * 0.5f);
    m_toolRowMin = pill; m_toolRowMax = ImVec2(pill.x + pillW, pill.y + pillH);
    const bool over = m_toolRowDragging || (ImGui::IsMousePosValid() && io.MousePos.x >= pill.x - Px(12.0f) && io.MousePos.x <= pill.x + pillW + Px(12.0f)
                                            && io.MousePos.y >= pill.y - Px(12.0f) && io.MousePos.y <= pill.y + pillH + Px(12.0f));
    const float lift = Animate(ImGui::GetID("##xformTools"), over ? 1.0f : 0.0f, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * (0.55f + 0.45f * lift));
    dl->AddRectFilled(pill, ImVec2(pill.x + pillW, pill.y + pillH), ImGui::GetColorU32(p.overlayBg), style.FrameRounding + pad);
    SourceTransform x = item ? item->transform : SourceTransform::Turned(s.spoutRotate, s.spoutFlipH, s.spoutFlipV);
    bool changed = false;
    // The grip: two columns of dots. A drag from it moves the row; it keeps the mouse's offset from the centre.
    ImGui::SetCursorScreenPos(ImVec2(pill.x + pad, pill.y + pad));
    ImGui::InvisibleButton("##toolRowGrip", ImVec2(gripW, bsz));
    if (ImGui::IsItemActivated()) m_toolRowDragOffset = ImVec2(io.MousePos.x - centre.x, io.MousePos.y - centre.y);
    if (ImGui::IsItemActive() && !m_toolRowDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) m_toolRowDragging = true;
    const bool gripHot = ImGui::IsItemHovered() || m_toolRowDragging;
    if (gripHot) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (!m_toolRowDragging) Tooltip(TR(TipToolRowGrip));
    {
        const ImVec2 gmin = ImGui::GetItemRectMin(), gmax = ImGui::GetItemRectMax();
        const ImVec2 gc((gmin.x + gmax.x) * 0.5f, (gmin.y + gmax.y) * 0.5f);
        const ImU32 dot = ImGui::GetColorU32(gripHot ? p.text : p.textDim);
        const float step = std::max(Px(4.0f), std::round(bsz * 0.18f));
        for (int col = -1; col <= 1; col += 2)
            for (int row = -1; row <= 1; ++row) dl->AddCircleFilled(ImVec2(gc.x + col * step * 0.5f, gc.y + row * step), 1.3f * Dpi(), dot);
    }
    ImGui::SameLine(0.0f, gap);
    if (IconButton("##turnL", Icon::RotateLeft, ImVec2(bsz, bsz), TR(TipRotateLeft), ButtonKind::Plain)) { TurnTransform(x, false); changed = true; }
    ImGui::SameLine(0.0f, gap);
    if (IconButton("##turnR", Icon::RotateRight, ImVec2(bsz, bsz), TR(TipRotateRight), ButtonKind::Plain)) { TurnTransform(x, true); changed = true; }
    ImGui::SameLine(0.0f, gap);
    if (IconButton("##flipH", Icon::FlipH, ImVec2(bsz, bsz), TR(TipFlipH), ButtonKind::Plain)) { MirrorTransform(x, true); changed = true; }
    ImGui::SameLine(0.0f, gap);
    if (IconButton("##flipV", Icon::FlipV, ImVec2(bsz, bsz), TR(TipFlipV), ButtonKind::Plain)) { MirrorTransform(x, false); changed = true; }
    if (item) {
        ImGui::SameLine(0.0f, gap);
        if (IconButton("##crop", Icon::Crop, ImVec2(bsz, bsz), TR(TipCrop), ButtonKind::Plain)) {
            m_cropEditing = true; m_cropItem = item->id; m_cropWork = item->transform; m_cropHandle = -1;
        }
    }
    ImGui::SameLine(0.0f, gap);
    ImGui::BeginDisabled(x.Identity());
    if (IconButton("##asItComes", Icon::Reset, ImVec2(bsz, bsz), TR(TipResetTransform), ButtonKind::Plain)) { x = SourceTransform(); changed = true; }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, gap);
    {
        // The arrow points at the nearest edge, where the row goes when it is pressed.
        const float d[4] = { centre.x - origin.x, origin.x + region.x - centre.x, centre.y - origin.y, origin.y + region.y - centre.y };
        int nearest = 3;
        for (int i = 0; i < 3; ++i) if (d[i] < d[nearest]) nearest = i;
        const float toEdge[4] = { pi * 0.5f, -pi * 0.5f, pi, 0.0f };
        if (ChevronButton("##toolRowTuck", toEdge[nearest], ImVec2(tuckW, bsz), TR(TipToolRowTuck), ButtonKind::Plain)) {
            s.toolRowX = (centre.x - origin.x) / region.x; s.toolRowY = (centre.y - origin.y) / region.y;
            s.toolRowDock = nearest + 1;
            ev.settingsChanged = true;
        }
    }
    ImGui::PopStyleVar();
    if (!changed) return;
    if (item) item->transform = x;
    else { s.spoutRotate = x.rotate; s.spoutFlipH = x.flipH; s.spoutFlipV = x.flipV; ev.settingsChanged = true; }
}

// Whether anything moves or waits for the user's hand: App draws at full rate while it does and rests otherwise.
bool MainUI::WantsFrames() const {
    if (Animating() || !m_toasts.empty()) return true;
    if (m_modePending >= 0 || m_modeFade > 0.0f || m_fsPending || m_fsRise) return true;
    const double now = ImGui::GetTime();
    if (m_startFade >= 0.0 && now - m_startFade < 0.6) return true;
    if (m_seekDragging || m_wipeDragging || m_toolRowDragging || m_libDrag || m_cropHandle >= 0 || m_seekTarget >= 0.0) return true;
    if ((m_spotUntil >= 0.0 && now < m_spotUntil) || m_scrollToAbout > 0.0) return true;
    if (m_mirrorBlockTime >= 0.0 && now - m_mirrorBlockTime < 0.5) return true;
    if (m_fullscreenControls > 0.0f && now - m_fullscreenMouseTime < 2.6) return true;
    if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return true;
    return ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput;
}

// Fades --------------------------------------------------------------------------------------

void MainUI::RequestFullscreen() {
    if (m_fsPending) return;
    m_fsPending = true;
    m_fsSent = false;
    m_fsRise = false;
    m_fsFadeStart = ImGui::GetTime();
}

// The sidebar, the library and the status bar dip their content in step with the preview's cover around a change
// of the source: what they drew this frame fades and drops a little, and the new content rises back into place.
void MainUI::ModeFadeContent(int fromVtx) {
    if (m_modeFade <= 0.001f) return;
    const float t = Ease(m_modeFade);
    FadeDrawn(ImGui::GetWindowDrawList(), fromVtx, 1.0f - t, ImGui::GetFontSize() * 0.45f * t);
}

// Painted last, over everything: the preview dips to its background around a change of the source, the whole
// window dips to black around the fullscreen switch, and the window comes up from its background once it is
// shown after the start-up card.
void MainUI::DrawFades(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const double now = ImGui::GetTime();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 vmin = vp->Pos, vmax(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    const Palette& p = Colors();

    // Source mode.
    constexpr double kDip = 0.12, kRise = 0.22;
    if (m_lastMode < 0) m_lastMode = s.sourceMode;
    if (m_modePending >= 0) {
        const double t = now - m_modeFadeStart;
        if (t < kDip) {
            m_modeFade = (float)(t / kDip);
        } else {
            s.sourceMode = m_modePending;
            ev.sourceModeChanged = true; ev.settingsChanged = true;
            m_modePending = -1;
            m_lastMode = s.sourceMode;
            m_modeFadeStart = now;
            m_modeFade = 1.0f;
        }
    } else if (s.sourceMode != m_lastMode) {   // changed elsewhere: a file dropped in, the library, the command line
        m_lastMode = s.sourceMode;
        m_modeFadeStart = now;
        m_modeFade = 1.0f;
    } else if (m_modeFade > 0.0f) {
        m_modeFade = std::max(0.0f, 1.0f - (float)((now - m_modeFadeStart) / kRise));
    }
    if (m_modeFade > 0.001f) {
        // Over the picture's card: square and black fullscreen, inside the card's rounded edge in the window.
        if (info.fullscreen) fg->AddRectFilled(m_previewMin, m_previewMax, WithAlpha(IM_COL32(0, 0, 0, 255), Ease(m_modeFade)));
        else fg->AddRectFilled(ImVec2(m_previewMin.x + 1.0f, m_previewMin.y + 1.0f), ImVec2(m_previewMax.x - 1.0f, m_previewMax.y - 1.0f),
                               WithAlpha(p.surface, Ease(m_modeFade)), std::max(0.0f, CardRounding() - 1.0f));
    }

    // Fullscreen: the switch is sent at the bottom of the dip and the cover lifts once the new layout has settled.
    constexpr double kFsDip = 0.16, kFsRise = 0.28;
    float fsCover = 0.0f;
    if (m_fsPending) {
        const double t = now - m_fsFadeStart;
        if (!m_fsSent) {
            fsCover = (float)std::min(1.0, t / kFsDip);
            if (t >= kFsDip) { ev.fullscreenToggle = true; m_fsSent = true; m_fsTarget = !info.fullscreen; m_fsFrames = 0; }
        } else {
            fsCover = 1.0f;
            const bool there = info.fullscreen == m_fsTarget && ++m_fsFrames >= 2;
            if (there || t > 2.0) { m_fsPending = false; m_fsRise = true; m_fsFadeStart = now; }
        }
    } else if (m_fsRise) {
        fsCover = std::max(0.0f, 1.0f - (float)((now - m_fsFadeStart) / kFsRise));
        if (fsCover <= 0.0f) m_fsRise = false;
    }
    if (fsCover > 0.001f) fg->AddRectFilled(vmin, vmax, IM_COL32(0, 0, 0, (int)(255.0f * Ease(fsCover))));

    // Start-up.
    constexpr double kStart = 0.45;
    if (m_startFade < 0.0 && info.windowShown) m_startFade = now;
    if (m_startFade >= 0.0) {
        const float t = (float)((now - m_startFade) / kStart);
        if (t < 1.0f) fg->AddRectFilled(vmin, vmax, WithAlpha(ImGui::ColorConvertFloat4ToU32(p.window), 1.0f - Ease(std::max(t, 0.0f))));
    }
}

} // namespace vdc::ui
