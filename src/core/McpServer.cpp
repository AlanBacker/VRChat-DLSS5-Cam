#include "core/McpServer.h"
#include "core/Log.h"
#include "core/Util.h"
#define _CRT_RAND_S
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>

#ifndef APP_VERSION_STRING
#define APP_VERSION_STRING "0.0.0"
#endif

namespace vdc {

// --- McpCall --------------------------------------------------------------------------------

void McpCall::Text(const std::string& t) { content.Push(Json::Obj().Set("type", "text").Set("text", t)); }
void McpCall::Json_(const Json& j) { Text(j.Dump(2)); if (j.type == Json::Object) structured = j; }
void McpCall::Image(const std::vector<uint8_t>& bytes, const char* mime) {
    content.Push(Json::Obj().Set("type", "image").Set("data", McpServer::Base64(bytes.data(), bytes.size())).Set("mimeType", mime));
}
void McpCall::Fail(const std::string& t) { content = Json::Arr(); Text(t); isError = true; Finish(); }
void McpCall::Fail(const std::string& t, const Json& detail) {
    content = Json::Arr();
    Text(t);
    if (detail.type == Json::Object) { Text(detail.Dump(2)); structured = detail; }
    isError = true;
    Finish();
}
void McpCall::Finish() { { std::lock_guard<std::mutex> lock(m_mutex); m_done = true; } m_cv.notify_all(); }
bool McpCall::Wait(double seconds) {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_cv.wait_for(lock, std::chrono::milliseconds((long long)(seconds * 1000.0)), [this] { return m_done; });
}
bool McpCall::Done() const { std::lock_guard<std::mutex> lock(m_mutex); return m_done; }

// --- tables ---------------------------------------------------------------------------------

namespace {

const char* kInstructions =
    "VRChat DLSS5 Cam applies NVIDIA DLSS 5 neural rendering to a live VRChat camera stream (Spout), to pictures and to videos, "
    "and writes the results as PNG, MP4, GIF, APNG or WebP. This server is the running program's window: every tool call "
    "shows in the interface, enters the undo history and is saved like a change made by hand.\n"
    "Typical flow: get_status -> open (a picture or video; add several with library add) or open_live -> set_settings "
    "(describe_settings lists every key with its range and meaning; the look is mostly nrIntensity, nrStyle, nrGlobalTone, "
    "nrLocalTone, nrLocalStructure and the blend values nrToneTransfer, nrColorStrength, nrShadowGain, nrHighlightGain) -> "
    "wait for=converged -> preview (look at the result; compareMode 1 shows the original, 2 a wipe of both) -> capture "
    "(the open file, or a photo of the live stream) or process (the library) -> wait for=idle. Paths are Windows paths on "
    "the server's computer. Sizes are pixels, times are seconds, strengths are 0..2 with 1 as the runtime's own value.\n"
    "From another computer, or as a bot serving many people: send files to the queue with submit (an upload_id from upload "
    "or POST /upload, a url the server fetches, or small base64 data) and follow them with jobs. submit answers at once with "
    "the job's place in the queue and an estimate: tell the person to wait, then poll with jobs wait (a long poll of up to "
    "60 s) until the job is done, and fetch the results from the download links (the same key). Never wait for a job by "
    "repeating submit. A queue_full or too_many_jobs refusal says when to try again. Keys have roles: viewer (looks), jobs "
    "(the queue) and admin (everything); in read-only mode only the looking tools answer.";

const std::vector<McpTool> kTools = {
    { "get_status",
      "The state of the program: version and edition, the source (live stream, picture or video) and what is loaded, the neural "
      "pass and the guidance passes, the output size, a running batch or capture, the library, the undo history and the frame rates.",
      R"json({"type":"object","properties":{},"additionalProperties":false})json", false, false, false },
    { "describe_settings",
      "The settings reference: every key with its group, type, range or values and meaning. Call it before set_settings.",
      R"json({"type":"object","properties":{"group":{"type":"string","description":"Only this group: source, video, resolution, hdr, neural, blend, guidance, dlaa, display, capture, hotkey, window, updates, mcp"},"key":{"type":"string","description":"Only this key"},"keys":{"type":"array","items":{"type":"string"},"description":"Only these keys"}},"additionalProperties":false})json", false, false, false },
    { "get_settings",
      "The current values of the settings (all of them, or the keys asked for).",
      R"json({"type":"object","properties":{"keys":{"type":"array","items":{"type":"string"},"description":"Only these keys; all when omitted"}},"additionalProperties":false})json", false, false, false },
    { "set_settings",
      "Changes settings, as the sidebar does: the values are clamped to their ranges, applied at once, recorded as an undo step "
      "and saved. Answers with the effective values and the keys it did not know.",
      R"json({"type":"object","properties":{"settings":{"type":"object","description":"Keys and their new values, for example {\"nrIntensity\": 1.5, \"nrStyle\": 2, \"compareMode\": 0}","additionalProperties":true}},"required":["settings"],"additionalProperties":false})json", true, false, false },
    { "reset_settings",
      "Every setting back to its default (the window placement, language and the opened file stay), like the Reset button.",
      R"json({"type":"object","properties":{},"additionalProperties":false})json", true, false, false },
    { "open",
      "Opens a picture or a video in the preview and adds it to the library; the source switches to that file. Animated GIF, APNG "
      "and WebP count as videos. By default it returns once the file is shown.",
      R"json({"type":"object","properties":{"path":{"type":"string","description":"Full Windows path of a picture (PNG, JPEG, WebP, BMP, TIFF, ...) or a video (MP4, MOV, MKV, AVI, GIF, APNG, WebP, ...)"},"wait":{"type":"boolean","default":true,"description":"Return once the file is decoded and shown"},"timeout":{"type":"number","default":30}},"required":["path"],"additionalProperties":false})json", true, false, false },
    { "open_live",
      "Switches to the live source: the VRChat camera picture arriving through Spout. Optionally selects a sender by name.",
      R"json({"type":"object","properties":{"sender":{"type":"string","description":"Spout sender name; empty = automatic (prefers VRCSender1)"}},"additionalProperties":false})json", true, false, false },
    { "list_senders",
      "The Spout senders on this computer (VRChat's camera among them when it streams) and the one in use.",
      R"json({"type":"object","properties":{},"additionalProperties":false})json", false, false, false },
    { "close_media",
      "Closes the opened picture or video: processing stops and the preview empties (the file stays in the library).",
      R"json({"type":"object","properties":{},"additionalProperties":false})json", true, false, false },
    { "library",
      "The media library under the preview: list its items (with ids), add files or folders, remove or select items, show one in "
      "the preview, set a video's processing range, or give an item effect values of its own.",
      R"json({"type":"object","properties":{"action":{"type":"string","enum":["list","add","remove","clear","select","show","set_range","set_own","clear_own"]},"paths":{"type":"array","items":{"type":"string"},"description":"add: files or folders"},"ids":{"type":"array","items":{"type":"integer"},"description":"remove, select, clear_own: item ids from list"},"id":{"type":"integer","description":"show, set_range, set_own: one item id"},"exclusive":{"type":"boolean","default":true,"description":"select: the other items are deselected"},"in":{"type":"number","description":"set_range: start in seconds (0 = the beginning)"},"out":{"type":"number","description":"set_range: end in seconds (0 = the end)"},"settings":{"type":"object","description":"set_own: the item's own effect values (keys of the neural and blend groups)","additionalProperties":true}},"required":["action"],"additionalProperties":false})json", true, false, false },
    { "process",
      "Processes the library (every item, the selected ones, or the given ids) with the current settings into the capture folder, as "
      "the Process buttons do. Returns at once, or when the run has finished with wait.",
      R"json({"type":"object","properties":{"scope":{"type":"string","enum":["all","selected","ids"],"default":"all"},"ids":{"type":"array","items":{"type":"integer"}},"wait":{"type":"boolean","default":false,"description":"Return when the run has finished (else at once; get_status and wait follow it)"},"timeout":{"type":"number","default":600}},"additionalProperties":false})json", true, false, false },
    { "capture",
      "The capture button (Ctrl+Alt+P): a photo of the live stream, the processed picture written as PNG, or the opened video "
      "processed to a file. Returns the file written when wait is on.",
      R"json({"type":"object","properties":{"wait":{"type":"boolean","default":true},"timeout":{"type":"number","default":180}},"additionalProperties":false})json", true, false, false },
    { "cancel",
      "Stops the running batch or video run.",
      R"json({"type":"object","properties":{},"additionalProperties":false})json", true, false, false },
    { "video",
      "The video controls under the preview: play, pause, seek, step frames, set the processing range (in, out), or read the state.",
      R"json({"type":"object","properties":{"action":{"type":"string","enum":["info","play","pause","toggle","seek","step","set_in","set_out","clear_range"]},"seconds":{"type":"number","description":"seek: the time to show; set_in, set_out: the range point (the current position when omitted)"},"frames":{"type":"integer","default":1,"description":"step: frames forward (negative = back)"}},"required":["action"],"additionalProperties":false})json", true, false, false },
    { "preview",
      "A picture of the preview as it is right now, scaled down, as an image. view=output gives the picture area with the compare "
      "mode applied (compareMode 0 output, 1 original, 2 wipe with the original on the left, 3 motion vectors, 4 depth); "
      "view=window gives the whole program window with its interface. Wait for=converged first on a fresh still picture.",
      R"json({"type":"object","properties":{"view":{"type":"string","enum":["output","window"],"default":"output"},"max_edge":{"type":"integer","default":1024,"minimum":64,"maximum":4096,"description":"The picture is scaled down to this long edge"},"save_to":{"type":"string","description":"Also write the full-size picture to this PNG path"}},"additionalProperties":false})json", false, false, false },
    { "wait",
      "Waits until the program reaches a state: idle (no processing, capture or batch runs), loaded (the opened file is decoded), "
      "converged (a still picture's passes have settled, so a preview shows the final look), display (a processed frame is on screen).",
      R"json({"type":"object","properties":{"for":{"type":"string","enum":["idle","loaded","converged","display"],"default":"idle"},"timeout":{"type":"number","default":60}},"additionalProperties":false})json", false, false, false },
    { "undo", "Undoes the last change (Ctrl+Z), or several.",
      R"json({"type":"object","properties":{"steps":{"type":"integer","default":1,"minimum":1}},"additionalProperties":false})json", true, false, false },
    { "redo", "Redoes an undone change (Ctrl+Y), or several.",
      R"json({"type":"object","properties":{"steps":{"type":"integer","default":1,"minimum":1}},"additionalProperties":false})json", true, false, false },
    { "history", "The undo history: every recorded state with a label, and which one is current; with index, the program goes to that state (the history panel's click).",
      R"json({"type":"object","properties":{"index":{"type":"integer","minimum":0,"description":"Go to this state of the list, 0 = the oldest"}},"additionalProperties":false})json", true, false, false },
    { "presets",
      "The user's presets of effect values (the preset row of the sidebar): list them with their values, apply one, save the "
      "current effect values under a name, delete or rename one.",
      R"json({"type":"object","properties":{"action":{"type":"string","enum":["list","apply","save","delete","rename"]},"name":{"type":"string"},"new_name":{"type":"string","description":"rename: the new name"}},"required":["action"],"additionalProperties":false})json", true, false, false },
    { "get_log",
      "The last lines of the program's log (what the runtime, the passes, the files and the errors reported).",
      R"json({"type":"object","properties":{"lines":{"type":"integer","default":50,"minimum":1,"maximum":500},"level":{"type":"string","enum":["info","warn","error"],"description":"Only entries of this level and above"},"contains":{"type":"string","description":"Only lines with this text"}},"additionalProperties":false})json", false, false, false },
    { "window",
      "The program window: bring it up, restore or minimize it, enter or leave the fullscreen view, resize it, show or hide the "
      "sidebar and the library.",
      R"json({"type":"object","properties":{"action":{"type":"string","enum":["show","restore","minimize","fullscreen","windowed","resize","sidebar","library"]},"width":{"type":"integer","description":"resize: client width"},"height":{"type":"integer","description":"resize: client height"},"visible":{"type":"boolean","description":"sidebar, library: shown or hidden"}},"required":["action"],"additionalProperties":false})json", true, false, false },
    { "reload",
      "Loads the neural runtime again, restarts the depth estimator, refreshes the Spout sender list, or resets the temporal history "
      "of the passes.",
      R"json({"type":"object","properties":{"what":{"type":"string","enum":["runtime","depth","senders","history"]}},"required":["what"],"additionalProperties":false})json", true, false, false },
    { "submit",
      "Sends a file to the processing queue and answers at once with the job id, its place in the queue and a time estimate. "
      "The file: upload_id (from upload or POST /upload), url (a picture or video the server fetches), data (base64, up to 16 MB) "
      "with name, or path (a file on the server's own disk: admin only). The look: preset (the name of a saved preset) and "
      "settings (an object of overrides: the neural, blend, resolution, video output and guidance keys of describe_settings; "
      "what is not given comes from the server's current settings). Videos: in and out (seconds) limit the range. label and "
      "client_ref (a chat's reference) come back with the job. wait: true waits up to timeout seconds (60 at most) and answers "
      "with the state reached then. The answer's message is meant to be relayed to the person waiting.",
      R"json({"type":"object","properties":{"upload_id":{"type":"string"},"url":{"type":"string"},"data":{"type":"string","description":"The file's bytes as base64"},"name":{"type":"string","description":"The file name of data or url (its extension says the format)"},"path":{"type":"string","description":"A file on the server's computer (admin only)"},"preset":{"type":"string"},"settings":{"type":"object","additionalProperties":true},"in":{"type":"number"},"out":{"type":"number"},"label":{"type":"string"},"client_ref":{"type":"string"},"wait":{"type":"boolean","default":false},"timeout":{"type":"number","default":30,"maximum":60}},"additionalProperties":false})json", true, true, false },
    { "upload",
      "Stores a file for submit: base64 data, up to 16 MB in one call. Larger files go by HTTP: POST /upload?name=<file name> "
      "with the raw bytes as the body and the same Authorization header, which answers the same JSON. An upload that no job "
      "uses is deleted after two hours.",
      R"json({"type":"object","properties":{"name":{"type":"string"},"data":{"type":"string","description":"The file's bytes as base64"}},"required":["name","data"],"additionalProperties":false})json", true, true, false },
    { "jobs",
      "The processing queue. list: your jobs and the queue's state (admin: everyone's). get: one job with its place in the "
      "queue, estimate, progress, results and download links. wait: a long poll that answers when the job ends or after "
      "timeout seconds (60 at most), with the state either way and never an error. result: like get, and inline: true adds a "
      "picture result as an image (up to 8 MB). cancel: a waiting or running job of yours. delete: the job and its files. "
      "Results are kept for the server's retention time; every answer says until when.",
      R"json({"type":"object","properties":{"action":{"type":"string","enum":["list","get","wait","result","cancel","delete"]},"id":{"type":"string","description":"The job id (get, wait, result, cancel, delete)"},"timeout":{"type":"number","default":30,"maximum":60},"inline":{"type":"boolean","default":false},"max_edge":{"type":"integer","description":"inline: scale a picture result down to this many pixels on its long edge (64..4096)"}},"required":["action"],"additionalProperties":false})json", true, true, false },
    { "_upload", "", R"json({"type":"object"})json", true, true, true },
    { "_download", "", R"json({"type":"object"})json", false, true, true },
};

const std::vector<McpSettingInfo> kSettings = {
    { "language", "source", "enum", "0 = automatic, 1 = English, 2 = Chinese, 3 = Japanese, 4 = Korean", "Interface language." },
    { "sourceMode", "source", "enum", "0 = live VRChat camera (Spout), 1 = picture file, 2 = video file", "What the program processes. The open and open_live tools set it together with the file." },
    { "senderName", "source", "string", "", "Spout sender to receive; empty = automatic (prefers VRCSender1)." },
    { "spoutRotate", "source", "int", "0..3", "Live source: quarter turns clockwise applied to the camera picture." },
    { "spoutFlipH", "source", "bool", "", "Live source: mirrored left-right." },
    { "spoutFlipV", "source", "bool", "", "Live source: mirrored top-bottom." },
    { "imagePath", "source", "string", "", "The opened picture (set by the open tool)." },
    { "videoPath", "source", "string", "", "The opened video (set by the open tool)." },
    { "videoMatchSource", "video", "bool", "", "The output follows the opened file's codec, frame rate and bitrate; a GIF, APNG or WebP comes out in its own format. Off: videoOutput and videoBitrateMbps apply." },
    { "videoOutput", "video", "enum", "0 = MP4 H.264, 1 = MP4 HEVC, 2 = PNG sequence, 3 = GIF, 4 = APNG, 5 = WebP", "Output format of a processed video when videoMatchSource is off." },
    { "videoBitrateMbps", "video", "int", "5..200", "MP4 video bitrate when videoMatchSource is off." },
    { "videoKeepAudio", "video", "bool", "", "Copy the sound track into the MP4 (AAC)." },
    { "webpQuality", "video", "int", "50..100", "Animated WebP output quality; 100 = lossless." },
    { "videoHardwareDecode", "video", "bool", "", "Decode with the GPU when the driver offers it." },
    { "customResolution", "resolution", "bool", "", "Output at customWidth x customHeight instead of the source size." },
    { "customWidth", "resolution", "int", "256..7680", "Output width when customResolution is on." },
    { "customHeight", "resolution", "int", "256..4320", "Output height when customResolution is on." },
    { "keepAspect", "resolution", "bool", "", "Keep the source's aspect ratio within the custom size." },
    { "upscaleMode", "resolution", "enum", "0 = DLSS super resolution, 1 = resampling", "How an output larger than the source is made." },
    { "hdrPaperWhite", "hdr", "float", "0.1..8", "HDR source: scene value mapped to display white." },
    { "hdrHighlightCompression", "hdr", "float", "0..1", "HDR source: 0 = hard clip above white, 1 = full soft roll-off." },
    { "nrEnabled", "neural", "bool", "", "The DLSS 5 neural rendering pass." },
    { "nrCaptureOnly", "neural", "bool", "", "Live source: the neural pass idles for the preview and runs for a burst before each capture." },
    { "nrPreset", "neural", "int", "0..3", "The runtime's preset." },
    { "nrStyle", "neural", "enum", "0 = default, 1 = natural, 2 = cinematic", "The look of the neural pass." },
    { "nrIntensity", "neural", "float", "0..2", "Overall strength: up to 1 goes to the runtime, above 1 the composite amplifies the change." },
    { "nrGlobalTone", "neural", "float", "0..2", "Strength of the global tone change." },
    { "nrLocalTone", "neural", "float", "0..2", "Strength of the local tone change." },
    { "nrLocalStructure", "neural", "float", "0..2", "Strength of the local structure (detail) change." },
    { "nrSkinStructure", "neural", "float", "-1 or 0..2", "Strength on skin; -1 = the runtime's default." },
    { "nrAutoMask", "neural", "bool", "", "The runtime's automatic mask." },
    { "nrUiCorrection", "neural", "bool", "", "The runtime's correction for interface elements." },
    { "nrDllPath", "neural", "string", "", "Path of nvngx_dlssnr.dll; empty = the bundled build for the adapter." },
    { "nrRuntimeBuild", "neural", "enum", "empty, blackwell, universal, other, exe", "The runtime build in use after a fallback; empty = the one for the adapter." },
    { "nrInputExposure", "blend", "float", "0.25..4", "Gain on the picture the network sees, undone afterwards." },
    { "nrToneTransfer", "blend", "float", "0..2", "Share of the neural pass's brightness change that reaches the output." },
    { "nrColorStrength", "blend", "float", "0..2", "Share of the neural pass's colour change that reaches the output." },
    { "nrShadowGain", "blend", "float", "0..2", "How much of the neural pass's darkening reaches the output." },
    { "nrHighlightGain", "blend", "float", "0..2", "How much of the neural pass's brightening reaches the output." },
    { "nrScaleMode", "blend", "enum", "0 = percentage (nrInputScale), 1 = maximum resolution (nrMaxLongEdge)", "How the neural pass resolution is chosen." },
    { "nrInputScale", "blend", "int", "25..100", "Neural pass resolution as a percentage of the input." },
    { "nrMaxLongEdge", "blend", "int", "256..7680", "Cap on the neural pass's long edge in the maximum-resolution mode." },
    { "nrPassBudgetMp", "blend", "int", "0..1000", "Starting pixel budget of the neural pass in megapixels; 0 = the built-in limit." },
    { "motionMode", "guidance", "enum", "0 = none, 1 = block matching, 2 = NVIDIA hardware optical flow (GeForce), 3 = FSR optical flow (this program's own, any card)", "Source of the motion vectors." },
    { "depthMode", "guidance", "enum", "0 = flat, 1 = gradient, 2 = zero, 3 = estimated by the depth network", "Source of the depth." },
    { "searchRadius", "guidance", "int", "2..12", "Block matching: search radius at quarter resolution." },
    { "motionConfidence", "guidance", "float", "0..1", "Vectors with a lower matching confidence are damped." },
    { "nvofGrid", "guidance", "enum", "4, 2, 1", "Hardware optical flow: source pixels between vectors." },
    { "nvofPerf", "guidance", "enum", "5 = slow, 10 = medium, 20 = fast", "Hardware optical flow: performance level." },
    { "nvofBidirectional", "guidance", "bool", "", "Hardware optical flow: forward/backward consistency check." },
    { "flowBidirectional", "guidance", "bool", "", "FSR optical flow: the same check (a second search at the finest level)." },
    { "depthInterval", "guidance", "int", "1..10", "Run the depth network every N processed frames." },
    { "depthLongSide", "guidance", "enum", "252, 336, 420, 518", "Depth network resolution (long side)." },
    { "depthModelPath", "guidance", "string", "", "Depth network file; empty = models\\depth_anything_v2_small_fp16.onnx next to the program." },
    { "autoReset", "guidance", "bool", "", "Reset the temporal history on detected scene cuts." },
    { "cutThreshold", "guidance", "float", "0.01..0.5", "Matching-cost jump that counts as a scene cut." },
    { "dlaaEnabled", "dlaa", "bool", "", "DLSS super resolution at native resolution (DLAA) before the neural pass; GeForce edition." },
    { "dlaaPreset", "dlaa", "int", "0..15", "0 = default, 1..15 = presets A..O." },
    { "compareMode", "display", "enum", "0 = output, 1 = original, 2 = wipe, 3 = motion vectors, 4 = depth", "What the preview shows." },
    { "wipePosition", "display", "float", "0..1", "Position of the wipe line." },
    { "checkerboard", "display", "bool", "", "Checkerboard behind transparent areas." },
    { "fitMode", "display", "enum", "0 = fit the window, 1 = one picture pixel per screen pixel", "Preview magnification." },
    { "vsync", "display", "bool", "", "The interface waits for the display's refresh." },
    { "processRateLimit", "display", "int", "0..240", "Live source: at most this many processed frames per second; 0 = every source frame." },
    { "showOverlay", "display", "bool", "", "The status overlay on the preview." },
    { "captureFolder", "capture", "string", "", "Where captures and processed files go; empty = Pictures\\VRChat DLSS5 Cam." },
    { "captureName", "capture", "string", "", "File-name template of live captures: tokens {name} {date} {time} {size} {width} {height} {insize} {inwidth} {inheight}; empty = VRChat_DLSS5_{date}_{time}_{size}." },
    { "outputName", "capture", "string", "", "File-name template of processed pictures and videos; empty = {name}_DLSS5_{size}." },
    { "keepAlpha", "capture", "bool", "", "Store the alpha channel of the camera picture." },
    { "saveOriginal", "capture", "bool", "", "Also save the unprocessed picture." },
    { "timelapseSeconds", "capture", "int", "0..3600", "Live source: a photo every N seconds; 0 = off." },
    { "hotkeyEnabled", "hotkey", "bool", "", "The global capture hotkey." },
    { "hotkeyModifiers", "hotkey", "int", "bit mask: 1 = Alt, 2 = Ctrl, 4 = Shift, 8 = Win", "Modifiers of the capture hotkey (3 = Ctrl+Alt)." },
    { "hotkeyKey", "hotkey", "int", "virtual-key code", "Key of the capture hotkey (80 = P)." },
    { "windowX", "window", "int", "", "Window position; -1 = centred." },
    { "windowY", "window", "int", "", "Window position; -1 = centred." },
    { "windowWidth", "window", "int", "800..10000", "Window size." },
    { "windowHeight", "window", "int", "500..10000", "Window size." },
    { "windowMaximized", "window", "bool", "", "The window is maximized." },
    { "sidebarVisible", "window", "bool", "", "The settings sidebar is shown." },
    { "libraryVisible", "window", "bool", "", "The media library strip is shown." },
    { "sidebarWidth", "window", "float", "0 or 16..48", "Sidebar width in font-size units; 0 = default." },
    { "libraryHeight", "window", "float", "0 or 7..30", "Library height in font-size units; 0 = default." },
    { "toolRowX", "window", "float", "-1 or 0..1", "Position of the turn/mirror/crop tool row; -1 = bottom centre." },
    { "toolRowY", "window", "float", "-1 or 0..1", "Position of the tool row." },
    { "toolRowDock", "window", "enum", "0 = shown, 1 = left, 2 = right, 3 = top, 4 = bottom", "The tool row tucked away at an edge." },
    { "theme", "window", "enum", "0 = follow Windows, 1 = dark, 2 = light", "Colour theme." },
    { "advancedControls", "window", "bool", "", "The expert sections of the sidebar are shown." },
    { "showLog", "window", "bool", "", "The log window is open." },
    { "reopenLast", "window", "bool", "", "Open the previous session's file again at start." },
    { "updateCheck", "updates", "bool", "", "Look for a new version at every start." },
    { "updateChannel", "updates", "enum", "0 = stable releases, 1 = pre-releases too", "Update channel." },
    { "githubMirror", "updates", "enum", "0 = GitHub directly, 1 = the fastest built-in mirror site, 2 = githubMirrorCustom", "How GitHub is reached." },
    { "githubMirrorCustom", "updates", "string", "", "The user's own mirror site (https://host)." },
    { "mcpEnabled", "mcp", "bool", "", "This MCP server runs whenever the program does." },
    { "mcpPort", "mcp", "int", "1024..65535", "TCP port of the server on 127.0.0.1." },
    { "mcpReadOnly", "mcp", "bool", "", "The assistant may look but not change anything." },
    { "mcpToken", "mcp", "string", "", "When set, every request must carry it as a bearer token." },
    { "mcpBind", "mcp", "enum", "0 = this computer only, 1 = the local network", "Where the server listens; other computers need a key." },
    { "mcpLocalNoKey", "mcp", "bool", "", "A client on this computer needs no key." },
    { "mcpKeepHours", "mcp", "int", "1..720", "Hours a job's files (input and results) are kept after it ended." },
    { "mcpQueueMax", "mcp", "int", "1..500", "Jobs waiting at most, all clients together." },
    { "mcpQueuePerKey", "mcp", "int", "1..100", "Jobs one key may have waiting." },
    { "mcpUploadMaxMb", "mcp", "int", "1..65536", "The largest upload, in MB." },
    { "mcpJobFolder", "mcp", "string", "", "Where uploads and the jobs' files live; empty = <settings folder>\\mcp." },
};


std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}
bool LocalOrigin(const std::string& origin) {
    if (origin.empty()) return true;
    std::string o = Lower(origin);
    const size_t p = o.find("://");
    if (p != std::string::npos) o = o.substr(p + 3);
    const size_t slash = o.find('/');
    if (slash != std::string::npos) o = o.substr(0, slash);
    const size_t colon = o.rfind(':');
    if (colon != std::string::npos && o.find(']') == std::string::npos) o = o.substr(0, colon);
    if (!o.empty() && o.back() == ']') { const size_t c = o.rfind(':'); if (c != std::string::npos) o = o.substr(0, c); }
    return o == "localhost" || o == "127.0.0.1" || o == "[::1]" || o == "::1" || o == "null";
}
std::string UrlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else if (s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}
std::string QueryValue(const std::string& query, const char* key) {
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        const std::string part = query.substr(pos, amp - pos);
        const size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == key) return UrlDecode(part.substr(eq + 1));
        pos = amp + 1;
    }
    return std::string();
}
std::string HtmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '<') out += "&lt;"; else if (c == '>') out += "&gt;"; else if (c == '&') out += "&amp;"; else if (c == '"') out += "&quot;"; else out += c;
    }
    return out;
}
Json RpcError(const Json& id, int code, const std::string& message) {
    Json e = Json::Obj().Set("jsonrpc", "2.0").Set("id", id).Set("error", Json::Obj().Set("code", code).Set("message", message));
    return e;
}
Json RpcResult(const Json& id, Json result) { return Json::Obj().Set("jsonrpc", "2.0").Set("id", id).Set("result", std::move(result)); }

std::string ReasonPhrase(int status) {
    switch (status) {
    case 200: return "OK"; case 202: return "Accepted"; case 204: return "No Content"; case 400: return "Bad Request";
    case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable"; case 413: return "Payload Too Large"; case 503: return "Service Unavailable";
    default: return "Error";
    }
}

bool SendAll(SOCKET s, const char* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        const int n = send(s, data + sent, (int)std::min<size_t>(size - sent, 1 << 16), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

// A plain HTTP exchange with a server: this computer's (the bridge, the wake-up check) or one elsewhere (--mcp-url).
bool HttpRequest(const std::string& host, int port, const std::string& method, const std::string& path, const std::string& body,
                 const std::string& token, int& status, std::string& response, double timeoutSeconds) {
    status = 0;
    response.clear();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* list = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list) != 0 || !list) return false;
    SOCKET s = INVALID_SOCKET;
    const DWORD to = (DWORD)(timeoutSeconds * 1000.0);
    for (addrinfo* a = list; a && s == INVALID_SOCKET; a = a->ai_next) {
        SOCKET c = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (c == INVALID_SOCKET) continue;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));
        if (connect(c, a->ai_addr, (int)a->ai_addrlen) == 0) s = c; else closesocket(c);
    }
    freeaddrinfo(list);
    if (s == INVALID_SOCKET) return false;
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\nConnection: close\r\nAccept: application/json\r\n";
    if (!token.empty()) req += "Authorization: Bearer " + token + "\r\n";
    if (!body.empty()) req += "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    if (!SendAll(s, req.data(), req.size())) { closesocket(s); return false; }
    std::string all;
    char buf[8192];
    for (;;) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        all.append(buf, (size_t)n);
        const size_t hdrEnd = all.find("\r\n\r\n");
        if (hdrEnd != std::string::npos) {
            // Stop once the announced body is in (the server closes the connection anyway).
            const std::string head = Lower(all.substr(0, hdrEnd));
            const size_t cl = head.find("content-length:");
            if (cl != std::string::npos) {
                const size_t want = (size_t)atoll(head.c_str() + cl + 15);
                if (all.size() - hdrEnd - 4 >= want) break;
            }
        }
    }
    closesocket(s);
    const size_t hdrEnd = all.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return false;
    if (all.size() > 9) status = atoi(all.c_str() + 9);
    response = all.substr(hdrEnd + 4);
    return status > 0;
}

std::string ContentTypeOf(const std::string& name) {
    std::string ext = Lower(name);
    const size_t dot = ext.find_last_of('.');
    ext = dot == std::string::npos ? std::string() : ext.substr(dot);
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif") return "image/gif";
    if (ext == ".apng") return "image/apng";
    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".mkv") return "video/x-matroska";
    if (ext == ".webm") return "video/webm";
    if (ext == ".txt" || ext == ".log") return "text/plain; charset=utf-8";
    if (ext == ".json") return "application/json";
    return "application/octet-stream";
}

} // namespace

const std::vector<McpTool>& McpServer::Tools() { return kTools; }
const std::vector<McpSettingInfo>& McpServer::SettingInfos() { return kSettings; }
const McpSettingInfo* McpServer::FindSetting(const std::string& key) {
    for (const McpSettingInfo& s : kSettings) if (key == s.key) return &s;
    return nullptr;
}
std::string McpServer::Url(int port) { return "http://127.0.0.1:" + std::to_string(port) + "/mcp"; }
std::string McpServer::Url(const std::string& host, int port) { return "http://" + host + ":" + std::to_string(port) + "/mcp"; }
const wchar_t* McpServer::WindowClass() { return L"VRChatDLSS5CamWindow"; }
const char* McpServer::RoleName(int role) { return role == McpRoleAdmin ? "admin" : role == McpRoleJobs ? "jobs" : "viewer"; }
int McpServer::RoleFromName(const std::string& name) {
    const std::string n = Lower(Trim(name));
    if (n == "admin" || n == "full") return McpRoleAdmin;
    if (n == "jobs" || n == "job" || n == "bot") return McpRoleJobs;
    if (n == "viewer" || n == "view" || n == "read" || n == "readonly" || n == "read-only") return McpRoleViewer;
    return -1;
}

std::string McpServer::ClientConfig(const std::wstring& exePath) {
    Json server = Json::Obj().Set("command", WideToUtf8(exePath)).Set("args", Json::Arr().Push("--mcp"));
    return Json::Obj().Set("mcpServers", Json::Obj().Set("vrchat-dlss5-cam", server)).Dump(2);
}

std::string McpServer::ClientConfigRemote(const std::string& url, const std::string& key) {
    Json server = Json::Obj().Set("type", "http").Set("url", url);
    if (!key.empty()) server.Set("headers", Json::Obj().Set("Authorization", "Bearer " + key));
    return Json::Obj().Set("mcpServers", Json::Obj().Set("vrchat-dlss5-cam", server)).Dump(2);
}

std::vector<std::string> McpServer::LocalAddresses() {
    std::vector<std::string> out;
    WSADATA wsa{};
    const bool started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    char name[256] = {};
    if (gethostname(name, sizeof(name)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* list = nullptr;
        if (getaddrinfo(name, nullptr, &hints, &list) == 0) {
            for (addrinfo* a = list; a; a = a->ai_next) {
                char text[64] = {};
                const sockaddr_in* in = (const sockaddr_in*)a->ai_addr;
                if (!inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text))) continue;
                const std::string s = text;
                if (s.rfind("127.", 0) == 0 || s.rfind("169.254.", 0) == 0) continue;
                if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
            }
            freeaddrinfo(list);
        }
    }
    if (started) WSACleanup();
    return out;
}

std::string McpServer::Base64(const uint8_t* data, size_t size) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < size; i += 3) {
        const unsigned v = (unsigned)data[i] << 16 | (unsigned)data[i + 1] << 8 | data[i + 2];
        out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
    }
    if (i < size) {
        unsigned v = (unsigned)data[i] << 16;
        if (i + 1 < size) v |= (unsigned)data[i + 1] << 8;
        out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63];
        out += i + 1 < size ? k[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool McpServer::Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(text.size() / 4 * 3);
    unsigned acc = 0;
    int bits = 0;
    for (char ch : text) {
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A'; else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26; else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '+' || ch == '-') v = 62; else if (ch == '/' || ch == '_') v = 63;
        else if (ch == '=' || ch == '\r' || ch == '\n' || ch == ' ') continue;
        else return false;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xFF)); }
    }
    return true;
}

std::string McpServer::NewSecret() {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string out = "vdc_";
    for (int i = 0; i < 32; ++i) {
        unsigned r = 0;
        { static thread_local std::random_device rd; r = rd(); }
        out += k[r % 62];
    }
    return out;
}

bool McpServer::LoadKeys(const std::wstring& file, std::vector<McpKey>& out) {
    out.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || !f) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    Json j;
    if (!JsonReader::Parse(text, j)) return false;
    const Json* keys = j.Find("keys");
    if (!keys || keys->type != Json::Array) return false;
    for (const Json& e : keys->arr) {
        McpKey k;
        k.name = e.Str("name");
        k.secret = e.Str("key");
        const int role = RoleFromName(e.Str("role", "jobs"));
        k.role = role < 0 ? McpRoleJobs : role;
        k.created = (long long)e.Num("created");
        k.lastUsed = (long long)e.Num("lastUsed");
        k.calls = (unsigned)e.Num("calls");
        if (!k.secret.empty()) out.push_back(k);
    }
    return true;
}

bool McpServer::SaveKeys(const std::wstring& file, const std::vector<McpKey>& keys) {
    Json list = Json::Arr();
    for (const McpKey& k : keys)
        list.Push(Json::Obj().Set("name", k.name).Set("key", k.secret).Set("role", RoleName(k.role)).Set("created", k.created)
                  .Set("lastUsed", k.lastUsed).Set("calls", k.calls));
    const std::string text = Json::Obj().Set("keys", list).Dump(2) + "\n";
    const std::wstring tmp = file + L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// --- server ---------------------------------------------------------------------------------

McpServer::~McpServer() { Stop(); }

void McpServer::SetAccess(const Access& access) { std::lock_guard<std::mutex> lock(m_mutex); m_access = access; }

void McpServer::SetUploads(const std::wstring& dir, uint64_t maxBytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_uploadDir = dir;
    m_uploadMax = maxBytes;
    if (!dir.empty()) CreateDirectories(dir);
}

McpServer::Status McpServer::Get() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Status s;
    s.running = m_running.load();
    s.port = m_port;
    s.bind = m_bind;
    s.error = m_error;
    s.calls = m_calls;
    s.lastTool = m_lastTool;
    s.lastKey = m_lastKey;
    s.lastTime = m_lastTime;
    s.connections = m_live.load();
    s.pending = m_pending.load();
    return s;
}

std::vector<McpServer::KeyUse> McpServer::TakeKeyUse() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<KeyUse> out;
    for (auto& kv : m_keyUse) out.push_back(kv.second);
    m_keyUse.clear();
    return out;
}

void McpServer::NoteCall(const std::string& tool, const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_calls;
    m_lastTool = tool;
    m_lastKey = key;
    m_lastTime = NowSeconds();
    KeyUse& u = m_keyUse[key];
    u.name = key;
    u.lastUsed = (long long)time(nullptr);
    ++u.calls;
}

bool McpServer::Start(int port, int bind, Dispatch dispatch) {
    Stop();
    m_dispatch = std::move(dispatch);
    m_port = port;
    m_bind = bind;
    m_quit.store(false);
    { std::lock_guard<std::mutex> lock(m_mutex); m_error.clear(); }
    WSADATA wsa{};
    if (!m_wsa) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::lock_guard<std::mutex> lock(m_mutex); m_error = "WSAStartup failed"; return false; }
        m_wsa = true;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { std::lock_guard<std::mutex> lock(m_mutex); m_error = "socket: " + std::to_string(WSAGetLastError()); return false; }
    const BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = htonl(bind == 1 ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 64) != 0) {
        const int err = WSAGetLastError();
        closesocket(s);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = err == WSAEADDRINUSE ? "port " + std::to_string(port) + " is in use" : "bind: " + std::to_string(err);
        Log::Warn("MCP: %s", m_error.c_str());
        return false;
    }
    m_listen = (uintptr_t)s;
    m_running.store(true);
    m_thread = std::thread([this] { ListenMain(); });
    Log::Info("MCP: listening at %s (%s)", Url(port).c_str(), bind == 1 ? "this computer and the local network" : "this computer only");
    return true;
}

void McpServer::Stop() {
    if (!m_running.load() && !m_thread.joinable()) return;
    m_quit.store(true);
    if (m_listen != ~(uintptr_t)0) { closesocket((SOCKET)m_listen); m_listen = ~(uintptr_t)0; }
    if (m_thread.joinable()) m_thread.join();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (uintptr_t cs : m_clientSockets) { shutdown((SOCKET)cs, SD_BOTH); closesocket((SOCKET)cs); }
        m_clientSockets.clear();
    }
    for (int i = 0; i < 500 && m_live.load() > 0; ++i) Sleep(10);
    m_running.store(false);
    Log::Info("MCP: stopped");
}

void McpServer::ListenMain() {
    while (!m_quit.load()) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET((SOCKET)m_listen, &rd);
        timeval tv{ 0, 200000 };
        const int r = select(0, &rd, nullptr, nullptr, &tv);
        if (r <= 0) { if (r < 0 && m_quit.load()) break; continue; }
        sockaddr_in from{};
        int len = sizeof(from);
        SOCKET c = accept((SOCKET)m_listen, (sockaddr*)&from, &len);
        if (c == INVALID_SOCKET) { if (m_quit.load()) break; continue; }
        const bool local = from.sin_addr.s_addr == htonl(INADDR_LOOPBACK);
        if (!local && m_bind != 1) { closesocket(c); continue; }   // this computer only
        char text[64] = {};
        inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
        const std::string peer = text;
        if (m_live.load() >= kMaxConnections) {
            // Full: the client hears so at once rather than waiting in the backlog.
            const std::string body = R"json({"error":"too many connections: try again in a moment"})json";
            const std::string resp = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nRetry-After: 2\r\nContent-Length: " +
                                     std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            SendAll(c, resp.data(), resp.size());
            closesocket(c);
            continue;
        }
        { std::lock_guard<std::mutex> lock(m_mutex); m_clientSockets.push_back((uintptr_t)c); }
        // One detached thread per connection; Stop closes every socket and waits for the count to reach zero.
        m_live.fetch_add(1);
        std::thread([this, c, peer, local] { Connection((uintptr_t)c, peer, local); m_live.fetch_sub(1); }).detach();
    }
}

bool McpServer::ReadRequest(uintptr_t socket, Request& r) {
    SOCKET s = (SOCKET)socket;
    std::string data;
    char buf[65536];
    size_t hdrEnd = std::string::npos;
    while (hdrEnd == std::string::npos) {
        const int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, (size_t)n);
        hdrEnd = data.find("\r\n\r\n");
        if (data.size() > 65536 && hdrEnd == std::string::npos) return false;
    }
    const std::string head = data.substr(0, hdrEnd);
    size_t eol = head.find("\r\n");
    const std::string line = head.substr(0, eol);
    const size_t sp1 = line.find(' ');
    const size_t sp2 = line.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    r.method = line.substr(0, sp1);
    std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    if (q != std::string::npos) { r.query = target.substr(q + 1); target.resize(q); }
    r.path = target;
    uint64_t contentLength = 0;
    bool chunked = false;
    r.keepAlive = true;
    size_t pos = eol == std::string::npos ? head.size() : eol + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) e = head.size();
        const std::string h = head.substr(pos, e - pos);
        pos = e + 2;
        const size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = Lower(Trim(h.substr(0, colon)));
        const std::string value = Trim(h.substr(colon + 1));
        if (name == "content-length") contentLength = (uint64_t)_strtoui64(value.c_str(), nullptr, 10);
        else if (name == "origin") r.origin = value;
        else if (name == "authorization") r.auth = value;
        else if (name == "accept") r.accept = value;
        else if (name == "host") r.host = value;
        else if (name == "connection" && Lower(value).find("close") != std::string::npos) r.keepAlive = false;
        else if (name == "transfer-encoding" && Lower(value).find("chunked") != std::string::npos) chunked = true;
    }
    if (chunked) return false;
    r.bodyBytes = contentLength;
    std::string first = data.substr(hdrEnd + 4);
    if (r.method == "POST" && r.path == "/upload") {
        // The body is a file: it streams to the upload folder rather than through memory. Too large, or uploads off:
        // nothing is read and the connection closes after the answer.
        std::wstring dir;
        uint64_t maxBytes = 0;
        { std::lock_guard<std::mutex> lock(m_mutex); dir = m_uploadDir; maxBytes = m_uploadMax; }
        if (dir.empty() || contentLength == 0 || contentLength > maxBytes) { r.keepAlive = false; return true; }
        const std::wstring file = JoinPath(dir, L"up_" + Utf8ToWide(NewSecret().substr(4, 12)) + L".part");
        HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) { r.keepAlive = false; return true; }
        uint64_t done = 0;
        bool ok = true;
        auto put = [&](const char* p, size_t n) {
            while (n > 0 && ok) {
                DWORD w = 0;
                if (!WriteFile(f, p, (DWORD)std::min<size_t>(n, 1 << 20), &w, nullptr) || w == 0) { ok = false; break; }
                p += w; n -= w; done += w;
            }
        };
        if (first.size() > contentLength) first.resize((size_t)contentLength);
        put(first.data(), first.size());
        while (ok && done < contentLength) {
            const int n = recv(s, buf, (int)std::min<uint64_t>(sizeof(buf), contentLength - done), 0);
            if (n <= 0) { ok = false; break; }
            put(buf, (size_t)n);
        }
        CloseHandle(f);
        if (!ok) { DeleteFileW(file.c_str()); return false; }
        r.bodyFile = file;
        return true;
    }
    if (contentLength > (64u << 20)) return false;
    r.body = std::move(first);
    while (r.body.size() < contentLength) {
        const int n = recv(s, buf, (int)std::min<uint64_t>(sizeof(buf), contentLength - r.body.size()), 0);
        if (n <= 0) return false;
        r.body.append(buf, (size_t)n);
    }
    r.body.resize((size_t)contentLength);
    return true;
}

void McpServer::Connection(uintptr_t socket, const std::string& peer, bool local) {
    SOCKET s = (SOCKET)socket;
    const DWORD idle = 60000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&idle, sizeof(idle));
    const BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    while (!m_quit.load()) {
        Request r;
        r.peer = peer;
        r.local = local;
        if (!ReadRequest(socket, r)) break;
        Response out;
        Handle(r, out);
        if (!r.bodyFile.empty() && FileExists(r.bodyFile)) DeleteFileW(r.bodyFile.c_str());   // not taken by the program: gone
        // A file as the body (a download): its bytes go out after the headers, never through a string.
        HANDLE file = INVALID_HANDLE_VALUE;
        uint64_t fileSize = 0;
        if (!out.filePath.empty()) {
            file = CreateFileW(out.filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            LARGE_INTEGER size{};
            if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size)) {
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                file = INVALID_HANDLE_VALUE;
                out.status = 404; out.type = "application/json"; out.body = R"json({"error":"the file is gone"})json"; out.filePath.clear(); out.extraHeaders.clear();
            } else fileSize = (uint64_t)size.QuadPart;
        }
        std::string resp = "HTTP/1.1 " + std::to_string(out.status) + " " + ReasonPhrase(out.status) + "\r\n";
        if (!out.body.empty() || file != INVALID_HANDLE_VALUE || (out.status != 202 && out.status != 204)) resp += "Content-Type: " + out.type + "\r\n";
        resp += "Content-Length: " + std::to_string(file != INVALID_HANDLE_VALUE ? fileSize : (uint64_t)out.body.size()) + "\r\n";
        resp += "Cache-Control: no-store\r\nAccess-Control-Allow-Origin: " + (r.origin.empty() ? std::string("*") : r.origin) + "\r\n";
        resp += "Access-Control-Allow-Headers: Content-Type, Authorization, Accept, Mcp-Session-Id, MCP-Protocol-Version\r\n";
        resp += "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
        resp += out.extraHeaders;
        resp += r.keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
        resp += "\r\n";
        if (file == INVALID_HANDLE_VALUE) resp += out.body;
        bool ok = SendAll(s, resp.data(), resp.size());
        if (file != INVALID_HANDLE_VALUE) {
            std::vector<char> chunk(1 << 18);
            uint64_t sent = 0;
            while (ok && sent < fileSize) {
                DWORD n = 0;
                if (!ReadFile(file, chunk.data(), (DWORD)std::min<uint64_t>(chunk.size(), fileSize - sent), &n, nullptr) || n == 0) { ok = false; break; }
                ok = SendAll(s, chunk.data(), n);
                sent += n;
            }
            CloseHandle(file);
        }
        if (!ok || !r.keepAlive) break;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clientSockets.erase(std::remove(m_clientSockets.begin(), m_clientSockets.end(), socket), m_clientSockets.end());
    }
    closesocket(s);
}

// Who is asking. A key names the client and gives its role; a keyless client on this computer is the user (admin)
// when the user allows that; everyone else is refused. Read-only mode makes everyone a viewer.
bool McpServer::Authorize(const Request& r, McpCaller& caller, Response& out) {
    Access a;
    { std::lock_guard<std::mutex> lock(m_mutex); a = m_access; }
    caller.peer = r.peer;
    caller.local = r.local;
    caller.host = r.host.empty() ? "127.0.0.1:" + std::to_string(m_port) : r.host;
    std::string given = r.auth.size() > 7 && Lower(r.auth.substr(0, 7)) == "bearer " ? Trim(r.auth.substr(7)) : QueryValue(r.query, "key");
    if (given.empty()) given = QueryValue(r.query, "token");
    if (!given.empty()) {
        bool found = false;
        for (const McpKey& k : a.keys) if (k.secret == given) { caller.role = k.role; caller.keyName = k.name; found = true; break; }
        if (!found && !a.legacyToken.empty() && given == a.legacyToken) { caller.role = McpRoleAdmin; caller.keyName = "token"; found = true; }
        if (!found) {
            out.status = 401;
            out.extraHeaders = "WWW-Authenticate: Bearer\r\n";
            out.body = R"json({"error":"unknown key: the user creates keys in the sidebar (AI assistant section)"})json";
            return false;
        }
    } else if (r.local && a.localNoKey) {
        caller.role = McpRoleAdmin;
        caller.keyName = "local";
        // A web page in this computer's browser must not drive the program unnoticed: only local origins pass without a key.
        if (!LocalOrigin(r.origin)) { out.status = 403; out.body = R"json({"error":"origin not allowed"})json"; return false; }
    } else {
        out.status = 401;
        out.extraHeaders = "WWW-Authenticate: Bearer\r\n";
        out.body = r.local ? R"json({"error":"a key is required (Authorization: Bearer <key>): the user creates one in the sidebar, or switches on 'This computer needs no key'"})json"
                           : R"json({"error":"a key is required (Authorization: Bearer <key>): the user creates one in the sidebar (AI assistant section)"})json";
        return false;
    }
    if (a.readOnly) caller.role = McpRoleViewer;
    return true;
}

void McpServer::Handle(const Request& r, Response& out) {
    if (r.method == "OPTIONS") { out.status = 204; return; }
    McpCaller caller;
    if (!Authorize(r, caller, out)) return;
    if (r.path == "/mcp" || r.path == "/") {
        if (r.method == "GET" && r.path == "/") { out.type = "text/html; charset=utf-8"; out.body = InfoPage(r, caller); return; }
        if (r.method == "GET") { out.status = 405; out.extraHeaders = "Allow: POST, DELETE, OPTIONS\r\n"; out.body = R"json({"error":"this server has no event stream; POST JSON-RPC messages"})json"; return; }
        if (r.method == "DELETE") { out.status = 200; out.body = "{}"; return; }
        if (r.method != "POST") { out.status = 405; out.extraHeaders = "Allow: POST, DELETE, OPTIONS\r\n"; return; }
        Json msg;
        if (!JsonReader::Parse(r.body, msg)) { out.status = 400; out.body = RpcError(Json(), -32700, "Parse error").Dump(); return; }
        if (msg.type == Json::Array) {
            Json replies = Json::Arr();
            for (const Json& m : msg.arr) { bool none = false; Json reply = HandleRpc(m, caller, none); if (!none) replies.Push(std::move(reply)); }
            if (replies.arr.empty()) { out.status = 202; return; }
            out.body = replies.Dump();
            return;
        }
        bool none = false;
        Json reply = HandleRpc(msg, caller, none);
        if (none) { out.status = 202; return; }
        out.body = reply.Dump();
        return;
    }
    if (r.path == "/upload") {
        if (r.method != "POST") { out.status = 405; out.extraHeaders = "Allow: POST, OPTIONS\r\n"; return; }
        if (caller.role < McpRoleJobs) { out.status = 403; out.body = R"json({"error":"this key may not upload (viewer role, or the server is read-only)"})json"; return; }
        if (r.bodyFile.empty()) {
            uint64_t maxBytes = 0;
            { std::lock_guard<std::mutex> lock(m_mutex); maxBytes = m_uploadMax; }
            if (r.bodyBytes == 0) { out.status = 400; out.body = R"json({"error":"send the file's bytes as the body with a Content-Length"})json"; return; }
            out.status = 413;
            out.body = StrPrintf(R"json({"error":"the upload is larger than the server allows (%llu MB)","maxBytes":%llu})json", (unsigned long long)(maxBytes >> 20), (unsigned long long)maxBytes);
            return;
        }
        std::string name = QueryValue(r.query, "name");
        if (name.empty()) name = "upload.bin";
        Json err;
        Json result = CallTool("_upload", Json::Obj().Set("file", WideToUtf8(r.bodyFile)).Set("name", name).Set("bytes", (unsigned long long)r.bodyBytes), caller, &err);
        if (err.type != Json::Null) { out.status = 500; out.body = Json::Obj().Set("error", err.Str("message")).Dump(); return; }
        if (result.Flag("isError")) {
            std::string text;
            if (const Json* c = result.Find("content")) for (const Json& item : c->arr) if (item.Str("type") == "text") { text = item.Str("text"); break; }
            out.status = 400; out.body = Json::Obj().Set("error", text).Dump(); return;
        }
        if (const Json* sc = result.Find("structuredContent")) out.body = sc->Dump(2); else out.body = "{}";
        return;
    }
    if (r.path.rfind("/download/", 0) == 0) {
        if (r.method != "GET") { out.status = 405; out.extraHeaders = "Allow: GET, OPTIONS\r\n"; return; }
        const std::string rest = r.path.substr(10);
        const size_t slash = rest.find('/');
        const std::string job = slash == std::string::npos ? rest : rest.substr(0, slash);
        const std::string file = slash == std::string::npos ? std::string() : UrlDecode(rest.substr(slash + 1));
        Json err;
        Json result = CallTool("_download", Json::Obj().Set("job", job).Set("file", file), caller, &err);
        if (err.type != Json::Null) { out.status = 500; out.body = Json::Obj().Set("error", err.Str("message")).Dump(); return; }
        const Json* sc = result.Find("structuredContent");
        if (result.Flag("isError") || !sc || sc->Str("path").empty()) {
            std::string text;
            if (const Json* c = result.Find("content")) for (const Json& item : c->arr) if (item.Str("type") == "text") { text = item.Str("text"); break; }
            out.status = text.find("not allowed") != std::string::npos ? 403 : 404;
            out.body = Json::Obj().Set("error", text).Dump();
            return;
        }
        out.filePath = Utf8ToWide(sc->Str("path"));
        out.type = ContentTypeOf(sc->Str("name"));
        std::string ascii;
        for (char c : sc->Str("name")) ascii += ((unsigned char)c < 32 || (unsigned char)c > 126 || c == '"' || c == '\\') ? '_' : c;
        out.extraHeaders = "Content-Disposition: " + std::string(QueryValue(r.query, "inline").empty() ? "attachment" : "inline") + "; filename=\"" + ascii + "\"\r\n";
        return;
    }
    if (r.method == "GET" && (r.path == "/preview.png" || r.path == "/window.png")) {
        Json args = Json::Obj().Set("view", r.path == "/window.png" ? "window" : "output");
        const std::string edge = QueryValue(r.query, "max_edge");
        if (!edge.empty()) args.Set("max_edge", atoi(edge.c_str()));
        Json err;
        Json result = CallTool("preview", args, caller, &err);
        std::string png;
        if (err.type == Json::Null && result.Find("content"))
            for (const Json& c : result.Find("content")->arr) if (c.Str("type") == "image") png = c.Str("data");
        if (png.empty()) { out.status = 404; out.body = result.Dump(); return; }
        std::vector<uint8_t> bytes;
        Base64Decode(png, bytes);
        out.type = "image/png";
        out.body.assign((const char*)bytes.data(), bytes.size());
        return;
    }
    if (r.method == "GET" && (r.path == "/status.json" || r.path == "/settings.json" || r.path == "/library.json" || r.path == "/log.txt" || r.path == "/jobs.json")) {
        const char* tool = r.path == "/status.json" ? "get_status" : r.path == "/settings.json" ? "get_settings" : r.path == "/library.json" ? "library"
                         : r.path == "/jobs.json" ? "jobs" : "get_log";
        Json args = Json::Obj();
        if (r.path == "/library.json" || r.path == "/jobs.json") args.Set("action", "list");
        if (r.path == "/log.txt") { args.Set("lines", 200); out.type = "text/plain; charset=utf-8"; }
        Json err;
        Json result = CallTool(tool, args, caller, &err);
        std::string text;
        if (result.Find("content")) for (const Json& c : result.Find("content")->arr) if (c.Str("type") == "text") text += c.Str("text");
        if (result.Flag("isError")) out.status = 403;
        out.body = text.empty() ? result.Dump() : text;
        return;
    }
    out.status = 404;
    out.body = R"json({"error":"not found"})json";
}

Json McpServer::HandleRpc(const Json& msg, const McpCaller& caller, bool& noReply) {
    noReply = false;
    if (msg.type != Json::Object) return RpcError(Json(), -32600, "Invalid Request");
    const Json* idp = msg.Find("id");
    const Json id = idp ? *idp : Json();
    const std::string method = msg.Str("method");
    const Json* paramsp = msg.Find("params");
    const Json params = paramsp ? *paramsp : Json::Obj();
    if (method.empty()) { noReply = true; return Json(); }   // a response from the client: nothing to answer
    if (method.rfind("notifications/", 0) == 0) { noReply = true; return Json(); }
    if (!idp) { noReply = true; return Json(); }   // a notification of another kind
    if (method == "initialize") {
        std::string version = params.Str("protocolVersion");
        if (version != "2024-11-05" && version != "2025-03-26" && version != "2025-06-18") version = "2025-06-18";
        Json caps = Json::Obj()
            .Set("tools", Json::Obj().Set("listChanged", false))
            .Set("resources", Json::Obj().Set("subscribe", false).Set("listChanged", false))
            .Set("prompts", Json::Obj().Set("listChanged", false));
        Json result = Json::Obj()
            .Set("protocolVersion", version)
            .Set("capabilities", caps)
            .Set("serverInfo", Json::Obj().Set("name", "VRChat DLSS5 Cam").Set("version", APP_VERSION_STRING))
            .Set("instructions", std::string(kInstructions) + "\nYou are connected as " + caller.keyName + " with the " + RoleName(caller.role) + " role.");
        return RpcResult(id, result);
    }
    if (method == "ping") return RpcResult(id, Json::Obj());
    if (method == "tools/list") {
        Json tools = Json::Arr();
        for (const McpTool& t : kTools) {
            if (t.hidden) continue;
            Json schema;
            JsonReader::Parse(t.schema, schema);
            tools.Push(Json::Obj().Set("name", t.name).Set("description", t.description).Set("inputSchema", schema));
        }
        return RpcResult(id, Json::Obj().Set("tools", tools));
    }
    if (method == "tools/call") {
        const std::string name = params.Str("name");
        const Json* ap = params.Find("arguments");
        Json err;
        Json result = CallTool(name, ap ? *ap : Json::Obj(), caller, &err);
        if (err.type != Json::Null) return Json::Obj().Set("jsonrpc", "2.0").Set("id", id).Set("error", err);
        return RpcResult(id, result);
    }
    if (method == "resources/list") {
        Json list = Json::Arr();
        auto add = [&](const char* uri, const char* name, const char* desc, const char* mime) {
            list.Push(Json::Obj().Set("uri", uri).Set("name", name).Set("description", desc).Set("mimeType", mime));
        };
        add("vdc://status", "Program status", "What get_status answers.", "application/json");
        add("vdc://settings", "Settings", "Every setting with its value.", "application/json");
        add("vdc://settings/schema", "Settings reference", "Every key with its type, range and meaning.", "application/json");
        add("vdc://library", "Media library", "The items of the library with their ids and states.", "application/json");
        add("vdc://jobs", "Processing queue", "Your jobs and the state of the queue.", "application/json");
        add("vdc://log", "Log", "The last 200 lines of the program's log.", "text/plain");
        add("vdc://preview", "Preview picture", "The preview as it is right now (compare mode applied), scaled to 1024 px.", "image/png");
        add("vdc://preview/window", "Window picture", "The whole program window, scaled to 1024 px.", "image/png");
        return RpcResult(id, Json::Obj().Set("resources", list));
    }
    if (method == "resources/read") {
        Json err;
        Json result = ReadResource(params.Str("uri"), caller, &err);
        if (err.type != Json::Null) return Json::Obj().Set("jsonrpc", "2.0").Set("id", id).Set("error", err);
        return RpcResult(id, result);
    }
    if (method == "resources/templates/list") return RpcResult(id, Json::Obj().Set("resourceTemplates", Json::Arr()));
    if (method == "prompts/list") return RpcResult(id, Json::Obj().Set("prompts", Json::Arr()));
    if (method == "logging/setLevel") return RpcResult(id, Json::Obj());
    if (method == "completion/complete") return RpcResult(id, Json::Obj().Set("completion", Json::Obj().Set("values", Json::Arr())));
    return RpcError(id, -32601, "Method not found: " + method);
}

Json McpServer::CallTool(const std::string& name, const Json& args, const McpCaller& caller, Json* rpcError) {
    const McpTool* tool = nullptr;
    for (const McpTool& t : kTools) if (name == t.name) { tool = &t; break; }
    if (!tool) { if (rpcError) *rpcError = Json::Obj().Set("code", -32602).Set("message", "Unknown tool: " + name); return Json(); }
    const std::string action = args.Str("action");
    // Looking: what a viewer may do, and what read-only mode allows.
    const bool looking = !tool->mutating || (name == "library" && action == "list") || (name == "video" && action == "info") ||
                         (name == "presets" && action == "list") || (name == "history" && !args.Has("index")) ||
                         (name == "jobs" && action != "cancel" && action != "delete");
    auto call = std::make_shared<McpCall>();
    call->name = name;
    call->args = args;
    call->caller = caller;
    call->started = NowSeconds();
    bool allowed = caller.role == McpRoleAdmin;
    if (caller.role == McpRoleJobs) allowed = tool->jobs || (looking && (name == "get_status" || name == "describe_settings" || name == "presets"));
    else if (caller.role == McpRoleViewer) allowed = looking;
    bool readOnly = false;
    { std::lock_guard<std::mutex> lock(m_mutex); readOnly = m_access.readOnly; }
    if (!allowed) {
        std::string why;
        if (readOnly) why = "The MCP server is in read-only mode: " + name + " would change something. The user can switch it off in the sidebar (AI assistant section).";
        else if (caller.role == McpRoleJobs) why = "The key " + caller.keyName + " has the jobs role: it sends files with upload and submit and follows them with jobs; " + name + " needs an admin key.";
        else why = "The key " + caller.keyName + " has the viewer role: it only looks; " + name + " needs " + (tool->jobs ? "a jobs or an admin key." : "an admin key.");
        call->Fail(why, Json::Obj().Set("reason", "not_allowed").Set("role", RoleName(caller.role)));
    } else if (!m_dispatch) {
        call->Fail("The program is not ready");
    } else if (m_pending.load() >= kMaxPending) {
        // A storm of calls: this one is refused at once rather than queued behind the others.
        call->Fail(StrPrintf("The program is busy with %d calls: try again in a moment", m_pending.load()),
                   Json::Obj().Set("reason", "busy").Set("retryAfterSeconds", 2));
    } else {
        if (!tool->hidden) NoteCall(name, caller.keyName);
        m_pending.fetch_add(1);
        m_dispatch(call);
        double timeout = 60.0;
        const double asked = args.Num("timeout", 0.0);
        if (asked > 0.0) timeout = asked + 30.0;
        if (!call->Wait(timeout)) {
            call->Fail(StrPrintf("No answer from the interface within %.0f s: the window may be busy with a dialog, or the run is still going", timeout));
        }
        m_pending.fetch_sub(1);
    }
    Json result = Json::Obj().Set("content", call->content).Set("isError", call->isError);
    if (call->structured.type == Json::Object) result.Set("structuredContent", call->structured);
    return result;
}

Json McpServer::ReadResource(const std::string& uri, const McpCaller& caller, Json* rpcError) {
    std::string tool;
    Json args = Json::Obj();
    std::string mime = "application/json";
    if (uri == "vdc://status") tool = "get_status";
    else if (uri == "vdc://settings") tool = "get_settings";
    else if (uri == "vdc://settings/schema") tool = "describe_settings";
    else if (uri == "vdc://library") { tool = "library"; args.Set("action", "list"); }
    else if (uri == "vdc://jobs") { tool = "jobs"; args.Set("action", "list"); }
    else if (uri == "vdc://log") { tool = "get_log"; args.Set("lines", 200); mime = "text/plain"; }
    else if (uri == "vdc://preview") { tool = "preview"; args.Set("view", "output"); mime = "image/png"; }
    else if (uri == "vdc://preview/window") { tool = "preview"; args.Set("view", "window"); mime = "image/png"; }
    else { if (rpcError) *rpcError = Json::Obj().Set("code", -32002).Set("message", "Resource not found: " + uri); return Json(); }
    Json err;
    Json result = CallTool(tool, args, caller, &err);
    if (err.type != Json::Null) { if (rpcError) *rpcError = err; return Json(); }
    Json contents = Json::Arr();
    if (const Json* c = result.Find("content")) {
        for (const Json& item : c->arr) {
            if (item.Str("type") == "image") contents.Push(Json::Obj().Set("uri", uri).Set("mimeType", "image/png").Set("blob", item.Str("data")));
            else if (item.Str("type") == "text" && mime != "image/png") contents.Push(Json::Obj().Set("uri", uri).Set("mimeType", mime).Set("text", item.Str("text")));
        }
    }
    if (contents.arr.empty() && result.Flag("isError")) {
        std::string text;
        if (const Json* c = result.Find("content")) for (const Json& item : c->arr) text += item.Str("text");
        if (rpcError) *rpcError = Json::Obj().Set("code", -32603).Set("message", text);
        return Json();
    }
    return Json::Obj().Set("contents", contents);
}

// --- the information page ---------------------------------------------------------------------

std::string McpServer::InfoPage(const Request& r, const McpCaller& caller) {
    Status st = Get();
    Access a;
    std::wstring uploadDir;
    uint64_t uploadMax = 0;
    { std::lock_guard<std::mutex> lock(m_mutex); a = m_access; uploadDir = m_uploadDir; uploadMax = m_uploadMax; }
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::string here = "http://" + caller.host + "/mcp";
    std::string h = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>VRChat DLSS5 Cam MCP server</title>"
                    "<style>body{font-family:Segoe UI,sans-serif;max-width:960px;margin:2em auto;padding:0 1em;color:#222;line-height:1.45}code,pre{background:#f3f3f3;padding:2px 5px;border-radius:4px}"
                    "pre{padding:10px;overflow:auto}table{border-collapse:collapse;margin:.5em 0}td,th{border:1px solid #ddd;padding:6px 10px;text-align:left;vertical-align:top}img{max-width:100%}"
                    ".ok{color:#1a7f37}.warn{color:#9a6700}small{color:#666}</style></head><body>";
    h += "<h1>VRChat DLSS5 Cam " APP_VERSION_STRING " &middot; MCP server</h1>";
    h += "<p>You are <b>" + HtmlEscape(caller.keyName) + "</b> (" + RoleName(caller.role) + (a.readOnly ? ", the server is read-only" : "") + ") from " + HtmlEscape(caller.peer) +
         ". " + StrPrintf("%u calls so far, %d connections open.", st.calls, st.connections) + "</p>";
    h += "<h2>Addresses</h2><ul>";
    h += "<li>On this computer: <code>" + HtmlEscape(Url(st.port)) + "</code>" + (a.localNoKey ? " <small>(no key needed here)</small>" : " <small>(a key is needed)</small>") + "</li>";
    if (st.bind == 1) {
        for (const std::string& addr : LocalAddresses()) h += "<li>From the local network: <code>" + HtmlEscape(Url(addr, st.port)) + "</code> <small>(a key is needed)</small></li>";
        h += "</ul><p>Other computers need the Windows firewall to allow the port (the sidebar has a button for that) and a key from the sidebar's AI assistant section. "
             "From outside the network, use a VPN such as Tailscale or a reverse proxy that adds HTTPS: this server speaks plain HTTP.</p>";
    } else {
        h += "</ul><p>The server listens on this computer only. Set <i>Reach</i> to <i>Local network</i> in the sidebar to let other computers connect (with a key).</p>";
    }
    h += "<h2>Connecting</h2>";
    h += "<p>Claude Desktop, Cursor and other stdio clients on this computer (the bridge starts the program when it is not running):</p><pre>" + HtmlEscape(ClientConfig(exe)) + "</pre>";
    h += "<p>A client that speaks Streamable HTTP, on this computer or another one (put your key in):</p><pre>" + HtmlEscape(ClientConfigRemote(here, "<your key>")) + "</pre>";
    h += "<p>Claude Code: <code>claude mcp add --transport http vrchat-dlss5-cam " + HtmlEscape(here) + " --header \"Authorization: Bearer &lt;your key&gt;\"</code></p>";
    h += "<p>The bridge on another computer: <code>VRChatDLSS5Cam.exe --mcp --mcp-url " + HtmlEscape(here) + " --mcp-key &lt;your key&gt;</code></p>";
    h += "<p><a href=\"/preview.png\">preview.png</a> &middot; <a href=\"/window.png\">window.png</a> &middot; <a href=\"/status.json\">status.json</a> &middot; "
         "<a href=\"/settings.json\">settings.json</a> &middot; <a href=\"/library.json\">library.json</a> &middot; <a href=\"/jobs.json\">jobs.json</a> &middot; <a href=\"/log.txt\">log.txt</a></p>";
    if (caller.role >= McpRoleJobs) {
        h += "<h2>Jobs</h2>";
        Json err;
        Json result = CallTool("jobs", Json::Obj().Set("action", "list"), caller, &err);
        const Json* sc = result.Find("structuredContent");
        if (sc) {
            h += StrPrintf("<p>%d waiting, %s, %d finished. Files of a finished job are kept for %d hours.</p>",
                           sc->Int("queued"), sc->Str("running").empty() ? "nothing running" : ("running: " + HtmlEscape(sc->Str("running"))).c_str(),
                           sc->Int("finished"), sc->Int("keepHours"));
            if (const Json* jobs = sc->Find("jobs")) {
                if (!jobs->arr.empty()) {
                    h += "<table><tr><th>Id</th><th>Owner</th><th>Name</th><th>State</th><th>Position</th><th>Results</th></tr>";
                    for (const Json& j : jobs->arr) {
                        std::string files;
                        if (const Json* outs = j.Find("outputs")) for (const Json& o : outs->arr) files += "<a href=\"" + HtmlEscape(o.Str("url")) + "\">" + HtmlEscape(o.Str("name")) + "</a> ";
                        h += "<tr><td><code>" + HtmlEscape(j.Str("id")) + "</code></td><td>" + HtmlEscape(j.Str("owner")) + "</td><td>" + HtmlEscape(j.Str("name")) + "</td><td>" + HtmlEscape(j.Str("state")) +
                             (j.Has("progress") ? StrPrintf(" %.0f%%", j.Num("progress") * 100.0) : "") + "</td><td>" + (j.Has("position") ? std::to_string(j.Int("position")) : "") + "</td><td>" +
                             (files.empty() ? HtmlEscape(j.Str("error")) : files) + "</td></tr>";
                    }
                    h += "</table>";
                }
            }
        } else if (result.Find("content")) {
            for (const Json& c : result.Find("content")->arr) if (c.Str("type") == "text") h += "<p>" + HtmlEscape(c.Str("text")) + "</p>";
        }
        h += "<h3>A bot in three calls</h3><p>Send a picture or a video, tell the person it is queued, poll with <code>jobs wait</code> (it answers within the timeout, never blocks longer), then hand out the download link:</p>";
        h += "<pre>import requests, base64, time\n"
             "URL, KEY = \"" + HtmlEscape(here) + "\", \"&lt;your key&gt;\"\n"
             "def call(tool, **args):\n"
             "    r = requests.post(URL, headers={\"Authorization\": \"Bearer \" + KEY, \"Accept\": \"application/json\"},\n"
             "                      json={\"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"tools/call\", \"params\": {\"name\": tool, \"arguments\": args}}, timeout=90)\n"
             "    return r.json()[\"result\"][\"structuredContent\"]\n"
             "\n"
             "with open(\"photo.png\", \"rb\") as f:\n"
             "    up = requests.post(\"http://" + HtmlEscape(caller.host) + "/upload?name=photo.png\", headers={\"Authorization\": \"Bearer \" + KEY}, data=f).json()\n"
             "job = call(\"submit\", upload_id=up[\"uploadId\"], preset=\"Natural\", label=\"group 12345\")\n"
             "print(job[\"message\"])                      # 'Queued at position 3, about 2 minutes' - tell the person now\n"
             "while job[\"state\"] in (\"queued\", \"running\"):\n"
             "    job = call(\"jobs\", action=\"wait\", id=job[\"jobId\"], timeout=30)\n"
             "if job[\"state\"] == \"done\":\n"
             "    print(job[\"outputs\"][0][\"url\"])         # download with the same Authorization header\n"
             "else:\n"
             "    print(job[\"error\"])\n</pre>";
        h += StrPrintf("<p>Uploads: <code>POST /upload?name=&lt;file name&gt;</code> with the bytes as the body (at most %llu MB), or the <code>upload</code> tool with base64 data (at most 16 MB). "
                       "Small files may go straight into <code>submit</code> as <code>data</code>; a public <code>url</code> works too.</p>", (unsigned long long)(uploadMax >> 20));
    }
    h += "<h2>Tools</h2><table><tr><th>Tool</th><th>What it does</th></tr>";
    for (const McpTool& t : kTools) {
        if (t.hidden) continue;
        h += "<tr><td><code>" + std::string(t.name) + "</code>" + (t.mutating ? "" : " <small>(read)</small>") + (t.jobs ? " <small>(jobs)</small>" : "") + "</td><td>" + HtmlEscape(t.description) + "</td></tr>";
    }
    h += "</table><h2>Settings keys</h2><table><tr><th>Key</th><th>Group</th><th>Type</th><th>Range</th><th>Meaning</th></tr>";
    for (const McpSettingInfo& s : kSettings)
        h += "<tr><td><code>" + std::string(s.key) + "</code></td><td>" + s.group + "</td><td>" + s.type + "</td><td>" + HtmlEscape(s.range) + "</td><td>" + HtmlEscape(s.description) + "</td></tr>";
    h += "</table></body></html>";
    (void)r;
    return h;
}

// --- the stdio bridge -----------------------------------------------------------------------

namespace {

void BridgeLog(const std::wstring& dir, const std::string& text) {
    if (dir.empty()) return;
    const std::wstring file = JoinPath(dir, L"mcp-bridge.txt");
    // The log rotates once at 1 MB rather than growing forever under a client that restarts often.
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh > 0 || fa.nFileSizeLow > (1u << 20)))
        MoveFileExW(file.c_str(), JoinPath(dir, L"mcp-bridge.old.txt").c_str(), MOVEFILE_REPLACE_EXISTING);
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"ab") != 0 || !f) return;
    SYSTEMTIME t{};
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text.c_str());
    fclose(f);
}

struct Target {
    std::string host = "127.0.0.1";
    int port = 0;
    std::string path = "/mcp";
    bool remote = false;   // named by --mcp-url: the bridge starts nothing
};

bool ParseUrl(const std::string& url, Target& t) {
    std::string rest = url;
    if (rest.rfind("http://", 0) == 0) rest = rest.substr(7);
    else if (rest.rfind("https://", 0) == 0) return false;   // the bridge speaks plain HTTP; put a proxy in front for HTTPS
    const size_t slash = rest.find('/');
    std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
    t.path = slash == std::string::npos ? "/mcp" : rest.substr(slash);
    if (t.path.empty() || t.path == "/") t.path = "/mcp";
    const size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos && hostPort.find(']') == std::string::npos) { t.port = atoi(hostPort.c_str() + colon + 1); hostPort.resize(colon); }
    if (t.port <= 0) t.port = 80;
    t.host = hostPort;
    t.remote = true;
    return !t.host.empty();
}

bool ServerAnswers(const Target& t, const std::string& key, std::string* why = nullptr) {
    int status = 0;
    std::string body;
    const bool ok = HttpRequest(t.host, t.port, "POST", t.path, R"json({"jsonrpc":"2.0","id":0,"method":"ping"})json", key, status, body, 3.0);
    if (why) *why = !ok ? "no connection" : status == 200 ? "" : StrPrintf("HTTP %d %s", status, body.c_str());
    return ok && status == 200;
}

bool WriteAll(HANDLE h, const std::string& s) {
    size_t done = 0;
    while (done < s.size()) {
        DWORD n = 0;
        if (!WriteFile(h, s.data() + done, (DWORD)std::min<size_t>(s.size() - done, 1 << 20), &n, nullptr) || n == 0) return false;
        done += n;
    }
    return true;
}

} // namespace

int McpServer::BridgeMain(const std::wstring& exePath, int port, const std::wstring& dataDir, const std::wstring& passThrough,
                          const std::string& url, const std::string& keyArg) {
    const std::wstring logDir = GetAppDataDir();
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    // The port and the legacy token come from the program's own settings file unless the command line names them.
    std::string key = keyArg;
    std::string legacyToken;
    int filePort = 0;
    bool localNoKey = true;
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, JoinPath(logDir, L"settings.ini").c_str(), L"rb") == 0 && f) {
            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                std::string l = Trim(line);
                if (l.rfind("mcpPort=", 0) == 0) filePort = atoi(l.c_str() + 8);
                else if (l.rfind("mcpToken=", 0) == 0) legacyToken = l.substr(9);
                else if (l.rfind("mcpLocalNoKey=", 0) == 0) localNoKey = atoi(l.c_str() + 14) != 0;
            }
            fclose(f);
        }
    }
    Target target;
    if (!url.empty() && !ParseUrl(url, target)) {
        MessageBoxW(nullptr, L"--mcp-url needs an address such as http://192.168.1.20:51550/mcp (plain HTTP).", L"VRChat DLSS5 Cam", MB_ICONINFORMATION | MB_OK);
        WSACleanup();
        return 3;
    }
    if (!target.remote) {
        target.port = port > 0 ? port : filePort > 0 ? filePort : DefaultPort();
        if (key.empty()) key = legacyToken;
        if (key.empty() && !localNoKey) {
            // Local clients need a key here: the first admin key of the file serves the bridge.
            std::vector<McpKey> keys;
            if (LoadKeys(JoinPath(dataDir.empty() ? logDir : dataDir, L"mcp-keys.json"), keys))
                for (const McpKey& k : keys) if (k.role == McpRoleAdmin) { key = k.secret; break; }
        }
    }
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (in == INVALID_HANDLE_VALUE || !in || out == INVALID_HANDLE_VALUE || !out) {
        MessageBoxW(nullptr, L"--mcp is the bridge for an MCP client: it reads JSON-RPC lines on its standard input. Start it from the client's configuration, not by hand.",
                    L"VRChat DLSS5 Cam", MB_ICONINFORMATION | MB_OK);
        WSACleanup();
        return 2;
    }
    BridgeLog(logDir, target.remote ? "bridge start, server " + target.host + ":" + std::to_string(target.port) + target.path
                                    : StrPrintf("bridge start, port %d%s", target.port, key.empty() ? "" : " with a key"));

    // The running program, or one started now. A window without the server (the setting is off) is asked to start it.
    // A remote server is only reached, never started.
    std::string lastWhy;
    auto ensureServer = [&]() -> bool {
        if (ServerAnswers(target, key, &lastWhy)) return true;
        if (target.remote) return false;
        if (lastWhy.rfind("HTTP", 0) == 0) return false;   // it answers, but refuses: starting another program changes nothing
        HWND w = FindWindowW(WindowClass(), nullptr);
        if (w) {
            PostMessageW(w, kWakeMessage, (WPARAM)target.port, 0);
            BridgeLog(logDir, "asked the running program to start its server");
            for (int i = 0; i < 50; ++i) { Sleep(100); if (ServerAnswers(target, key, &lastWhy)) return true; }
        }
        std::wstring cmd = L"\"" + exePath + L"\" --mcp-port " + std::to_wstring(target.port);
        if (!dataDir.empty()) cmd += L" --data-dir \"" + dataDir + L"\"";
        if (!passThrough.empty()) cmd += L" " + passThrough;
        BridgeLog(logDir, "starting the program: " + WideToUtf8(cmd));
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        std::wstring dir = exePath;
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash);
        if (!CreateProcessW(exePath.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, dir.c_str(), &si, &pi)) {
            BridgeLog(logDir, StrPrintf("CreateProcess failed: %lu", GetLastError()));
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        for (int i = 0; i < 600; ++i) { Sleep(200); if (ServerAnswers(target, key, &lastWhy)) return true; }
        BridgeLog(logDir, "the program did not answer within 120 s");
        return false;
    };

    std::string pending;
    char buf[65536];
    int code = 0;
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(in, buf, sizeof(buf), &n, nullptr) || n == 0) break;   // the client closed its end
        pending.append(buf, n);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = Trim(pending.substr(0, nl));
            pending.erase(0, nl + 1);
            if (line.empty()) continue;
            Json msg;
            const bool parsed = JsonReader::Parse(line, msg);
            const bool notification = parsed && msg.type == Json::Object && !msg.Find("id");
            if (!ensureServer()) {
                BridgeLog(logDir, "server not reachable: " + lastWhy);
                if (!notification) {
                    const Json id = parsed && msg.Find("id") ? *msg.Find("id") : Json();
                    std::string text = target.remote ? "VRChat DLSS5 Cam at " + target.host + ":" + std::to_string(target.port) + " does not answer"
                                                     : "VRChat DLSS5 Cam is not running and could not be started";
                    if (lastWhy.rfind("HTTP", 0) == 0) text += " (" + lastWhy + ")";
                    if (lastWhy.rfind("HTTP 401", 0) == 0 || lastWhy.rfind("HTTP 403", 0) == 0)
                        text += key.empty() ? ": it needs a key, pass one with --mcp-key <key>" : ": the key is not accepted, check --mcp-key";
                    WriteAll(out, RpcError(id, -32000, text).Dump() + "\n");
                }
                continue;
            }
            int status = 0;
            std::string body;
            const bool ok = HttpRequest(target.host, target.port, "POST", target.path, line, key, status, body, 900.0);
            if (!ok) {
                BridgeLog(logDir, "no answer from the server");
                if (!notification) {
                    const Json id = parsed && msg.Find("id") ? *msg.Find("id") : Json();
                    WriteAll(out, RpcError(id, -32000, "no answer from VRChat DLSS5 Cam").Dump() + "\n");
                }
                continue;
            }
            if (status == 202 || body.empty()) continue;   // a notification: nothing comes back
            if (status == 401 || status == 403) {
                if (!notification) {
                    const Json id = parsed && msg.Find("id") ? *msg.Find("id") : Json();
                    Json errBody;
                    std::string why = JsonReader::Parse(body, errBody) ? errBody.Str("error") : body;
                    WriteAll(out, RpcError(id, -32000, "VRChat DLSS5 Cam refused the bridge: " + why + " (start the bridge with --mcp-key <key>)").Dump() + "\n");
                }
                continue;
            }
            // One line per message: the server's JSON carries no raw newlines.
            if (!WriteAll(out, body + "\n")) { code = 0; goto done; }
        }
    }
done:
    BridgeLog(logDir, "bridge end");
    WSACleanup();
    return code;
}

} // namespace vdc
