#include "core/I18n.h"
#include "core/Util.h"

namespace vdc {

namespace {
struct Entry { const char* text[4]; };
const Entry kTable[] = {
#define VDC_STR_ROW(id, en, zh, ja, ko) { { en, zh, ja, ko } },
    VDC_STRING_LIST(VDC_STR_ROW)
#undef VDC_STR_ROW
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == (size_t)Str::Count, "string table out of sync");
Lang g_lang = Lang::English;
} // namespace

namespace I18n {

void SetLanguage(Lang lang) { g_lang = lang; }
Lang Current() { return g_lang; }

Lang Detect() {
    const LANGID id = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(id)) {
        case LANG_CHINESE:  return Lang::Chinese;
        case LANG_JAPANESE: return Lang::Japanese;
        case LANG_KOREAN:   return Lang::Korean;
        default:            return Lang::English;
    }
}

Lang FromSetting(int setting) {
    switch (setting) {
        case 1: return Lang::English;
        case 2: return Lang::Chinese;
        case 3: return Lang::Japanese;
        case 4: return Lang::Korean;
        default: return Detect();
    }
}

const char* T(Str id) {
    const size_t i = (size_t)id;
    if (i >= (size_t)Str::Count) return "?";
    const char* s = kTable[i].text[(int)g_lang];
    return (s && *s) ? s : kTable[i].text[0];
}

const char* LanguageName(Lang lang) {
    switch (lang) {
        case Lang::Chinese:  return "简体中文";
        case Lang::Japanese: return "日本語";
        case Lang::Korean:   return "한국어";
        default:             return "English";
    }
}

} // namespace I18n
} // namespace vdc
