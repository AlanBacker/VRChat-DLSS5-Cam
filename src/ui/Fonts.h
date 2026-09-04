// VRChat DLSS5 Cam - font loading (Segoe UI + CJK fallbacks from the Windows font folder, dynamic sizes).
#pragma once
#include "core/I18n.h"
#include "imgui.h"
#include <string>

namespace vdc::ui {

class Fonts {
public:
    // Rebuilds the atlas. Call between frames (before ImGui::NewFrame).
    bool Build(Lang lang);
    Lang BuiltFor() const { return m_lang; }
    bool Built() const { return m_ui != nullptr; }
    ImFont* Ui() const { return m_ui; }
    ImFont* Bold() const { return m_bold ? m_bold : m_ui; }
    ImFont* Mono() const { return m_mono ? m_mono : m_ui; }

private:
    ImFont* AddFamily(const wchar_t* baseFile, Lang lang, bool bold);
    Lang    m_lang = Lang::English;
    ImFont* m_ui = nullptr;
    ImFont* m_bold = nullptr;
    ImFont* m_mono = nullptr;
};

} // namespace vdc::ui
