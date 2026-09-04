#include "ui/Fonts.h"
#include "core/Util.h"
#include "core/Log.h"
#include <vector>

namespace vdc::ui {

namespace {

const wchar_t* kCjkFonts[][3] = {
    { L"msyh.ttc", L"msyhbd.ttc", L"simhei.ttf" },       // Chinese (Microsoft YaHei / SimHei)
    { L"YuGothM.ttc", L"YuGothB.ttc", L"meiryo.ttc" },   // Japanese (Yu Gothic / Meiryo)
    { L"malgun.ttf", L"malgunbd.ttf", L"gulim.ttc" },    // Korean (Malgun Gothic / Gulim)
};

bool TryAdd(ImFontAtlas* atlas, const std::wstring& path, ImFontConfig& cfg, ImFont** out) {
    if (!FileExists(path)) return false;
    ImFont* f = atlas->AddFontFromFileTTF(WideToUtf8(path).c_str(), 0.0f, &cfg);
    if (!f) return false;
    if (out && !*out) *out = f;
    return true;
}

} // namespace

ImFont* Fonts::AddFamily(const wchar_t* baseFile, Lang lang, bool bold) {
    ImGuiIO& io = ImGui::GetIO();
    const std::wstring fontsDir = GetWindowsFontsDir();
    ImFont* font = nullptr;
    ImFontConfig cfg;
    cfg.PixelSnapH = false;
    if (!TryAdd(io.Fonts, JoinPath(fontsDir, baseFile), cfg, &font)) {
        if (!bold && !TryAdd(io.Fonts, JoinPath(fontsDir, L"arial.ttf"), cfg, &font)) font = io.Fonts->AddFontDefault(&cfg);
        else if (bold) font = nullptr;
    }
    if (!font) return nullptr;

    // Merge CJK fallbacks; the current language's font comes first so its glyph variants win.
    std::vector<int> order = { 0, 1, 2 };
    const int primary = (lang == Lang::Chinese) ? 0 : (lang == Lang::Japanese) ? 1 : (lang == Lang::Korean) ? 2 : -1;
    if (primary >= 0) { order.erase(order.begin() + primary); order.insert(order.begin(), primary); }
    for (int idx : order) {
        ImFontConfig m;
        m.MergeMode = true;
        m.PixelSnapH = false;
        const wchar_t* candidates[2] = { bold ? kCjkFonts[idx][1] : kCjkFonts[idx][0], bold ? kCjkFonts[idx][0] : kCjkFonts[idx][2] };
        bool added = false;
        for (const wchar_t* c : candidates) {
            if (TryAdd(io.Fonts, JoinPath(fontsDir, c), m, nullptr)) { added = true; break; }
        }
        if (!added && !bold) TryAdd(io.Fonts, JoinPath(fontsDir, kCjkFonts[idx][2]), m, nullptr);
    }
    // Symbols (status glyphs).
    ImFontConfig sym;
    sym.MergeMode = true;
    TryAdd(io.Fonts, JoinPath(fontsDir, L"seguisym.ttf"), sym, nullptr);
    return font;
}

bool Fonts::Build(Lang lang) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.FontDefault = nullptr;
    m_ui = AddFamily(L"segoeui.ttf", lang, false);
    m_bold = AddFamily(L"segoeuib.ttf", lang, true);
    // The monospace font carries translated text too (overlay, status bar, log), so it needs the
    // same CJK and symbol fallbacks; a mono font without them renders every CJK glyph as '?'.
    m_mono = AddFamily(L"consola.ttf", lang, false);
    if (!m_ui) { Log::Error("No UI font could be loaded"); return false; }
    io.FontDefault = m_ui;
    m_lang = lang;
    Log::Info("Fonts built for %s", I18n::LanguageName(lang));
    return true;
}

} // namespace vdc::ui
