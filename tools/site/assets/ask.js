
/* ---------------------------------------------------------------------------------------------- AI Q&A
   build.py adds this to site.js only when site.json names a Mintlify widget ID ("askWidget"); without one the pages
   carry no trace of it. Mintlify's widget answers from this documentation (the copy in mintlify/).
   - Nothing from Mintlify loads with the page. Pointing at or focusing an entry point (the Ask AI button, the Ask row
     of the search popup) fetches the widget's script once the page has finished opening; that draws nothing and
     sends nothing. A click starts the widget and opens it in the same step.
   - Mintlify draws its own button in the corner whenever its panel is closed, and its stylesheet resets its host
     element with `all: initial !important`, which beats every rule of the page. The host is therefore moved into a
     layer of this page that is there only while the panel is open: the layer fades in as the panel opens and out as
     it slides away, then leaves the page, so the widget's button never shows. Ask AI in the top bar is the way back
     to the conversation. The click starts the widget with defaultOpen, so it is drawn open from the start.
   - An answer's link to another page opens it in a new tab, so the answer stays where the reader left it (after a
     page load the widget starts with an empty panel); a link into this page glides there. */
(function () {
  'use strict';
  var doc = document.documentElement, body = document.body, site = {};
  try { site = JSON.parse(body.getAttribute('data-site') || '{}'); } catch (e) { return; }
  var cfg = site.ask, btn = document.querySelector('.ask-btn');
  if (!cfg || !cfg.id || !btn || !window.Promise || !window.MutationObserver) return;

  var SRC = 'https://widget.mintlify.com/v1/embed.js';
  var side = window.matchMedia('(min-width: 768px)');   // from 768 px the widget docks its panel at the side
  var dark = window.matchMedia('(prefers-color-scheme: dark)');
  var api = null, ready = null, started = null, mounted = false, isOpen = false, layer = null, layerTimer = 0;
  var tries = 0, warming = false, pressedOpen = false, refocusUntil = 0, busyTimer = 0, live = null, note = null;
  function noop() {}

  /* ---------------------------------------------------------------- loading */
  // after the page has finished opening (the sidebar's highlight glides then), in idle time
  function settle(fn) {
    function idle() { if (window.requestIdleCallback) window.requestIdleCallback(fn, { timeout: 500 }); else setTimeout(fn, 1); }
    function go() { setTimeout(idle, Math.max(0, 450 - performance.now())); }
    if (document.readyState === 'complete') go(); else window.addEventListener('load', go, { once: true });
  }
  function look() {
    return { theme: doc.getAttribute('data-theme') || 'system', accent: getComputedStyle(body).getPropertyValue('--accent').trim() };
  }
  function config() {
    var cs = getComputedStyle(body), l = look();
    return {
      id: cfg.id,
      defaultOpen: true,   // started only by a click that opens it: it is drawn open, and its own button never shows first
      appearance: {
        variant: 'panel', theme: l.theme, accent: l.accent,
        radius: cs.getPropertyValue('--r-card').trim(), font: cs.getPropertyValue('--font').trim().replace(/\s+/g, ' '),
        logo: new URL((site.root || '') + 'assets/mark.svg', location.href).href,
        zIndex: 75,   // above the top bar, its menus and the suggestion bar; under the menu sheet and its scrim
        dismissOnInteractOutside: !side.matches   // docked at the side it stays while the reader works in the page
      },
      labels: cfg.labels,
      starterQuestions: cfg.questions,
      filter: { language: cfg.language },
      analytics: { capturePathname: false },
      hooks: { event: onEvent, error: noop }   // the widget shows its own errors in the panel
    };
  }
  function load() {   // the widget's script only: it draws nothing and sends nothing until start()
    if (ready) return ready;
    ready = new Promise(function (resolve, reject) {
      var s = document.createElement('script');
      s.type = 'module';
      s.src = SRC + (tries++ ? '?retry=' + tries : '');   // a module that failed stays failed under its address
      s.onload = resolve;
      s.onerror = function () { s.remove(); reject(new Error('load')); };
      document.head.appendChild(s);
    }).then(function () {
      api = window.MintlifyAssistant;
      if (!api || !api.init) throw new Error('init');
    });
    ready.catch(function () { ready = null; api = null; });
    return ready;
  }
  function start() {   // draws the widget, open (defaultOpen); the caller then hands it the focus or the question
    if (started) return started;
    var l0 = look();
    adopt();
    started = load().then(function () { return api.init(config()); }).then(function () {
      mounted = true;
      var l1 = look();   // the theme was changed while it started
      if (l1.theme !== l0.theme || l1.accent !== l0.accent) api.update({ appearance: l1 }).catch(noop);
    });
    started.catch(function () { started = null; mounted = false; });
    return started;
  }
  function warm() {
    if (ready || warming) return;
    warming = true;
    speaker();
    settle(function () { warming = false; if (!ready) load().catch(noop); });
  }

  /* ---------------------------------------------------------------- the layer */
  // The widget appends its host to the body; it is moved into the layer before anything is drawn. The layer stays
  // in the page (with no box of its own) while the panel is open or sliding away, and leaves it once the panel is
  // gone. If the widget ever puts its host elsewhere, it is simply left there: its corner button then shows.
  function adopt() {
    if (layer) return;
    layer = document.createElement('div');
    layer.className = 'ask-layer';
    layer.hidden = true;
    body.appendChild(layer);
    new MutationObserver(function (ms) {
      for (var i = 0; i < ms.length; i++) for (var j = 0; j < ms[i].addedNodes.length; j++) {
        var n = ms[i].addedNodes[j];
        if (n.localName === 'mintlify-assistant') layer.appendChild(n);
      }
    }).observe(body, { childList: true });
  }
  function layerOn(on) {
    if (!layer) return;
    clearTimeout(layerTimer);
    if (on) {
      if (layer.hidden) { layer.hidden = false; void layer.offsetWidth; }   // the fade starts from nothing
      layer.classList.add('is-on');
      return;
    }
    layer.classList.remove('is-on');
    layerTimer = setTimeout(function () { if (!isOpen) layer.hidden = true; }, 520);   // the panel's slide: 0.43 s
  }

  /* ---------------------------------------------------------------- open, close */
  function onEvent(e) {
    if (!e) return;
    if (e.type === 'open' || e.type === 'ask') shown(true);
    else if (e.type === 'close' || e.type === 'destroy') shown(false);
  }
  // The widget gives the focus back to its own button when the panel closes, and again as it finishes sliding away
  // (about 0.4 s). That button is never shown (see the layer), so the focus goes back to our button instead, for the
  // second of the closing.
  document.addEventListener('focusin', function (e) {
    if (refocusUntil && performance.now() < refocusUntil && e.target.localName === 'mintlify-assistant') btn.focus({ preventScroll: true });
  });
  function shown(on) {
    if (on === isOpen) return;
    isOpen = on;
    layerOn(on);
    btn.setAttribute('aria-expanded', on ? 'true' : 'false');
    if (on) { refocusUntil = 0; hideNote(); return; }
    var host = document.querySelector('mintlify-assistant'), hadFocus = !!host && document.activeElement === host;
    refocusUntil = performance.now() + 1000;
    setTimeout(function () {   // out of the closed panel at once, also when the widget leaves it there or lets it fall
      var a = document.activeElement;
      if (refocusUntil && (a === host || (hadFocus && (!a || a === body)))) btn.focus({ preventScroll: true });
    }, 60);
    setTimeout(function () { if (refocusUntil && document.activeElement === host) btn.focus({ preventScroll: true }); refocusUntil = 0; }, 1000);
  }
  function busy(on) {
    clearTimeout(busyTimer);
    if (on) {
      // a started widget opens at once: the turning arc only shows when starting takes longer than 0.12 s
      busyTimer = setTimeout(function () { btn.classList.add('is-loading'); btn.setAttribute('aria-busy', 'true'); say(cfg.text.loading); }, 120);
    } else {
      btn.classList.remove('is-loading');
      btn.removeAttribute('aria-busy');
    }
  }
  function openPanel(source, question) {
    hideNote();
    speaker();
    if (!mounted) busy(true);
    start().then(function () {
      busy(false);
      say('');
      return question ? api.ask(question, { source: source, open: true, focus: true }) : api.open({ source: source, focus: true });
    }).catch(function () { busy(false); fail(); });
  }
  // a press on our button while the panel is open closes it; on a narrow screen the widget closes it itself on that
  // press (outside the panel), so the click that follows must not open it again
  window.addEventListener('pointerdown', function (e) { pressedOpen = isOpen && btn.contains(e.target); }, true);
  btn.addEventListener('click', function () {
    var was = pressedOpen || isOpen;
    pressedOpen = false;
    if (was) { if (isOpen && api) api.close(); return; }
    openPanel('topbar');
  });
  btn.addEventListener('pointerenter', warm);
  btn.addEventListener('focus', warm);

  /* ---------------------------------------------------------------- messages */
  function speaker() {
    if (live) return;
    live = document.createElement('span');
    live.className = 'ask-live';
    live.setAttribute('role', 'status');
    body.appendChild(live);
  }
  function say(text) { if (live) live.textContent = text; }
  function hideNote() { if (note) { note.remove(); note = null; } }
  function fail() {
    hideNote();
    note = document.createElement('div');
    note.className = 'ask-note';
    note.textContent = cfg.text.failed;
    var r = btn.getBoundingClientRect();
    note.style.top = Math.round(r.bottom + 8) + 'px';
    note.style.right = Math.max(12, Math.round(doc.clientWidth - r.right)) + 'px';
    body.appendChild(note);
    say(cfg.text.failed);
  }
  document.addEventListener('click', function (e) { if (note && !btn.contains(e.target)) hideNote(); });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') hideNote(); });

  /* ---------------------------------------------------------------- the page's look */
  function restyle() { if (mounted) api.update({ appearance: look() }).catch(noop); }
  new MutationObserver(restyle).observe(doc, { attributes: true, attributeFilter: ['data-theme'] });
  if (dark.addEventListener) dark.addEventListener('change', restyle);
  if (side.addEventListener) side.addEventListener('change', function () { if (mounted) api.update({ appearance: { dismissOnInteractOutside: !side.matches } }).catch(noop); });

  /* ---------------------------------------------------------------- answer links */
  var MINT = { en: 'en', cn: 'zh', zh: 'zh', jp: 'ja', ja: 'ja', ko: 'ko' };   // Mintlify's language folders → ours
  function slugText(t) {   // a heading's text as Mintlify turns it into an anchor
    var s = String(t).trim().toLowerCase();
    try { s = s.replace(/[^\p{L}\p{M}\p{N}\s_-]+/gu, ''); } catch (e) { s = s.replace(/[^\w\s-]+/g, ''); }
    return s.replace(/\s+/g, '-').replace(/-+/g, '-');
  }
  function heading(hash) {   // our heading for an anchor Mintlify made from its text (ours are short ids)
    var id = hash.replace(/^#/, '');
    try { id = decodeURIComponent(id); } catch (e) { /* as it is */ }
    if (!id || document.getElementById(id)) return null;
    var want = slugText(id), hs = document.querySelectorAll('.doc h2[id], .doc h3[id]');
    for (var i = 0; i < hs.length; i++) if (slugText(hs[i].textContent) === want) return hs[i];
    return null;
  }
  function ours(path) {   // /en/install#x, /cn/live, /jp, … → this site's page, as an address
    var u;
    try { u = new URL(path, 'https://docs.invalid/'); } catch (e) { return null; }
    var m = /^\/([a-z]{2})(?:\/([^\/]+))?\/?$/.exec(u.pathname.replace(/\.(mdx?|html)$/, ''));
    var code = m && MINT[m[1]], dirs = site.dirs || {};
    if (!code || !(code in dirs)) return null;
    var slug = m[2] || 'index';
    if ((cfg.pages || []).indexOf(slug) < 0) slug = 'index';
    return new URL((site.root || '') + (dirs[code] ? dirs[code] + '/' : '') + slug + '.html' + u.hash, location.href);
  }
  function click(href) {   // a real link, clicked: the site's own handlers glide within the page or to the next one
    var a = document.createElement('a');
    a.href = href;
    a.hidden = true;
    body.appendChild(a);
    a.click();
    a.remove();
  }
  document.addEventListener('mintlify-assistant:navigate', function (e) {
    var d = e.detail || {}, to = ours(d.path || d.url || '');
    if (!to) return;   // not one of these pages: the widget handles it
    e.preventDefault();
    if (to.pathname === location.pathname) {
      if (!side.matches && api) api.close();   // the sheet would cover the place it glides to
      var h = to.hash && heading(to.hash);
      if (to.hash) click(h ? '#' + h.id : to.hash);
      return;
    }
    var w = window.open(to.href, '_blank');
    if (w) w.opener = null; else click(to.href);   // a blocked new tab: this one goes instead
  });
  (function arrive() {   // an anchor in Mintlify's form: go to our heading of that name at once (no history entry)
    if (location.hash.length < 2) return;
    var h = heading(location.hash);
    if (h) location.replace('#' + h.id);
  })();

  /* ---------------------------------------------------------------- the search popup's Ask row */
  (function searchRow() {
    var input = document.querySelector('.search-input'), list = document.getElementById('search-list');
    if (!input || !list) return;
    var row = null;
    function add() {
      var q = input.value.trim();
      if (!q || list.querySelector('.ask-row')) return;   // our own insertion comes back here too
      row = document.createElement('li');
      row.className = 'ask-row';
      row.id = 'sr-' + list.children.length;   // the site's keys move through the rows by these ids
      row.setAttribute('role', 'option');
      row.setAttribute('aria-selected', 'false');
      row.innerHTML = '<button type="button" tabindex="-1">' + cfg.icon + '<span><span class="r-title"></span><span class="r-page"></span></span></button>';
      var title = row.querySelector('.r-title'), parts = cfg.text.search.split('{q}'), mark = document.createElement('span');
      mark.className = 'ask-q';
      mark.textContent = q;
      title.appendChild(document.createTextNode(parts[0]));
      title.appendChild(mark);
      title.appendChild(document.createTextNode(parts.slice(1).join('{q}')));
      row.querySelector('.r-page').textContent = cfg.labels.title;
      row.firstChild.addEventListener('click', function () { openPanel('search', q); });   // the list's own click closes the popup
      row.addEventListener('pointerenter', warm);
      list.appendChild(row);
    }
    // each search rewrites the list, and always its status line (an empty list after an empty one changes nothing)
    var mo = new MutationObserver(add), status = document.querySelector('.search-status');
    mo.observe(list, { childList: true });
    if (status) mo.observe(status, { childList: true, characterData: true, subtree: true });
    new MutationObserver(function () { if (row && row.isConnected && input.getAttribute('aria-activedescendant') === row.id) warm(); })
      .observe(input, { attributes: true, attributeFilter: ['aria-activedescendant'] });
    input.addEventListener('keydown', function (e) {
      if (e.key !== 'Enter' || e.isComposing || !row || !row.isConnected || input.getAttribute('aria-activedescendant') !== row.id) return;
      e.preventDefault();
      row.firstChild.click();
    });
  })();
})();
