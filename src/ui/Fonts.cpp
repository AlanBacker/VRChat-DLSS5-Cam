#include "ui/Fonts.h"
#include "core/Util.h"
#include "core/Log.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>

namespace vdc::ui {

namespace {

#include "ui/IconFont.inc"

const wchar_t* kCjkFonts[][3] = {
    { L"msyh.ttc", L"msyhbd.ttc", L"simhei.ttf" },       // Chinese (Microsoft YaHei / SimHei)
    { L"YuGothM.ttc", L"YuGothB.ttc", L"meiryo.ttc" },   // Japanese (Yu Gothic / Meiryo)
    { L"malgun.ttf", L"malgunbd.ttf", L"gulim.ttc" },    // Korean (Malgun Gothic / Gulim)
};

// A face's vertical proportions from its tables, as shares of the font size: ImGui sizes a face by its ascent minus
// its descent, so a face that leaves little room above and below its letters (Consolas) draws them larger than
// another (Segoe UI) at the same size. Zero when the tables cannot be read.
struct Shares { float em = 0.0f, cap = 0.0f; };

Shares ReadShares(const void* data, int size, int faceNo) {
    const unsigned char* d = static_cast<const unsigned char*>(data);
    const size_t n = size > 0 ? (size_t)size : 0;
    auto u16 = [&](size_t o) { return o + 2 <= n ? (uint32_t)d[o] << 8 | d[o + 1] : 0u; };
    auto u32 = [&](size_t o) { return o + 4 <= n ? (uint32_t)d[o] << 24 | (uint32_t)d[o + 1] << 16 | (uint32_t)d[o + 2] << 8 | d[o + 3] : 0u; };
    size_t face = 0;
    if (n >= 16 && std::memcmp(d, "ttcf", 4) == 0) face = u32(12 + 4 * (size_t)faceNo);   // a collection: its faces' offsets follow
    size_t head = 0, hhea = 0, os2 = 0;
    const uint32_t tables = u16(face + 4);
    for (uint32_t i = 0; i < tables; ++i) {
        const size_t rec = face + 12 + 16 * (size_t)i;
        if (rec + 16 > n) break;
        if (std::memcmp(d + rec, "head", 4) == 0) head = u32(rec + 8);
        else if (std::memcmp(d + rec, "hhea", 4) == 0) hhea = u32(rec + 8);
        else if (std::memcmp(d + rec, "OS/2", 4) == 0) os2 = u32(rec + 8);
    }
    Shares s;
    if (!head || !hhea) return s;
    const float unitsPerEm = (float)u16(head + 18);
    const float height = (float)(int16_t)u16(hhea + 4) - (float)(int16_t)u16(hhea + 6);   // ascent minus the (negative) descent
    if (unitsPerEm <= 0.0f || height <= 0.0f) return s;
    s.em = unitsPerEm / height;
    if (os2 && u16(os2) >= 2) s.cap = (float)(int16_t)u16(os2 + 88) / height;             // sCapHeight (OS/2 version 2 and later)
    return s;
}

Shares ReadShares(const ImFont* font) {
    if (!font || font->Sources.Size == 0) return {};
    const ImFontConfig* src = font->Sources[0];
    return ReadShares(src->FontData, src->FontDataSize, src->FontNo);
}

} // namespace

Fonts::~Fonts() { ReleaseFiles(); }

void Fonts::ReleaseFiles() {
    for (File& f : m_files)
        if (f.data) UnmapViewOfFile(f.data);
    m_files.clear();
}

const Fonts::File* Fonts::Load(const std::wstring& path) {
    for (const File& f : m_files)
        if (f.path == path) return f.data ? &f : nullptr;
    File f;
    f.path = path;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size) && size.QuadPart > 100 && size.QuadPart < INT_MAX) {   // smaller: not a font the atlas takes
            const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
            if (mapping) {
                f.data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
                if (f.data) f.size = (int)size.QuadPart;
                CloseHandle(mapping);   // the view keeps the mapping, and the mapping the file
            }
        }
        CloseHandle(file);
    }
    m_files.push_back(f);   // a missing file is remembered too, so it is looked for once
    return f.data ? &m_files.back() : nullptr;
}

bool Fonts::Add(const std::wstring& path, ImFontConfig& cfg, ImFont** out) {
    const File* file = Load(path);
    if (!file) return false;
    cfg.FontDataOwnedByAtlas = false;   // the mapping stays with m_files; the atlas only reads it
    ImFont* f = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<void*>(file->data), file->size, 0.0f, &cfg);
    if (!f) return false;
    if (out && !*out) *out = f;
    return true;
}

ImFont* Fonts::AddFamily(const wchar_t* baseFile, Lang lang, bool bold, float matchEm, float matchCap) {
    ImGuiIO& io = ImGui::GetIO();
    const std::wstring fontsDir = GetWindowsFontsDir();
    ImFont* font = nullptr;
    ImFontConfig cfg;
    cfg.PixelSnapH = false;
    if (!Add(JoinPath(fontsDir, baseFile), cfg, &font)) {
        if (!bold && !Add(JoinPath(fontsDir, L"arial.ttf"), cfg, &font)) { cfg.FontDataOwnedByAtlas = true; font = io.Fonts->AddFontDefault(&cfg); }
        else if (bold) font = nullptr;
    }
    if (!font) return nullptr;

    // The em square the CJK glyphs get here, as a share of the font size: next to this family's capitals as large as
    // the matched face's em is next to its capitals. The CJK faces are drawn at Segoe UI's em (they are made to sit
    // beside it); Consolas fills its whole line with its letters, so at the same size they looked a size too small
    // among its figures.
    float cjkEm = 0.0f;
    if (matchEm > 0.0f && matchCap > 0.0f) {
        const Shares own = ReadShares(font);
        if (own.cap > 0.0f) cjkEm = matchEm * own.cap / matchCap;
    }
    auto addCjk = [&](const wchar_t* file) {
        const std::wstring path = JoinPath(fontsDir, file);
        const File* f = Load(path);
        if (!f) return false;
        ImFontConfig m;
        m.MergeMode = true;
        m.PixelSnapH = false;
        if (cjkEm > 0.0f) {
            const Shares s = ReadShares(f->data, f->size, 0);
            if (s.em > 0.0f) m.ExtraSizeScale = std::clamp(cjkEm / s.em, 0.8f, 1.5f);
        }
        return Add(path, m, nullptr);
    };
    // Merge CJK fallbacks; the current language's font comes first so its glyph variants win.
    std::vector<int> order = { 0, 1, 2 };
    const int primary = (lang == Lang::Chinese) ? 0 : (lang == Lang::Japanese) ? 1 : (lang == Lang::Korean) ? 2 : -1;
    if (primary >= 0) { order.erase(order.begin() + primary); order.insert(order.begin(), primary); }
    for (int idx : order) {
        const wchar_t* candidates[2] = { bold ? kCjkFonts[idx][1] : kCjkFonts[idx][0], bold ? kCjkFonts[idx][0] : kCjkFonts[idx][2] };
        bool added = false;
        for (const wchar_t* c : candidates) {
            if (addCjk(c)) { added = true; break; }
        }
        if (!added && !bold) addCjk(kCjkFonts[idx][2]);
    }
    // Symbols (status glyphs).
    ImFontConfig sym;
    sym.MergeMode = true;
    Add(JoinPath(fontsDir, L"seguisym.ttf"), sym, nullptr);
    return font;
}

bool Fonts::Build(Lang lang) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.FontDefault = nullptr;
    ReleaseFiles();   // the cleared atlas no longer refers to them
    m_ui = AddFamily(L"segoeui.ttf", lang, false);
    // Titles in Segoe UI Semibold (lighter than Bold, which reads heavy in the section cards); Bold where it is missing.
    m_bold = AddFamily(L"seguisb.ttf", lang, true);
    if (!m_bold) m_bold = AddFamily(L"segoeuib.ttf", lang, true);
    // The monospace font carries translated text too (overlay, status bar, log), so it needs the
    // same CJK and symbol fallbacks; a mono font without them renders every CJK glyph as '?'. Its CJK glyphs keep
    // the size they have next to the UI font's letters.
    const Shares ui = ReadShares(m_ui);
    m_mono = AddFamily(L"consola.ttf", lang, false, ui.em, ui.cap);
    // The line icons are compiled in (no font file to look for); the atlas reads them from the program's own data.
    ImFontConfig icons;
    icons.FontDataOwnedByAtlas = false;
    icons.PixelSnapH = true;
    m_icons = io.Fonts->AddFontFromMemoryTTF((void*)kIconFontData, (int)sizeof(kIconFontData), 0.0f, &icons);
    if (!m_ui) { Log::Error("No UI font could be loaded"); return false; }
    io.FontDefault = m_ui;
    m_lang = lang;
    Log::Info("Fonts built for %s", I18n::LanguageName(lang));
    return true;
}

} // namespace vdc::ui
