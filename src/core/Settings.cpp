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
    mutable size_t used = 0;   // keys that matched a setting
    bool Has(const char* k) const { return kv.count(k) != 0; }
    void Get(const char* k, int& v) const { auto it = kv.find(k); if (it != kv.end()) { v = atoi(it->second.c_str()); ++used; } }
    void Get(const char* k, unsigned& v) const { auto it = kv.find(k); if (it != kv.end()) { v = (unsigned)strtoul(it->second.c_str(), nullptr, 10); ++used; } }
    void Get(const char* k, float& v) const { auto it = kv.find(k); if (it != kv.end()) { v = (float)atof(it->second.c_str()); ++used; } }
    void Get(const char* k, bool& v) const { auto it = kv.find(k); if (it != kv.end()) { v = it->second == "1" || it->second == "true"; ++used; } }
    void Get(const char* k, std::string& v) const { auto it = kv.find(k); if (it != kv.end()) { v = it->second; ++used; } }
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
    ApplyText(data);
    return true;
}

bool Settings::Apply(const std::string& key, const std::string& value) {
    return ApplyText(key + "=" + value + "\n");
}

// Reads "key=value" lines; keys that are not present keep their value. True when every key was known.
bool Settings::ApplyText(const std::string& data) {
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
    const size_t keys = r.kv.size();

    r.Get("language", language);
    r.Get("senderName", senderName);
    r.Get("sourceMode", sourceMode);
    r.Get("spoutRotate", spoutRotate);
    r.Get("spoutFlipH", spoutFlipH);
    r.Get("spoutFlipV", spoutFlipV);
    r.Get("imagePath", imagePath);
    r.Get("videoPath", videoPath);
    r.Get("videoMatchSource", videoMatchSource);
    r.Get("videoOutput", videoOutput);
    r.Get("videoBitrateMbps", videoBitrateMbps);
    r.Get("videoKeepAudio", videoKeepAudio);
    r.Get("videoHardwareDecode", videoHardwareDecode);
    r.Get("customResolution", customResolution);
    r.Get("customWidth", customWidth);
    r.Get("customHeight", customHeight);
    r.Get("keepAspect", keepAspect);
    r.Get("upscaleMode", upscaleMode);
    r.Get("nrEnabled", nrEnabled);
    r.Get("nrCaptureOnly", nrCaptureOnly);
    r.Get("nrRoute", nrRoute);
    r.Get("nrDllPath", nrDllPath);
    r.Get("nrRuntimeBuild", nrRuntimeBuild);
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
    r.Get("nrInputExposure", nrInputExposure);
    r.Get("nrToneTransfer", nrToneTransfer);
    r.Get("nrColorStrength", nrColorStrength);
    r.Get("nrShadowGain", nrShadowGain);
    r.Get("nrHighlightGain", nrHighlightGain);
    r.Get("nrInputScale", nrInputScale);
    r.Get("nrScaleMode", nrScaleMode);
    r.Get("nrMaxLongEdge", nrMaxLongEdge);
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
    if (fileVersion < 4 && nrRoute == RouteSignedSnippet) {
        // 1.4.0 added the automatic route choice (the FSR host on a Radeon card); files that kept the old default
        // move to it, an explicit NGX core choice stays.
        nrRoute = RouteAuto;
    }
    settingsVersion = Settings().settingsVersion;
    r.Get("dlaaEnabled", dlaaEnabled);
    r.Get("dlaaPreset", dlaaPreset);
    r.Get("compareMode", compareMode);
    r.Get("wipePosition", wipePosition);
    r.Get("checkerboard", checkerboard);
    r.Get("fitMode", fitMode);
    r.Get("vsync", vsync);
    r.Get("processRateLimit", processRateLimit);
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
    r.Get("libraryVisible", libraryVisible);
    r.Get("sidebarWidth", sidebarWidth);
    r.Get("libraryHeight", libraryHeight);
    r.Get("toolRowX", toolRowX);
    r.Get("toolRowY", toolRowY);
    r.Get("toolRowDock", toolRowDock);
    r.Get("updateCheck", updateCheck);
    r.Get("updateChannel", updateChannel);
    r.Get("portConsent", portConsent);
    r.Get("portInstalledTag", portInstalledTag);
    r.Get("advancedControls", showAdvanced);
    r.Get("theme", theme);
    r.Get("showLog", showLog);
    r.Get("reopenLast", reopenLast);
    r.Get("debugLayer", debugLayer);
    Clamp();
    return r.used == keys;
}

namespace {
// The values an undo step covers: everything the sidebar and the item windows adjust.
void PutParameters(Writer& w, const Settings& s) {
    w.Put("videoMatchSource", s.videoMatchSource);
    w.Put("videoOutput", s.videoOutput);
    w.Put("videoBitrateMbps", s.videoBitrateMbps);
    w.Put("videoKeepAudio", s.videoKeepAudio);
    w.Put("videoHardwareDecode", s.videoHardwareDecode);
    w.Put("spoutRotate", s.spoutRotate);
    w.Put("spoutFlipH", s.spoutFlipH);
    w.Put("spoutFlipV", s.spoutFlipV);
    w.Put("customResolution", s.customResolution);
    w.Put("customWidth", s.customWidth);
    w.Put("customHeight", s.customHeight);
    w.Put("keepAspect", s.keepAspect);
    w.Put("upscaleMode", s.upscaleMode);
    w.Put("nrEnabled", s.nrEnabled);
    w.Put("nrCaptureOnly", s.nrCaptureOnly);
    w.Put("nrRoute", s.nrRoute);
    w.Put("nrPreset", s.nrPreset);
    w.Put("nrStyle", s.nrStyle);
    w.Put("nrIntensity", s.nrIntensity);
    w.Put("nrGlobalTone", s.nrGlobalTone);
    w.Put("nrLocalTone", s.nrLocalTone);
    w.Put("nrLocalStructure", s.nrLocalStructure);
    w.Put("nrSkinStructure", s.nrSkinStructure);
    w.Put("nrAutoMask", s.nrAutoMask);
    w.Put("hdrPaperWhite", s.hdrPaperWhite);
    w.Put("hdrHighlightCompression", s.hdrHighlightCompression);
    w.Put("nrUiCorrection", s.nrUiCorrection);
    w.Put("nrInputExposure", s.nrInputExposure);
    w.Put("nrToneTransfer", s.nrToneTransfer);
    w.Put("nrColorStrength", s.nrColorStrength);
    w.Put("nrShadowGain", s.nrShadowGain);
    w.Put("nrHighlightGain", s.nrHighlightGain);
    w.Put("nrInputScale", s.nrInputScale);
    w.Put("nrScaleMode", s.nrScaleMode);
    w.Put("nrMaxLongEdge", s.nrMaxLongEdge);
    w.Put("motionMode", s.motionMode);
    w.Put("depthMode", s.depthMode);
    w.Put("searchRadius", s.searchRadius);
    w.Put("motionConfidence", s.motionConfidence);
    w.Put("nvofGrid", s.nvofGrid);
    w.Put("nvofPerf", s.nvofPerf);
    w.Put("nvofBidirectional", s.nvofBidirectional);
    w.Put("depthInterval", s.depthInterval);
    w.Put("depthLongSide", s.depthLongSide);
    w.Put("autoReset", s.autoReset);
    w.Put("cutThreshold", s.cutThreshold);
    w.Put("dlaaEnabled", s.dlaaEnabled);
    w.Put("dlaaPreset", s.dlaaPreset);
    w.Put("compareMode", s.compareMode);
    w.Put("wipePosition", s.wipePosition);
    w.Put("checkerboard", s.checkerboard);
    w.Put("fitMode", s.fitMode);
    w.Put("vsync", s.vsync);
    w.Put("processRateLimit", s.processRateLimit);
    w.Put("showOverlay", s.showOverlay);
    w.Put("keepAlpha", s.keepAlpha);
    w.Put("saveOriginal", s.saveOriginal);
    w.Put("timelapseSeconds", s.timelapseSeconds);
}
} // namespace

std::string Settings::ProcessingText(bool strengthsInPass) const {
    // Everything the processing passes read; not the display and blend values (composite only), not the interface.
    // The preset, style and strengths are parameters of the NGX feature; the FSR host route hands the picture to
    // DLSS-NR-on-AMD, which keeps its own, so there a change of them composites the existing result (strengths above 1)
    // without running the still through its passes again.
    Writer w;
    w.Put("sourceMode", sourceMode);
    w.Put("imagePath", imagePath);
    w.Put("videoPath", videoPath);
    w.Put("senderName", senderName);
    w.Put("videoHardwareDecode", videoHardwareDecode);
    w.Put("customResolution", customResolution);
    w.Put("customWidth", customWidth);
    w.Put("customHeight", customHeight);
    w.Put("keepAspect", keepAspect);
    w.Put("upscaleMode", upscaleMode);
    w.Put("hdrPaperWhite", hdrPaperWhite);
    w.Put("hdrHighlightCompression", hdrHighlightCompression);
    w.Put("nrEnabled", nrEnabled);
    w.Put("nrCaptureOnly", nrCaptureOnly);
    w.Put("nrRoute", nrRoute);
    w.Put("nrDllPath", nrDllPath);
    w.Put("nrRuntimeBuild", nrRuntimeBuild);
    if (strengthsInPass) {
        w.Put("nrPreset", nrPreset);
        w.Put("nrStyle", nrStyle);
        w.Put("nrIntensity", nrIntensity);
        w.Put("nrGlobalTone", nrGlobalTone);
        w.Put("nrLocalTone", nrLocalTone);
        w.Put("nrLocalStructure", nrLocalStructure);
        w.Put("nrSkinStructure", nrSkinStructure);
        w.Put("nrAutoMask", nrAutoMask);
        w.Put("nrUiCorrection", nrUiCorrection);
    }
    w.Put("nrInputExposure", nrInputExposure);
    w.Put("nrInputScale", nrInputScale);
    w.Put("nrScaleMode", nrScaleMode);
    w.Put("nrMaxLongEdge", nrMaxLongEdge);
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
    return w.out;
}

std::string Settings::ParameterText() const {
    Writer w;
    PutParameters(w, *this);
    return w.out;
}

std::string Settings::EffectText() const {
    Writer w;
    w.Put("nrStyle", nrStyle);
    w.Put("nrIntensity", nrIntensity);
    w.Put("nrGlobalTone", nrGlobalTone);
    w.Put("nrLocalTone", nrLocalTone);
    w.Put("nrLocalStructure", nrLocalStructure);
    w.Put("nrSkinStructure", nrSkinStructure);
    w.Put("nrAutoMask", nrAutoMask);
    w.Put("nrUiCorrection", nrUiCorrection);
    w.Put("nrInputExposure", nrInputExposure);
    w.Put("nrToneTransfer", nrToneTransfer);
    w.Put("nrColorStrength", nrColorStrength);
    w.Put("nrShadowGain", nrShadowGain);
    w.Put("nrHighlightGain", nrHighlightGain);
    w.Put("nrInputScale", nrInputScale);
    w.Put("nrScaleMode", nrScaleMode);
    w.Put("nrMaxLongEdge", nrMaxLongEdge);
    return w.out;
}

bool Settings::Save(const std::wstring& path) const {
    Writer w;
    w.out += "# VRChat DLSS5 Cam settings\n";
    w.Put("language", language);
    w.Put("senderName", senderName);
    w.Put("sourceMode", sourceMode);
    w.Put("spoutRotate", spoutRotate);
    w.Put("spoutFlipH", spoutFlipH);
    w.Put("spoutFlipV", spoutFlipV);
    w.Put("imagePath", imagePath);
    w.Put("videoPath", videoPath);
    w.Put("videoMatchSource", videoMatchSource);
    w.Put("videoOutput", videoOutput);
    w.Put("videoBitrateMbps", videoBitrateMbps);
    w.Put("videoKeepAudio", videoKeepAudio);
    w.Put("videoHardwareDecode", videoHardwareDecode);
    w.Put("customResolution", customResolution);
    w.Put("customWidth", customWidth);
    w.Put("customHeight", customHeight);
    w.Put("keepAspect", keepAspect);
    w.Put("upscaleMode", upscaleMode);
    w.Put("nrEnabled", nrEnabled);
    w.Put("nrCaptureOnly", nrCaptureOnly);
    w.Put("nrRoute", nrRoute);
    w.Put("nrDllPath", nrDllPath);
    w.Put("nrRuntimeBuild", nrRuntimeBuild);
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
    w.Put("nrInputExposure", nrInputExposure);
    w.Put("nrToneTransfer", nrToneTransfer);
    w.Put("nrColorStrength", nrColorStrength);
    w.Put("nrShadowGain", nrShadowGain);
    w.Put("nrHighlightGain", nrHighlightGain);
    w.Put("nrInputScale", nrInputScale);
    w.Put("nrScaleMode", nrScaleMode);
    w.Put("nrMaxLongEdge", nrMaxLongEdge);
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
    w.Put("processRateLimit", processRateLimit);
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
    w.Put("libraryVisible", libraryVisible);
    w.Put("sidebarWidth", sidebarWidth);
    w.Put("libraryHeight", libraryHeight);
    w.Put("toolRowX", toolRowX);
    w.Put("toolRowY", toolRowY);
    w.Put("toolRowDock", toolRowDock);
    w.Put("updateCheck", updateCheck);
    w.Put("updateChannel", updateChannel);
    w.Put("portConsent", portConsent);
    w.Put("portInstalledTag", portInstalledTag);
    w.Put("advancedControls", showAdvanced);
    w.Put("theme", theme);
    w.Put("showLog", showLog);
    w.Put("reopenLast", reopenLast);
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
    upscaleMode = std::clamp(upscaleMode, 0, 1);
    nrRoute = std::clamp(nrRoute, -1, 2);
    if (nrRuntimeBuild != "blackwell" && nrRuntimeBuild != "universal" && nrRuntimeBuild != "other" && nrRuntimeBuild != "exe") nrRuntimeBuild.clear();
    nrPreset = std::clamp(nrPreset, 0, 3);
    if (sidebarWidth != 0.0f) sidebarWidth = std::clamp(sidebarWidth, 16.0f, 48.0f);
    if (libraryHeight != 0.0f) libraryHeight = std::clamp(libraryHeight, 7.0f, 30.0f);
    toolRowX = toolRowX < 0.0f ? -1.0f : std::clamp(toolRowX, 0.0f, 1.0f);   // either one below zero = the default place
    toolRowY = toolRowY < 0.0f ? -1.0f : std::clamp(toolRowY, 0.0f, 1.0f);   // (each key is applied, and clamped, on its own)
    toolRowDock = std::clamp(toolRowDock, 0, 4);
    updateChannel = std::clamp(updateChannel, 0, 1);
    nrStyle = std::clamp(nrStyle, 0, 2);
    sourceMode = std::clamp(sourceMode, 0, 2);
    spoutRotate = std::clamp(spoutRotate, 0, 3);
    videoOutput = std::clamp(videoOutput, 0, 2);
    videoBitrateMbps = std::clamp(videoBitrateMbps, 5, 200);
    nrIntensity = std::clamp(nrIntensity, 0.0f, 2.0f);
    nrInputExposure = std::clamp(nrInputExposure, 0.25f, 4.0f);
    nrToneTransfer = std::clamp(nrToneTransfer, 0.0f, 2.0f);
    nrColorStrength = std::clamp(nrColorStrength, 0.0f, 2.0f);
    nrShadowGain = std::clamp(nrShadowGain, 0.0f, 2.0f);
    nrHighlightGain = std::clamp(nrHighlightGain, 0.0f, 2.0f);
    nrInputScale = std::clamp(nrInputScale, 25, 100);
    nrScaleMode = std::clamp(nrScaleMode, 0, 1);
    nrMaxLongEdge = std::clamp(nrMaxLongEdge, 256, 7680);
    processRateLimit = std::clamp(processRateLimit, 0, 240);
    nrGlobalTone = std::clamp(nrGlobalTone, 0.0f, 2.0f);
    nrLocalTone = std::clamp(nrLocalTone, 0.0f, 2.0f);
    nrLocalStructure = std::clamp(nrLocalStructure, 0.0f, 2.0f);
    nrSkinStructure = nrSkinStructure < 0.0f ? -1.0f : std::clamp(nrSkinStructure, 0.0f, 2.0f);
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
    theme = std::clamp(theme, 0, 2);
    timelapseSeconds = std::clamp(timelapseSeconds, 0, 3600);
    windowWidth = std::clamp(windowWidth, 800, 10000);
    windowHeight = std::clamp(windowHeight, 500, 10000);
    hotkeyModifiers &= 0x000F;
}

void Settings::CopyEffects(const Settings& from) {
    nrPreset = from.nrPreset; nrStyle = from.nrStyle;
    nrIntensity = from.nrIntensity; nrGlobalTone = from.nrGlobalTone; nrLocalTone = from.nrLocalTone;
    nrLocalStructure = from.nrLocalStructure; nrSkinStructure = from.nrSkinStructure;
    nrAutoMask = from.nrAutoMask; nrUiCorrection = from.nrUiCorrection;
    nrInputExposure = from.nrInputExposure; nrToneTransfer = from.nrToneTransfer; nrColorStrength = from.nrColorStrength;
    nrShadowGain = from.nrShadowGain; nrHighlightGain = from.nrHighlightGain; nrInputScale = from.nrInputScale;
    nrScaleMode = from.nrScaleMode; nrMaxLongEdge = from.nrMaxLongEdge;
}

} // namespace vdc
