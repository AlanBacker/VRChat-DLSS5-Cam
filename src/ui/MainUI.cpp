#include "ui/MainUI.h"
#include "ui/Theme.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Util.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

std::string FormatMs(double ms) { return StrPrintf("%.2f ms", ms); }
std::string FormatMsFixed(double ms) { return StrPrintf("%6.2f ms", ms); }   // for the monospace font: constant width
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
    if (ImGui::BeginCombo(label, items[*value])) {
        for (int i = 0; i < count; ++i) {
            const bool selected = (*value == i);
            if (ImGui::Selectable(items[i], selected)) { *value = i; changed = true; }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tooltip);
    return changed;
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
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##host", nullptr, flags);
    ImGui::PopStyleVar(3);

    DrawTopBar(s, info, ev, fonts);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float statusH = ImGui::GetFrameHeight() + style.ItemSpacing.y * 2.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bodyH = std::max(50.0f, avail.y - statusH - style.ItemSpacing.y);
    const float sidebarW = std::min(ImGui::GetFontSize() * 25.0f, avail.x * 0.5f);

    if (s.sidebarVisible) {
        ImGui::BeginChild("##sidebar", ImVec2(sidebarW, bodyH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::SetScrollX(0.0f);   // the sidebar only ever scrolls vertically
        DrawSidebar(s, info, ev, fonts);
        ImGui::EndChild();
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(9, 10, 13, 255));
    ImGui::BeginChild("##preview", ImVec2(0, bodyH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawPreview(s, info, ev, fonts);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    DrawStatusBar(s, info, ev, fonts);
    ImGui::End();

    if (s.showLog) DrawLogWindow(s, ev, fonts);
    DrawToasts(fonts);
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawTopBar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const Palette& p = Colors();
    const float frameH = ImGui::GetFrameHeight();
    const float rowH = frameH * 1.35f;
    // One padded row with every element centred on its middle line. The bar never scrolls: its parts are measured
    // first and the optional ones (rates, the wide language box, the status badges) are dropped when the window is too
    // narrow for all of them.
    ImGui::BeginChild("##top", ImVec2(0, rowH + style.WindowPadding.y * 2.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetScrollX(0.0f);
    ImGui::SetScrollY(0.0f);
    const float top = style.WindowPadding.y;
    auto centred = [&](float itemH) { return top + (rowH - itemH) * 0.5f; };
    const bool imageMode = s.sourceMode == SourceImage;

    // Left part: title and badges.
    const bool haveSenders = info.senders && !info.senders->empty();
    const char* badge = imageMode ? (info.imageLoaded ? TR(ImageLabel) : TR(NoImage))
                      : info.sourceConnected ? TR(StatusConnected) : haveSenders ? TR(StatusWaiting) : TR(StatusNoSpout);
    const ImU32 badgeFg = imageMode ? (info.imageLoaded ? p.good : p.muted)
                        : info.sourceConnected ? p.good : haveSenders ? p.warn : p.muted;
    const ImU32 badgeBg = WithAlpha(badgeFg, badgeFg == p.muted ? 0.2f : 0.18f);
    const bool nrBadge = info.status && info.status->nrActive;
    ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.25f);
    const ImVec2 titleSize = ImGui::CalcTextSize(TR(AppTitle));
    ImGui::PopFont();
    const float textH = ImGui::GetTextLineHeight();
    const float pillPad = 18.0f;   // Pill() adds 9 px on each side
    const float pillH = textH + 6.0f;
    const float badgesW = ImGui::CalcTextSize(badge).x + pillPad + (nrBadge ? ImGui::CalcTextSize("DLSS 5").x + pillPad + 6.0f : 0.0f);

    // Right part: processing / interface rates, language, sidebar toggle, capture. Rates are padded to three digits
    // in the monospace font and their reserved width comes from a template, so a changing number never moves the
    // controls to its right.
    const char* captureText = imageMode ? TR(ProcessAndSave) : TR(Capture);
    const float captureW = ImGui::CalcTextSize(captureText).x + style.FramePadding.x * 2.0f + 24.0f;
    const float sidebarBtnW = frameH + 6.0f;
    const std::string fpsText = imageMode
        ? StrPrintf("%s %3.0f %s", TR(UiFps), m_shown.fps, TR(Fps))
        : StrPrintf("%s %3.0f %s  \xC2\xB7  %s %3.0f %s", TR(ProcessingFps), m_shown.processingFps, TR(Fps), TR(UiFps), m_shown.fps, TR(Fps));
    const std::string fpsTemplate = imageMode
        ? StrPrintf("%s 000 %s", TR(UiFps), TR(Fps))
        : StrPrintf("%s 000 %s  \xC2\xB7  %s 000 %s", TR(ProcessingFps), TR(Fps), TR(UiFps), TR(Fps));
    ImGui::PushFont(fonts.Mono(), 0.0f);
    const float fpsW = ImGui::CalcTextSize(fpsTemplate.c_str()).x;
    ImGui::PopFont();

    const float availW = ImGui::GetWindowWidth() - style.WindowPadding.x * 2.0f;
    const float gap = 24.0f;
    float langW = ImGui::GetFontSize() * 8.0f;
    bool showFps = true, showBadges = true;
    auto rightW = [&]() {
        return captureW + sidebarBtnW + langW + style.ItemSpacing.x * 2.0f + (showFps ? fpsW + style.ItemSpacing.x : 0.0f);
    };
    auto leftW = [&]() { return titleSize.x + (showBadges ? 14.0f + badgesW : 0.0f); };
    if (leftW() + gap + rightW() > availW) showFps = false;
    if (leftW() + gap + rightW() > availW) langW = ImGui::GetFontSize() * 4.5f;
    if (leftW() + gap + rightW() > availW) showBadges = false;

    ImGui::SetCursorPosY(centred(titleSize.y));
    ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.25f);
    ImGui::TextUnformatted(TR(AppTitle));
    ImGui::PopFont();
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
        ImGui::SetCursorPosY(centred(textH));
        ImGui::PushFont(fonts.Mono(), 0.0f);
        ImGui::TextDisabled("%s", fpsText.c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipUiFps));
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
    if (ImGui::Button(s.sidebarVisible ? "<" : ">", ImVec2(sidebarBtnW, 0))) { s.sidebarVisible = !s.sidebarVisible; ev.settingsChanged = true; }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(Sidebar));
    ImGui::SameLine();
    ImGui::SetCursorPosY(centred(frameH));
    const std::string captureLabel = std::string("\xE2\x97\x8F ") + captureText;
    if (AccentButton(captureLabel.c_str(), ImVec2(captureW, 0))) ev.captureNow = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s (%s)", imageMode ? TR(ImageHint) : TR(CaptureHint), info.hotkeyText.c_str());
    ImGui::EndChild();
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawSidebar(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.5f);
    if (SectionHeader(TR(SecSource), "source")) SectionSource(s, info, ev);
    if (SectionHeader(TR(SecNeural), "neural")) SectionNeural(s, info, ev);
    if (SectionHeader(TR(SecGuidance), "guidance", false)) SectionGuidance(s, info, ev);
    if (SectionHeader(TR(SecDlaa), "dlaa", false)) SectionDlaa(s, info, ev);
    if (SectionHeader(TR(SecCapture), "capture")) SectionCapture(s, info, ev);
    if (SectionHeader(TR(SecDisplay), "display", false)) SectionDisplay(s, info, ev);
    if (SectionHeader(TR(SecAbout), "about", false)) SectionAbout(s, info, ev, fonts);
    ImGui::PopItemWidth();
}

void MainUI::SectionSource(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    // Live camera stream or a picture from disk.
    {
        const char* modes[] = { TR(SourceSpout), TR(SourceImage) };
        if (ComboIds(TR(SourceMode), &s.sourceMode, modes, 2, TR(SourceModeHint))) { ev.sourceModeChanged = true; ev.settingsChanged = true; }
    }
    if (s.sourceMode == SourceImage) {
        if (ImGui::Button(TR(OpenImage), ImVec2(-FLT_MIN, 0))) ev.openImage = true;
        if (info.imageLoaded) {
            StatusDot(p.good, StrPrintf("%s  %ux%u", info.imageName.c_str(), info.imageOrigWidth, info.imageOrigHeight).c_str());
            if (info.imageWidth != info.imageOrigWidth || info.imageHeight != info.imageOrigHeight)
                ImGui::TextDisabled("%s (%ux%u)", TR(ImageDownscaled), info.imageWidth, info.imageHeight);
            // Always one line here: a line that only appears while the picture converges would shift everything
            // below it each time a setting changes.
            ImGui::TextDisabled("%s", info.imageConverging ? TR(Processing) : TR(Converged));
            if (AccentButton(TR(ProcessAndSave), ImVec2(-FLT_MIN, 0))) ev.captureNow = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(CaptureHint));
        } else {
            StatusDot(p.muted, TR(NoImage));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", TR(ImageHint));
            ImGui::PopStyleColor();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", TR(DropHint));
        ImGui::PopStyleColor();
    } else {
    // Sender selection.
    {
        const std::string preview = s.senderName.empty() ? std::string(TR(SenderAuto)) : s.senderName;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::BeginCombo("##sender", preview.c_str())) {
            if (ImGui::Selectable(TR(SenderAuto), s.senderName.empty())) { s.senderName.clear(); ev.senderChanged = true; ev.settingsChanged = true; }
            if (info.senders) {
                for (const auto& name : *info.senders) {
                    if (ImGui::Selectable(name.c_str(), name == s.senderName)) { s.senderName = name; ev.senderChanged = true; ev.settingsChanged = true; }
                }
                if (info.senders->empty()) ImGui::TextDisabled("%s", TR(NoSenders));
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button("\xE2\x86\xBB", ImVec2(ImGui::GetFrameHeight(), 0))) ev.refreshSenders = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(Refresh));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(Sender));
    }
    // Detected info.
    if (info.status && info.sourceConnected) {
        StatusDot(p.good, StrPrintf("%s  %ux%u  %s  %3.0f %s", info.senderName.c_str(), info.status->srcWidth, info.status->srcHeight,
                                    info.sourceFormat.c_str(), m_shown.senderFps, TR(Fps)).c_str());
    } else {
        StatusDot(p.muted, TR(StatusWaiting));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", TR(HowToEnable));
        ImGui::PopStyleColor();
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
    if (s.sourceMode != SourceImage) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", TR(VrchatResHint));
        ImGui::PopStyleColor();
    }
    // HDR source controls: only meaningful for floating-point (scene-linear) Spout textures.
    if (info.sourceIsHdr && s.sourceMode != SourceImage) {
        ImGui::Spacing();
        ImGui::TextUnformatted(TR(HdrSource));
        ImGui::Indent(6.0f);
        bool ch = false;
        ch |= SliderReset(TR(PaperWhite), &s.hdrPaperWhite, 0.1f, 8.0f, 1.0f, "%.2f", TR(TipPaperWhite));
        ch |= SliderReset(TR(HighlightCompression), &s.hdrHighlightCompression, 0.0f, 1.0f, 1.0f, "%.2f", TR(TipHighlightCompression));
        if (ch) ev.settingsChanged = true;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", TR(HdrSourceHint));
        ImGui::PopStyleColor();
        ImGui::Unindent(6.0f);
    }
    ImGui::Spacing();
}

void MainUI::SectionNeural(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    if (Toggle(TR(NrEnable), &s.nrEnabled)) { ev.nrChanged = true; ev.settingsChanged = true; }
    ImGui::SameLine(0.0f, 12.0f);
    if (st) {
        if (st->nrActive) Pill(TR(Active), WithAlpha(p.good, 0.18f), p.good);
        else if (st->nrFailed) Pill(TR(Failed), WithAlpha(p.bad, 0.18f), p.bad);
        else if (st->nrStandby) Pill(TR(Standby), WithAlpha(p.warn, 0.18f), p.warn);
        else Pill(TR(Inactive), WithAlpha(p.muted, 0.2f), p.muted);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR(NrHint));
    ImGui::PopStyleColor();
    if (s.sourceMode != SourceImage) {
        if (Toggle(TR(NrCaptureOnly), &s.nrCaptureOnly)) ev.settingsChanged = true;
        Help(TR(TipNrCaptureOnly));
    }

    // Runtime.
    ImGui::Spacing();
    if (st && st->nrRuntimeLoaded) {
        StatusDot(p.good, StrPrintf("%s: %s %s", TR(Runtime), TR(Loaded), st->nrRuntimeVersion.c_str()).c_str());
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
    if (st && st->nrActive && (st->nrOutState == 2 || st->nrOutState == 3)) {
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
        if (ImGui::Button("...##runtime", ImVec2(btnW, 0))) ev.browseRuntime = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(Browse));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(RuntimePath));
        if (ImGui::SmallButton(TR(Reload))) ev.reloadRuntime = true;
    }
    ImGui::Spacing();
    {
        const char* routes[] = { TR(RouteSnippet), TR(RouteCore) };
        if (ComboIds(TR(Route), &s.nrRoute, routes, 2, TR(TipRoute))) { ev.nrChanged = true; ev.settingsChanged = true; }
    }
    ImGui::BeginDisabled(!s.nrEnabled);
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
        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipSkinStructure));
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
    ImGui::Spacing();
    ImGui::TextUnformatted(TR(OutputBlend));
    // The exposure changes what the network sees (neural pass re-run); the two strengths only change the composite.
    ch |= SliderReset(TR(InputExposure), &s.nrInputExposure, 0.25f, 4.0f, 1.0f, "%.2fx", TR(TipInputExposure));
    bool blend = false;
    blend |= SliderReset(TR(ToneTransfer), &s.nrToneTransfer, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipToneTransfer));
    blend |= SliderReset(TR(ColorStrength), &s.nrColorStrength, 0.0f, 2.0f, 1.0f, "%.2f", TR(TipColorStrength));
    ImGui::EndDisabled();
    if (blend) ev.settingsChanged = true;
    if (ch) { ev.nrChanged = true; ev.settingsChanged = true; }
    ImGui::Spacing();
    if (ImGui::Button(TR(ResetHistory))) ev.resetHistory = true;
    ImGui::SameLine();
    if (ImGui::Button(TR(ResetDefaults))) {
        s.nrPreset = 0; s.nrStyle = 0; s.nrIntensity = 1.0f; s.nrGlobalTone = 1.0f; s.nrLocalTone = 1.0f;
        s.nrLocalStructure = 1.0f; s.nrSkinStructure = -1.0f; s.nrAutoMask = false; s.nrUiCorrection = false;
        s.nrInputExposure = 1.0f; s.nrToneTransfer = 1.0f; s.nrColorStrength = 1.0f;
        ev.nrChanged = true; ev.settingsChanged = true;
    }
    if (st) {
        Readout(m_fonts, TR(GpuTime), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Neural]));
        Readout(m_fonts, TR(Frames), StrPrintf("%llu", m_shown.processedFrames));
        Readout(m_fonts, TR(NrOutputCheck), st->nrActive && m_shown.nrOutDelta >= 0.0f ? StrPrintf("%5.3f", m_shown.nrOutDelta) : std::string("    -"));
        Help(TR(TipNrOutputCheck));
    }
    ImGui::Spacing();
}

void MainUI::SectionGuidance(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    const PipelineStatus* st = info.status;
    {
        const char* items[] = { TR(MotionZero), TR(MotionCompute), TR(MotionNvof) };
        if (ComboIds(TR(MotionSource), &s.motionMode, items, 3, TR(TipMotion))) ev.settingsChanged = true;
    }
    if (s.motionMode == MotionCompute) {
        if (ImGui::SliderInt(TR(SearchRadius), &s.searchRadius, 2, 12, "%d px", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipSearchRadius));
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
            if (st->nvofReady) StatusDot(p.good, StrPrintf("%s: %s (%u px%s)", TR(Nvof), TR(Available), st->nvofGrid, st->nvofBidirectional ? " \xE2\x87\x84" : "").c_str());
            else if (!st->nvofAvailable) StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), TR(NotAvailable)).c_str());
            else StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), st->nvofError.empty() ? TR(NotAvailable) : st->nvofError.c_str()).c_str());
        }
    }
    if (s.motionMode != MotionZero) {
        if (ImGui::SliderFloat(TR(MotionConfidence), &s.motionConfidence, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipConfidence));
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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipDepthInterval));
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
            if (ImGui::Button("...##depthmodel", ImVec2(btnW, 0))) ev.browseDepthModel = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(Browse));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(TR(DepthModel));
            if (ImGui::SmallButton(StrPrintf("%s##depthreload", TR(Reload)).c_str())) ev.reloadDepth = true;
        }
    }
    if (Toggle(TR(AutoReset), &s.autoReset)) ev.settingsChanged = true;
    Help(TR(TipAutoReset));
    if (s.autoReset) {
        if (ImGui::SliderFloat(TR(CutThreshold), &s.cutThreshold, 0.01f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipCutThreshold));
    }
    if (st) {
        Readout(m_fonts, TR(FrameCost), StrPrintf("%5.3f  max %5.3f", m_shown.statAvgCost, m_shown.statMaxCost));
        Readout(m_fonts, "|mv|", StrPrintf("%5.2f px", m_shown.statAvgMotion));
        Readout(m_fonts, TR(Resets), StrPrintf("%llu", m_shown.resets));
        Readout(m_fonts, TR(TmGuidance), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Guidance]));
        Readout(m_fonts, TR(TmOpticalFlow), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::OpticalFlow]));
    }
    ImGui::Spacing();
}

void MainUI::SectionDlaa(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
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
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR(DlaaHint));
    ImGui::PopStyleColor();
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

void MainUI::SectionCapture(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    if (AccentButton(s.sourceMode == SourceImage ? TR(ProcessAndSave) : TR(Capture), ImVec2(-FLT_MIN, 0))) ev.captureNow = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(CaptureHint));
    {
        SyncBuffer(m_folderBuf, sizeof(m_folderBuf), s.captureFolder, m_folderEditing);
        const float btnW = ImGui::GetFrameHeight() * 1.6f;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - btnW * 2.0f - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
        const std::string hint = WideToUtf8(info.captureFolder);
        ImGui::InputTextWithHint("##folder", hint.c_str(), m_folderBuf, sizeof(m_folderBuf));
        m_folderEditing = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) { s.captureFolder = m_folderBuf; ev.settingsChanged = true; }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button("...##folder", ImVec2(btnW, 0))) ev.browseFolder = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(Browse));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button("\xE2\x86\x97", ImVec2(btnW, 0))) ev.openCaptureFolder = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(OpenFolder));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(CaptureFolder));
    }
    if (Toggle(TR(KeepAlpha), &s.keepAlpha)) ev.settingsChanged = true;
    Help(TR(TipKeepAlpha));
    if (Toggle(TR(SaveOriginal), &s.saveOriginal)) ev.settingsChanged = true;
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
        if (ImGui::BeginCombo(TR(Key), HotkeyKeyName(s.hotkeyKey))) {
            for (const auto& h : kHotkeys) {
                if (ImGui::Selectable(h.name, h.vk == s.hotkeyKey)) { s.hotkeyKey = h.vk; hc = true; }
            }
            ImGui::EndCombo();
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

void MainUI::SectionDisplay(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    {
        const char* items[] = { TR(CompareOutput), TR(CompareOriginal), TR(CompareWipe), TR(CompareMotion), TR(CompareDepth) };
        if (ComboIds(TR(Compare), &s.compareMode, items, 5)) ev.settingsChanged = true;
    }
    if (s.compareMode == CompareWipe) {
        if (ImGui::SliderFloat("##wipe", &s.wipePosition, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
    }
    if (Toggle(TR(Checkerboard), &s.checkerboard)) ev.settingsChanged = true;
    {
        const char* items[] = { TR(FitWindowLabel), TR(OneToOne) };
        if (ComboIds("##fit", &s.fitMode, items, 2)) { ev.settingsChanged = true; m_pan = ImVec2(0, 0); m_zoom = 1.0f; }
        // Manual magnification on top of the fit, shown relative to the picture's pixels. The wheel over the preview
        // does the same; the button returns to the fitted view.
        ImGui::PushID("zoom");
        const ImGuiStyle& style = ImGui::GetStyle();
        const float resetW = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - resetW - style.ItemInnerSpacing.x);
        float pct = m_zoom * m_baseScale * 100.0f;
        if (ImGui::SliderFloat("##z", &pct, kZoomMin * 100.0f, kZoomMax * 100.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
            m_zoom = pct / (100.0f * std::max(m_baseScale, 1e-6f));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipZoom));
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::BeginDisabled(std::fabs(m_zoom - 1.0f) < 1e-4f && m_pan.x == 0.0f && m_pan.y == 0.0f);
        if (ImGui::Button("\xE2\x86\xBA", ImVec2(resetW, 0))) { m_pan = ImVec2(0, 0); m_zoom = 1.0f; }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(ResetView));
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(TR(Zoom));
        ImGui::PopID();
    }
    if (Toggle(TR(Vsync), &s.vsync)) ev.settingsChanged = true;
    if (Toggle(TR(Overlay), &s.showOverlay)) ev.settingsChanged = true;
    if (Toggle(TR(ShowLog), &s.showLog)) ev.settingsChanged = true;
    if (info.status) {
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
    }
    ImGui::Spacing();
}

void MainUI::SectionAbout(Settings& /*s*/, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    ImGui::PushFont(fonts.Bold(), 0.0f);
    ImGui::Text("%s %s", TR(AppTitle), info.appVersion.c_str());
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR(AboutText));
    ImGui::PopStyleColor();
    if (info.adapter) {
        ImGui::TextDisabled("%s:", TR(Gpu)); ImGui::SameLine(); ImGui::TextWrapped("%s", WideToUtf8(info.adapter->name).c_str());
        const std::wstring& drv = info.adapter->nvidiaDriverVersion.empty() ? info.adapter->driverVersion : info.adapter->nvidiaDriverVersion;
        ImGui::TextDisabled("%s:", TR(Driver)); ImGui::SameLine(); ImGui::TextUnformatted(WideToUtf8(drv).c_str());
    }
    if (info.status) {
        ImGui::TextDisabled("%s:", TR(NgxStatus)); ImGui::SameLine(); ImGui::TextUnformatted(info.status->ngxStatus.c_str());
        ImGui::TextDisabled("%s:", TR(Nvof)); ImGui::SameLine(); ImGui::TextUnformatted(info.status->nvofAvailable ? TR(Available) : TR(NotAvailable));
    }
    if (ImGui::Button(TR(OpenLogFile))) ev.openLogFile = true;
    ImGui::SameLine();
    if (ImGui::Button(TR(OpenSettingsFolder))) ev.openSettingsFolder = true;
    if (ImGui::Button(TR(ProjectPage))) ev.openProjectPage = true;
    ImGui::SameLine();
    if (ImGui::Button(TR(Licenses))) ev.openLicenses = true;
    ImGui::Spacing();
    if (ImGui::Button(TR(ResetAllSettings))) ImGui::OpenPopup("##resetall");
    if (ImGui::BeginPopup("##resetall")) {
        ImGui::TextUnformatted(TR(ResetAllSettings));
        ImGui::Separator();
        if (ImGui::Button(TR(Ok), ImVec2(120, 0))) { ev.resetDefaults = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button(TR(Cancel), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Spacing();
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawPreview(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
    const Palette& p = Colors();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 region = ImGui::GetContentRegionAvail();
    if (region.x < 8.0f || region.y < 8.0f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (!info.hasDisplay || !info.displayTexture || !info.displayWidth || !info.displayHeight) {
        const char* text = (s.sourceMode == SourceImage) ? (info.imageLoaded ? TR(NoDisplay) : TR(ImageHint))
                                                         : (info.sourceConnected ? TR(NoDisplay) : TR(PreviewHint));
        ImGui::PushFont(fonts.Ui(), ImGui::GetStyle().FontSizeBase * 1.1f);
        const float wrap = std::min(region.x * 0.8f, ImGui::GetFontSize() * 30.0f);
        const ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, wrap);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + (region.x - size.x) * 0.5f, origin.y + (region.y - size.y) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        return;
    }

    // Image rectangle: fitted to the view or 1:1, times the manual magnification (wheel, slider; double-click resets).
    const float texW = (float)info.displayWidth, texH = (float)info.displayHeight;
    m_baseScale = (s.fitMode == FitWindow) ? std::min(region.x / texW, region.y / texH) : 1.0f;
    const float zoomMin = kZoomMin / m_baseScale, zoomMax = kZoomMax / m_baseScale;
    m_zoom = std::clamp(m_zoom, zoomMin, zoomMax);
    auto imageSize = [&]() { return ImVec2(texW * m_baseScale * m_zoom, texH * m_baseScale * m_zoom); };
    auto imagePos = [&](const ImVec2& size) {
        return ImVec2(origin.x + (region.x - size.x) * 0.5f + m_pan.x, origin.y + (region.y - size.y) * 0.5f + m_pan.y);
    };

    ImGui::SetCursorScreenPos(origin);
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
            const float old = m_zoom;
            m_zoom = std::clamp(m_zoom * (io.MouseWheel > 0 ? 1.25f : 0.8f), zoomMin, zoomMax);
            if (std::fabs(m_zoom - 1.0f) < 0.06f) m_zoom = 1.0f;   // snaps back to the fitted view
            // Keep the picture point under the cursor where it is.
            const float k = m_zoom / old;
            const ImVec2 centre(origin.x + region.x * 0.5f, origin.y + region.y * 0.5f);
            m_pan.x = (m_pan.x + centre.x - io.MousePos.x) * k + io.MousePos.x - centre.x;
            m_pan.y = (m_pan.y + centre.y - io.MousePos.y) * k + io.MousePos.y - centre.y;
        }
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { m_zoom = 1.0f; m_pan = ImVec2(0, 0); }
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
    if (s.compareMode == CompareWipe) {
        const float wipeX = imgPos.x + imgSize.x * s.wipePosition;
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
    dl->AddImageRounded(ImTextureRef(info.displayTexture), imgPos, imgMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
    if (s.compareMode == CompareWipe) {
        const float wipeX = imgPos.x + imgSize.x * s.wipePosition;
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
            : StrPrintf("GPU %s   %s %3.0f %s   %s %3.0f %s   %s %3.0f %s", FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Frame]).c_str(),
                        TR(ProcessingFps), m_shown.processingFps, TR(Fps), TR(UiFps), m_shown.fps, TR(Fps), TR(SenderFps), m_shown.senderFps, TR(Fps));
        ImGui::PushFont(fonts.Mono(), ImGui::GetStyle().FontSizeBase * 0.92f);
        float w = 0.0f;
        for (auto& l : lines) w = std::max(w, ImGui::CalcTextSize(l.c_str()).x);
        const float lh = ImGui::GetFontSize() + 3.0f;
        const ImVec2 pad(10.0f, 8.0f);
        const ImVec2 bpos(origin.x + 12.0f, origin.y + 12.0f);
        dl->AddRectFilled(bpos, ImVec2(bpos.x + w + pad.x * 2.0f, bpos.y + lh * 4.0f + pad.y * 2.0f), p.overlayBg, 8.0f);
        for (int i = 0; i < 4; ++i)
            dl->AddText(ImVec2(bpos.x + pad.x, bpos.y + pad.y + lh * i), i == 1 && st.nrActive ? p.good : IM_COL32(235, 237, 242, 255), lines[i].c_str());
        ImGui::PopFont();
    }
    if (s.fitMode == FitOneToOne || std::fabs(m_zoom - 1.0f) > 1e-3f) {
        ImGui::PushFont(fonts.Mono(), 0.0f);
        const std::string z = StrPrintf("%s %.0f%%", TR(Zoom), m_baseScale * m_zoom * 100.0f);
        const ImVec2 zs = ImGui::CalcTextSize(z.c_str());
        dl->AddRectFilled(ImVec2(origin.x + region.x - zs.x - 28.0f, origin.y + 12.0f), ImVec2(origin.x + region.x - 12.0f, origin.y + 12.0f + zs.y + 10.0f), p.overlayBg, 6.0f);
        dl->AddText(ImVec2(origin.x + region.x - zs.x - 20.0f, origin.y + 17.0f), IM_COL32(235, 237, 242, 255), z.c_str());
        ImGui::PopFont();
    }
    dl->PopClipRect();
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawStatusBar(Settings& s, const UiFrameInfo& info, UiEvents& /*ev*/, const Fonts& fonts) {
    const Palette& p = Colors();
    ImGui::BeginChild("##status", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetScrollX(0.0f);
    ImGui::SetCursorPosY(ImGui::GetStyle().ItemSpacing.y);
    // Monospace, padded figures throughout: nothing here may shift when a number changes.
    ImGui::PushFont(fonts.Mono(), 0.0f);
    if (s.sourceMode == SourceImage) {
        if (info.imageLoaded) StatusDot(p.good, StrPrintf("%s  %ux%u", info.imageName.c_str(), info.imageWidth, info.imageHeight).c_str());
        else StatusDot(p.muted, TR(NoImage));
    } else if (info.sourceConnected && info.status) {
        StatusDot(p.good, StrPrintf("%s  %ux%u @ %3.0f", info.senderName.c_str(), info.status->srcWidth, info.status->srcHeight, m_shown.senderFps).c_str());
    } else {
        StatusDot(p.muted, TR(StatusWaiting));
    }
    if (info.status) {
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::TextDisabled("%s %s | %s %s | %s %s | %s %s", TR(TmGuidance),
                            FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Guidance] + m_shown.gpuMs[(UINT)GpuTimer::OpticalFlow]).c_str(),
                            TR(TmNeural), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Neural]).c_str(),
                            TR(TmComposite), FormatMsFixed(m_shown.gpuMs[(UINT)GpuTimer::Composite]).c_str(),
                            TR(TmUi), FormatMsFixed(m_shown.uiGpuMs).c_str());
    }
    ImGui::PopFont();
    if (!info.lastCapture.empty()) {
        const std::string text = StrPrintf("%s: %s", TR(LastCapture), info.lastCapture.c_str());
        const float w = ImGui::CalcTextSize(text.c_str()).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 24.0f, ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x));
        ImGui::PushStyleColor(ImGuiCol_Text, info.lastCaptureOk ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : p.bad);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void MainUI::DrawLogWindow(Settings& s, UiEvents& ev, const Fonts& fonts) {
    ImGui::SetNextWindowSize(ImVec2(760, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(TR(LogTitle), &s.showLog)) {
        if (ImGui::Button(TR(OpenLogFile))) ev.openLogFile = true;
        ImGui::SameLine();
        if (ImGui::Button(TR(Clear))) { m_logCache.clear(); m_logGeneration = Log::Generation(); }
        if (Log::Generation() != m_logGeneration) { m_logCache = Log::Snapshot(); m_logGeneration = Log::Generation(); }
        ImGui::BeginChild("##logtext", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
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
        const float wrap = std::min(vp->WorkSize.x * 0.5f, ImGui::GetFontSize() * 36.0f);
        const ImVec2 ts = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, wrap);
        const ImVec2 pad(14.0f, 10.0f);
        const ImVec2 size(ts.x + pad.x * 2.0f + 14.0f, ts.y + pad.y * 2.0f);
        const ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x - size.x - 22.0f, y - size.y);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), WithAlpha(IM_COL32(30, 33, 41, 255), alpha * 0.96f), 8.0f);
        dl->AddRectFilled(pos, ImVec2(pos.x + 5.0f, pos.y + size.y), WithAlpha(it->error ? p.bad : p.accent, alpha), 8.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddText(nullptr, 0.0f, ImVec2(pos.x + pad.x + 8.0f, pos.y + pad.y), WithAlpha(IM_COL32(235, 237, 242, 255), alpha), it->text.c_str(), nullptr, wrap);
        y = pos.y - 8.0f;
    }
    ImGui::PopFont();
}

} // namespace vdc::ui
