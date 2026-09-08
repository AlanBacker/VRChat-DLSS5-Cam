// VRChat DLSS5 Cam - the main window. Flat layout: a top bar with the source switch and the main action, the preview
// in the middle with the video controls and the media library under it, a sidebar of plain sections on the right.
#include "ui/MainUI.h"
#include "ui/Theme.h"
#include "core/I18n.h"
#include "core/Log.h"
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

// The "Open..." button with, once a file is open, a close button beside it.
void OpenCloseRow(const char* openLabel, bool loaded, bool& open, bool& close) {
    const float closeW = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float openW = loaded ? std::max(40.0f, ImGui::GetContentRegionAvail().x - closeW - spacing) : -FLT_MIN;
    if (FlatButton(openLabel, ImVec2(openW, 0))) open = true;
    if (!loaded) return;
    ImGui::SameLine(0.0f, spacing);
    if (IconButton("##closeMedia", Icon::Close, ImVec2(closeW, 0), TR(TipCloseMedia))) close = true;
}
constexpr float kZoomMin = 0.1f, kZoomMax = 8.0f;   // preview magnification limits, relative to the picture's pixels

// A dimmed label with its value at the end of the line, in the monospace font and at a fixed column: a figure that
// changes never pushes anything else around, and padded formats keep even its digits in place.
void Readout(const Fonts* fonts, const char* label, const std::string& value) {
    ImGui::TextDisabled("%s:", label);
    const float labelEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetScrollX();
    ImGui::SameLine(std::max(ImGui::GetFontSize() * 9.0f, labelEnd + ImGui::GetStyle().ItemSpacing.x));
    if (fonts) ImGui::PushFont(fonts->Mono(), 0.0f);
    ImGui::TextDisabled("%s", value.c_str());
    if (fonts) ImGui::PopFont();
}

void SyncBuffer(char* buf, size_t size, const std::string& value, bool editing) {
    if (editing) return;
    if (std::strncmp(buf, value.c_str(), size) != 0) std::snprintf(buf, size, "%s", value.c_str());
}

bool ComboIds(const char* label, int* value, const char* const* items, int count, const char* tooltip = nullptr) {
    bool changed = false;
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

void Tip(const char* text) { Tooltip(text); }

// "2 min 05 s" style text of a duration estimate.
std::string FormatEstimate(double seconds) {
    const int t = (int)(std::max(0.0, seconds) + 0.5);
    if (t >= 3600) return StrPrintf(TR(EstHours), t / 3600, (t / 60) % 60);
    if (t >= 60) return StrPrintf(TR(EstMinutes), t / 60, t % 60);
    return StrPrintf(TR(EstSeconds), std::max(1, t));
}

bool IsHevc(const std::string& codec) { return codec.find("HEV") != std::string::npos || codec == "H265"; }

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

} // namespace

void MainUI::Toast(const std::string& text, bool error) {
    m_toasts.push_back({ text, ImGui::GetTime(), error });
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
    }
    if (!m_undoInit) { m_undoBase = { s.ParameterText(), LibrarySnapshot(info) }; m_undoInit = true; }
    if (!io.WantTextInput && io.KeyCtrl && !io.KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) ApplyUndo(s, info, ev, io.KeyShift);
        else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) ApplyUndo(s, info, ev, true);
    }
    // F11 enters and leaves the fullscreen view, Esc leaves it (unless a popup or a text field takes the key).
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) ev.fullscreenToggle = true;
        else if (info.fullscreen && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                 !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ev.fullscreenToggle = true;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, info.fullscreen ? ImVec2(0, 0) : ImVec2(10, 8));
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
        DrawToasts(fonts);
        TrackUndo(s, info);
        return;
    }

    DrawTopBar(s, info, ev, fonts);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float statusH = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bodyH = std::max(50.0f, avail.y - statusH - style.ItemSpacing.y);
    const float sidebarW = std::min(ImGui::GetFontSize() * 24.0f, avail.x * 0.5f);
    // The sidebar slides in and out behind a slim handle on the edge of the preview; the preview takes the room it
    // frees while it moves.
    const float handleW = 14.0f;
    const float sideT = Ease(AnimateLinear(ImGui::GetID("##sidebarSlide"), s.sidebarVisible ? 1.0f : 0.0f, 0.22f));
    const float shownW = sidebarW * sideT;
    const float previewW = std::max(50.0f, avail.x - shownW - handleW - style.ItemSpacing.x * sideT);
    const ImVec2 bodyOrigin = ImGui::GetCursorScreenPos();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().surface);
    ImGui::BeginChild("##preview", ImVec2(previewW, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    DrawPreview(s, info, ev, fonts);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    {
        const Palette& p = Colors();
        const ImVec2 hpos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##sidebarHandle", ImVec2(handleW, bodyH));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) { s.sidebarVisible = !s.sidebarVisible; ev.settingsChanged = true; }
        Tip(s.sidebarVisible ? TR(SidebarHide) : TR(SidebarShow));
        const float hov = Animate(ImGui::GetID("##sidebarHandleHover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(hpos, ImVec2(hpos.x + handleW, hpos.y + bodyH), Mix(p.panel, p.controlHover, hov), 3.0f);
        DrawChevron(dl, ImVec2(hpos.x + handleW * 0.5f, hpos.y + bodyH * 0.5f), 8.0f, IM_PI * 0.5f - IM_PI * sideT, Mix(p.textDim, p.text, hov));
    }

    if (shownW > 0.5f) {
        ImGui::SameLine(0.0f, style.ItemSpacing.x * sideT);
        ImGui::PushClipRect(bodyOrigin, ImVec2(bodyOrigin.x + avail.x, bodyOrigin.y + bodyH), true);
        ImGui::BeginChild("##sidebar", ImVec2(sidebarW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetScrollX(0.0f);   // the sidebar only ever scrolls vertically
        SmoothScroll(false, ImGui::GetFontSize() * 3.6f);
        DrawSidebar(s, info, ev, fonts);
        ImGui::EndChild();
        ImGui::PopClipRect();
    }

    DrawStatusBar(s, info, ev, fonts);
    ImGui::End();

    if (s.showLog) DrawLogWindow(s, ev, fonts);
    DrawItemParams(s, info, ev, fonts);
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
    const float frameH = ImGui::GetFrameHeight();
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || ImGui::IsAnyMouseDown()) m_fullscreenMouseTime = now;
    const bool transport = s.sourceMode == SourceVideo && info.videoLoaded;
    const float transportH = transport ? frameH * 2.0f + style.ItemSpacing.y * 3.0f + 8.0f : 0.0f;
    const bool overBar = transport && ImGui::IsMousePosValid() && io.MousePos.y >= origin.y + region.y - transportH - 24.0f;
    const bool wantControls = now - m_fullscreenMouseTime < 2.5 || overBar || m_seekDragging || ImGui::IsAnyItemActive();
    m_fullscreenControls = Ease(AnimateLinear(ImGui::GetID("##fullscreenControls"), wantControls ? 1.0f : 0.0f, 0.3f));

    DrawPicture(s, info, ev, fonts, origin, region);
    if (transport) {
        if (m_fullscreenControls > 0.02f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * m_fullscreenControls);
            DrawTransport(s, info, ev, fonts, ImVec2(origin.x, origin.y + region.y - transportH), ImVec2(region.x, transportH));
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
    const ImVec2 pos(origin.x + region.x - bsz - 12.0f, origin.y + 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + bsz, pos.y + bsz), ImGui::GetColorU32(Colors().overlayBg), 4.0f);
    ImGui::SetCursorScreenPos(pos);
    if (IconButton("##fullscreen", info.fullscreen ? Icon::ExitFullscreen : Icon::Fullscreen, ImVec2(bsz, bsz),
                   info.fullscreen ? TR(TipExitFullscreen) : TR(TipFullscreen), ButtonKind::Plain)) ev.fullscreenToggle = true;
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
        out.push_back(std::move(e));
    }
    return out;
}

// The files and their own values count; the processing range is carried along without making a step of its own.
bool MainUI::SameLibrary(const std::vector<LibrarySnapshotItem>& a, const std::vector<LibrarySnapshotItem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].path != b[i].path || a[i].useOwn != b[i].useOwn || a[i].own != b[i].own) return false;
    }
    return true;
}

void MainUI::TrackUndo(const Settings& s, const UiFrameInfo& info) {
    if (ImGui::IsAnyItemActive()) return;   // a slider is held or a field is being typed in: one step per edit
    std::string cur = s.ParameterText();
    std::vector<LibrarySnapshotItem> lib = LibrarySnapshot(info);
    if (cur == m_undoBase.params && SameLibrary(lib, m_undoBase.library)) return;
    m_undo.push_back(std::move(m_undoBase));
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_undoBase = { std::move(cur), std::move(lib) };
}

void MainUI::ApplyUndo(Settings& s, const UiFrameInfo& info, UiEvents& ev, bool redo) {
    std::vector<UndoStep>& from = redo ? m_redo : m_undo;
    std::vector<UndoStep>& to = redo ? m_undo : m_redo;
    if (from.empty()) return;
    to.push_back({ s.ParameterText(), LibrarySnapshot(info) });
    UndoStep step = std::move(from.back());
    from.pop_back();
    const bool nrWas = s.nrEnabled, dlaaWas = s.dlaaEnabled;
    s.ApplyText(step.params);
    s.Clamp();
    if (!SameLibrary(step.library, to.back().library)) { ev.libraryRestore = true; ev.libraryRestoreItems = step.library; }
    m_undoBase = std::move(step);
    ev.settingsChanged = true;
    ev.nrChanged = true;
    ev.dlaaChanged = true;
    if (nrWas != s.nrEnabled || dlaaWas != s.dlaaEnabled) ev.resetHistory = true;
    ImGui::ClearActiveID();
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
    // One padded row with every element centred on its middle line. The bar never scrolls: its parts are measured
    // first and the optional ones (rates, the wide language box, the status badge, the title) are dropped when the
    // window is too narrow for all of them.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
    ImGui::BeginChild("##top", ImVec2(0, rowH + style.WindowPadding.y * 2.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::SetScrollX(0.0f);
    ImGui::SetScrollY(0.0f);
    const float top = style.WindowPadding.y;
    auto centred = [&](float itemH) { return top + (rowH - itemH) * 0.5f; };
    // Text items add the line's baseline offset themselves (a badge before them leaves one): taken off here.
    auto centredText = [&](float itemH) { return centred(itemH) - ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset; };
    const bool imageMode = s.sourceMode == SourceImage;
    const bool videoMode = s.sourceMode == SourceVideo;
    const bool busy = info.videoProcessing || info.batchRunning;   // the main button cancels the run

    // Left part: title and the source switch.
    ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.2f);
    const ImVec2 titleSize = ImGui::CalcTextSize(TR(AppTitle));
    ImGui::PopFont();
    const char* modes[] = { TR(TopSpout), TR(TopImage), TR(TopVideo) };
    float switchW = 0.0f;
    for (const char* m : modes) switchW += ImGui::CalcTextSize(m).x + style.FramePadding.x * 2.2f;

    // Status badge.
    const bool haveSenders = info.senders && !info.senders->empty();
    const std::string batchBadge = info.batchRunning ? StrPrintf(TR(BatchRunning), std::min(info.batchIndex + 1, info.batchCount), info.batchCount) : std::string();
    const char* badge = info.batchRunning ? batchBadge.c_str()
                      : videoMode ? (info.videoProcessing ? TR(VideoProcessing) : info.videoLoaded ? TR(VideoLabel) : TR(NoVideo))
                      : imageMode ? (info.imageLoaded ? TR(ImageLabel) : TR(NoImage))
                      : info.sourceConnected ? TR(StatusConnected) : haveSenders ? TR(StatusWaiting) : TR(StatusNoSpout);
    const ImU32 badgeFg = info.batchRunning ? p.accentHover
                        : videoMode ? (info.videoProcessing ? p.warn : info.videoLoaded ? p.good : p.muted)
                        : imageMode ? (info.imageLoaded ? p.good : p.muted)
                        : info.sourceConnected ? p.good : haveSenders ? p.warn : p.muted;
    const ImU32 badgeBg = WithAlpha(badgeFg, badgeFg == p.muted ? 0.2f : 0.18f);
    const bool nrBadge = info.status && info.status->nrActive;
    const float textH = ImGui::GetTextLineHeight();
    const float pillPad = 18.0f;   // Pill() adds 9 px on each side
    const float pillH = textH + 6.0f;
    const float badgesW = ImGui::CalcTextSize(badge).x + pillPad + (nrBadge ? ImGui::CalcTextSize("DLSS 5").x + pillPad + 6.0f : 0.0f);

    // Right part: rates, language, the main action. Rates are padded to three digits in the monospace font and
    // their reserved width comes from a template, so a changing number never moves the controls to its right.
    const bool uiRateOnly = (imageMode || videoMode) && !info.videoProcessing && !info.videoPlaying;
    const char* actionText = busy ? TR(Cancel) : videoMode ? TR(ProcessVideo) : imageMode ? TR(ProcessAndSave) : TR(Capture);
    const float actionW = ImGui::CalcTextSize(actionText).x + style.FramePadding.x * 2.0f + 24.0f;
    const std::string fpsText = uiRateOnly
        ? StrPrintf("%s %3.0f %s", TR(UiFps), m_shown.fps, TR(Fps))
        : StrPrintf("%s %3.0f %s  \xC2\xB7  %s %3.0f %s", TR(ProcessingFps), m_shown.processingFps, TR(Fps), TR(UiFps), m_shown.fps, TR(Fps));
    const std::string fpsTemplate = uiRateOnly
        ? StrPrintf("%s 000 %s", TR(UiFps), TR(Fps))
        : StrPrintf("%s 000 %s  \xC2\xB7  %s 000 %s", TR(ProcessingFps), TR(Fps), TR(UiFps), TR(Fps));
    ImGui::PushFont(fonts.Mono(), 0.0f);
    const float fpsW = ImGui::CalcTextSize(fpsTemplate.c_str()).x;
    ImGui::PopFont();

    const float availW = ImGui::GetWindowWidth() - style.WindowPadding.x * 2.0f;
    const float gap = 20.0f;
    float langW = ImGui::GetFontSize() * 8.0f;
    const float undoW = frameH * 2.0f + style.ItemInnerSpacing.x;
    bool showFps = true, showBadges = true, showTitle = true, showUndo = true;
    auto rightW = [&]() { return actionW + langW + style.ItemSpacing.x + (showUndo ? undoW + style.ItemSpacing.x : 0.0f) + (showFps ? fpsW + style.ItemSpacing.x : 0.0f); };
    auto leftW = [&]() { return (showTitle ? titleSize.x + 16.0f : 0.0f) + switchW + (showBadges ? 14.0f + badgesW : 0.0f); };
    if (leftW() + gap + rightW() > availW) showFps = false;
    if (leftW() + gap + rightW() > availW) langW = ImGui::GetFontSize() * 4.5f;
    if (leftW() + gap + rightW() > availW) showUndo = false;
    if (leftW() + gap + rightW() > availW) showBadges = false;
    if (leftW() + gap + rightW() > availW) showTitle = false;

    if (showTitle) {
        ImGui::SetCursorPosY(centredText(titleSize.y));
        ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.2f);
        ImGui::TextUnformatted(TR(AppTitle));
        ImGui::PopFont();
        ImGui::SameLine(0.0f, 16.0f);
    }
    ImGui::SetCursorPosY(centred(frameH));
    ImGui::BeginDisabled(busy);
    if (Segmented("##source", modes, 3, &s.sourceMode)) { ev.sourceModeChanged = true; ev.settingsChanged = true; }
    ImGui::EndDisabled();
    Tip(TR(SourceModeHint));
    if (showBadges) {
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::SetCursorPosY(centred(pillH));
        Pill(badge, badgeBg, badgeFg);
        if (nrBadge) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::SetCursorPosY(centred(pillH));
            Pill("DLSS 5", WithAlpha(p.accent, 0.2f), p.accentHover);
        }
    }
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

    ImGui::SameLine(std::max(leftEnd + gap, ImGui::GetWindowWidth() - style.WindowPadding.x - rightW()));
    if (showFps) {
        ImGui::SetCursorPosY(centredText(textH));
        ImGui::PushFont(fonts.Mono(), 0.0f);
        ImGui::TextDisabled("%s", fpsText.c_str());
        ImGui::PopFont();
        Tip(TR(TipUiFps));
        ImGui::SameLine();
    }
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
        ImGui::SameLine();
    }
    ImGui::SetCursorPosY(centred(frameH));
    ImGui::SetNextItemWidth(langW);
    {
        const char* items[] = { TR(LangAuto), "English", "简体中文", "日本語", "한국어" };
        if (ComboIds("##lang", &s.language, items, 5)) { ev.languageChanged = true; ev.settingsChanged = true; }
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(centred(frameH));
    const std::string actionLabel = std::string(busy ? "\xE2\x96\xA0 " : "\xE2\x97\x8F ") + actionText;
    if (AccentButton(actionLabel.c_str(), ImVec2(actionW, 0))) {
        if (info.batchRunning) ev.batchCancel = true;
        else if (info.videoProcessing) ev.cancelVideo = true;
        else ev.captureNow = true;
    }
    if (!busy) Tooltip(StrPrintf("%s (%s)", videoMode ? TR(VideoHint) : imageMode ? TR(ImageHint) : TR(CaptureHint), info.hotkeyText.c_str()).c_str());
    ImGui::EndChild();
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawSidebar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    // While a file is processed the settings are locked: a change half-way through would make a result that mixes
    // two looks, and a still image being saved must not restart its passes.
    const bool locked = info.videoProcessing || info.videoFinishing || info.batchRunning || info.capturePending > 0
                     || (info.status && info.status->capturesInFlight > 0);
    const float lockT = Animate(ImGui::GetID("##lock"), locked ? 1.0f : 0.0f, 12.0f);
    if (lockT > 0.001f) {
        const float frameH = ImGui::GetFrameHeight();
        const float bannerH = frameH + 10.0f;
        const ImVec2 b0 = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(b0, ImVec2(b0.x + w, b0.y + bannerH), WithAlpha(p.warn, 0.14f * lockT), style.FrameRounding);
        DrawIcon(dl, Icon::Lock, ImVec2(b0.x + 8.0f + frameH * 0.4f, b0.y + bannerH * 0.5f), frameH * 0.62f, WithAlpha(p.warn, lockT));
        const bool cancellable = info.batchRunning || info.videoProcessing;
        const float cancelW = cancellable ? ImGui::CalcTextSize(TR(Cancel)).x + style.FramePadding.x * 2.0f + 8.0f : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(p.warn, lockT));
        ImGui::PushClipRect(b0, ImVec2(b0.x + w - cancelW - 4.0f, b0.y + bannerH), true);
        ImGui::SetCursorScreenPos(ImVec2(b0.x + 12.0f + frameH * 0.8f, b0.y + (bannerH - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(TR(LockedWhileBusy));
        ImGui::PopClipRect();
        ImGui::PopStyleColor();
        if (cancellable) {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(b0.x + w - cancelW + 4.0f, b0.y + 5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * lockT);
            if (AccentButton(TR(Cancel), ImVec2(cancelW - 8.0f, frameH))) { if (info.batchRunning) ev.batchCancel = true; else ev.cancelVideo = true; }
            ImGui::PopStyleVar();
        }
        ImGui::SetCursorScreenPos(ImVec2(b0.x, b0.y + bannerH * lockT));
        ImGui::Spacing();
    }
    ImGui::BeginDisabled(locked);
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);
    if (SectionHeader(TR(SecSource), "source")) { BlockSource(s, info, ev); SectionEnd(); }
    if (SectionHeader(TR(SecNeural), "neural")) { BlockNeural(s, info, ev); SectionEnd(); }
    if (SectionHeader(TR(SecCapture), "save")) { BlockSave(s, info, ev); SectionEnd(); }
    if (SectionHeader(TR(SecDisplay), "view", false)) { BlockView(s, info, ev); SectionEnd(); }
    // The expert sections fade in and out with the Advanced switch.
    const float adv = Animate(ImGui::GetID("##advanced"), s.showAdvanced ? 1.0f : 0.0f, 12.0f);
    if (adv > 0.001f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * adv);
        if (SectionHeader(TR(SecGuidance), "guidance", false)) { BlockGuidance(s, info, ev); SectionEnd(); }
        if (SectionHeader(TR(SecDlaa), "dlaa", false)) { BlockDlaa(s, info, ev); SectionEnd(); }
        if (SectionHeader(TR(SecInternals), "internals", false)) { BlockInternals(s, info, ev); SectionEnd(); }
        ImGui::PopStyleVar();
    }
    if (SectionHeader(TR(SecAbout), "about", false)) { BlockAbout(s, info, ev, fonts); SectionEnd(); }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
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
        OpenCloseRow(TR(OpenVideo), info.videoLoaded, ev.openVideo, ev.closeMedia);
        ImGui::EndDisabled();
        if (info.videoLoaded) {
            StatusDot(p.good, StrPrintf("%s  %ux%u  %.3g %s  %s", info.videoName.c_str(), info.videoWidth, info.videoHeight, info.videoFps, TR(Fps),
                                        FormatDuration(info.videoDurationSeconds).c_str()).c_str());
            ImGui::TextDisabled("%s: %s (%s)  \xC2\xB7  %s", TR(VideoDecoder), info.videoCodec.c_str(),
                                info.videoHardwareDecode ? TR(HwLabel) : TR(SwLabel), info.videoHasAudio ? TR(Audio) : TR(NoAudio));
            if (info.videoProcessing) {
                const unsigned long long total = std::max(info.videoFrames, info.videoFrame);
                const float frac = total ? (float)((double)info.videoFrame / (double)total) : 0.0f;
                const std::string label = StrPrintf(TR(FrameOf), (unsigned long long)info.videoFrame, total);
                ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), label.c_str());
                const double rate = info.videoElapsed > 0.5 ? (double)info.videoFrame / info.videoElapsed : 0.0;
                const double remaining = (rate > 0.0 && total > info.videoFrame) ? (double)(total - info.videoFrame) / rate : 0.0;
                if (info.videoFinishing) ImGui::TextDisabled("%s", TR(VideoFinishing));
                else ImGui::TextDisabled("%5.1f %s  \xC2\xB7  %s %s", rate, TR(Fps), FormatDuration(remaining).c_str(), TR(Remaining));
            } else {
                ImGui::TextDisabled("%s", info.videoPlaying ? TR(Play) : info.imageConverging ? TR(Processing) : TR(Converged));
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
            if (s.videoMatchSource) {
                if (info.videoLoaded) {
                    const std::string rate = info.videoBitrateKbps > 0 ? StrPrintf("%.1f Mbit/s", info.videoBitrateKbps / 1000.0) : std::string(TR(BitrateUnknown));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                    ImGui::TextDisabled(TR(MatchedSpecs), IsHevc(info.videoCodec) ? TR(VideoOutputHevc) : TR(VideoOutputH264), rate.c_str(), info.videoFps);
                    ImGui::PopTextWrapPos();
                }
                if (Toggle(TR(KeepAudio), &s.videoKeepAudio)) ev.settingsChanged = true;
                Help(TR(TipKeepAudio));
            } else {
                const char* outputs[] = { TR(VideoOutputH264), TR(VideoOutputHevc), TR(VideoOutputPng) };
                if (ComboIds(TR(VideoOutput), &s.videoOutput, outputs, 3, TR(TipVideoOutput))) ev.settingsChanged = true;
                if (s.videoOutput != 2) {
                    if (SliderIntReset(TR(Bitrate), &s.videoBitrateMbps, 5, 200, 40, "%d Mbit/s", TR(TipBitrate))) ev.settingsChanged = true;
                    if (Toggle(TR(KeepAudio), &s.videoKeepAudio)) ev.settingsChanged = true;
                    Help(TR(TipKeepAudio));
                }
            }
            if (s.showAdvanced) {
                if (Toggle(TR(HardwareDecode), &s.videoHardwareDecode)) ev.settingsChanged = true;
                Help(TR(TipHardwareDecode));
            }
            ImGui::EndDisabled();
        }
    } else if (s.sourceMode == SourceImage) {
        OpenCloseRow(TR(OpenImage), info.imageLoaded, ev.openImage, ev.closeMedia);
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
            ImGui::TextUnformatted(TR(Sender));
        }
        if (info.status && info.sourceConnected) {
            StatusDot(p.good, StrPrintf("%s  %ux%u  %s  %3.0f %s", info.senderName.c_str(), info.status->srcWidth, info.status->srcHeight,
                                        info.sourceFormat.c_str(), m_shown.senderFps, TR(Fps)).c_str());
        } else {
            StatusDot(p.muted, TR(StatusWaiting));
            Hint(TR(HowToEnable));
        }
    }
    ImGui::Spacing();
    // Custom resolution.
    if (Toggle(TR(CustomResolution), &s.customResolution)) ev.settingsChanged = true;
    Help(TR(CustomResolutionHint));
    if (s.customResolution) {
        ImGui::Indent(6.0f);
        if (ImGui::InputInt(TR(Width), &s.customWidth, 2, 64)) { s.Clamp(); ev.settingsChanged = true; }
        ImGui::BeginDisabled(s.keepAspect);
        if (ImGui::InputInt(TR(Height), &s.customHeight, 2, 64)) { s.Clamp(); ev.settingsChanged = true; }
        ImGui::EndDisabled();
        if (ImGui::Checkbox(TR(KeepAspect), &s.keepAspect)) ev.settingsChanged = true;
        ImGui::TextDisabled("%s:", TR(Presets));
        ImGui::SameLine();
        struct { const char* n; int w, h; } presets[] = { {"720p",1280,720}, {"1080p",1920,1080}, {"1440p",2560,1440}, {"4K",3840,2160} };
        for (auto& pr : presets) {
            if (ImGui::SmallButton(pr.n)) { s.customWidth = pr.w; s.customHeight = pr.h; ev.settingsChanged = true; }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::Unindent(6.0f);
    }
    if (s.sourceMode == SourceSpout) Hint(TR(VrchatResHint));
    // HDR source controls: only meaningful for floating-point (scene-linear) Spout textures.
    if (info.sourceIsHdr && s.sourceMode == SourceSpout) {
        ImGui::Spacing();
        ImGui::TextUnformatted(TR(HdrSource));
        ImGui::Indent(6.0f);
        bool ch = false;
        ch |= SliderReset(TR(PaperWhite), &s.hdrPaperWhite, 0.1f, 8.0f, 1.0f, "%.2f", TR(TipPaperWhite));
        ch |= SliderReset(TR(HighlightCompression), &s.hdrHighlightCompression, 0.0f, 1.0f, 1.0f, "%.2f", TR(TipHighlightCompression));
        if (ch) ev.settingsChanged = true;
        Hint(TR(HdrSourceHint));
        ImGui::Unindent(6.0f);
    }
    ImGui::Spacing();
}

void MainUI::BlockNeural(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    const ImGuiStyle& style = ImGui::GetStyle();
    if (Toggle(TR(NrEnable), &s.nrEnabled)) { ev.nrChanged = true; ev.settingsChanged = true; }
    ImGui::SameLine(0.0f, 12.0f);
    if (st) {
        if (st->nrActive) Pill(TR(Active), WithAlpha(p.good, 0.18f), p.good);
        else if (st->nrFailed) Pill(TR(Failed), WithAlpha(p.bad, 0.18f), p.bad);
        else if (st->nrStandby) Pill(TR(Standby), WithAlpha(p.warn, 0.18f), p.warn);
        else Pill(TR(Inactive), WithAlpha(p.muted, 0.2f), p.muted);
    }
    // The Advanced switch sits at the right end of this row: it decides how much of this section (and of the
    // others) is shown.
    {
        const float toggleW = ImGui::GetFrameHeight() * 0.86f * 1.8f + style.ItemInnerSpacing.x + ImGui::CalcTextSize(TR(Advanced)).x;
        ImGui::SameLine();
        const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), rightEdge - toggleW));
        if (Toggle(TR(Advanced), &s.showAdvanced)) ev.settingsChanged = true;
        Tip(TR(TipAdvanced));
    }
    Hint(TR(NrHint));
    if (s.sourceMode == SourceSpout && s.showAdvanced) {
        if (Toggle(TR(NrCaptureOnly), &s.nrCaptureOnly)) ev.settingsChanged = true;
        Help(TR(TipNrCaptureOnly));
    }

    // Runtime.
    ImGui::Spacing();
    if (st && st->nrRuntimeLoaded) {
        StatusDot(p.good, StrPrintf("%s: %s %s", TR(Runtime), TR(Loaded), st->nrRuntimeVersion.c_str()).c_str());
    } else if (st && st->nrRuntimeIdle) {
        StatusDot(p.muted, StrPrintf("%s: %s %s", TR(Runtime), st->nrRuntimeVersion.c_str(), TR(RuntimeIdle)).c_str());
    } else if (st && !st->ngxInitialized) {
        StatusDot(p.bad, StrPrintf("%s: %s", TR(NgxStatus), st->ngxStatus.c_str()).c_str());
    } else {
        StatusDot(p.warn, StrPrintf("%s: %s", TR(Runtime), TR(NotLoaded)).c_str());
        if (!info.nrRuntimeExists) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", TR(RuntimeMissing));
            ImGui::PopStyleColor();
        }
    }
    if (st && st->nrFailed && !st->nrError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("%s", st->nrError.c_str());
        ImGui::PopStyleColor();
        // The 310.8 runtime build only carries RTX 50 code: say so on older cards instead of leaving a bare NGX code.
        const int gen = info.adapter ? info.adapter->RtxGeneration() : 0;
        if (gen >= 2 && gen <= 4 && (st->nrRuntimeVersion.empty() || st->nrRuntimeVersion.rfind("310.8", 0) == 0)) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
            ImGui::TextWrapped("%s", TR(NrArchHint));
            ImGui::PopStyleColor();
        }
    }
    if (st && st->nrActive && (st->nrOutState == 2 || (st->nrOutState == 3 && s.nrIntensity > 0.05f))) {
        // The runtime reports success but the output check found a black or unchanged picture.
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("%s", I18n::T(st->nrOutState == 2 ? Str::NrOutBlack : Str::NrOutSame));
        ImGui::PopStyleColor();
    }
    {
        SyncBuffer(m_runtimeBuf, sizeof(m_runtimeBuf), s.nrDllPath, m_runtimeEditing);
        const float btnW = ImGui::GetFrameHeight() * 1.6f;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - btnW - ImGui::GetStyle().ItemInnerSpacing.x);
        const std::string hint = WideToUtf8(info.nrRuntimePath);
        ImGui::InputTextWithHint("##nrpath", hint.c_str(), m_runtimeBuf, sizeof(m_runtimeBuf));
        m_runtimeEditing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { s.nrDllPath = m_runtimeBuf; ev.reloadRuntime = true; ev.settingsChanged = true; }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (FlatButton("...##runtime", ImVec2(btnW, 0))) ev.browseRuntime = true;
        Tip(TR(Browse));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(RuntimePath));
        if (ImGui::SmallButton(TR(Reload))) ev.reloadRuntime = true;
    }
    if (s.showAdvanced) {
        const char* routes[] = { TR(RouteSnippet), TR(RouteCore) };
        if (ComboIds(TR(Route), &s.nrRoute, routes, 2, TR(TipRoute))) { ev.nrChanged = true; ev.settingsChanged = true; }
    }
    ImGui::Spacing();
    EffectControls(s, ev, s.showAdvanced, s.nrEnabled);
    if (st && s.showAdvanced) {
        Readout(m_fonts, TR(GpuTime), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Neural]));
        Readout(m_fonts, TR(Frames), StrPrintf("%llu", m_shown.processedFrames));
        Readout(m_fonts, TR(NrPassSize), st->nrActive && st->nrPassWidth ? StrPrintf("%ux%u", st->nrPassWidth, st->nrPassHeight) : std::string("-"));
        Readout(m_fonts, TR(NrOutputCheck), st->nrActive && m_shown.nrOutDelta >= 0.0f ? StrPrintf("%5.3f", m_shown.nrOutDelta) : std::string("    -"));
        Help(TR(TipNrOutputCheck));
    }
    ImGui::Spacing();
}

// The effect controls of the DLSS 5 pass: shared by the sidebar and by the window of a library item's own values.
void MainUI::EffectControls(Settings& s, UiEvents& ev, bool advanced, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    {
        const char* presets[] = { "Preset A (0)", "Preset B (1)", "Preset C (2)", "Preset D (3)" };
        if (ComboIds(TR(Preset), &s.nrPreset, presets, 4, TR(TipPreset))) { ev.nrChanged = true; ev.settingsChanged = true; }
        const char* styles[] = { TR(StyleDefault), TR(StyleNatural), TR(StyleCinematic) };
        if (ComboIds(TR(Style), &s.nrStyle, styles, 3, TR(TipStyle))) { ev.nrChanged = true; ev.settingsChanged = true; }
    }
    bool ch = false;
    // 0..2: up to 1 goes to the runtime (which stops there); above 1 the composite pass amplifies the matching part
    // of the change the network made (see the tooltips).
    ch |= SliderReset(TR(Intensity), &s.nrIntensity, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipIntensity));
    if (!advanced) Hint(TR(NrStrengthHint));
    bool blend = false;
    if (advanced) {
        ch |= SliderReset(TR(GlobalTone), &s.nrGlobalTone, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipGlobalTone));
        ch |= SliderReset(TR(LocalTone), &s.nrLocalTone, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipLocalTone));
        ch |= SliderReset(TR(LocalStructure), &s.nrLocalStructure, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipLocalStructure));
        {
            bool useDefault = s.nrSkinStructure < 0.0f;
            ImGui::PushID("skin");
            if (ImGui::Checkbox(TR(UseDefault), &useDefault)) { s.nrSkinStructure = useDefault ? -1.0f : 1.0f; ch = true; }
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", TR(SkinStructure));
            if (!useDefault) ch |= SliderReset(TR(SkinStructure), &s.nrSkinStructure, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipSkinStructure));
            else Tip(TR(TipSkinStructure));
            ImGui::PopID();
        }
        if (Toggle(TR(AutoMask), &s.nrAutoMask)) ch = true;
        Help(TR(TipAutoMask));
        if (Toggle(TR(UiCorrection), &s.nrUiCorrection)) ch = true;
        Help(TR(TipUiCorrection));
        if (s.customResolution) {
            if (Toggle(TR(NrUpscale), &s.nrUpscale)) ch = true;
            Help(TR(TipUpscale));
        }
        {
            // Pass resolution: the neural pass runs on a smaller picture and its change is upsampled onto the full one.
            ImGui::BeginDisabled(s.customResolution && s.nrUpscale);
            ch |= SliderIntReset(TR(NrInputScale), &s.nrInputScale, 25, 100, 100, "%d%%", TR(TipNrInputScale));
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::TextUnformatted(TR(OutputBlend));
        // The exposure changes what the network sees (neural pass re-run); the strengths only change the composite.
        ch |= SliderReset(TR(InputExposure), &s.nrInputExposure, 0.25f, 4.0f, 1.0f, "%.2fx", TR(TipInputExposure));
        blend |= SliderReset(TR(ToneTransfer), &s.nrToneTransfer, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipToneTransfer));
        blend |= SliderReset(TR(ColorStrength), &s.nrColorStrength, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipColorStrength));
        blend |= SliderReset(TR(ShadowGain), &s.nrShadowGain, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipShadowGain));
        blend |= SliderReset(TR(HighlightGain), &s.nrHighlightGain, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipHighlightGain));
    }
    ImGui::EndDisabled();
    if (blend) ev.settingsChanged = true;
    if (ch) { ev.nrChanged = true; ev.settingsChanged = true; }
    ImGui::Spacing();
    if (GhostButton(TR(ResetHistory))) ev.resetHistory = true;
    ImGui::SameLine();
    if (GhostButton(TR(ResetDefaults))) {
        s.nrPreset = 0; s.nrStyle = 0; s.nrIntensity = 1.0f; s.nrGlobalTone = 1.0f; s.nrLocalTone = 1.0f;
        s.nrLocalStructure = 1.0f; s.nrSkinStructure = -1.0f; s.nrAutoMask = false; s.nrUiCorrection = false;
        s.nrInputExposure = 1.0f; s.nrToneTransfer = 1.0f; s.nrColorStrength = 1.0f;
        s.nrShadowGain = 1.0f; s.nrHighlightGain = 1.0f; s.nrInputScale = 100;
        ev.nrChanged = true; ev.settingsChanged = true;
    }
}

void MainUI::BlockSave(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const bool busy = info.videoProcessing || info.batchRunning;
    ImGui::BeginDisabled(busy);
    if (AccentButton(s.sourceMode == SourceVideo ? TR(ProcessVideo) : s.sourceMode == SourceImage ? TR(ProcessAndSave) : TR(Capture), ImVec2(-FLT_MIN, 0)))
        ev.captureNow = true;
    ImGui::EndDisabled();
    Tip(s.sourceMode == SourceVideo ? TR(VideoHint) : s.sourceMode == SourceImage ? TR(ImageHint) : TR(CaptureHint));
    Hint(TR(OutputHint));
    if (s.sourceMode != SourceSpout) {
        // What the run would take on this card, so a slow card can be judged before the wait.
        Readout(m_fonts, TR(Estimate), EstimateText(s, info));
        Help(TR(TipEstimate));
    }
    {
        SyncBuffer(m_folderBuf, sizeof(m_folderBuf), s.captureFolder, m_folderEditing);
        const float btnW = ImGui::GetFrameHeight() * 1.6f;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - btnW * 2.0f - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
        const std::string hint = WideToUtf8(info.captureFolder);
        ImGui::InputTextWithHint("##folder", hint.c_str(), m_folderBuf, sizeof(m_folderBuf));
        m_folderEditing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { s.captureFolder = m_folderBuf; ev.settingsChanged = true; }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (FlatButton("...##folder", ImVec2(btnW, 0))) ev.browseFolder = true;
        Tip(TR(Browse));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (IconButton("##openfolder", Icon::OpenExternal, ImVec2(btnW, 0), TR(OpenFolder))) ev.openCaptureFolder = true;
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(CaptureFolder));
    }
    if (Toggle(TR(SaveOriginal), &s.saveOriginal)) ev.settingsChanged = true;
    if (s.showAdvanced) {
        if (Toggle(TR(KeepAlpha), &s.keepAlpha)) ev.settingsChanged = true;
        Help(TR(TipKeepAlpha));
    }
    if (s.sourceMode == SourceSpout) {
        ImGui::Spacing();
        // Hotkey.
        if (Toggle(TR(Hotkey), &s.hotkeyEnabled)) { ev.hotkeyChanged = true; ev.settingsChanged = true; }
        Help(TR(TipHotkey));
        if (s.hotkeyEnabled) {
            ImGui::Indent(6.0f);
            bool ctrl = (s.hotkeyModifiers & 0x0002) != 0, alt = (s.hotkeyModifiers & 0x0001) != 0, shift = (s.hotkeyModifiers & 0x0004) != 0, win = (s.hotkeyModifiers & 0x0008) != 0;
            bool hc = false;
            hc |= ImGui::Checkbox("Ctrl", &ctrl); ImGui::SameLine();
            hc |= ImGui::Checkbox("Alt", &alt); ImGui::SameLine();
            hc |= ImGui::Checkbox("Shift", &shift); ImGui::SameLine();
            hc |= ImGui::Checkbox("Win", &win);
            if (hc) s.hotkeyModifiers = (ctrl ? 0x0002u : 0u) | (alt ? 0x0001u : 0u) | (shift ? 0x0004u : 0u) | (win ? 0x0008u : 0u);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            if (BeginDropdown(TR(Key), HotkeyKeyName(s.hotkeyKey))) {
                for (const auto& h : kHotkeys) {
                    if (ImGui::Selectable(h.name, h.vk == s.hotkeyKey)) { s.hotkeyKey = h.vk; hc = true; }
                }
                EndDropdown();
            }
            if (hc) { ev.hotkeyChanged = true; ev.settingsChanged = true; }
            ImGui::Unindent(6.0f);
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
    {
        const char* themes[] = { TR(ThemeSystem), TR(ThemeDark), TR(ThemeLight) };
        if (Segmented("##theme", themes, 3, &s.theme, ImGui::CalcItemWidth())) ev.settingsChanged = true;
        Tip(TR(TipTheme));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(Theme));
    }
    {
        const char* items[] = { TR(CompareOutput), TR(CompareOriginal), TR(CompareWipe), TR(CompareMotion), TR(CompareDepth) };
        if (ComboIds(TR(Compare), &s.compareMode, items, 5)) ev.settingsChanged = true;
    }
    if (s.compareMode == CompareWipe) {
        if (ImGui::SliderFloat("##wipe", &s.wipePosition, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
    }
    {
        const char* items[] = { TR(FitWindowLabel), TR(OneToOne) };
        if (ComboIds("##fit", &s.fitMode, items, 2)) { ev.settingsChanged = true; ResetView(false); }
        // Manual magnification on top of the fit, shown relative to the picture's pixels. The wheel over the preview
        // does the same; the button returns to the fitted view.
        ImGui::PushID("zoom");
        const ImGuiStyle& style = ImGui::GetStyle();
        const float resetW = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
        float pct = m_zoomTarget * m_baseScale * 100.0f;
        if (ImGui::SliderFloat("##z", &pct, kZoomMin * 100.0f, kZoomMax * 100.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
            m_zoom = m_zoomTarget = pct / (100.0f * std::max(m_baseScale, 1e-6f));
            m_panHome = false;
        }
        Tip(TR(TipZoom));
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::BeginDisabled(std::fabs(m_zoomTarget - 1.0f) < 1e-4f && m_pan.x == 0.0f && m_pan.y == 0.0f);
        if (IconButton("##resetview", Icon::Reset, ImVec2(resetW, 0), TR(ResetView), ButtonKind::Plain)) ResetView(true);
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(Zoom));
        ImGui::PopID();
    }
    if (Toggle(TR(Checkerboard), &s.checkerboard)) ev.settingsChanged = true;
    if (Toggle(TR(ShowLibrary), &s.libraryVisible)) ev.settingsChanged = true;
    if (Toggle(TR(Overlay), &s.showOverlay)) ev.settingsChanged = true;
    if (Toggle(TR(ShowLog), &s.showLog)) ev.settingsChanged = true;
    if (s.showAdvanced) {
        if (Toggle(TR(Vsync), &s.vsync)) ev.settingsChanged = true;
        if (SliderIntReset(TR(RateLimit), &s.processRateLimit, 0, 240, 0, s.processRateLimit > 0 ? "%d fps" : TR(RateLimitOff), TR(TipRateLimit)))
            ev.settingsChanged = true;
    }
    ImGui::Spacing();
}

void MainUI::BlockGuidance(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    // A picture or a paused video has no motion to measure: say so rather than reporting the flow as unavailable.
    const bool still = (s.sourceMode == SourceImage) || (s.sourceMode == SourceVideo && !info.videoPlaying && !info.videoProcessing);
    {
        const char* items[] = { TR(MotionZero), TR(MotionCompute), TR(MotionNvof) };
        if (ComboIds(TR(MotionSource), &s.motionMode, items, 3, TR(TipMotion))) ev.settingsChanged = true;
    }
    if (s.motionMode == MotionCompute) {
        if (ImGui::SliderInt(TR(SearchRadius), &s.searchRadius, 2, 12, "%d px", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        Tip(TR(TipSearchRadius));
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
            else if (!st->nvofAvailable) StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), TR(NotAvailable)).c_str());
            else StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), st->nvofError.empty() ? TR(NotAvailable) : st->nvofError.c_str()).c_str());
        }
    }
    if (s.motionMode != MotionZero) {
        if (ImGui::SliderFloat(TR(MotionConfidence), &s.motionConfidence, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        Tip(TR(TipConfidence));
    }
    {
        // Display order puts the estimated depth first; the enum keeps the 0.1.x numbering.
        const char* items[] = { TR(DepthEstimated), TR(DepthFlat), TR(DepthGradient), TR(DepthZero) };
        int sel = (s.depthMode == DepthEstimated) ? 0 : std::clamp(s.depthMode, 0, 2) + 1;
        if (ComboIds(TR(DepthSource), &sel, items, 4, TR(TipDepth))) { s.depthMode = (sel == 0) ? DepthEstimated : sel - 1; ev.settingsChanged = true; }
    }
    if (s.depthMode == DepthEstimated) {
        if (st) {
            switch (st->depthState) {
            case (int)DepthEstimatorState::Ready:
                StatusDot(p.good, StrPrintf("%s: %s  %s %ux%u  %s %5.1f ms", TR(DepthStatus), TR(DepthReady), st->depthBackend.c_str(),
                                            st->depthInferW, st->depthInferH, TR(Inference), m_shown.depthMs).c_str());
                break;
            case (int)DepthEstimatorState::Initializing:
                StatusDot(p.warn, StrPrintf("%s: %s", TR(DepthStatus), TR(DepthInitializing)).c_str());
                break;
            default:
                StatusDot(p.warn, StrPrintf("%s: %s", TR(DepthStatus), TR(DepthUnavailable)).c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
                if (!st->depthModelExists) ImGui::TextWrapped("%s", TR(DepthModelMissing));
                else if (!st->depthMessage.empty()) ImGui::TextWrapped("%s", st->depthMessage.c_str());
                ImGui::PopStyleColor();
                break;
            }
        }
        if (ImGui::SliderInt(TR(DepthInterval), &s.depthInterval, 1, 10, "%d", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        Tip(TR(TipDepthInterval));
        {
            static const int kSides[] = { 252, 336, 420, 518 };
            const char* sides[] = { "252 px", "336 px", "420 px", "518 px" };
            int res = 1;
            for (int i = 0; i < 4; ++i) if (s.depthLongSide == kSides[i]) res = i;
            if (ComboIds(TR(DepthResolution), &res, sides, 4, TR(TipDepthResolution))) { s.depthLongSide = kSides[res]; ev.settingsChanged = true; }
        }
        {
            SyncBuffer(m_depthModelBuf, sizeof(m_depthModelBuf), s.depthModelPath, m_depthModelEditing);
            const float btnW = ImGui::GetFrameHeight() * 1.6f;
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - btnW - ImGui::GetStyle().ItemInnerSpacing.x);
            const std::string hint = st ? WideToUtf8(st->depthModelPath) : std::string();
            ImGui::InputTextWithHint("##depthmodel", hint.c_str(), m_depthModelBuf, sizeof(m_depthModelBuf));
            m_depthModelEditing = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) { s.depthModelPath = m_depthModelBuf; ev.settingsChanged = true; }
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (FlatButton("...##depthmodel", ImVec2(btnW, 0))) ev.browseDepthModel = true;
            Tip(TR(Browse));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(TR(DepthModel));
            if (ImGui::SmallButton(StrPrintf("%s##depthreload", TR(Reload)).c_str())) ev.reloadDepth = true;
        }
    }
    if (Toggle(TR(AutoReset), &s.autoReset)) ev.settingsChanged = true;
    Help(TR(TipAutoReset));
    if (s.autoReset) {
        if (ImGui::SliderFloat(TR(CutThreshold), &s.cutThreshold, 0.01f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        Tip(TR(TipCutThreshold));
    }
    if (st) {
        Readout(m_fonts, TR(FrameCost), StrPrintf("%5.3f  max %5.3f", m_shown.statAvgCost, m_shown.statMaxCost));
        Readout(m_fonts, "|mv|", StrPrintf("%5.2f px", m_shown.statAvgMotion));
        Readout(m_fonts, TR(Resets), StrPrintf("%llu", m_shown.resets));
    }
    ImGui::Spacing();
}

void MainUI::BlockDlaa(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    const bool available = st && st->ngxInitialized && st->dlssAvailable;
    ImGui::BeginDisabled(!available);
    if (Toggle(TR(DlaaEnable), &s.dlaaEnabled)) { ev.dlaaChanged = true; ev.settingsChanged = true; }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 12.0f);
    if (st) {
        if (st->dlaaActive) Pill(TR(Active), WithAlpha(p.good, 0.18f), p.good);
        else if (st->dlaaFailed) Pill(TR(Failed), WithAlpha(p.bad, 0.18f), p.bad);
        else if (!available) Pill(TR(Unsupported), WithAlpha(p.muted, 0.2f), p.muted);
        else Pill(TR(Inactive), WithAlpha(p.muted, 0.2f), p.muted);
    }
    Hint(TR(DlaaHint));
    if (st && st->dlaaFailed && !st->dlaaError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.bad);
        ImGui::TextWrapped("%s", st->dlaaError.c_str());
        ImGui::PopStyleColor();
    }
    {
        const char* presets[] = { "Default", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O" };
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
    // Fixed-width value column in the monospace font: the table must not re-flow when a figure changes width.
    ImFont* mono = m_fonts ? m_fonts->Mono() : nullptr;
    ImGui::PushFont(mono, 0.0f);
    const float valueW = ImGui::CalcTextSize("0000.00 ms").x + ImGui::GetStyle().CellPadding.x * 2.0f;
    ImGui::PopFont();
    if (ImGui::BeginTable("timers", 2)) {
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthFixed, valueW);
        auto row = [&](const char* name, double ms) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", name);
            ImGui::TableNextColumn();
            ImGui::PushFont(mono, 0.0f);
            ImGui::TextUnformatted(FormatMsFixed(ms).c_str());
            ImGui::PopFont();
        };
        for (const auto& t : timers) row(t.name, m_shown.gpuMs[(UINT)t.t]);
        row(TR(TmUi), m_shown.uiGpuMs);
        row(StrPrintf("%s CPU", TR(UiFps)).c_str(), m_shown.cpuMs);
        ImGui::EndTable();
    }
    ImGui::Spacing();
}

void MainUI::BlockAbout(Settings& /*s*/, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    ImGui::PushFont(fonts.Bold(), 0.0f);
    ImGui::Text("%s %s", TR(AppTitle), info.appVersion.c_str());
    ImGui::PopFont();
    Hint(TR(AboutText));
    if (info.adapter) {
        ImGui::TextDisabled("%s:", TR(Gpu)); ImGui::SameLine(); ImGui::TextWrapped("%s", WideToUtf8(info.adapter->name).c_str());
        const std::wstring& drv = info.adapter->nvidiaDriverVersion.empty() ? info.adapter->driverVersion : info.adapter->nvidiaDriverVersion;
        ImGui::TextDisabled("%s:", TR(Driver)); ImGui::SameLine(); ImGui::TextUnformatted(WideToUtf8(drv).c_str());
    }
    if (info.status) {
        ImGui::TextDisabled("%s:", TR(NgxStatus)); ImGui::SameLine(); ImGui::TextUnformatted(info.status->ngxStatus.c_str());
        ImGui::TextDisabled("%s:", TR(Nvof)); ImGui::SameLine(); ImGui::TextUnformatted(info.status->nvofAvailable ? TR(Available) : TR(NotAvailable));
    }
    // Two rows of two equal buttons and the reset across the full width, all the same height.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float halfW = std::floor((fullW - style.ItemSpacing.x) * 0.5f);
    const ImVec2 half(halfW, 0.0f);
    if (GhostButton(TR(OpenLogFile), half)) ev.openLogFile = true;
    ImGui::SameLine(0.0f, style.ItemSpacing.x);
    if (GhostButton(TR(OpenSettingsFolder), half)) ev.openSettingsFolder = true;
    if (GhostButton(TR(ProjectPage), half)) ev.openProjectPage = true;
    ImGui::SameLine(0.0f, style.ItemSpacing.x);
    if (GhostButton(TR(Licenses), half)) ev.openLicenses = true;
    if (GhostButton(TR(ResetAllSettings), ImVec2(fullW, 0.0f))) ImGui::OpenPopup("##resetall");
    if (BeginPopupFade("##resetall")) {
        ImGui::TextUnformatted(TR(ResetAllSettings));
        ImGui::Separator();
        if (AccentButton(TR(Ok), ImVec2(120, 0))) { ev.resetDefaults = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (FlatButton(TR(Cancel), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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
    const bool transport = s.sourceMode == SourceVideo && info.videoLoaded;
    const float transportH = transport ? frameH * 2.0f + style.ItemSpacing.y * 3.0f + 8.0f : 0.0f;
    const float thumbH = ImGui::GetFontSize() * 4.5f;
    m_libraryFold = Ease(AnimateLinear(ImGui::GetID("##libraryFold"), s.libraryVisible ? 1.0f : 0.0f, 0.2f));
    const float libraryOpenH = frameH + thumbH + ImGui::GetTextLineHeight() * 2.0f + style.ItemSpacing.y * 4.0f + style.ScrollbarSize + 16.0f;
    const float libraryClosedH = frameH + 12.0f;
    const float libraryH = libraryClosedH + (libraryOpenH - libraryClosedH) * m_libraryFold;
    const float pictureH = std::max(60.0f, region.y - transportH - libraryH);

    DrawPicture(s, info, ev, fonts, origin, ImVec2(region.x, pictureH));
    float y = origin.y + pictureH;
    if (transport) {
        DrawTransport(s, info, ev, fonts, ImVec2(origin.x, y), ImVec2(region.x, transportH));
        y += transportH;
    }
    const float remaining = origin.y + region.y - y;
    if (remaining > 8.0f) DrawLibrary(s, info, ev, fonts, ImVec2(origin.x, y), ImVec2(region.x, remaining));
}

void MainUI::DrawPicture(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& origin, const ImVec2& region) {
    const Palette& p = Colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(origin);

    if (!info.hasDisplay || !info.displayTexture || !info.displayWidth || !info.displayHeight) {
        const bool loadedSomething = (s.sourceMode == SourceVideo && info.videoLoaded) || (s.sourceMode == SourceImage && info.imageLoaded)
                                  || (s.sourceMode == SourceSpout && info.sourceConnected);
        if (loadedSomething) {
            const char* text = (s.sourceMode == SourceVideo && (info.videoSeeking || m_seekTarget >= 0.0)) ? TR(Seeking) : TR(NoDisplay);
            const ImVec2 size = ImGui::CalcTextSize(text);
            ImGui::SetCursorScreenPos(ImVec2(origin.x + (region.x - size.x) * 0.5f, origin.y + (region.y - size.y) * 0.5f));
            ImGui::TextDisabled("%s", text);
            ImGui::Dummy(ImVec2(0, 0));
            FullscreenButton(info, ev, origin, region);
            return;
        }
        if (info.fullscreen) FullscreenButton(info, ev, origin, region);   // a way out that is not a key
        // Nothing open yet: the three steps, with the ways to get a picture in.
        const float wrap = std::min(region.x * 0.8f, ImGui::GetFontSize() * 34.0f);
        const float blockH = ImGui::GetFontSize() * 13.0f;
        const float x = origin.x + (region.x - wrap) * 0.5f;
        const float fade = AnimateFrom(ImGui::GetID("##welcome"), 0.0f, 1.0f, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * fade);
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y + std::max(12.0f, (region.y - blockH) * 0.5f) + (1.0f - fade) * 12.0f));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(x + wrap);
        ImGui::PushFont(fonts.Bold(), ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::Text("1  %s", TR(StepOne));
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
        ImGui::TextUnformatted(TR(WelcomeLive));
        ImGui::Spacing();
        ImGui::TextUnformatted(TR(WelcomeFiles));
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (FlatButton(TR(OpenImage))) ev.openImage = true;
        ImGui::SameLine();
        if (FlatButton(TR(OpenVideo))) ev.openVideo = true;
        ImGui::SameLine();
        if (FlatButton(TR(AddFiles))) ev.libraryAddFiles = true;
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::PushFont(fonts.Bold(), ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
        ImGui::Text("2  %s", TR(StepTwo));
        ImGui::Text("3  %s", TR(StepThree));
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        return;
    }
    AnimateSnap(ImGui::GetID("##welcome"), 0.0f);   // the card fades in again the next time nothing is open

    // Image rectangle: fitted to the view or 1:1, times the manual magnification (wheel, slider; double-click resets).
    const float texW = (float)info.displayWidth, texH = (float)info.displayHeight;
    m_baseScale = (s.fitMode == FitWindow) ? std::min(region.x / texW, region.y / texH) : 1.0f;
    const float zoomMin = kZoomMin / m_baseScale, zoomMax = kZoomMax / m_baseScale;
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
    if (!m_wipeDragging) {
        if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
            m_pan.x += io.MouseDelta.x; m_pan.y += io.MouseDelta.y;
        }
        if (hovered && io.MouseWheel != 0.0f) {
            float t = std::clamp(m_zoomTarget * (io.MouseWheel > 0 ? 1.25f : 0.8f), zoomMin, zoomMax);
            if (std::fabs(t - 1.0f) < 0.06f) t = 1.0f;   // snaps back to the fitted view
            m_zoomTarget = t;
            m_zoomAnchor = io.MousePos;   // the picture point under the cursor stays put while the zoom moves
            m_panHome = false;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { ResetView(true); m_zoomAnchor = centre; }
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
    }
    if (m_panHome) {
        const float f = std::exp(-20.0f * std::min(io.DeltaTime, 0.05f));
        m_pan.x *= f; m_pan.y *= f;
        if (std::fabs(m_pan.x) < 0.5f && std::fabs(m_pan.y) < 0.5f) { m_pan = ImVec2(0, 0); m_panHome = false; }
    }
    // The picture stays within reach: centred while it is smaller than the view, and never leaving a gap on a side
    // once it is larger.
    const ImVec2 imgSize = imageSize();
    const float slackX = std::max(0.0f, (imgSize.x - region.x) * 0.5f), slackY = std::max(0.0f, (imgSize.y - region.y) * 0.5f);
    m_pan.x = std::clamp(m_pan.x, -slackX, slackX);
    m_pan.y = std::clamp(m_pan.y, -slackY, slackY);
    const ImVec2 imgPos = imagePos(imgSize);
    const ImVec2 imgMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);

    // Wipe handle.
    // The handle is drawn where the shown composite splits the picture (the processing thread's latest result), not
    // at the requested position: while it is dragged the line keeps to the picture instead of running ahead of it.
    const float shownWipe = info.hasDisplay ? info.displayWipe : s.wipePosition;
    if (s.compareMode == CompareWipe) {
        const float wipeX = imgPos.x + imgSize.x * shownWipe;
        const bool nearHandle = hovered && std::fabs(io.MousePos.x - wipeX) < 8.0f && io.MousePos.y >= imgPos.y && io.MousePos.y <= imgMax.y;
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
    if (active && !m_wipeDragging && (slackX > 0.0f || slackY > 0.0f)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    dl->PushClipRect(origin, ImVec2(origin.x + region.x, origin.y + region.y), true);
    dl->AddImage(ImTextureRef(info.displayTexture), imgPos, imgMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    if (s.compareMode == CompareWipe) {
        const float wipeX = imgPos.x + imgSize.x * shownWipe;
        dl->AddLine(ImVec2(wipeX, imgPos.y), ImVec2(wipeX, imgMax.y), IM_COL32(255, 255, 255, 220), 2.0f);
        dl->AddCircleFilled(ImVec2(wipeX, imgPos.y + imgSize.y * 0.5f), 9.0f, IM_COL32(255, 255, 255, 235));
        dl->AddCircleFilled(ImVec2(wipeX, imgPos.y + imgSize.y * 0.5f), 6.0f, p.accent);
        ImGui::PushFont(fonts.Bold(), 0.0f);
        dl->AddText(ImVec2(imgPos.x + 10.0f, imgMax.y - ImGui::GetFontSize() - 8.0f), IM_COL32(255, 255, 255, 200), TR(CompareOriginal));
        const ImVec2 outSize = ImGui::CalcTextSize(TR(Output));
        dl->AddText(ImVec2(imgMax.x - outSize.x - 10.0f, imgMax.y - ImGui::GetFontSize() - 8.0f), IM_COL32(255, 255, 255, 200), TR(Output));
        ImGui::PopFont();
    }

    // Overlay.
    if (s.showOverlay && info.status) {
        const PipelineStatus& st = *info.status;
        std::string lines[4];
        lines[0] = StrPrintf("%s %ux%u  \xE2\x86\x92  %s %ux%u", TR(Source), st.srcWidth, st.srcHeight, TR(Output), st.outWidth, st.outHeight);
        lines[1] = StrPrintf("DLSS 5: %s", st.nrActive ? StrPrintf("%s  (P%d, %s %d, %.2f)", TR(Active), s.nrPreset, TR(Style), s.nrStyle, s.nrIntensity).c_str()
                                                        : (st.nrFailed ? TR(Failed) : TR(Bypass)));
        const char* motion = st.motionModeActive == MotionNvOpticalFlow ? "NVOF" : st.motionModeActive == MotionCompute ? TR(MotionCompute) : TR(MotionZero);
        const char* depth = st.depthModeActive == DepthEstimated ? "Depth Anything V2" : st.depthModeActive == DepthGradient ? TR(DepthGradient)
                          : st.depthModeActive == DepthZero ? TR(DepthZero) : TR(DepthFlat);
        lines[2] = StrPrintf("%s: %s   %s: %s%s%s", TR(MotionSource), motion, TR(DepthSource), depth, st.dlaaActive ? "  +DLAA" : "",
                             st.sceneCut ? StrPrintf("  [%s]", TR(SceneCut)).c_str() : "");
        lines[3] = (s.sourceMode == SourceImage)
            ? StrPrintf("GPU %s   %s %3.0f %s   %s", FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Frame]).c_str(), TR(UiFps), m_shown.fps, TR(Fps), info.imageName.c_str())
            : (s.sourceMode == SourceVideo)
            ? StrPrintf("GPU %s   %s %3.0f %s   %s %3.0f %s   %s", FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Frame]).c_str(),
                        TR(ProcessingFps), m_shown.processingFps, TR(Fps), TR(UiFps), m_shown.fps, TR(Fps), info.videoName.c_str())
            : StrPrintf("GPU %s   %s %3.0f %s   %s %3.0f %s   %s %3.0f %s", FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Frame]).c_str(),
                        TR(ProcessingFps), m_shown.processingFps, TR(Fps), TR(UiFps), m_shown.fps, TR(Fps), TR(SenderFps), m_shown.senderFps, TR(Fps));
        ImGui::PushFont(fonts.Mono(), ImGui::GetStyle().FontSizeBase * 0.92f);
        float w = 0.0f;
        for (auto& l : lines) w = std::max(w, ImGui::CalcTextSize(l.c_str()).x);
        const float lh = ImGui::GetFontSize() + 3.0f;
        const ImVec2 pad(10.0f, 8.0f);
        const ImVec2 bpos(origin.x + 12.0f, origin.y + 12.0f);
        dl->AddRectFilled(bpos, ImVec2(bpos.x + w + pad.x * 2.0f, bpos.y + lh * 4.0f + pad.y * 2.0f), p.overlayBg, 4.0f);
        for (int i = 0; i < 4; ++i)
            dl->AddText(ImVec2(bpos.x + pad.x, bpos.y + pad.y + lh * i), i == 1 && st.nrActive ? p.good : p.text, lines[i].c_str());
        ImGui::PopFont();
    }
    if (s.fitMode == FitOneToOne || std::fabs(m_zoom - 1.0f) > 1e-3f) {
        ImGui::PushFont(fonts.Mono(), 0.0f);
        const std::string z = StrPrintf("%s %.0f%%", TR(Zoom), m_baseScale * m_zoom * 100.0f);
        const ImVec2 zs = ImGui::CalcTextSize(z.c_str());
        const float right = origin.x + region.x - ImGui::GetFrameHeight() - 20.0f;   // left of the fullscreen button
        dl->AddRectFilled(ImVec2(right - zs.x - 16.0f, origin.y + 12.0f), ImVec2(right, origin.y + 12.0f + zs.y + 10.0f), p.overlayBg, 4.0f);
        dl->AddText(ImVec2(right - zs.x - 8.0f, origin.y + 17.0f), p.text, z.c_str());
        ImGui::PopFont();
    }
    // A video frame that is (almost) black, such as the fade-in at the start of a film: say so, or the user takes the
    // dark preview for a failure.
    if (s.sourceMode == SourceVideo && info.videoLoaded && !info.videoPlaying && !info.videoProcessing && info.videoPreviewLuma < 0.02f) {
        const float wrap = std::min(region.x - 40.0f, ImGui::GetFontSize() * 28.0f);
        const ImVec2 ts = ImGui::CalcTextSize(TR(DarkFrameHint), nullptr, false, wrap);
        const ImVec2 pad(12.0f, 8.0f);
        const ImVec2 bpos(origin.x + (region.x - ts.x - pad.x * 2.0f) * 0.5f, origin.y + region.y - ts.y - pad.y * 2.0f - 16.0f);
        dl->AddRectFilled(bpos, ImVec2(bpos.x + ts.x + pad.x * 2.0f, bpos.y + ts.y + pad.y * 2.0f), p.overlayBg, 4.0f);
        dl->AddText(nullptr, 0.0f, ImVec2(bpos.x + pad.x, bpos.y + pad.y), p.text, TR(DarkFrameHint), nullptr, wrap);
    }
    dl->PopClipRect();
    FullscreenButton(info, ev, origin, region);
}

// The video controls: seek bar with the in/out range and a hover picture, play/pause, frame steps, range buttons.
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

void MainUI::DrawTransport(Settings& /*s*/, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 6));
    ImGui::BeginChild("##transport", size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    const bool busy = info.videoProcessing || info.batchRunning;
    const double frame = info.videoFps > 0.0 ? 1.0 / info.videoFps : 1.0 / 30.0;
    const double duration = std::max(info.videoDurationSeconds, frame);
    const float frameH = ImGui::GetFrameHeight();

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
    const float barH = frameH * 0.9f;
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
    const float trackH = std::max(4.0f, barH * 0.28f);
    dl->AddRectFilled(ImVec2(barPos.x, cy - trackH * 0.5f), ImVec2(barPos.x + barW, cy + trackH * 0.5f), p.track, trackH * 0.5f);
    const double inSec = std::max(0.0, info.videoIn);
    const double outSec = info.videoOut > inSec ? info.videoOut : duration;
    const bool ranged = inSec > 0.0 || info.videoOut > 0.0;
    if (ranged)
        dl->AddRectFilled(ImVec2(xAt(inSec), cy - trackH * 0.9f), ImVec2(xAt(outSec), cy + trackH * 0.9f), p.rangeFill, trackH * 0.5f);
    if (busy && info.videoProcessing) {
        const unsigned long long total = std::max(info.videoFrames, info.videoFrame);
        const double frac = total ? (double)info.videoFrame / (double)total : 0.0;
        const double from = info.batchRunning ? 0.0 : inSec, to = info.batchRunning ? duration : outSec;
        dl->AddRectFilled(ImVec2(xAt(from), cy - trackH * 0.5f), ImVec2(xAt(from + (to - from) * frac), cy + trackH * 0.5f), p.warn, trackH * 0.5f);
    } else {
        dl->AddRectFilled(ImVec2(barPos.x, cy - trackH * 0.5f), ImVec2(xAt(shownPos), cy + trackH * 0.5f), p.accent, trackH * 0.5f);
        if (ranged) {
            const float mh = barH * 0.42f;
            dl->AddRectFilled(ImVec2(xAt(inSec) - 1.5f, cy - mh), ImVec2(xAt(inSec) + 1.5f, cy + mh), p.knob);
            if (info.videoOut > 0.0) dl->AddRectFilled(ImVec2(xAt(outSec) - 1.5f, cy - mh), ImVec2(xAt(outSec) + 1.5f, cy + mh), p.knob);
        }
        const float grow = Animate(ImGui::GetID("##knob"), (barHovered || m_seekDragging) ? 1.0f : 0.0f, 18.0f);
        const float r = barH * (0.27f + 0.07f * grow);
        dl->AddCircleFilled(ImVec2(xAt(shownPos), cy), r, p.knob);
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
        const float w = std::min(ImGui::GetFontSize() * 13.0f, barW);
        const float h = w * 9.0f / 16.0f;
        const float lineH = ImGui::GetTextLineHeight();
        const float boxH = (cell >= 0 ? h + 4.0f : 0.0f) + lineH + 10.0f;
        const float x = std::clamp(io.MousePos.x - w * 0.5f, barPos.x, barPos.x + barW - w);
        const float y0 = barPos.y - boxH - 6.0f;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        const float lift = (1.0f - hoverT) * 6.0f;   // the card rises and fades in
        fg->AddRectFilled(ImVec2(x, y0 + lift), ImVec2(x + w, y0 + boxH + lift), WithAlpha(p.overlayBg, 0.82f * hoverT), 4.0f);
        if (cell >= 0) {
            float u0, v0, u1, v1;
            info.atlas->Uv(cell, u0, v0, u1, v1);
            fg->AddImageRounded(ImTextureRef((ImTextureID)info.atlas->TextureHandle()), ImVec2(x + 2.0f, y0 + 2.0f + lift), ImVec2(x + w - 2.0f, y0 + 2.0f + h + lift),
                                ImVec2(u0, v0), ImVec2(u1, v1), WithAlpha(IM_COL32_WHITE, hoverT), 3.0f);
        }
        ImGui::PushFont(fonts.Mono(), 0.0f);
        const std::string tt = FormatClock(t);
        const ImVec2 ts = ImGui::CalcTextSize(tt.c_str());
        fg->AddText(ImVec2(x + (w - ts.x) * 0.5f, y0 + boxH - lineH - 5.0f + lift), WithAlpha(IM_COL32(235, 237, 242, 255), hoverT), tt.c_str());
        ImGui::PopFont();
        fg->AddLine(ImVec2(io.MousePos.x, barPos.y), ImVec2(io.MousePos.x, barPos.y + barH), WithAlpha(p.knob, 0.5f * hoverT));
    }

    // Controls row.
    ImGui::BeginDisabled(busy);
    if (IconButton("##back", Icon::StepBack, ImVec2(frameH * 1.6f, frameH), TR(PrevFrame))) ev.videoStep -= 1;
    ImGui::SameLine(0.0f, 4.0f);
    if (IconButton("##play", info.videoPlaying ? Icon::Pause : Icon::Play, ImVec2(frameH * 2.2f, frameH), info.videoPlaying ? TR(Pause) : TR(Play), ButtonKind::Accent))
        ev.videoPlayToggle = true;
    ImGui::SameLine(0.0f, 4.0f);
    if (IconButton("##fwd", Icon::StepForward, ImVec2(frameH * 1.6f, frameH), TR(NextFrame))) ev.videoStep += 1;
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::PushFont(fonts.Mono(), 0.0f);
    ImGui::AlignTextToFramePadding();
    if (busy && info.videoProcessing) {
        ImGui::TextUnformatted(StrPrintf(TR(FrameOf), (unsigned long long)info.videoFrame, (unsigned long long)std::max(info.videoFrames, info.videoFrame)).c_str());
    } else {
        ImGui::TextUnformatted(FormatClock(shownPos).c_str());
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(" / %s", FormatDuration(duration).c_str());
    }
    ImGui::PopFont();
    if (!busy && (info.videoSeeking || m_seekTarget >= 0.0)) {
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR(Seeking));
    }

    // Range buttons at the right end.
    const char* inText = TR(SetIn);
    const char* outText = TR(SetOut);
    const char* wholeText = TR(WholeVideo);
    auto bw = [&](const char* t) { return ImGui::CalcTextSize(t).x + style.FramePadding.x * 2.0f; };
    const std::string rangeText = ranged ? StrPrintf("%s %s \xE2\x80\x93 %s", TR(RangeLabel), FormatDuration(inSec).c_str(), FormatDuration(outSec).c_str()) : std::string();
    const float rangeW = ranged ? ImGui::CalcTextSize(rangeText.c_str()).x + 12.0f : 0.0f;
    const float helpW = ImGui::GetFontSize() * 1.05f + style.ItemSpacing.x;
    const float rightW = bw(inText) + bw(outText) + bw(wholeText) + style.ItemSpacing.x * 2.0f + rangeW + helpW;
    const float rightX = ImGui::GetWindowWidth() - style.WindowPadding.x - rightW;
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    {
        ImGui::SameLine(std::max(leftEnd + 16.0f, rightX));
        if (ranged) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", rangeText.c_str());
            ImGui::SameLine(0.0f, 12.0f);
        }
        ImGui::BeginDisabled(busy);
        if (GhostButton(inText)) ev.videoSetIn = true;
        ImGui::SameLine();
        if (GhostButton(outText)) ev.videoSetOut = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(!ranged);
        if (GhostButton(wholeText)) ev.videoClearRange = true;
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        Help(TR(RangeHint));
    }
    ImGui::EndChild();
}

// The media library: a strip of thumbnails under the preview. A click previews a file, a drag across the cards
// selects them for processing (Ctrl adds, Shift extends), the right button opens a card's menu.
void MainUI::DrawLibrary(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts, const ImVec2& pos, const ImVec2& size) {
    const Palette& p = Colors();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 6));
    ImGui::BeginChild("##library", size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    std::vector<LibraryItem>* lib = info.library;
    const int count = lib ? (int)lib->size() : 0;
    int selected = 0;
    if (lib) for (const auto& it : *lib) if (it.selected && it.probe != 2) ++selected;
    const bool running = info.batchRunning;
    const bool busy = running || info.videoProcessing;
    const float frameH = ImGui::GetFrameHeight();
    const float fold = m_libraryFold;

    // Header line: caption, counts, and the actions at the right end.
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(fonts.Bold(), 0.0f);
    ImGui::TextUnformatted(TR(SecLibrary));
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::AlignTextToFramePadding();
    std::string caption = count ? StrPrintf(TR(LibraryCount), count, selected) : std::string(TR(BatchEmpty));
    if (running) {
        const double left = BatchRemaining(info);
        if (left >= 0.0) caption += "  \xC2\xB7  " + StrPrintf(TR(RemainingFmt), ("\xE2\x89\x88 " + FormatEstimate(left)).c_str());
    }
    ImGui::TextDisabled("%s", caption.c_str());
    if (count && !running && fold > 0.5f) {
        ImGui::SameLine(0.0f, 10.0f);
        if (ImGui::SmallButton(TR(SelectAll))) { for (auto& it : *lib) it.selected = it.probe != 2; }
        ImGui::SameLine();
        if (ImGui::SmallButton(TR(SelectNone))) { for (auto& it : *lib) it.selected = false; }
        Help(TR(TipLibrarySelect));
    }
    {
        const char* addF = TR(AddFiles);
        const char* addD = TR(AddFolder);
        const char* procSel = TR(ProcessSelected);
        const char* procAll = TR(BatchStart);
        const char* del = TR(Delete);
        const char* cancel = TR(Cancel);
        auto bw = [&](const char* t) { return ImGui::CalcTextSize(t).x + style.FramePadding.x * 2.0f; };
        const float toggleW = frameH + 6.0f;
        float rightW = toggleW + style.ItemSpacing.x;
        if (running) rightW += bw(cancel) + style.ItemSpacing.x;
        else rightW += bw(addF) + bw(addD) + bw(procSel) + bw(procAll) + bw(del) + style.ItemSpacing.x * 5.0f;
        const float rightX = ImGui::GetWindowWidth() - style.WindowPadding.x - rightW;
        const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        ImGui::SameLine(std::max(leftEnd + 12.0f, rightX));
        if (running) {
            if (AccentButton(cancel)) ev.batchCancel = true;
            ImGui::SameLine();
        } else {
            if (GhostButton(addF)) ev.libraryAddFiles = true;
            ImGui::SameLine();
            if (GhostButton(addD)) ev.libraryAddFolder = true;
            ImGui::SameLine();
            ImGui::BeginDisabled(selected == 0 || busy);
            if (AccentButton(procSel)) ev.libraryProcessSelected = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(count == 0 || busy);
            if (FlatButton(procAll)) ev.libraryProcessAll = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            // Red only while something is selected. It drops the items from the library; the files stay.
            ImGui::BeginDisabled(selected == 0);
            if (DangerButton(del)) ev.libraryDeleteSelected = true;
            Tip(TR(TipDelete));
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ChevronButton("##fold", IM_PI * (1.0f - fold), ImVec2(toggleW, 0), TR(ShowLibrary))) { s.libraryVisible = !s.libraryVisible; ev.settingsChanged = true; }
    }
    if (fold <= 0.001f) { m_libDrag = false; ImGui::EndChild(); return; }

    // The strip.
    const float cardW = ImGui::GetFontSize() * 8.0f;
    const float thumbH = ImGui::GetFontSize() * 4.5f;
    const float lineH = ImGui::GetTextLineHeight();
    const float cardH = thumbH + lineH * 2.0f + 6.0f;
    const float stripH = std::max(cardH + style.ScrollbarSize + 4.0f, ImGui::GetContentRegionAvail().y);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##strip", ImVec2(0, stripH), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGuiIO& io = ImGui::GetIO();
    SmoothScroll(true, cardW + style.ItemSpacing.x);
    if (count == 0) {
        m_libDrag = false;
        const float wrap = std::min(ImGui::GetContentRegionAvail().x - 20.0f, ImGui::GetFontSize() * 40.0f);
        const ImVec2 ts = ImGui::CalcTextSize(TR(LibraryHint), nullptr, false, wrap);
        const ImVec2 o = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorScreenPos(ImVec2(o.x + std::max(0.0f, (avail.x - ts.x) * 0.5f), o.y + std::max(0.0f, (avail.y - ts.y) * 0.5f)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextDisabled("%s", TR(LibraryHint));
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::EndChild();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImTextureRef atlasTex = (info.atlas && info.atlas->Ready()) ? ImTextureRef((ImTextureID)info.atlas->TextureHandle()) : ImTextureRef();
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    const bool stripHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    const bool menuOpen = ImGui::IsPopupOpen("##libctx");   // judged here: inside a card's PushID the name would hash differently
    // Ctrl+A while the cursor is on the strip (or it was clicked last) selects every readable file.
    if ((stripHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) && !running && !io.WantTextInput && io.KeyCtrl && !io.KeyAlt
        && ImGui::IsKeyPressed(ImGuiKey_A, false))
        for (auto& it : *lib) it.selected = it.probe != 2;
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
        if (dragging && it.probe != 2) {
            const bool inRect = marquee.Overlaps(ImRect(c0, c1));
            const bool kept = io.KeyCtrl && std::find(m_libDragKeep.begin(), m_libDragKeep.end(), it.id) != m_libDragKeep.end();
            it.selected = inRect || kept;
        }
        const float hov = Animate(ImGui::GetID("##hover"), hovered ? 1.0f : 0.0f, 16.0f);
        const float sel = Animate(ImGui::GetID("##selected"), it.selected ? 1.0f : 0.0f, 16.0f);
        if (hovered && !dragging && !menuOpen) {
            if (it.probe == 2 && !it.error.empty()) TooltipShow(it.id, it.error.c_str());
            else if (it.state == LibraryItem::Failed && !it.error.empty()) TooltipShow(it.id, it.error.c_str());
            else if (it.state == LibraryItem::Done && !it.outName.empty()) TooltipShow(it.id, StrPrintf("%s: %s", TR(Saved), it.outName.c_str()).c_str());
            else TooltipShow(it.id, it.name.c_str());
        }
        const bool current = (s.sourceMode == SourceVideo && it.isVideo && !info.videoPath.empty() && it.path == info.videoPath)
                          || (s.sourceMode == SourceImage && !it.isVideo && !info.imagePath.empty() && it.path == info.imagePath);
        if (sel > 0.001f) dl->AddRectFilled(ImVec2(c0.x - 3.0f, c0.y - 3.0f), ImVec2(c1.x + 3.0f, c1.y + 1.0f), WithAlpha(p.selection, sel * 0.9f), 5.0f);
        dl->AddRectFilled(c0, t1, p.control, 3.0f);
        if (info.atlas && it.thumbCell >= 0 && info.atlas->Filled(it.thumbCell)) {
            float u0, v0, u1, v1;
            info.atlas->Uv(it.thumbCell, u0, v0, u1, v1);
            dl->AddImageRounded(atlasTex, c0, t1, ImVec2(u0, v0), ImVec2(u1, v1), IM_COL32_WHITE, 3.0f);
        } else {
            const char* mark = it.probe == 2 ? "!" : "\xE2\x80\xA6";
            const ImVec2 ms = ImGui::CalcTextSize(mark);
            dl->AddText(ImVec2(c0.x + (cardW - ms.x) * 0.5f, c0.y + (thumbH - ms.y) * 0.5f), it.probe == 2 ? p.bad : p.textDim, mark);
        }
        if (it.state == LibraryItem::Queued) dl->AddRectFilled(c0, t1, IM_COL32(0, 0, 0, 110), 3.0f);
        if (it.state == LibraryItem::Processing) {
            const float ph = 4.0f;
            dl->AddRectFilled(ImVec2(c0.x, t1.y - ph), t1, IM_COL32(0, 0, 0, 160), 3.0f, ImDrawFlags_RoundCornersBottom);
            dl->AddRectFilled(ImVec2(c0.x, t1.y - ph), ImVec2(c0.x + cardW * std::clamp(it.progress, 0.02f, 1.0f), t1.y), p.accent, 3.0f, ImDrawFlags_RoundCornersBottom);
        }
        if (hov > 0.001f) dl->AddRectFilled(c0, t1, WithAlpha(IM_COL32(255, 255, 255, 255), 0.05f * hov), 3.0f);
        if (current) dl->AddRect(ImVec2(c0.x - 1.0f, c0.y - 1.0f), ImVec2(t1.x + 1.0f, t1.y + 1.0f), p.accent, 4.0f, 2.0f);
        else if (sel > 0.001f) dl->AddRect(c0, t1, WithAlpha(p.accent, sel), 3.0f);
        else if (hov > 0.001f) dl->AddRect(c0, t1, WithAlpha(p.knob, 0.6f * hov), 3.0f);
        // State badge at the top right of the picture.
        if (const char* st = StateText(it)) {
            const ImU32 col = (it.state == LibraryItem::Done) ? p.good : (it.state == LibraryItem::Failed || it.probe == 2) ? p.bad
                            : (it.state == LibraryItem::Processing) ? p.accentHover : p.warn;
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.82f);
            const ImVec2 ss = ImGui::CalcTextSize(st);
            const ImVec2 b0(t1.x - ss.x - 12.0f, c0.y + 4.0f);
            dl->AddRectFilled(b0, ImVec2(b0.x + ss.x + 8.0f, b0.y + ss.y + 4.0f), WithAlpha(IM_COL32(0, 0, 0, 255), 0.7f), 3.0f);
            dl->AddText(ImVec2(b0.x + 4.0f, b0.y + 2.0f), col, st);
            ImGui::PopFont();
        }
        // A file with its own values carries a small mark at the bottom left of the picture.
        if (it.useOwn && it.own) {
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.78f);
            const ImVec2 os = ImGui::CalcTextSize(TR(OwnBadge));
            const ImVec2 b0(c0.x + 4.0f, t1.y - os.y - 8.0f);
            dl->AddRectFilled(b0, ImVec2(b0.x + os.x + 8.0f, b0.y + os.y + 4.0f), WithAlpha(p.accent, 0.85f), 3.0f);
            dl->AddText(ImVec2(b0.x + 4.0f, b0.y + 2.0f), p.accentText, TR(OwnBadge));
            ImGui::PopFont();
        }
        // Name and details.
        const float textY = t1.y + 3.0f;
        ImGui::RenderTextEllipsis(dl, ImVec2(c0.x, textY), ImVec2(c0.x + cardW, textY + lineH), c0.x + cardW, it.name.c_str(), nullptr, nullptr);
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
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.88f);
        ImGui::RenderTextEllipsis(dl, ImVec2(c0.x, textY + lineH), ImVec2(c0.x + cardW, textY + lineH * 2.0f), c0.x + cardW, detail.c_str(), nullptr, nullptr);
        ImGui::PopFont();
        // The selection box and the remove button sit on the picture.
        if (it.probe != 2) {
            ImGui::SetCursorScreenPos(ImVec2(c0.x + 4.0f, c0.y + 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::BeginDisabled(running);
            if (ImGui::Checkbox("##sel", &it.selected)) m_lastClicked = it.id;
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
            overControl |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        }
        if (hov > 0.01f && !running && it.state != LibraryItem::Processing) {
            const float bs = frameH * 0.8f;
            const ImVec2 b0(t1.x - bs - 4.0f, t1.y - bs - 4.0f);
            dl->AddRectFilled(b0, ImVec2(b0.x + bs, b0.y + bs), WithAlpha(IM_COL32(0, 0, 0, 255), 0.55f * hov), 3.0f);
            ImGui::SetCursorScreenPos(b0);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * hov);
            if (IconButton("##remove", Icon::Close, ImVec2(bs, bs), TR(Remove), ButtonKind::Plain)) ev.libraryRemove = it.id;
            ImGui::PopStyleVar();
            overControl |= ImGui::IsItemHovered();
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + (float)count * (cardW + style.ItemSpacing.x), rowStart.y));
    ImGui::Dummy(ImVec2(1.0f, cardH));

    // Presses on the strip: a click previews (Ctrl toggles the box, Shift extends from the last one), a drag selects.
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
            if (!m_libDragMoved && !running && (std::fabs(io.MousePos.x - a.x) > 4.0f || std::fabs(io.MousePos.y - a.y) > 4.0f)) m_libDragMoved = true;
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
                        ev.libraryPreview = item->id;
                        m_lastClicked = item->id;
                    }
                }
            }
            m_libDrag = false;
            m_libDragMoved = false;
        }
    }
    if (dragging) {
        dl->AddRectFilled(marquee.Min, marquee.Max, WithAlpha(p.accent, 0.14f), 2.0f);
        dl->AddRect(marquee.Min, marquee.Max, WithAlpha(p.accent, 0.8f), 2.0f);
    }
    if (menuCard) { m_ctxItem = menuCard; m_libDrag = false; ImGui::OpenPopup("##libctx"); }
    DrawLibraryMenu(s, info, ev);
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
        SmoothScroll(false, ImGui::GetFontSize() * 3.6f);
        ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);
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
            EffectControls(*lead->own, sub, s.showAdvanced, s.nrEnabled);
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

void MainUI::DrawStatusBar(Settings& s, const UiFrameInfo& info, UiEvents& /*ev*/, const Fonts& fonts) {
    const Palette& p = Colors();
    ImGui::BeginChild("##status", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetScrollX(0.0f);
    ImGui::SetCursorPosY(ImGui::GetStyle().ItemSpacing.y);
    // Monospace, padded figures throughout: nothing here may shift when a number changes.
    ImGui::PushFont(fonts.Mono(), 0.0f);
    auto leftText = [&](double seconds) { return "  \xC2\xB7  " + StrPrintf(TR(RemainingFmt), ("\xE2\x89\x88 " + FormatEstimate(seconds)).c_str()); };
    if (info.batchRunning) {
        const std::string progress = StrPrintf(TR(BatchRunning), std::min(info.batchIndex + 1, info.batchCount), info.batchCount);
        std::string line = StrPrintf("%s  %s", progress.c_str(), info.batchItemName.c_str());
        const double left = BatchRemaining(info);
        if (left >= 0.0) line += leftText(left);
        StatusDot(p.accentHover, line.c_str());
    } else if (s.sourceMode == SourceVideo) {
        if (info.videoProcessing) {
            const unsigned long long total = std::max(info.videoFrames, info.videoFrame);
            std::string line = StrPrintf("%s  %s", info.videoName.c_str(), StrPrintf(TR(FrameOf), (unsigned long long)info.videoFrame, total).c_str());
            if (info.videoFrame >= 5 && info.videoElapsed > 0.5 && !info.videoFinishing)
                line += leftText((double)(total - info.videoFrame) * info.videoElapsed / (double)info.videoFrame);
            StatusDot(p.warn, line.c_str());
        } else if (info.videoLoaded) {
            StatusDot(p.good, StrPrintf("%s  %ux%u @ %3.0f", info.videoName.c_str(), info.videoWidth, info.videoHeight, info.videoFps).c_str());
        } else {
            StatusDot(p.muted, TR(NoVideo));
        }
    } else if (s.sourceMode == SourceImage) {
        if (info.imageLoaded) StatusDot(p.good, StrPrintf("%s  %ux%u", info.imageName.c_str(), info.imageWidth, info.imageHeight).c_str());
        else StatusDot(p.muted, TR(NoImage));
    } else if (info.sourceConnected && info.status) {
        StatusDot(p.good, StrPrintf("%s  %ux%u @ %3.0f", info.senderName.c_str(), info.status->srcWidth, info.status->srcHeight, m_shown.senderFps).c_str());
    } else {
        StatusDot(p.muted, TR(StatusWaiting));
    }
    const float rightEdge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    std::string capture;
    float captureW = 0.0f;
    if (!info.lastCapture.empty()) {
        capture = StrPrintf("%s: %s", TR(LastCapture), info.lastCapture.c_str());
        captureW = ImGui::CalcTextSize(capture.c_str()).x;
    }
    if (info.status) {
        const std::string timers = StrPrintf("%s %s | %s %s | %s %s | %s %s", TR(TmGuidance),
                            FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Guidance] + m_shown.gpuMs[(UINT)GpuTimer::OpticalFlow]).c_str(),
                            TR(TmNeural), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Neural]).c_str(),
                            TR(TmComposite), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Composite]).c_str(),
                            TR(TmUi), FormatMsFixed(m_shown.uiGpuMs).c_str());
        const float timersW = ImGui::CalcTextSize(timers.c_str()).x;
        if (leftEnd + 24.0f + timersW + (captureW > 0.0f ? captureW + 24.0f : 0.0f) <= rightEdge) {
            ImGui::SameLine(0.0f, 24.0f);
            ImGui::TextDisabled("%s", timers.c_str());
            leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        }
    }
    ImGui::PopFont();
    if (captureW > 0.0f && leftEnd + 24.0f + captureW <= rightEdge) {
        ImGui::SameLine(rightEdge - captureW);
        ImGui::PushStyleColor(ImGuiCol_Text, info.lastCaptureOk ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : p.bad);
        ImGui::TextUnformatted(capture.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void MainUI::DrawLogWindow(Settings& s, UiEvents& ev, const Fonts& fonts) {
    ImGui::SetNextWindowSize(ImVec2(760, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR(LogTitle), &s.showLog)) {
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
    const double lifetime = 4.5;
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [&](const ToastItem& t) { return now - t.time > lifetime; }), m_toasts.end());
    if (m_toasts.empty()) return;
    const Palette& p = Colors();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGui::PushFont(fonts.Ui(), 0.0f);
    float y = vp->WorkPos.y + vp->WorkSize.y - 48.0f;
    for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
        const double age = now - it->time;
        const float alpha = (float)std::clamp(std::min(age / 0.2, (lifetime - age) / 0.6), 0.0, 1.0);
        const float slide = (1.0f - Ease((float)std::clamp(age / 0.25, 0.0, 1.0))) * 24.0f;
        const float wrap = std::min(vp->WorkSize.x * 0.5f, ImGui::GetFontSize() * 36.0f);
        const ImVec2 ts = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, wrap);
        const ImVec2 pad(14.0f, 10.0f);
        const ImVec2 size(ts.x + pad.x * 2.0f + 14.0f, ts.y + pad.y * 2.0f);
        const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - size.x - 22.0f + slide, y - size.y);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), WithAlpha(IM_COL32(30, 33, 41, 255), alpha * 0.96f), 4.0f);
        dl->AddRectFilled(pos, ImVec2(pos.x + 4.0f, pos.y + size.y), WithAlpha(it->error ? p.bad : p.accent, alpha), 4.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddText(nullptr, 0.0f, ImVec2(pos.x + pad.x + 8.0f, pos.y + pad.y), WithAlpha(IM_COL32(235, 237, 242, 255), alpha), it->text.c_str(), nullptr, wrap);
        y = pos.y - 8.0f;
    }
    ImGui::PopFont();
}

} // namespace vdc::ui
