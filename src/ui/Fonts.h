// VRChat DLSS5 Cam - font loading (Segoe UI + CJK fallbacks from the Windows font folder, the built-in icon font,
// dynamic sizes).
#pragma once
#include "core/I18n.h"
#include "imgui.h"
#include <string>
#include <vector>

namespace vdc::ui {

class Fonts {
public:
    Fonts() = default;
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;
    ~Fonts();
    // Rebuilds the atlas. Call between frames (before ImGui::NewFrame).
    bool Build(Lang lang);
    Lang BuiltFor() const { return m_lang; }
    bool Built() const { return m_ui != nullptr; }
    ImFont* Ui() const { return m_ui; }
    ImFont* Bold() const { return m_bold ? m_bold : m_ui; }
    ImFont* Mono() const { return m_mono ? m_mono : m_ui; }
    ImFont* Icons() const { return m_icons; }   // the Lucide line icons (private-use code points, see IconFont.h)

private:
    // A font file mapped into memory once per build (read-only; the system brings in the pages of the glyphs that
    // are drawn, and nothing is copied): the faces that merge the same file (the CJK and symbol fonts sit under the
    // UI and the monospace text alike) share it.
    struct File { std::wstring path; const void* data = nullptr; int size = 0; };
    const File* Load(const std::wstring& path);   // null when the file is missing or unreadable
    bool Add(const std::wstring& path, ImFontConfig& cfg, ImFont** out);
    void ReleaseFiles();                          // only once the atlas no longer refers to them
    // matchEm/matchCap: the em square and the capital height of the face whose look the CJK glyphs should keep
    // next to this family's letters, as shares of the font size (0: merged as they come).
    ImFont* AddFamily(const wchar_t* baseFile, Lang lang, bool bold, float matchEm = 0.0f, float matchCap = 0.0f);
    std::vector<File> m_files;
    Lang    m_lang = Lang::English;
    ImFont* m_ui = nullptr;
    ImFont* m_bold = nullptr;
    ImFont* m_mono = nullptr;
    ImFont* m_icons = nullptr;
};

} // namespace vdc::ui
