// VRChat DLSS5 Cam - the Ask AI panel (see AskPanel.h).
//
// The control is hidden while the page and Mintlify's widget start; the program draws the panel's card and a turning
// arc meanwhile. The page paints the same card behind the widget (its corners, hairline and shadow match the program's
// cards), so the moment the control appears changes nothing; the widget's panel then fades in on the program's curve.
#include "AskPanel.h"

#include "Json.h"
#include "Util.h"

#include <WebView2.h>
#include <objidl.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <cstring>
#include <utility>

namespace vdc {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char   kPageBase[]   = "https://alanbacker.github.io/VRChat-DLSS5-Cam/app-ask/";
constexpr char   kWidgetId[]   = "mint_widget_0fcdadee-c765-458f-8755-df3fc716e509";
constexpr double kStartTimeout = 30.0;   // creation, or the page's first word; the page itself waits 20 s for the widget

// The documentation's pages (tools/site/site.json): an answer's link to one of them opens it in the interface's language.
const char* const kPages[] = { "index", "install", "first-picture", "live", "videos", "library", "presets", "saving",
                               "updates", "how-it-works", "settings", "shortcuts", "command-line", "mcp", "linux",
                               "radeon", "troubleshooting", "faq", "ai-qa", "privacy", "changelog" };

// A COM object for one WebView2 handler interface that calls a function (the SDK's WRL Callback without WRL).
template <class I, class M> struct Handler;
template <class I, class... A>
struct Handler<I, HRESULT (STDMETHODCALLTYPE I::*)(A...)> final : I {
    Handler(const IID& iid, std::function<HRESULT(A...)> f) : fn(std::move(f)), id(iid) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, id)) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn ? fn(a...) : S_OK; }
    std::function<HRESULT(A...)> fn;
    const IID& id;
    std::atomic<ULONG> refs{ 1 };
};
template <class I, class F> ComPtr<I> MakeHandler(const IID& iid, F&& f) {
    ComPtr<I> p;
    p.Attach(new Handler<I, decltype(&I::Invoke)>(iid, std::forward<F>(f)));
    return p;
}

template <class T> ComPtr<T> Query(IUnknown* from, const IID& iid) {
    ComPtr<T> p;
    if (from) from->QueryInterface(iid, reinterpret_cast<void**>(p.GetAddressOf()));
    return p;
}

std::string TakeString(LPWSTR s) {   // a string the control handed over, freed
    std::string r = s ? WideToUtf8(s) : std::string();
    if (s) CoTaskMemFree(s);
    return r;
}

bool IsWebUrl(const std::string& url) { return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0; }

std::string Shorten(const std::string& s, size_t n = 160) { return s.size() <= n ? s : s.substr(0, n) + "..."; }

std::string HtmlText(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '&') r += "&amp;"; else if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;"; else if (c == '"') r += "&quot;"; else r += c;
    }
    return r;
}

std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}

// Only what edits text stays in the right-click menu: nothing that navigates, saves, prints or inspects.
void FilterMenu(ICoreWebView2ContextMenuRequestedEventArgs* args) {
    ComPtr<ICoreWebView2ContextMenuItemCollection> items;
    if (FAILED(args->get_MenuItems(items.GetAddressOf())) || !items.Get()) return;
    static const char* const keep[] = { "copy", "cut", "paste", "selectAll", "undo", "redo", "copyLinkLocation", "emoji" };
    auto separator = [&](UINT32 i) {
        ComPtr<ICoreWebView2ContextMenuItem> item;
        COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND kind = COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_COMMAND;
        if (SUCCEEDED(items->GetValueAtIndex(i, item.GetAddressOf())) && item.Get()) item->get_Kind(&kind);
        return kind == COREWEBVIEW2_CONTEXT_MENU_ITEM_KIND_SEPARATOR;
    };
    UINT32 n = 0;
    items->get_Count(&n);
    for (UINT32 i = n; i-- > 0;) {
        if (separator(i)) continue;
        ComPtr<ICoreWebView2ContextMenuItem> item;
        if (FAILED(items->GetValueAtIndex(i, item.GetAddressOf())) || !item.Get()) continue;
        LPWSTR name = nullptr;
        item->get_Name(&name);
        const std::string nm = TakeString(name);
        bool ok = false;
        for (const char* k : keep) ok = ok || nm == k;
        if (!ok) items->RemoveValueAtIndex(i);
    }
    // separators: none at either end, never two in a row
    items->get_Count(&n);
    bool afterSeparator = true;
    for (UINT32 i = 0; i < n;) {
        const bool sep = separator(i);
        if (sep && afterSeparator) { items->RemoveValueAtIndex(i); --n; continue; }
        afterSeparator = sep;
        ++i;
    }
    if (n > 0 && separator(n - 1)) { items->RemoveValueAtIndex(n - 1); --n; }
    if (n == 0) args->put_Handled(TRUE);   // nothing left: no menu
}

// ------------------------------------------------------------------------------------------------ the page
// The page's own look: the window's colour behind a card like the program's (radius, hairline, the shadow in its
// corners), the same colours as the panel the program draws while the page loads.
const char kPageCss[] = R"vdc(
:root{color-scheme:dark;--vdc-win:#0F1014;--vdc-card:#191C22;--vdc-border:#262A33;--vdc-shadow:rgba(0,0,0,.34);--vdc-hair:1px;--vdc-r:10px;--vdc-ease:cubic-bezier(.25,.6,.4,1);
--vdc-text:#E8EAF0;--vdc-dim:#969CAC;--vdc-ctl:#252932;--vdc-ctl-h:#2F343F;--vdc-ctl-a:#3A404E;--vdc-acc:#5C6CF5;--vdc-acc-h:#7281FF;--vdc-acc-a:#4C5BDE;--vdc-warn:#FFB847;--vdc-lift:rgba(0,0,0,.45)}
:root[data-theme=light]{color-scheme:light;--vdc-win:#F1F3F7;--vdc-card:#FFFFFF;--vdc-border:#DFE2E9;--vdc-shadow:rgba(22,28,50,.22);
--vdc-text:#1B1E25;--vdc-dim:#666D7D;--vdc-ctl:#EEF0F5;--vdc-ctl-h:#E4E7EE;--vdc-ctl-a:#D6DAE4;--vdc-acc:#4658E6;--vdc-acc-h:#5C6DF2;--vdc-acc-a:#3848CC;--vdc-warn:#C27A08;--vdc-lift:rgba(22,28,50,.16)}
html,body{margin:0;width:100%;height:100%;overflow:hidden}
html{background:var(--vdc-win);transition:background-color .3s var(--vdc-ease)}
body{font-family:var(--vdc-font,system-ui);-webkit-user-select:none;user-select:none}
.card{position:fixed;inset:0;border-radius:var(--vdc-r);background:var(--vdc-card);box-shadow:inset 0 0 0 var(--vdc-hair) var(--vdc-border),0 2px 8px var(--vdc-shadow);transition:background-color .3s var(--vdc-ease),box-shadow .3s var(--vdc-ease)}
#vdc-note{position:fixed;left:12px;right:12px;bottom:140px;z-index:20;box-sizing:border-box;display:flex;align-items:flex-start;gap:10px;padding:12px 8px 12px 14px;border-radius:8px;background:var(--vdc-card);color:var(--vdc-text);box-shadow:inset 0 0 0 var(--vdc-hair) var(--vdc-border),0 6px 20px var(--vdc-lift);font:14px/20px var(--vdc-font,system-ui);opacity:0;transform:translateY(6px);visibility:hidden;pointer-events:none;transition:opacity .12s cubic-bezier(.32,0,.67,0),transform .12s cubic-bezier(.32,0,.67,0),visibility 0s linear .12s,background-color .3s var(--vdc-ease),color .3s var(--vdc-ease),box-shadow .3s var(--vdc-ease)}
:root:not([data-vdc-away]) #vdc-note.on{opacity:1;transform:none;visibility:visible;pointer-events:auto;transition:opacity .28s cubic-bezier(.18,1,.56,1),transform .28s cubic-bezier(.18,1,.56,1),visibility 0s,background-color .3s var(--vdc-ease),color .3s var(--vdc-ease),box-shadow .3s var(--vdc-ease)}
#vdc-note .i{flex:none;display:flex;align-items:center;height:20px;color:var(--vdc-warn);transition:color .3s var(--vdc-ease)}
#vdc-note .b{flex:1;min-width:0}
#vdc-note .t{margin:0;-webkit-user-select:text;user-select:text}
#vdc-note .a{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:8px;margin-top:10px}
#vdc-note button{font:inherit;border:0;margin:0;cursor:pointer;outline:none;transition:background-color .16s var(--vdc-ease),color .16s var(--vdc-ease),box-shadow .16s var(--vdc-ease)}
#vdc-note .btn{height:30px;padding:0 10px;border-radius:6px;background:var(--vdc-ctl);color:var(--vdc-text);white-space:nowrap}
#vdc-note .btn:hover{background:var(--vdc-ctl-h)}
#vdc-note .btn:active{background:var(--vdc-ctl-a)}
#vdc-note .btn.pri{background:var(--vdc-acc);color:#fff}
#vdc-note .btn.pri:hover{background:var(--vdc-acc-h)}
#vdc-note .btn.pri:active{background:var(--vdc-acc-a)}
#vdc-note .x{flex:none;display:flex;align-items:center;justify-content:center;width:24px;height:24px;margin-top:-2px;padding:0;border-radius:6px;background:transparent;color:var(--vdc-dim)}
#vdc-note .x:hover{background:var(--vdc-ctl-h);color:var(--vdc-text)}
#vdc-note button:focus-visible{box-shadow:0 0 0 2px var(--vdc-acc)}
@media (prefers-reduced-motion:reduce){html,.card,#vdc-note,:root:not([data-vdc-away]) #vdc-note.on{transition:none}}
)vdc";

// Put into the widget's (closed) shadow root. Its stylesheet is all in cascade layers, so these plain rules win
// without !important. The colours are the program's palette; the panel fills the page edge to edge (no inset, no
// shadow of its own: the page's card is the program's), draws the hairline inside its edge, and fades in and out on
// the program's curves instead of sliding in from the side.
const char kShadowCss[] = R"vdc(
:host{
--assistant-background-gray:light-dark(#FFFFFF,#191C22);
--assistant-background-subtle:light-dark(#F1F3F7,#20232A);
--assistant-background-muted:light-dark(#EEF0F5,#252932);
--assistant-background-disabled:light-dark(#EEF0F5,#20232A);
--assistant-background-emphasis:light-dark(#E4E7EE,#2F343F);
--assistant-background-inverted:light-dark(#1B1E25,#E8EAF0);
--assistant-foreground-gray:light-dark(#1B1E25,#E8EAF0);
--assistant-foreground-subtle:light-dark(#666D7D,#969CAC);
--assistant-foreground-muted:light-dark(#767D8C,#808696);
--assistant-foreground-disabled:light-dark(#D6DAE4,#3A404E);
--assistant-foreground-inverted:light-dark(#FFFFFF,#0F1014);
--assistant-border-gray:light-dark(#DFE2E9,#262A33);
--assistant-border-muted:light-dark(#EEF0F5,#20232A);
--assistant-border-emphasis:light-dark(#D4D8E1,#343945);
--assistant-border-strong:light-dark(#A9AFBC,#4F5666);
--assistant-border-disabled:light-dark(#DFE2E9,#262A33);
--assistant-border-solid:light-dark(#1B1E25,#E8EAF0);
}
.assistant-panel-viewport{padding:0}
[data-mintlify-assistant-panel]{width:100%;max-width:none;height:100%;box-shadow:none;transform:none;opacity:0;transition:opacity .12s cubic-bezier(.32,0,.67,0),background-color .3s cubic-bezier(.25,.6,.4,1),color .3s cubic-bezier(.25,.6,.4,1)}
[data-mintlify-assistant-panel]::after{content:"";position:absolute;inset:0;border:var(--vdc-hair,1px) solid var(--assistant-border-gray);border-radius:inherit;pointer-events:none;z-index:2147483647;transition:border-color .3s cubic-bezier(.25,.6,.4,1)}
:host([data-vdc-shown]) [data-mintlify-assistant-panel]:not([data-starting-style]):not([data-ending-style]){opacity:1;transition:opacity .28s cubic-bezier(.18,1,.56,1),background-color .3s cubic-bezier(.25,.6,.4,1),color .3s cubic-bezier(.25,.6,.4,1)}
:host([data-vdc-theming]) *,:host([data-vdc-theming]) *::before,:host([data-vdc-theming]) *::after{transition-property:background-color,border-color,color,fill,stroke,outline-color;transition-duration:.3s;transition-timing-function:cubic-bezier(.25,.6,.4,1)}
:host([data-vdc-instant]) [data-mintlify-assistant-panel]{transition:none!important}
:host([data-vdc-note]) [data-mintlify-assistant-panel] [role=alert]{display:none}
@media (prefers-reduced-motion:reduce){[data-mintlify-assistant-panel],[data-mintlify-assistant-panel]::after{transition:none!important}}
)vdc";

// The page's script: starts the widget (drawn open, its panel docked over the whole page), keeps its shadow root to
// dress it and to answer its links, and talks with the program (chrome.webview messages). In pieces of less than
// 16 KB: the compiler takes no longer string literal (it joins the pieces).
const char kPageJs[] = R"vdc(
(function () {
  'use strict';
  var C = window.VDC_ASK || {}, doc = document.documentElement, wv = window.chrome && window.chrome.webview;
  if (!wv) return;
  var ORIGIN = 'https://alanbacker.github.io', SITE = ORIGIN + '/VRChat-DLSS5-Cam/', BASE = SITE + 'app-ask/';
  var SRC = 'https://widget.mintlify.com/v1/embed.js', MINT = { en: 1, cn: 1, zh: 1, jp: 1, ja: 1, ko: 1 };
  var api = null, root = null, host = null, inited = false, sentReady = false, sentFail = false, used = null;
  var shown = false, open = true, timer = 0, themeTimer = 0;
  function noop() {}
  function post(o) { try { wv.postMessage(o); } catch (e) { /* the program has let go of the page */ } }
  function log(t) { post({ type: 'log', text: String(t).slice(0, 300) }); }
  function settle(p) { return Promise.resolve(p).catch(function (e) { log('widget: ' + ((e && e.message) || e)); }); }

  /* the hairline is one device pixel, as the program's; the font is the interface's */
  function hair() { doc.style.setProperty('--vdc-hair', (1 / (window.devicePixelRatio || 1)) + 'px'); }
  hair();
  window.addEventListener('resize', hair);
  doc.style.setProperty('--vdc-font', C.font || 'system-ui');
  doc.style.setProperty('--vdc-r', (C.radius || 10) + 'px');

  /* from 768 px the widget docks its panel at the side; here the page is the panel, at any width */
  var mm = window.matchMedia;
  window.matchMedia = function (q) {
    if (String(q).replace(/\s+/g, '') === '(min-width:768px)') {
      return { matches: true, media: String(q), onchange: null, addListener: noop, removeListener: noop,
        addEventListener: noop, removeEventListener: noop, dispatchEvent: function () { return false; } };
    }
    return mm.apply(window, arguments);
  };

  /* the widget's closed shadow root: kept, dressed in the program's look, watched for its panel */
  var attach = Element.prototype.attachShadow;
  Element.prototype.attachShadow = function () {
    var r = attach.apply(this, arguments);
    if (this.localName === 'mintlify-assistant' && !root) {
      host = this;
      root = r;
      dress();
      r.addEventListener('click', onLink, true);
      r.addEventListener('auxclick', onLink, true);
      r.addEventListener('input', function () { if (noteKind) requestAnimationFrame(place); }, true);   // the composer grows
      new MutationObserver(check).observe(r, { childList: true, subtree: true });
    }
    return r;
  };
  function dress() {
    if (!root || root.querySelector('style[data-vdc]')) return;
    var t = document.getElementById('vdc-shadow');
    if (t) root.insertBefore(t.content.cloneNode(true), root.firstChild);
  }
  function panel() { return root ? root.querySelector('[data-mintlify-assistant-panel]') : null; }
  function check() {   // a mutation, not a frame: a hidden page draws no frames
    dress();
    if (inited && !sentReady && !sentFail && panel()) { sentReady = true; clearTimeout(timer); post({ type: 'ready' }); }
  }

  /* A question that could not be answered: a notice above the composer, in the interface's language, with the way
     out (the documentation, the question again, or a new conversation). It waits a moment, since the widget tries
     some failures again by itself; a new question or an answer takes it away. */
  var N = C.notice || {}, note = null, noteKind = '', noteTimer = 0, alertTimer = 0, awaiting = false, lastQ = '', failWith = null;
  var SVG = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" ';
  function kindOf(st) { return st === 403 || st === 429 ? 'busy' : st === 413 ? 'full' : st === 418 ? 'net' : 'off'; }
  function noteEl() {
    if (note) return note;
    note = document.createElement('div');
    note.id = 'vdc-note';
    note.setAttribute('role', 'status');
    note.innerHTML = '<span class="i">' + SVG + 'width="16" height="16"><circle cx="12" cy="12" r="10"/><path d="M12 8v4"/><path d="M12 16h.01"/></svg></span>' +
      '<div class="b"><p class="t"></p><div class="a"></div></div>' +
      '<button type="button" class="x">' + SVG + 'width="14" height="14"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></svg></button>';
    var x = note.querySelector('.x');
    x.setAttribute('aria-label', N.close || 'Close');
    x.addEventListener('click', function () { unnote(true); });
    document.body.appendChild(note);
    return note;
  }
  function button(text, primary, fn) {
    var b = document.createElement('button');
    b.type = 'button';
    b.className = primary ? 'btn pri' : 'btn';
    b.textContent = text;
    b.addEventListener('click', fn);
    return b;
  }
  function place() {   /* just above the composer: the outermost box around its input that is still a small part of the page */
    if (!note) return;
    var h = window.innerHeight, p = panel(), el = root && root.querySelector('[data-mintlify-assistant-panel] textarea'), y = 0;
    for (var i = 0; el && i < 8; i++) {
      var up = el.parentElement;
      if (!up || up === p || up.getBoundingClientRect().height > h * 0.45) break;
      el = up;
    }
    if (el) y = el.getBoundingClientRect().top;
    note.style.bottom = (y > 40 && y < h ? Math.round(h - y + 8) : 140) + 'px';
  }
  function notice(kind, status) {
    clearTimeout(noteTimer);
    awaiting = false;
    var text = N[kind] || N.off;
    if (!text) return;
    noteKind = kind;
    var n = noteEl(), a = n.querySelector('.a');
    n.querySelector('.t').textContent = text;
    a.textContent = '';
    var docs = button(N.docs || 'Documentation', kind === 'off', function () { post({ type: 'docs' }); });
    if (kind === 'full') { a.appendChild(docs); a.appendChild(button(N.fresh || 'New conversation', true, fresh)); }
    else if (kind === 'off') { a.appendChild(button(N.retry || 'Try again', false, retry)); a.appendChild(docs); }
    else { a.appendChild(docs); a.appendChild(button(N.retry || 'Try again', true, retry)); }
    place();
    clearTimeout(alertTimer);
    if (host) host.setAttribute('data-vdc-note', '');
    getComputedStyle(n).opacity;   // drawn hidden first, so the fade runs
    n.classList.add('on');
    log('notice ' + kind + (status ? ' (HTTP ' + status + ')' : ' (not sent)'));
  }
  function soon(kind, status) {
    clearTimeout(noteTimer);
    noteTimer = setTimeout(function () { notice(kind, status); }, 1200);
  }
  function unnote(byUser) {   /* the widget's own alert stays hidden: it would repeat the notice, in English */
    clearTimeout(noteTimer);
    if (!noteKind) return;
    noteKind = '';
    note.classList.remove('on');
    if (byUser) focus();
  }
  function alerts() {   /* the next question or a new conversation: the widget's alerts show again, once its old one has gone */
    clearTimeout(alertTimer);
    alertTimer = setTimeout(function () { if (!noteKind && host) host.removeAttribute('data-vdc-note'); }, 400);
  }
  function retry() {
    unnote(false);
    var b = root && root.querySelector('[data-mintlify-assistant-panel] [role=alert] button');
    if (b) { log('try again: the widget\'s own retry'); b.click(); return; }
    if (lastQ && inited) { log('try again: the question asked again'); settle(api.ask(lastQ, { source: 'app', open: true, focus: false })); return; }
    focus();
  }
  function fresh() {
    unnote(false);
    alerts();
    if (inited) { log('a new conversation'); settle(api.reset()); }
    focus();
  }
  function question(body) {   /* the last question in a request, to ask it again */
    if (typeof body !== 'string') return '';
    try {
      var ms = JSON.parse(body).messages || [];
      for (var i = ms.length - 1; i >= 0; i--) {
        var m = ms[i];
        if (!m || m.role !== 'user') continue;
        if (typeof m.content === 'string' && m.content) return m.content;
        var t = '', ps = m.parts || [];
        for (var j = 0; j < ps.length; j++) if (ps[j] && ps[j].type === 'text') t += ps[j].text || '';
        return t;
      }
    } catch (e) { /* not a request to read */ }
    return '';
  }
  window.addEventListener('resize', function () { if (noteKind) place(); });

  /* answers: the program learns that one came, and how long it is (in test runs, how it starts) */
  var fetch0 = window.fetch;
  window.fetch = function (input, init) {
    var url = typeof input === 'string' ? input : (input && input.url) || '';
    if (String(url).indexOf('/v2/message') < 0) return fetch0.apply(window, arguments);
    var q = question(init && init.body);
    if (q) lastQ = q;
    awaiting = true;
    unnote(false);
    alerts();
    var p;
    if (failWith === null) p = fetch0.apply(window, arguments);
    else {   /* test runs: the failure asked for, and nothing goes to the service */
      log('test: the question met ' + (failWith === 'net' ? 'a network failure' : 'HTTP ' + failWith) + ', nothing was sent');
      p = failWith === 'net' ? Promise.reject(new TypeError('Failed to fetch'))
        : Promise.resolve(new Response('{"error":"test"}', { status: failWith, headers: { 'Content-Type': 'application/json' } }));
    }
    p.then(function (r) {
      if (r.ok) { awaiting = false; unnote(false); } else soon(kindOf(r.status), r.status);
    }, function (e) {
      if (e && e.name === 'AbortError') { awaiting = false; return; }   // stopped by the user
      soon('net', 0);
    });
    p.then(function (r) {
      var c = null;
      try { c = r.clone(); } catch (e) { c = null; }
      if (!c) { post({ type: 'answer', status: r.status, chars: -1 }); return; }
      c.text().then(function (t) {
        var text = '', lines = t.split('\n');
        for (var i = 0; i < lines.length; i++) {
          if (lines[i].indexOf('data:') !== 0) continue;
          try {
            var o = JSON.parse(lines[i].slice(5));
            if (o && o.type === 'text-delta') text += o.delta || o.textDelta || '';
          } catch (e) { /* not a part of the text */ }
        }
        var m = { type: 'answer', status: r.status, chars: text.length };
        if (C.test) m.text = text.slice(0, 800);
        post(m);
      }, function () { post({ type: 'answer', status: r.status, chars: -1 }); });
    }, noop);
    return p;
  };

)vdc" R"vdc(
  /* starting */
  function look() {
    var light = doc.getAttribute('data-theme') === 'light';
    return { theme: light ? 'light' : 'dark', accent: light ? C.accentLight : C.accentDark };
  }
  function config() {
    used = look();
    return {
      id: C.id,
      defaultOpen: true,
      appearance: { variant: 'panel', theme: used.theme, accent: used.accent, radius: (C.radius || 10) + 'px', font: C.font,
        logo: BASE + 'mark.svg', zIndex: 10, dismissOnInteractOutside: false },
      labels: C.labels,
      starterQuestions: C.questions,
      filter: { language: C.mint },
      analytics: { capturePathname: false },
      hooks: { event: onEvent, error: function (e) {
        log('widget error: ' + ((e && (e.message || e.code || e.type)) || e) + (e && e.status ? ' (HTTP ' + e.status + ')' : ''));
        if (awaiting && e && typeof e.status === 'number' && e.status >= 400) notice(kindOf(e.status), e.status);   // the widget's last word on it
      } }
    };
  }
  function load() {
    return new Promise(function (resolve, reject) {
      var s = document.createElement('script');
      s.type = 'module';
      s.src = C.breakLoad ? SRC + '.missing' : SRC;
      s.onload = resolve;
      s.onerror = function () { reject(new Error('the widget script could not be loaded')); };
      document.head.appendChild(s);
    }).then(function () {
      api = window.MintlifyAssistant;
      if (!api || !api.init) throw new Error('the widget script has no init');
    });
  }
  function start() {
    timer = setTimeout(function () { fail('timed out'); }, 20000);
    load().then(function () { return api.init(config()); }).then(function () {
      inited = true;
      var l = look();
      if (used && l.theme !== used.theme) settle(api.update({ appearance: l }));   // the theme changed while it started
      check();
    }).catch(function (e) { fail((e && e.message) || 'error'); });
  }
  function fail(why) {
    if (sentReady || sentFail) return;
    sentFail = true;
    clearTimeout(timer);
    post({ type: 'failed', why: String(why).slice(0, 200) });
  }
  function onEvent(e) {
    if (!e) return;
    if (e.type === 'open' || e.type === 'ask') open = true;
    else if (e.type === 'close' || e.type === 'destroy') {
      var was = open;
      open = false;
      if (was && shown) post({ type: 'close' });   // the widget's own close control closes the program's panel
    }
  }

  /* the program's messages */
  wv.addEventListener('message', function (ev) {
    var m = ev.data || {};
    if (m.type === 'show') show(m);
    else if (m.type === 'hide') hide();
    else if (m.type === 'focus') focus();
    else if (m.type === 'look') theme(!!m.light);
    else if (m.type === 'ask') { if (inited) settle(api.ask(String(m.q || ''), { source: 'app', open: true, focus: false })); }
    else if (m.type === 'test') test(String(m.what || ''));
  });
  function frames(fn) { requestAnimationFrame(function () { requestAnimationFrame(fn); }); }
  function show(m) {
    shown = true;
    doc.removeAttribute('data-vdc-away');
    if (host) {
      if (m.instant) {
        host.setAttribute('data-vdc-instant', '');
        host.setAttribute('data-vdc-shown', '');
        frames(function () { host.removeAttribute('data-vdc-instant'); });
      } else {
        /* from nothing, whatever the page showed when it was last drawn (a hidden page runs no transitions) */
        host.setAttribute('data-vdc-instant', '');
        host.removeAttribute('data-vdc-shown');
        if (panel()) getComputedStyle(panel()).opacity;
        host.removeAttribute('data-vdc-instant');
        frames(function () { if (shown) host.setAttribute('data-vdc-shown', ''); });   // drawn hidden once, then the fade
      }
    }
    if (inited && !open) settle(api.open({ source: 'app', focus: !!m.focus }));
    else if (m.focus) focus();
  }
  function hide() {
    shown = false;
    if (host) host.removeAttribute('data-vdc-shown');
    doc.setAttribute('data-vdc-away', '');   // the notice fades with the panel
  }
  function focus() {
    var t = root && root.querySelector('[data-mintlify-assistant-panel] textarea, [data-mintlify-assistant-panel] input:not([type=hidden]):not([type=file])');
    if (!t) return;
    try { t.focus({ preventScroll: true }); } catch (e) { t.focus(); }
  }
  function theme(light) {
    doc.setAttribute('data-theme', light ? 'light' : 'dark');
    if (host) {
      host.setAttribute('data-vdc-theming', '');
      clearTimeout(themeTimer);
      themeTimer = setTimeout(function () { host.removeAttribute('data-vdc-theming'); }, 450);
    }
    if (inited) settle(api.update({ appearance: look() }));
  }

  /* links: never in this page. A page of the documentation opens in the interface's language, anything else as it is */
  var PAGES = C.pages || [];
  function ours(href) {
    var u;
    try { u = new URL(href, SITE); } catch (e) { return null; }
    if (u.origin !== ORIGIN) return null;
    var home = '/VRChat-DLSS5-Cam/', p = u.pathname, mine = p.indexOf(home) === 0;
    var seg = (mine ? p.slice(home.length) : p).replace(/\.(mdx?|html)$/, '').split('/').filter(Boolean);
    if (seg.length && MINT[seg[0]]) seg.shift();
    else if (!mine && !(seg.length === 1 && PAGES.indexOf(seg[0]) >= 0)) return null;
    var slug = seg[0] || 'index';
    if (seg.length > 1 || PAGES.indexOf(slug) < 0) {
      if (mine && /\.[a-z0-9]+$/i.test(p) && !/\.(mdx?|html)$/i.test(p)) return u.href;   // a file of the site
      slug = 'index';
    }
    return SITE + (C.dir || '') + (slug === 'index' ? '' : slug + '.html') + u.hash;
  }
  function link(href) { if (href) post({ type: 'link', url: ours(href) || String(href) }); }
  function onLink(e) {
    if (e.defaultPrevented || (e.type === 'click' ? e.button !== 0 : e.button !== 1)) return;
    var a = e.target && e.target.closest ? e.target.closest('a[href]') : null;
    if (!a || (a.getAttribute('href') || '').charAt(0) === '#') return;   // a footnote: the widget scrolls its own panel
    e.preventDefault();
    e.stopPropagation();
    link(a.href);
  }
  window.addEventListener('mintlify-assistant:navigate', function (e) {
    var d = e.detail || {};
    e.preventDefault();
    link(d.url || d.path || '');
  }, true);

  /* Escape closes the program's panel (not while an input method is composing, not over a menu of the widget's) */
  window.addEventListener('keydown', function (e) {
    if (e.key !== 'Escape' || e.isComposing || e.keyCode === 229) return;
    if (root && root.querySelector('[role="menu"]')) return;
    e.preventDefault();
    e.stopPropagation();
    if (noteKind) { unnote(true); return; }   // the notice first, the panel with the next press
    post({ type: 'escape' });
  }, true);

  /* test runs */
  function test(what) {
    if (what === 'close') {
      var b = root && root.querySelector('[data-mintlify-assistant-panel] [aria-keyshortcuts="Escape"]');
      log('test close: ' + (b ? 'the close control' : 'no close control, closed by the API'));
      if (b) b.click(); else if (api) settle(api.close());
    } else if (what === 'link') {
      var as = root ? root.querySelectorAll('[data-mintlify-assistant-panel] a[href]') : [], a = null;
      for (var i = 0; i < as.length && !a; i++) if ((as[i].getAttribute('href') || '').charAt(0) !== '#') a = as[i];
      log('test link: ' + (a ? a.getAttribute('href') : 'no link in the panel'));
      if (a) a.click();
    } else if (what.indexOf('fail:') === 0) {
      var v = what.slice(5);
      failWith = v === 'off' ? null : v === 'net' ? 'net' : Math.min(599, Math.max(400, parseInt(v, 10) || 503));
      log('test fail: ' + (failWith === null ? 'off, questions go to the service again' : failWith));
    } else if (what.indexOf('note:') === 0) {   /* a notice's button pressed: docs, retry, fresh or close */
      var want = what.slice(5), nb = null;
      if (note && noteKind) nb = want === 'close' ? note.querySelector('.x')
        : [].filter.call(note.querySelectorAll('.a button'), function (x) { return x.textContent === N[want]; })[0] || null;
      log('test note: ' + (nb ? want : 'no ' + want + ' button on the notice'));
      if (nb) nb.click();
    }
  }

  function begin() { post({ type: 'hello' }); start(); }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', begin, { once: true }); else begin();
})();
)vdc";

// The program's mark (the documentation site's assets/mark.svg), the logo in the panel's header.
const char kMarkSvg[] = R"vdc(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" role="img" aria-label="VRChat DLSS5 Cam"><defs><clipPath id="l32"><rect x="0" y="0" width="16" height="32"/></clipPath><clipPath id="r32"><rect x="17" y="0" width="15" height="32"/></clipPath></defs><path d="M7.52 0L24.48 0C29.8 0 32 2.2 32 7.52L32 24.48C32 29.8 29.8 32 24.48 32L7.52 32C2.2 32 0 29.8 0 24.48L0 7.52C0 2.2 2.2 0 7.52 0Z" fill="#121526"/><g clip-path="url(#l32)"><polygon points="16.5,4.8 10.5,10.99 22.27,10.99" fill="#F9FAFF" stroke="#F9FAFF" stroke-width="0.3" stroke-linejoin="round"/><polygon points="16.5,4.8 22.27,10.99 26.07,10.99" fill="#FFFFFF" stroke="#FFFFFF" stroke-width="0.3" stroke-linejoin="round"/><polygon points="16.5,4.8 7.02,10.99 10.5,10.99" fill="#7581E3" stroke="#7581E3" stroke-width="0.3" stroke-linejoin="round"/><polygon points="26.07,10.99 22.27,10.99 25.98,21.01" fill="#F4F5FF" stroke="#F4F5FF" stroke-width="0.3" stroke-linejoin="round"/><polygon points="22.27,10.99 10.5,10.99 16.36,21.01" fill="#CED4FF" stroke="#CED4FF" stroke-width="0.3" stroke-linejoin="round"/><polygon points="10.5,10.99 7.02,10.99 6.93,21.01" fill="#3A46B4" stroke="#3A46B4" stroke-width="0.3" stroke-linejoin="round"/><polygon points="16.5,27.2 25.98,21.01 16.36,21.01" fill="#3A46B4" stroke="#3A46B4" stroke-width="0.3" stroke-linejoin="round"/><polygon points="16.5,27.2 16.36,21.01 6.93,21.01" fill="#3A46B4" stroke="#3A46B4" stroke-width="0.3" stroke-linejoin="round"/><polygon points="16.36,21.01 25.98,21.01 22.27,10.99" fill="#CBD1FF" stroke="#CBD1FF" stroke-width="0.3" stroke-linejoin="round"/><polygon points="6.93,21.01 16.36,21.01 10.5,10.99" fill="#3C48B6" stroke="#3C48B6" stroke-width="0.3" stroke-linejoin="round"/></g><g clip-path="url(#r32)"><circle cx="16.5" cy="16" r="11.2" fill="#3A46B4"/><path d="M25.85 22.06L26.05 21.81L26.23 21.54L26.39 21.25L26.54 20.95L26.69 20.65L26.83 20.33L26.96 20L27.09 19.65L27.21 19.29L27.31 18.92L27.41 18.52L27.5 18.11L27.57 17.68L27.63 17.22L27.68 16.75L27.7 16.25L27.7 15.72L27.67 15.17L27.61 14.6L27.52 14L27.39 13.38L27.22 12.74L27 12.09L26.72 11.42L26.4 10.75L26.01 10.09L25.57 9.43L25.08 8.8L24.53 8.19L23.93 7.62L23.3 7.1L22.63 6.63L21.94 6.21L21.24 5.85L20.53 5.55L19.82 5.3L19.13 5.11L18.45 4.97L17.8 4.88L17.17 4.82L16.56 4.8L15.99 4.81L15.44 4.85L14.92 4.91L14.43 4.99L13.96 5.09L13.51 5.21L13.09 5.33L12.69 5.47L12.32 5.61L11.96 5.76L11.61 5.92L11.28 6.09L10.97 6.26L10.67 6.44L10.38 6.62L10.11 6.8L9.84 7L9.59 7.21L9.37 7.44L9.16 7.69L8.98 7.96L8.83 8.25L8.7 8.56L8.59 8.88L8.51 9.22L8.45 9.58L8.41 9.95L8.41 10.33L8.42 10.73L8.47 11.13L8.54 11.55L8.63 11.97L8.74 12.4L8.89 12.84L9.05 13.29L9.24 13.74L9.45 14.19L9.69 14.64L9.94 15.09L10.22 15.54L10.52 15.99L10.83 16.44L11.17 16.88L11.52 17.32L11.89 17.74L12.28 18.16L12.68 18.58L13.09 18.98L13.52 19.36L13.95 19.74L14.4 20.1L14.86 20.45L15.32 20.78L15.79 21.1L16.26 21.4L16.74 21.68L17.22 21.94L17.7 22.18L18.18 22.4L18.66 22.6L19.14 22.78L19.61 22.94L20.08 23.07L20.54 23.18L20.99 23.27L21.44 23.34L21.87 23.38L22.29 23.4L22.7 23.39L23.09 23.36L23.47 23.31L23.84 23.23L24.18 23.13L24.51 23.01L24.82 22.86L25.11 22.69L25.38 22.5L25.62 22.29Z" fill="#97A3FF"/><path d="M26.79 16.45L26.94 16.27L27.07 16.07L27.19 15.86L27.28 15.64L27.36 15.4L27.42 15.15L27.46 14.9L27.49 14.63L27.49 14.35L27.48 14.06L27.45 13.77L27.4 13.47L27.33 13.16L27.25 12.84L27.15 12.52L27.04 12.2L26.91 11.87L26.77 11.53L26.62 11.2L26.45 10.86L26.26 10.51L26.07 10.17L25.85 9.84L25.62 9.5L25.37 9.17L25.11 8.84L24.84 8.52L24.55 8.21L24.25 7.91L23.93 7.62L23.61 7.34L23.27 7.08L22.93 6.83L22.58 6.59L22.23 6.37L21.87 6.17L21.51 5.98L21.14 5.81L20.78 5.65L20.42 5.51L20.06 5.38L19.71 5.27L19.36 5.17L19.02 5.09L18.68 5.01L18.35 4.95L18.03 4.91L17.72 4.88L17.41 4.86L17.11 4.87L16.83 4.89L16.55 4.93L16.29 4.98L16.04 5.06L15.8 5.15L15.58 5.25L15.37 5.37L15.17 5.51L14.99 5.66L14.83 5.83L14.68 6.02L14.55 6.21L14.44 6.42L14.34 6.65L14.26 6.88L14.2 7.13L14.16 7.39L14.14 7.65L14.13 7.93L14.14 8.22L14.17 8.51L14.22 8.82L14.29 9.12L14.38 9.44L14.48 9.76L14.6 10.08L14.74 10.4L14.89 10.73L15.06 11.06L15.25 11.39L15.45 11.72L15.66 12.04L15.89 12.37L16.14 12.69L16.39 13L16.66 13.31L16.94 13.62L17.23 13.92L17.53 14.21L17.84 14.49L18.16 14.76L18.48 15.03L18.81 15.28L19.15 15.52L19.49 15.75L19.83 15.97L20.18 16.17L20.53 16.36L20.88 16.53L21.23 16.7L21.58 16.84L21.92 16.97L22.27 17.08L22.61 17.18L22.94 17.26L23.27 17.33L23.59 17.37L23.9 17.4L24.21 17.42L24.51 17.41L24.79 17.39L25.07 17.35L25.33 17.3L25.58 17.22L25.82 17.13L26.05 17.03L26.26 16.91L26.45 16.77L26.63 16.62Z" fill="#FFFFFF"/></g></svg>)vdc";

} // namespace

// ------------------------------------------------------------------------------------------------ addresses
bool AskPanel::RuntimeVersion(std::string& version) {
    LPWSTR v = nullptr;
    const HRESULT hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &v);
    version = TakeString(v);
    return SUCCEEDED(hr) && !version.empty();
}

std::string AskPanel::PageUrl() { return kPageBase; }

bool AskPanel::IsPageUrl(const std::string& url) { return url.rfind(kPageBase, 0) == 0; }

bool AskPanel::DecodePng(const std::vector<uint8_t>& png, std::vector<uint8_t>& rgba, int& width, int& height) {
    if (png.empty()) return false;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(png.data(), (UINT)png.size()));
    if (!stream.Get()) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapSource> conv;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) return false;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;
    if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA, frame.Get(), &conv))) return false;
    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || !w || !h) return false;
    rgba.resize((size_t)w * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)rgba.size(), rgba.data()))) return false;
    width = (int)w;
    height = (int)h;
    return true;
}

// ------------------------------------------------------------------------------------------------ creation
bool AskPanel::Start(HWND parent, const std::wstring& userDataFolder, const AskPageConfig& cfg, uint32_t background,
                     std::string& error) {
    if (m_state == State::Creating || m_state == State::Loading || m_state == State::Ready) {
        Configure(cfg, background);
        return true;
    }
    Destroy();
    m_parent = parent;
    m_folder = userDataFolder;
    m_cfg = cfg;
    m_background = background;
    CreateDirectories(userDataFolder);
    const unsigned gen = m_generation;
    auto done = MakeHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
        [this, gen](HRESULT hr, ICoreWebView2Environment* env) { OnEnvironment(hr, env, gen); return S_OK; });
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr, done.Get());
    if (FAILED(hr)) {
        error = FormatHr(hr);
        m_state = State::Failed;
        return false;
    }
    m_state = State::Creating;
    m_since = NowSeconds();
    return true;
}

void AskPanel::OnEnvironment(HRESULT hr, ICoreWebView2Environment* env, unsigned gen) {
    if (gen != m_generation) return;
    if (FAILED(hr) || !env) { Fail(true, "the environment: " + FormatHr(hr)); return; }
    env->AddRef();
    m_env = env;
    auto done = MakeHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
        [this, gen](HRESULT hr2, ICoreWebView2Controller* c) { OnController(hr2, c, gen); return S_OK; });
    hr = env->CreateCoreWebView2Controller(m_parent, done.Get());
    if (FAILED(hr)) Fail(true, "the control: " + FormatHr(hr));
}

void AskPanel::OnController(HRESULT hr, ICoreWebView2Controller* controller, unsigned gen) {
    if (gen != m_generation) return;
    if (FAILED(hr) || !controller) { Fail(true, "the control: " + FormatHr(hr)); return; }
    controller->AddRef();
    m_controller = controller;
    controller->put_IsVisible(FALSE);
    if (FAILED(controller->get_CoreWebView2(&m_view)) || !m_view) { Fail(true, "the control has no web view"); return; }
    if (m_bounds.right > m_bounds.left && m_bounds.bottom > m_bounds.top) controller->put_Bounds(m_bounds);
    ApplyBackground();

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(m_view->get_Settings(settings.GetAddressOf())) && settings.Get()) {
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        ComPtr<ICoreWebView2Settings3> s3 = Query<ICoreWebView2Settings3>(settings.Get(), IID_ICoreWebView2Settings3);
        if (s3.Get()) s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);   // no find bar, printing, reloading or zoom keys
        ComPtr<ICoreWebView2Settings4> s4 = Query<ICoreWebView2Settings4>(settings.Get(), IID_ICoreWebView2Settings4);
        if (s4.Get()) { s4->put_IsPasswordAutosaveEnabled(FALSE); s4->put_IsGeneralAutofillEnabled(FALSE); }
        ComPtr<ICoreWebView2Settings5> s5 = Query<ICoreWebView2Settings5>(settings.Get(), IID_ICoreWebView2Settings5);
        if (s5.Get()) s5->put_IsPinchZoomEnabled(FALSE);
        ComPtr<ICoreWebView2Settings6> s6 = Query<ICoreWebView2Settings6>(settings.Get(), IID_ICoreWebView2Settings6);
        if (s6.Get()) s6->put_IsSwipeNavigationEnabled(FALSE);
    }
    EventRegistrationToken token{};
    ComPtr<ICoreWebView2_11> v11 = Query<ICoreWebView2_11>(m_view, IID_ICoreWebView2_11);
    if (v11.Get()) {
        v11->add_ContextMenuRequested(MakeHandler<ICoreWebView2ContextMenuRequestedEventHandler>(
            IID_ICoreWebView2ContextMenuRequestedEventHandler,
            [](ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs* args) { FilterMenu(args); return S_OK; }).Get(), &token);
    } else if (settings.Get()) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
    }

    // The page and its mark come from here; the request never leaves the computer.
    m_view->AddWebResourceRequestedFilter((Utf8ToWide(kPageBase) + L"*").c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    m_view->add_WebResourceRequested(MakeHandler<ICoreWebView2WebResourceRequestedEventHandler>(
        IID_ICoreWebView2WebResourceRequestedEventHandler,
        [this, gen](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) {
            if (gen == m_generation) Serve(args);
            return S_OK;
        }).Get(), &token);

    // The control never leaves the page: a link the page did not catch opens in the default browser.
    m_view->add_NavigationStarting(MakeHandler<ICoreWebView2NavigationStartingEventHandler>(
        IID_ICoreWebView2NavigationStartingEventHandler,
        [this, gen](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
            if (gen != m_generation) return S_OK;
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            const std::string url = TakeString(uri);
            if (IsPageUrl(url)) return S_OK;
            args->put_Cancel(TRUE);
            BOOL user = FALSE;
            args->get_IsUserInitiated(&user);
            if (user && IsWebUrl(url)) Push(Event::Link, url);
            else Push(Event::Log, "a navigation away from the page was stopped: " + Shorten(url));
            return S_OK;
        }).Get(), &token);
    m_view->add_NavigationCompleted(MakeHandler<ICoreWebView2NavigationCompletedEventHandler>(
        IID_ICoreWebView2NavigationCompletedEventHandler,
        [this, gen](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) {
            if (gen != m_generation) return S_OK;
            BOOL ok = FALSE;
            args->get_IsSuccess(&ok);
            if (ok) return S_OK;
            COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
            args->get_WebErrorStatus(&status);
            if (status != COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED && m_state == State::Loading)
                Fail(false, StrPrintf("the page did not open (web error %d)", (int)status));
            return S_OK;
        }).Get(), &token);
    m_view->add_NewWindowRequested(MakeHandler<ICoreWebView2NewWindowRequestedEventHandler>(
        IID_ICoreWebView2NewWindowRequestedEventHandler,
        [this, gen](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
            args->put_Handled(TRUE);   // no window of its own: the default browser
            if (gen != m_generation) return S_OK;
            LPWSTR uri = nullptr;
            args->get_Uri(&uri);
            const std::string url = TakeString(uri);
            BOOL user = FALSE;
            args->get_IsUserInitiated(&user);
            if (user && IsWebUrl(url)) Push(Event::Link, url);
            else Push(Event::Log, "a window the page asked for was not opened: " + Shorten(url));
            return S_OK;
        }).Get(), &token);
    m_view->add_WebMessageReceived(MakeHandler<ICoreWebView2WebMessageReceivedEventHandler>(
        IID_ICoreWebView2WebMessageReceivedEventHandler,
        [this, gen](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
            if (gen != m_generation) return S_OK;
            LPWSTR source = nullptr;
            args->get_Source(&source);
            if (!IsPageUrl(TakeString(source))) return S_OK;
            LPWSTR json = nullptr;
            if (SUCCEEDED(args->get_WebMessageAsJson(&json))) OnMessage(TakeString(json));
            return S_OK;
        }).Get(), &token);
    m_view->add_ProcessFailed(MakeHandler<ICoreWebView2ProcessFailedEventHandler>(
        IID_ICoreWebView2ProcessFailedEventHandler,
        [this, gen](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) {
            if (gen != m_generation) return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
            args->get_ProcessFailedKind(&kind);
            if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) m_lost = true;
            else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                     kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) m_reload = true;
            return S_OK;
        }).Get(), &token);

    // F11 switches the program's full screen from the page too (the page itself asks for no keys of the browser's).
    controller->add_AcceleratorKeyPressed(MakeHandler<ICoreWebView2AcceleratorKeyPressedEventHandler>(
        IID_ICoreWebView2AcceleratorKeyPressedEventHandler,
        [this, gen](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
            if (gen != m_generation) return S_OK;
            COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP;
            args->get_KeyEventKind(&kind);
            if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) return S_OK;
            UINT vk = 0;
            args->get_VirtualKey(&vk);
            COREWEBVIEW2_PHYSICAL_KEY_STATUS status{};
            args->get_PhysicalKeyStatus(&status);
            if (vk == VK_F11) {
                args->put_Handled(TRUE);
                if (!status.WasKeyDown) Push(Event::Fullscreen);
            }
            return S_OK;
        }).Get(), &token);
    controller->add_GotFocus(MakeHandler<ICoreWebView2FocusChangedEventHandler>(
        IID_ICoreWebView2FocusChangedEventHandler,
        [this, gen](ICoreWebView2Controller*, IUnknown*) { if (gen == m_generation) m_focused = true; return S_OK; }).Get(), &token);
    controller->add_LostFocus(MakeHandler<ICoreWebView2FocusChangedEventHandler>(
        IID_ICoreWebView2FocusChangedEventHandler,
        [this, gen](ICoreWebView2Controller*, IUnknown*) { if (gen == m_generation) m_focused = false; return S_OK; }).Get(), &token);

    Navigate();
}

void AskPanel::Serve(ICoreWebView2WebResourceRequestedEventArgs* args) {
    ComPtr<ICoreWebView2WebResourceRequest> request;
    if (!m_env || FAILED(args->get_Request(request.GetAddressOf())) || !request.Get()) return;
    LPWSTR uri = nullptr;
    request->get_Uri(&uri);
    const std::string url = TakeString(uri);
    std::string path = IsPageUrl(url) ? url.substr(sizeof(kPageBase) - 1) : std::string("?");
    path = path.substr(0, path.find_first_of("?#"));
    std::string body, type = "text/plain; charset=utf-8";
    int status = 200;
    if (path.empty() || path == "index.html") { body = PageHtml(); type = "text/html; charset=utf-8"; }
    else if (path == "mark.svg") { body = kMarkSvg; type = "image/svg+xml"; }
    else { status = 404; body = "Not found"; }
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(body.data()), (UINT)body.size()));
    if (!stream.Get()) return;
    const std::wstring headers = L"Content-Type: " + Utf8ToWide(type) + L"\r\nCache-Control: no-store";
    ComPtr<ICoreWebView2WebResourceResponse> response;
    if (SUCCEEDED(m_env->CreateWebResourceResponse(stream.Get(), status, status == 200 ? L"OK" : L"Not Found",
                                                   headers.c_str(), response.GetAddressOf())) && response.Get())
        args->put_Response(response.Get());
}

void AskPanel::Navigate() {
    if (!m_view) return;
    if (m_visible) Hide();
    m_pageAlive = false;
    m_queued.clear();
    m_state = State::Loading;
    m_since = NowSeconds();
    const HRESULT hr = m_view->Navigate(Utf8ToWide(PageUrl()).c_str());
    if (FAILED(hr)) Fail(false, "the page did not open: " + FormatHr(hr));
}

void AskPanel::Fail(bool creating, const std::string& why) {
    m_state = State::Failed;
    if (m_visible) Hide();
    Push(creating ? Event::CreateFailed : Event::LoadFailed, why);
}

void AskPanel::Retry() {
    if (m_controller && m_view) { Navigate(); return; }
    const HWND parent = m_parent;
    const std::wstring folder = m_folder;
    const AskPageConfig cfg = m_cfg;
    std::string error;
    if (!Start(parent, folder, cfg, m_background, error)) Push(Event::CreateFailed, error);
}

// ------------------------------------------------------------------------------------------------ the page
std::string AskPanel::ConfigJson() const {
    Json labels = Json::Obj();
    labels.Set("title", m_cfg.title).Set("trigger", m_cfg.trigger)   // the keys the widget accepts (it refuses others)
          .Set("placeholder", m_cfg.placeholder).Set("disclaimer", m_cfg.disclaimer).Set("suggestions", m_cfg.suggestions);
    Json questions = Json::Arr();
    for (const std::string& q : m_cfg.questions) questions.Push(q);
    Json pages = Json::Arr();
    for (const char* p : kPages) pages.Push(p);
    Json notice = Json::Obj();
    notice.Set("off", m_cfg.noticeOff).Set("busy", m_cfg.noticeBusy).Set("net", m_cfg.noticeNet).Set("full", m_cfg.noticeFull)
          .Set("docs", m_cfg.openDocs).Set("retry", m_cfg.retry).Set("fresh", m_cfg.newChat).Set("close", m_cfg.close);
    Json c = Json::Obj();
    c.Set("id", kWidgetId).Set("mint", m_cfg.mintLang).Set("dir", m_cfg.dir).Set("font", m_cfg.font)
     .Set("radius", (double)m_cfg.radius).Set("labels", std::move(labels)).Set("questions", std::move(questions))
     .Set("pages", std::move(pages)).Set("notice", std::move(notice)).Set("accentDark", "#5C6CF5").Set("accentLight", "#4658E6")
     .Set("test", m_cfg.test)
     .Set("breakLoad", m_cfg.breakLoad);
    // inside a <script> element: no "</" (it would end the element) and no "<!--"
    return ReplaceAll(ReplaceAll(c.Dump(), "</", "<\\/"), "<!--", "<\\!--");
}

std::string AskPanel::PageHtml() const {
    std::string h;
    h.reserve(sizeof(kPageCss) + sizeof(kShadowCss) + sizeof(kPageJs) + 2048);
    h += "<!doctype html><html lang=\"";
    h += HtmlText(m_cfg.htmlLang);
    h += "\" data-theme=\"";
    h += m_cfg.light ? "light" : "dark";
    h += "\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>";
    h += HtmlText(m_cfg.title);
    h += "</title><style>";
    h += kPageCss;
    h += "</style><template id=\"vdc-shadow\"><style data-vdc>";
    h += kShadowCss;
    h += "</style></template><script>window.VDC_ASK=";
    h += ConfigJson();
    h += ";</script><script>";
    h += kPageJs;
    h += "</script></head><body><div class=\"card\"></div></body></html>";
    return h;
}

void AskPanel::OnMessage(const std::string& text) {
    Json m;
    if (!JsonReader::Parse(text, m)) return;
    const std::string type = m.Str("type");
    if (type == "hello") {
        m_pageAlive = true;
        std::vector<std::string> queued;
        queued.swap(m_queued);
        for (const std::string& q : queued) Post(q);
    } else if (type == "ready") {
        if (m_state == State::Loading) { m_state = State::Ready; Push(Event::Ready, StrPrintf("%.1f s", NowSeconds() - m_since)); }
    } else if (type == "failed") {
        if (m_state == State::Loading) Fail(false, m.Str("why"));
    } else if (type == "close") {
        Push(Event::Close);
    } else if (type == "escape") {
        Push(Event::Escape);
    } else if (type == "docs") {
        Push(Event::Docs);
    } else if (type == "link") {
        const std::string url = m.Str("url");
        if (IsWebUrl(url)) Push(Event::Link, url);
    } else if (type == "answer") {
        std::string s = StrPrintf("HTTP %d, %d characters", m.Int("status"), m.Int("chars"));
        if (m.Has("text")) s += ": " + m.Str("text");
        Push(Event::Answered, s);
    } else if (type == "log") {
        Push(Event::Log, Shorten(m.Str("text"), 300));
    }
}

void AskPanel::Post(const std::string& json) {
    if (!m_view) return;
    if (!m_pageAlive) { m_queued.push_back(json); return; }
    m_view->PostWebMessageAsJson(Utf8ToWide(json).c_str());
}

void AskPanel::ApplyBackground() {
    ComPtr<ICoreWebView2Controller2> c2 = Query<ICoreWebView2Controller2>(m_controller, IID_ICoreWebView2Controller2);
    if (!c2.Get()) return;
    COREWEBVIEW2_COLOR color;
    color.A = 255;
    color.R = (BYTE)((m_background >> 16) & 0xFF);
    color.G = (BYTE)((m_background >> 8) & 0xFF);
    color.B = (BYTE)(m_background & 0xFF);
    c2->put_DefaultBackgroundColor(color);
}

// ------------------------------------------------------------------------------------------------ the program's side
void AskPanel::Configure(const AskPageConfig& cfg, uint32_t background) {
    const bool language = cfg.mintLang != m_cfg.mintLang || cfg.title != m_cfg.title || cfg.font != m_cfg.font;
    const bool look = cfg.light != m_cfg.light;
    m_cfg = cfg;
    if (background != m_background) { m_background = background; ApplyBackground(); }
    if (!m_view) return;
    if (language) { Navigate(); return; }   // the page in the new language (the conversation starts again)
    if (look) Post(Json::Obj().Set("type", "look").Set("light", cfg.light).Dump());
}

void AskPanel::SetBounds(const RECT& r) {
    if (EqualRect(&r, &m_bounds)) return;
    m_bounds = r;
    if (m_controller) m_controller->put_Bounds(r);
}

void AskPanel::Show(bool focus, bool instant) {
    if (!m_controller || m_state != State::Ready) return;
    if (!m_visible) { m_controller->put_IsVisible(TRUE); m_visible = true; }
    Post(Json::Obj().Set("type", "show").Set("focus", focus).Set("instant", instant).Dump());
    if (focus) m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void AskPanel::BeginHide() {
    if (m_view && m_pageAlive) Post("{\"type\":\"hide\"}");
}

void AskPanel::FocusBack() {
    if (m_parent && IsWindow(m_parent)) {
        const HWND f = GetFocus();
        if (m_focused || (f && f != m_parent && IsChild(m_parent, f))) SetFocus(m_parent);
    }
    m_focused = false;
}

void AskPanel::Hide() {
    if (!m_controller) return;
    FocusBack();
    if (m_visible) { m_controller->put_IsVisible(FALSE); m_visible = false; }
}

void AskPanel::Focus() {
    if (!m_controller || !m_visible) return;
    m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    Post("{\"type\":\"focus\"}");
}

void AskPanel::Ask(const std::string& question) { Post(Json::Obj().Set("type", "ask").Set("q", question).Dump()); }

void AskPanel::Test(const char* what) { Post(Json::Obj().Set("type", "test").Set("what", what ? what : "").Dump()); }

bool AskPanel::SendKey(UINT vk) {
    if (!m_parent || !m_controller) return false;
    // The page's keyboard window: the render widget's where there is one, else the browser's widget window (newer
    // runtimes host the page without a render widget window of its own).
    struct Found { HWND render = nullptr, widget = nullptr; std::string classes; } found;
    EnumChildWindows(m_parent, [](HWND h, LPARAM lp) -> BOOL {
        Found* f = reinterpret_cast<Found*>(lp);
        wchar_t cls[128] = {};
        GetClassNameW(h, cls, 127);
        if (!f->classes.empty()) f->classes += ", ";
        f->classes += WideToUtf8(cls);
        if (!f->render && wcscmp(cls, L"Chrome_RenderWidgetHostHWND") == 0) f->render = h;
        if (!f->widget && wcscmp(cls, L"Chrome_WidgetWin_1") == 0) f->widget = h;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    Push(Event::Log, "the control's windows: " + (found.classes.empty() ? std::string("none") : found.classes));
    const HWND hit = found.render ? found.render : found.widget;
    if (!hit) return false;
    m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    found.render = hit;
    const LPARAM scan = (LPARAM)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16;
    PostMessageW(found.render, WM_KEYDOWN, vk, 1 | scan);
    PostMessageW(found.render, WM_KEYUP, vk, 1 | scan | (LPARAM)(1u << 30) | (LPARAM)(1u << 31));
    return true;
}

void AskPanel::NotifyMoved() {
    if (m_controller) m_controller->NotifyParentWindowPositionChanged();
}

// The page is a window of its own over the interface, so a tooltip of the interface that reaches over it would be
// hidden. Where one does, the window that hosts the page in this process gets a hole (a window region), and the
// interface's own picture, with the tooltip, shows through it; the page's content stays where it is around it.
void AskPanel::SetHoles(const std::vector<Hole>& holes) {
    HWND host = nullptr;
    if (m_controller && m_parent) {
        for (HWND w = FindWindowExW(m_parent, nullptr, L"Chrome_WidgetWin_0", nullptr); w; w = FindWindowExW(m_parent, w, L"Chrome_WidgetWin_0", nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            if (pid == GetCurrentProcessId()) { host = w; break; }
        }
    }
    if (host != m_holeHost) {
        if (m_holeHost && IsWindow(m_holeHost)) SetWindowRgn(m_holeHost, nullptr, TRUE);
        m_holeHost = host;
        m_holes.clear();
    }
    if (!host || holes == m_holes) return;
    m_holes = holes;
    if (holes.empty()) { SetWindowRgn(host, nullptr, TRUE); return; }
    HRGN region = CreateRectRgn(0, 0, 32767, 32767);   // larger than the window, so a later resize keeps it whole
    for (const Hole& h : holes) {
        const int d = h.radius * 2;
        HRGN cut = d > 0 ? CreateRoundRectRgn(h.r.left, h.r.top, h.r.right + 1, h.r.bottom + 1, d, d)
                         : CreateRectRgn(h.r.left, h.r.top, h.r.right, h.r.bottom);
        CombineRgn(region, region, cut, RGN_DIFF);
        DeleteObject(cut);
    }
    if (!SetWindowRgn(host, region, TRUE)) DeleteObject(region);   // on success the region belongs to the window
}

bool AskPanel::CapturePng(std::function<void(std::vector<uint8_t>&&)> done) {
    if (!m_view || !m_visible) return false;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0));
    if (!stream.Get()) return false;
    auto handler = MakeHandler<ICoreWebView2CapturePreviewCompletedHandler>(
        IID_ICoreWebView2CapturePreviewCompletedHandler,
        [stream, done](HRESULT hr) {
            std::vector<uint8_t> png;
            STATSTG st{};
            if (SUCCEEDED(hr) && SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME)) && st.cbSize.QuadPart > 0) {
                png.resize((size_t)st.cbSize.QuadPart);
                LARGE_INTEGER zero{};
                ULONG got = 0;
                stream->Seek(zero, STREAM_SEEK_SET, nullptr);
                stream->Read(png.data(), (ULONG)png.size(), &got);
                png.resize(got);
            }
            if (done) done(std::move(png));
            return S_OK;
        });
    return SUCCEEDED(m_view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(), handler.Get()));
}

void AskPanel::Destroy() {
    ++m_generation;
    m_holes.clear();
    m_holeHost = nullptr;   // the window goes with the control
    if (m_controller) {
        FocusBack();
        m_controller->Close();
    }
    if (m_view) { m_view->Release(); m_view = nullptr; }
    if (m_controller) { m_controller->Release(); m_controller = nullptr; }
    if (m_env) { m_env->Release(); m_env = nullptr; }
    m_state = State::Idle;
    m_visible = m_focused = m_pageAlive = m_lost = m_reload = false;
    m_queued.clear();
}

std::vector<AskPanel::Event> AskPanel::Poll() {
    if (m_lost) {   // the browser process is gone, and the control with it: Try again makes a new one
        Destroy();
        m_state = State::Failed;
        Push(Event::LoadFailed, "the browser process ended");
    }
    if (m_reload) {
        m_reload = false;
        Push(Event::Log, "the page's process ended, the page opens again");
        Navigate();
    }
    if ((m_state == State::Creating || m_state == State::Loading) && NowSeconds() - m_since > kStartTimeout) {
        if (m_state == State::Creating) {
            Destroy();
            m_state = State::Failed;
            Push(Event::CreateFailed, "the control was not created in time");
        } else {
            Fail(false, "the page did not answer in time");
        }
    }
    std::vector<Event> e;
    e.swap(m_events);
    return e;
}

} // namespace vdc
