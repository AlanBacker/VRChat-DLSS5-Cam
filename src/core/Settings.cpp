#include "core/Settings.h"
#include "core/Util.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace vdc {

namespace {
std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

struct Reader {
    std::map<std::string, std::string> kv;
    bool Has(const char* k) const { return kv.count(k) != 0; }
    void Get(const char* k, int& v) const { auto it = kv.find(k); if (it != kv.end()) v = atoi(it->second.c_str()); }
    void Get(const char* k, unsigned& v) const { auto it = kv.find(k); if (it != kv.end()) v = (unsigned)strtoul(it->second.c_str(), nullptr, 10); }
    void Get(const char* k, float& v) const { auto it = kv.find(k); if (it != kv.end()) v = (float)atof(it->second.c_str()); }
    void Get(const char* k, bool& v) const { auto it = kv.find(k); if (it != kv.end()) v = it->second == "1" || it->second == "true"; }
    void Get(const char* k, std::string& v) const { auto it = kv.find(k); if (it != kv.end()) v = it->second; }
};

struct Writer {
    std::string out;
    void Put(const char* k, int v) { out += StrPrintf("%s=%d\n", k, v); }
    void Put(const char* k, unsigned v) { out += StrPrintf("%s=%u\n", k, v); }
    void Put(const char* k, float v) { out += StrPrintf("%s=%.4f\n", k, v); }
    void Put(const char* k, bool v) { out += StrPrintf("%s=%d\n", k, v ? 1 : 0); }
    void Put(const char* k, const std::string& v) { out += std::string(k) + "=" + v + "\n"; }
};
} // namespace

bool Settings::Load(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);

    Reader r;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        std::string line = Trim(data.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        r.kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }

    r.Get("language", language);
    r.Get("senderName", senderName);
    r.Get("customResolution", customResolution);
    r.Get("customWidth", customWidth);
    r.Get("customHeight", customHeight);
    r.Get("keepAspect", keepAspect);
    r.Get("nrEnabled", nrEnabled);
    r.Get("nrRoute", nrRoute);
    r.Get("nrDllPath", nrDllPath);
    r.Get("nrPreset", nrPreset);
    r.Get("nrStyle", nrStyle);
    r.Get("nrIntensity", nrIntensity);
    r.Get("nrGlobalTone", nrGlobalTone);
    r.Get("nrLocalTone", nrLocalTone);
    r.Get("nrLocalStructure", nrLocalStructure);
    r.Get("nrSkinStructure", nrSkinStructure);
    r.Get("nrAutoMask", nrAutoMask);
    r.Get("hdrPaperWhite", hdrPaperWhite);
    r.Get("hdrHighlightCompression", hdrHighlightCompression);
    r.Get("nrUiCorrection", nrUiCorrection);
    r.Get("nrUpscale", nrUpscale);
    r.Get("motionMode", motionMode);
    r.Get("depthMode", depthMode);
    r.Get("searchRadius", searchRadius);
    r.Get("motionConfidence", motionConfidence);
    r.Get("nvofGrid", nvofGrid);
    r.Get("nvofPerf", nvofPerf);
    r.Get("nvofBidirectional", nvofBidirectional);
    r.Get("depthInterval", depthInterval);
    r.Get("depthLongSide", depthLongSide);
    r.Get("depthModelPath", depthModelPath);
    r.Get("autoReset", autoReset);
    r.Get("cutThreshold", cutThreshold);
    int fileVersion = 1;
    r.Get("settingsVersion", fileVersion);
    if (fileVersion < 2) {
        // 0.1.x files: block matching, flat depth and auto reset were the defaults. 0.2.0 moved to hardware optical
        // flow, estimated depth and no automatic resets; adopt the new defaults once.
        motionMode = Settings().motionMode;
        depthMode = Settings().depthMode;
        autoReset = Settings().autoReset;
    }
    settingsVersion = Settings().settingsVersion;
    r.Get("dlaaEnabled", dlaaEnabled);
    r.Get("dlaaPreset", dlaaPreset);
    r.Get("compareMode", compareMode);
    r.Get("wipePosition", wipePosition);
    r.Get("checkerboard", checkerboard);
    r.Get("fitMode", fitMode);
    r.Get("vsync", vsync);
    r.Get("showOverlay", showOverlay);
    r.Get("captureFolder", captureFolder);
    r.Get("keepAlpha", keepAlpha);
    r.Get("saveOriginal", saveOriginal);
    r.Get("hotkeyEnabled", hotkeyEnabled);
    r.Get("hotkeyModifiers", hotkeyModifiers);
    r.Get("hotkeyKey", hotkeyKey);
    r.Get("timelapseSeconds", timelapseSeconds);
    r.Get("windowX", windowX);
    r.Get("windowY", windowY);
    r.Get("windowWidth", windowWidth);
    r.Get("windowHeight", windowHeight);
    r.Get("windowMaximized", windowMaximized);
    r.Get("sidebarVisible", sidebarVisible);
    r.Get("showLog", showLog);
    r.Get("debugLayer", debugLayer);
    Clamp();
    return true;
}

bool Settings::Save(const std::wstring& path) const {
    Writer w;
    w.out += "# VRChat DLSS5 Cam settings\n";
    w.Put("language", language);
    w.Put("senderName", senderName);
    w.Put("customResolution", customResolution);
    w.Put("customWidth", customWidth);
    w.Put("customHeight", customHeight);
    w.Put("keepAspect", keepAspect);
    w.Put("nrEnabled", nrEnabled);
    w.Put("nrRoute", nrRoute);
    w.Put("nrDllPath", nrDllPath);
    w.Put("nrPreset", nrPreset);
    w.Put("nrStyle", nrStyle);
    w.Put("nrIntensity", nrIntensity);
    w.Put("nrGlobalTone", nrGlobalTone);
    w.Put("nrLocalTone", nrLocalTone);
    w.Put("nrLocalStructure", nrLocalStructure);
    w.Put("nrSkinStructure", nrSkinStructure);
    w.Put("nrAutoMask", nrAutoMask);
    w.Put("hdrPaperWhite", hdrPaperWhite);
    w.Put("hdrHighlightCompression", hdrHighlightCompression);
    w.Put("nrUiCorrection", nrUiCorrection);
    w.Put("nrUpscale", nrUpscale);
    w.Put("settingsVersion", settingsVersion);
    w.Put("motionMode", motionMode);
    w.Put("depthMode", depthMode);
    w.Put("searchRadius", searchRadius);
    w.Put("motionConfidence", motionConfidence);
    w.Put("nvofGrid", nvofGrid);
    w.Put("nvofPerf", nvofPerf);
    w.Put("nvofBidirectional", nvofBidirectional);
    w.Put("depthInterval", depthInterval);
    w.Put("depthLongSide", depthLongSide);
    w.Put("depthModelPath", depthModelPath);
    w.Put("autoReset", autoReset);
    w.Put("cutThreshold", cutThreshold);
    w.Put("dlaaEnabled", dlaaEnabled);
    w.Put("dlaaPreset", dlaaPreset);
    w.Put("compareMode", compareMode);
    w.Put("wipePosition", wipePosition);
    w.Put("checkerboard", checkerboard);
    w.Put("fitMode", fitMode);
    w.Put("vsync", vsync);
    w.Put("showOverlay", showOverlay);
    w.Put("captureFolder", captureFolder);
    w.Put("keepAlpha", keepAlpha);
    w.Put("saveOriginal", saveOriginal);
    w.Put("hotkeyEnabled", hotkeyEnabled);
    w.Put("hotkeyModifiers", hotkeyModifiers);
    w.Put("hotkeyKey", hotkeyKey);
    w.Put("timelapseSeconds", timelapseSeconds);
    w.Put("windowX", windowX);
    w.Put("windowY", windowY);
    w.Put("windowWidth", windowWidth);
    w.Put("windowHeight", windowHeight);
    w.Put("windowMaximized", windowMaximized);
    w.Put("sidebarVisible", sidebarVisible);
    w.Put("showLog", showLog);
    w.Put("debugLayer", debugLayer);

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    fwrite(w.out.data(), 1, w.out.size(), f);
    fclose(f);
    return true;
}

void Settings::Clamp() {
    language = std::clamp(language, 0, 4);
    customWidth = std::clamp(customWidth, 256, 7680);
    customHeight = std::clamp(customHeight, 256, 4320);
    nrRoute = std::clamp(nrRoute, 0, 1);
    nrPreset = std::clamp(nrPreset, 0, 3);
    nrStyle = std::clamp(nrStyle, 0, 2);
    nrIntensity = std::clamp(nrIntensity, 0.0f, 2.0f);
    nrGlobalTone = std::clamp(nrGlobalTone, 0.0f, 2.0f);
    nrLocalTone = std::clamp(nrLocalTone, 0.0f, 2.0f);
    nrLocalStructure = std::clamp(nrLocalStructure, 0.0f, 2.0f);
    nrSkinStructure = std::clamp(nrSkinStructure, -1.0f, 2.0f);
    hdrPaperWhite = std::clamp(hdrPaperWhite, 0.1f, 8.0f);
    hdrHighlightCompression = std::clamp(hdrHighlightCompression, 0.0f, 1.0f);
    motionMode = std::clamp(motionMode, 0, 2);
    depthMode = std::clamp(depthMode, 0, 3);
    depthInterval = std::clamp(depthInterval, 1, 10);
    if (depthLongSide != 252 && depthLongSide != 336 && depthLongSide != 420 && depthLongSide != 518) depthLongSide = 336;
    searchRadius = std::clamp(searchRadius, 2, 12);
    motionConfidence = std::clamp(motionConfidence, 0.0f, 1.0f);
    if (nvofGrid != 1 && nvofGrid != 2 && nvofGrid != 4) nvofGrid = 4;
    if (nvofPerf != 5 && nvofPerf != 10 && nvofPerf != 20) nvofPerf = 10;
    cutThreshold = std::clamp(cutThreshold, 0.01f, 0.5f);
    if (dlaaPreset != 0 && dlaaPreset != 10 && dlaaPreset != 11 && dlaaPreset != 12 && dlaaPreset != 13) dlaaPreset = 11;
    compareMode = std::clamp(compareMode, 0, 4);
    wipePosition = std::clamp(wipePosition, 0.0f, 1.0f);
    fitMode = std::clamp(fitMode, 0, 1);
    timelapseSeconds = std::clamp(timelapseSeconds, 0, 3600);
    windowWidth = std::clamp(windowWidth, 800, 10000);
    windowHeight = std::clamp(windowHeight, 500, 10000);
    hotkeyModifiers &= 0x000F;
}

} // namespace vdc
