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

void MainUI::Draw(Settings& s, const UiFrameInfo& info, UiEvents& ev, const Fonts& fonts) {
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
    const float h = ImGui::GetFrameHeight() * 1.35f;
    ImGui::BeginChild("##top", ImVec2(0, h + style.WindowPadding.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    const float y = (h - ImGui::GetFrameHeight()) * 0.5f;
    ImGui::SetCursorPosY(y);

    ImGui::PushFont(fonts.Bold(), style.FontSizeBase * 1.25f);
    ImGui::TextUnformatted(TR(AppTitle));
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 14.0f);
    ImGui::SetCursorPosY(y + 2.0f);
    if (info.sourceConnected) Pill(TR(StatusConnected), WithAlpha(p.good, 0.18f), p.good);
    else if (info.senders && !info.senders->empty()) Pill(TR(StatusWaiting), WithAlpha(p.warn, 0.18f), p.warn);
    else Pill(TR(StatusNoSpout), WithAlpha(p.muted, 0.2f), p.muted);
    if (info.status && info.status->nrActive) {
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosY(y + 2.0f);
        Pill("DLSS 5", WithAlpha(p.accent, 0.2f), p.accentHover);
    }

    // Right-aligned controls: FPS, language, sidebar toggle, capture.
    const float captureW = ImGui::CalcTextSize(TR(Capture)).x + style.FramePadding.x * 2.0f + 24.0f;
    const float langW = ImGui::GetFontSize() * 8.0f;
    const float sidebarBtnW = ImGui::GetFrameHeight() + 6.0f;
    const std::string fpsText = StrPrintf("%.0f %s", info.fps, TR(Fps));
    const float fpsW = ImGui::CalcTextSize(fpsText.c_str()).x;
    const float total = captureW + langW + sidebarBtnW + fpsW + style.ItemSpacing.x * 3.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - style.WindowPadding.x - total));
    ImGui::SetCursorPosY(y + style.FramePadding.y);
    ImGui::PushFont(fonts.Mono(), 0.0f);
    ImGui::TextDisabled("%s", fpsText.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(y);
    ImGui::SetNextItemWidth(langW);
    {
        const char* items[] = { TR(LangAuto), "English", "简体中文", "日本語", "한국어" };
        if (ComboIds("##lang", &s.language, items, 5)) { ev.languageChanged = true; ev.settingsChanged = true; }
    }
    ImGui::SameLine();
    ImGui::SetCursorPosY(y);
    if (ImGui::Button(s.sidebarVisible ? "<" : ">", ImVec2(sidebarBtnW, 0))) { s.sidebarVisible = !s.sidebarVisible; ev.settingsChanged = true; }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(Sidebar));
    ImGui::SameLine();
    ImGui::SetCursorPosY(y);
    const std::string captureLabel = std::string("\xE2\x97\x8F ") + TR(Capture);
    if (AccentButton(captureLabel.c_str(), ImVec2(captureW, 0))) ev.captureNow = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s (%s)", TR(CaptureHint), info.hotkeyText.c_str());
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
        StatusDot(p.good, StrPrintf("%s  %ux%u  %s  %.0f %s", info.senderName.c_str(), info.status->srcWidth, info.status->srcHeight,
                                    info.sourceFormat.c_str(), info.senderFps, TR(Fps)).c_str());
    } else {
        StatusDot(p.muted, TR(StatusWaiting));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", TR(HowToEnable));
        ImGui::PopStyleColor();
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
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR(VrchatResHint));
    ImGui::PopStyleColor();
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
        else Pill(TR(Inactive), WithAlpha(p.muted, 0.2f), p.muted);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR(NrHint));
    ImGui::PopStyleColor();

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
        if (ImGui::Button("...", ImVec2(btnW, 0))) ev.browseRuntime = true;
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
    ImGui::EndDisabled();
    if (ch) { ev.nrChanged = true; ev.settingsChanged = true; }
    ImGui::Spacing();
    if (ImGui::Button(TR(ResetHistory))) ev.resetHistory = true;
    ImGui::SameLine();
    if (ImGui::Button(TR(ResetDefaults))) {
        s.nrPreset = 0; s.nrStyle = 0; s.nrIntensity = 1.0f; s.nrGlobalTone = 1.0f; s.nrLocalTone = 1.0f;
        s.nrLocalStructure = 1.0f; s.nrSkinStructure = -1.0f; s.nrAutoMask = false; s.nrUiCorrection = false;
        ev.nrChanged = true; ev.settingsChanged = true;
    }
    if (st && info.device) {
        ImGui::TextDisabled("%s: %s   %s: %llu", TR(GpuTime), FormatMs(info.device->TimerMs(GpuTimer::Neural)).c_str(),
                            TR(Frames), (unsigned long long)st->processedFrames);
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
        int grid = (s.nvofGrid == 1) ? 0 : (s.nvofGrid == 4) ? 2 : 1;
        const char* grids[] = { "1 px", "2 px", "4 px" };
        if (ComboIds(TR(NvofGrid), &grid, grids, 3, TR(TipNvof))) { s.nvofGrid = (grid == 0) ? 1 : (grid == 2) ? 4 : 2; ev.settingsChanged = true; }
        int perf = (s.nvofPerf == 5) ? 0 : (s.nvofPerf == 20) ? 2 : 1;
        const char* perfs[] = { TR(PerfSlow), TR(PerfMedium), TR(PerfFast) };
        if (ComboIds(TR(NvofPerf), &perf, perfs, 3)) { s.nvofPerf = (perf == 0) ? 5 : (perf == 2) ? 20 : 10; ev.settingsChanged = true; }
        if (st) {
            if (st->nvofReady) StatusDot(p.good, StrPrintf("%s: %s", TR(Nvof), TR(Available)).c_str());
            else if (!st->nvofAvailable) StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), TR(NotAvailable)).c_str());
            else StatusDot(p.warn, StrPrintf("%s: %s", TR(Nvof), st->nvofError.empty() ? TR(NotAvailable) : st->nvofError.c_str()).c_str());
        }
    }
    if (s.motionMode != MotionZero) {
        if (ImGui::SliderFloat(TR(MotionConfidence), &s.motionConfidence, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipConfidence));
    }
    {
        const char* items[] = { TR(DepthFlat), TR(DepthGradient), TR(DepthZero) };
        if (ComboIds(TR(DepthSource), &s.depthMode, items, 3, TR(TipDepth))) ev.settingsChanged = true;
    }
    if (Toggle(TR(AutoReset), &s.autoReset)) ev.settingsChanged = true;
    Help(TR(TipAutoReset));
    if (s.autoReset) {
        if (ImGui::SliderFloat(TR(CutThreshold), &s.cutThreshold, 0.01f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", TR(TipCutThreshold));
    }
    if (st) {
        ImGui::TextDisabled("%s: %.3f (max %.3f)   |mv| %.2f px", TR(FrameCost), st->statAvgCost, st->statMaxCost, st->statAvgMotion);
        ImGui::TextDisabled("%s: %llu", TR(Resets), (unsigned long long)st->resets);
        if (info.device)
            ImGui::TextDisabled("%s: %s / %s: %s", TR(TmGuidance), FormatMs(info.device->TimerMs(GpuTimer::Guidance)).c_str(),
                                TR(TmOpticalFlow), FormatMs(info.device->TimerMs(GpuTimer::OpticalFlow)).c_str());
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
    if (info.device) ImGui::TextDisabled("%s: %s", TR(GpuTime), FormatMs(info.device->TimerMs(GpuTimer::Dlaa)).c_str());
    ImGui::Spacing();
}

void MainUI::SectionCapture(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    const Palette& p = Colors();
    if (AccentButton(TR(Capture), ImVec2(-FLT_MIN, 0))) ev.captureNow = true;
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
        if (ImGui::Button("...", ImVec2(btnW, 0))) ev.browseFolder = true;
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
    if (info.capturePending) ImGui::TextDisabled("%zu %s", info.capturePending, TR(Pending));
    ImGui::Spacing();
}

void MainUI::SectionDisplay(Settings& s, const UiFrameInfo& info, UiEvents& ev) {
    {
        const char* items[] = { TR(CompareOutput), TR(CompareOriginal), TR(CompareWipe), TR(CompareMotion) };
        if (ComboIds(TR(Compare), &s.compareMode, items, 4)) ev.settingsChanged = true;
    }
    if (s.compareMode == CompareWipe) {
        if (ImGui::SliderFloat("##wipe", &s.wipePosition, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) ev.settingsChanged = true;
    }
    if (Toggle(TR(Checkerboard), &s.checkerboard)) ev.settingsChanged = true;
    {
        const char* items[] = { TR(FitWindowLabel), TR(OneToOne) };
        if (ComboIds("##fit", &s.fitMode, items, 2)) { ev.settingsChanged = true; m_pan = ImVec2(0, 0); m_zoom = 1.0f; }
        if (s.fitMode == FitOneToOne) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR(ResetView))) { m_pan = ImVec2(0, 0); m_zoom = 1.0f; }
        }
    }
    if (Toggle(TR(Vsync), &s.vsync)) ev.settingsChanged = true;
    if (Toggle(TR(Overlay), &s.showOverlay)) ev.settingsChanged = true;
    if (Toggle(TR(ShowLog), &s.showLog)) ev.settingsChanged = true;
    if (info.device) {
        ImGui::TextDisabled("%s:", TR(Timers));
        const struct { GpuTimer t; const char* name; } timers[] = {
            { GpuTimer::Convert, TR(TmConvert) }, { GpuTimer::Guidance, TR(TmGuidance) }, { GpuTimer::OpticalFlow, TR(TmOpticalFlow) },
            { GpuTimer::Dlaa, TR(TmDlaa) }, { GpuTimer::Neural, TR(TmNeural) }, { GpuTimer::Composite, TR(TmComposite) }, { GpuTimer::Ui, TR(TmUi) },
        };
        if (ImGui::BeginTable("timers", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& t : timers) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextDisabled("%s", t.name);
                ImGui::TableNextColumn(); ImGui::Text("%s", FormatMs(info.device->TimerMs(t.t)).c_str());
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextDisabled("CPU");
            ImGui::TableNextColumn(); ImGui::Text("%s", FormatMs(info.cpuMs).c_str());
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
        const char* text = info.sourceConnected ? TR(NoDisplay) : TR(PreviewHint);
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

    // Image rectangle.
    const float texW = (float)info.displayWidth, texH = (float)info.displayHeight;
    ImVec2 imgSize;
    if (s.fitMode == FitWindow) {
        const float scale = std::min(region.x / texW, region.y / texH);
        imgSize = ImVec2(texW * scale, texH * scale);
    } else {
        imgSize = ImVec2(texW * m_zoom, texH * m_zoom);
    }
    ImVec2 imgPos(origin.x + (region.x - imgSize.x) * 0.5f + m_pan.x, origin.y + (region.y - imgSize.y) * 0.5f + m_pan.y);
    if (s.fitMode == FitOneToOne) {
        // Keep the image within reach.
        if (imgSize.x > region.x) imgPos.x = std::clamp(imgPos.x, origin.x + region.x - imgSize.x, origin.x);
        if (imgSize.y > region.y) imgPos.y = std::clamp(imgPos.y, origin.y + region.y - imgSize.y, origin.y);
    }
    const ImVec2 imgMax(imgPos.x + imgSize.x, imgPos.y + imgSize.y);

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##canvas", region, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

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
    // Pan / zoom in 1:1 mode.
    if (s.fitMode == FitOneToOne) {
        if (active && !m_wipeDragging && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
            m_pan.x += io.MouseDelta.x; m_pan.y += io.MouseDelta.y;
        }
        if (hovered && io.MouseWheel != 0.0f) {
            const float old = m_zoom;
            m_zoom = std::clamp(m_zoom * (io.MouseWheel > 0 ? 1.25f : 0.8f), 0.1f, 8.0f);
            if (std::fabs(m_zoom - 1.0f) < 0.06f) m_zoom = 1.0f;
            const float k = m_zoom / old;
            m_pan.x = (m_pan.x + (origin.x + region.x * 0.5f - io.MousePos.x)) * k - (origin.x + region.x * 0.5f - io.MousePos.x);
            m_pan.y = (m_pan.y + (origin.y + region.y * 0.5f - io.MousePos.y)) * k - (origin.y + region.y * 0.5f - io.MousePos.y);
        }
    }

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
        const char* motion = st.motionModeActive == MotionNvOpticalFlow ? TR(MotionNvof) : st.motionModeActive == MotionCompute ? TR(MotionCompute) : TR(MotionZero);
        lines[2] = StrPrintf("%s: %s%s%s", TR(MotionSource), motion, st.dlaaActive ? "  +DLAA" : "", st.sceneCut ? StrPrintf("  [%s]", TR(SceneCut)).c_str() : "");
        lines[3] = info.device ? StrPrintf("GPU %s   %.0f %s   %s %.0f %s", FormatMs(info.device->TimerMs(GpuTimer::Frame)).c_str(), info.fps, TR(Fps),
                                           TR(SenderFps), info.senderFps, TR(Fps))
                               : std::string();
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
    if (s.fitMode == FitOneToOne) {
        ImGui::PushFont(fonts.Mono(), 0.0f);
        const std::string z = StrPrintf("%s %.0f%%", TR(Zoom), m_zoom * 100.0f);
        const ImVec2 zs = ImGui::CalcTextSize(z.c_str());
        dl->AddRectFilled(ImVec2(origin.x + region.x - zs.x - 28.0f, origin.y + 12.0f), ImVec2(origin.x + region.x - 12.0f, origin.y + 12.0f + zs.y + 10.0f), p.overlayBg, 6.0f);
        dl->AddText(ImVec2(origin.x + region.x - zs.x - 20.0f, origin.y + 17.0f), IM_COL32(235, 237, 242, 255), z.c_str());
        ImGui::PopFont();
    }
    dl->PopClipRect();
}

// ------------------------------------------------------------------------------------------

void MainUI::DrawStatusBar(Settings& /*s*/, const UiFrameInfo& info, UiEvents& /*ev*/, const Fonts& fonts) {
    const Palette& p = Colors();
    ImGui::BeginChild("##status", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY(ImGui::GetStyle().ItemSpacing.y);
    if (info.sourceConnected && info.status)
        StatusDot(p.good, StrPrintf("%s  %ux%u @ %.0f", info.senderName.c_str(), info.status->srcWidth, info.status->srcHeight, info.senderFps).c_str());
    else
        StatusDot(p.muted, TR(StatusWaiting));
    if (info.status && info.device) {
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::PushFont(fonts.Mono(), 0.0f);
        ImGui::TextDisabled("%s %s | %s %s | %s %s | %s %s", TR(TmGuidance), FormatMs(info.device->TimerMs(GpuTimer::Guidance) + info.device->TimerMs(GpuTimer::OpticalFlow)).c_str(),
                            TR(TmNeural), FormatMs(info.device->TimerMs(GpuTimer::Neural)).c_str(), TR(TmComposite), FormatMs(info.device->TimerMs(GpuTimer::Composite)).c_str(),
                            TR(TmUi), FormatMs(info.device->TimerMs(GpuTimer::Ui)).c_str());
        ImGui::PopFont();
    }
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
