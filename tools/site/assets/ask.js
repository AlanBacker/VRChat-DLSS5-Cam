
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
   - An answer's link to another page of the site puts that page in place of this one without a page load, so the
     panel and the conversation stay (a page load would start the widget with an empty panel); a link into this page
     glides there. Every form Mintlify writes such a link in counts, a whole address too. Once the widget has started,
     the page's own links to other pages of its language are followed the same way.
   - Docked at the side of a wide enough window, the panel does not cover the page: the page makes room for it.
   - An address with ?ask opens the panel as the page opens. */
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
  var tries = 0, warming = false, pressedOpen = false, refocusUntil = 0, busyTimer = 0, live = null, note = null, shadow = null;
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
    var unkeep = keepRoot();
    started = load().then(function () { return api.init(config()); }).then(function () {
      mounted = true;
      var l1 = look();   // the theme was changed while it started
      if (l1.theme !== l0.theme || l1.accent !== l0.accent) api.update({ appearance: l1 }).catch(noop);
    });
    started.then(unkeep, unkeep);
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
    makeRoom(true);
    btn.setAttribute('aria-expanded', on ? 'true' : 'false');
    if (on) {
      refocusUntil = 0;
      hideNote();
      setTimeout(function () { if (isOpen) makeRoom(false); }, 520);   // landed: its own measure, if it differs
      return;
    }
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
  // an address with ?ask (the program opens one where it cannot show the panel itself) opens the panel once the page
  // has finished opening; the parameter leaves the address, so a reload or a copied link does not open it again
  if (/(?:^|&)ask(?:[=&]|$)/.test(location.search.slice(1))) {
    try { history.replaceState(history.state, '', location.pathname + location.hash); } catch (e) { /* the address stays */ }
    if (document.readyState === 'complete') openPanel('link'); else window.addEventListener('load', function () { openPanel('link'); }, { once: true });
  }

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
  // An answer's link → this site's page, as an address. Mintlify writes its own form (/en/install#x, /cn/live, /jp),
  // often with this site's base path before it (/VRChat-DLSS5-Cam/zh/install); a whole address of a page here
  // (…/zh/install.html, or …/install.html for English, whose pages have no folder) counts too.
  function ours(path) {
    var u, home = new URL(site.root || './', location.href);
    try { u = new URL(path, location.href); } catch (e) { return null; }
    if (u.origin !== location.origin) return null;   // another site: the widget opens it
    var mine = u.pathname.indexOf(home.pathname) === 0, pages = cfg.pages || [], dirs = site.dirs || {};
    var seg = (mine ? u.pathname.slice(home.pathname.length) : u.pathname).replace(/\.(mdx?|html)$/, '').split('/').filter(Boolean);
    var code = MINT[seg[0]];
    if (code) seg.shift();
    else if (mine || (seg.length === 1 && pages.indexOf(seg[0]) >= 0)) code = 'en';
    else return null;
    if (seg.length > 1 || !(code in dirs)) return null;
    var slug = seg[0] || 'index';
    if (pages.indexOf(slug) < 0) {
      if (slug.indexOf('.') >= 0) return null;   // a file of the site (a picture, a licence), not a page
      slug = 'index';
    }
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
  function bare(p) { return p.replace(/index\.html$/, ''); }
  function still() { return !!window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches; }

  // Another page of this language is put in place of this one: its text fades out (0.12 s), the sidebar's highlight
  // and the top bar's tab glide to the new page as they do after a followed link (vdc:moved, the layout's nav-early
  // script) while its text rises in (0.28 s, the program's curve), and the address and the history follow. The
  // browser's back and forward buttons swap the pages back, to the place the reader left (kept in the history entry
  // as they scroll). site.js sets up the new page's parts on vdc:swap. A page that cannot be fetched opens the
  // ordinary way.
  var shownPath = bare(location.pathname), moving = 0, keeping = false, keepTimer = 0;
  function fetchPage(url) {
    return fetch(url, { credentials: 'same-origin' }).then(function (r) {
      if (!r.ok) throw new Error('http ' + r.status);
      return r.text();
    }).then(function (html) {
      var d = new DOMParser().parseFromString(html, 'text/html'), s = null;
      try { s = JSON.parse(d.body.getAttribute('data-site') || 'null'); } catch (e) { s = null; }
      if (!s || s.lang !== site.lang || s.root !== site.root || !d.getElementById('content') || !d.querySelector('.aside-toc')) throw new Error('page');
      return d;
    });
  }
  function parts() { return [document.getElementById('content'), document.querySelector('.aside-toc')]; }
  function leave(els) {   // resolves once the page's text has faded out
    if (still()) return Promise.resolve();
    return Promise.all(els.map(function (el) {
      if (!el || !el.animate) return null;
      var a = el.animate([{ opacity: getComputedStyle(el).opacity }, { opacity: 0 }], { duration: 120, easing: 'cubic-bezier(.32,0,.67,0)', fill: 'forwards' });
      return a.finished ? a.finished.catch(noop) : new Promise(function (r) { setTimeout(r, 120); });
    }));
  }
  function keep() {   // the place read, in this history entry, for the back and forward buttons
    clearTimeout(keepTimer);
    keepTimer = setTimeout(function () { try { history.replaceState({ vdc: 1, y: Math.round(window.scrollY) }, ''); } catch (e) { /* not kept */ } }, 200);
  }
  // push: a link was followed. Otherwise the back or forward button has already changed the address (and the browser
  // has scrolled the page shown to the place it kept): the text is hidden in the same frame and the page put in.
  function show(target, push, y) {
    var id = ++moving, page = new URL(target.href), els = parts();
    page.hash = '';
    if (!push) els.forEach(function (el) { if (el) el.style.opacity = '0'; });
    Promise.all([fetchPage(page.href), push ? leave(els) : null]).then(function (r) {
      if (id === moving) swap(r[0], target, push, y);
    }).catch(function () {
      if (id !== moving) return;
      if (push) location.assign(target.href); else location.reload();
    });
  }
  function swap(d, target, push, y) {
    var i, url = new URL(target.href);
    url.hash = '';
    if (push) {   // before the page changes, so that the entry left keeps its place
      try {
        clearTimeout(keepTimer);
        history.replaceState({ vdc: 1, y: Math.round(window.scrollY) }, '');
        history.pushState({ vdc: 1 }, '', url.href);
      } catch (e) { location.assign(target.href); return; }
    }
    shownPath = bare(location.pathname);
    if (!keeping) { keeping = true; window.addEventListener('scroll', keep, { passive: true }); }
    // the head: the tab's title, the summary, the page's addresses in every language
    document.title = d.title;
    var desc = document.querySelector('meta[name="description"]'), desc2 = d.querySelector('meta[name="description"]');
    if (desc && desc2) desc.setAttribute('content', desc2.getAttribute('content'));
    var head = document.head, links = head.querySelectorAll('link[rel="canonical"], link[rel="alternate"]'), at = links.length ? links[0] : null;
    d.head.querySelectorAll('link[rel="canonical"], link[rel="alternate"]').forEach(function (l) { head.insertBefore(document.importNode(l, true), at); });
    for (i = 0; i < links.length; i++) links[i].remove();
    body.className = d.body.className;
    body.setAttribute('data-site', d.body.getAttribute('data-site'));
    try { site = JSON.parse(body.getAttribute('data-site')); } catch (e) { /* the same folder: the same values */ }
    // the page's text and its contents list
    var main = document.adoptNode(d.getElementById('content')), aside = document.adoptNode(d.querySelector('.aside-toc')), old = parts();
    old[0].replaceWith(main);
    old[1].replaceWith(aside);
    // the language menu and the footer lead to this page in the other languages
    var la = document.querySelectorAll('a[data-lang]'), lb = d.querySelectorAll('a[data-lang]');
    if (la.length === lb.length) for (i = 0; i < la.length; i++) la[i].setAttribute('href', lb[i].getAttribute('href'));
    var tabs = d.querySelectorAll('.top-links a'), tab = -1;
    for (i = 0; i < tabs.length; i++) if (tabs[i].hasAttribute('aria-current')) tab = i;
    document.dispatchEvent(new CustomEvent('vdc:swap'));
    document.dispatchEvent(new CustomEvent('vdc:moved', { detail: { url: location.href, tab: tab } }));
    // where the reader lands: the heading asked for (its spotlight plays), the place left on the way back, or the top
    var h = null;
    if (target.hash.length > 1) {
      try { h = document.getElementById(decodeURIComponent(target.hash.slice(1))); } catch (e) { h = null; }
      h = h || heading(target.hash);
    }
    if (push && h) location.replace('#' + h.id);
    else if (!push && y >= 0) window.scrollTo(0, y);
    else if (h) h.scrollIntoView();
    else window.scrollTo(0, 0);
    say(document.title);
    if (still()) return;
    var opt = { duration: 280, easing: 'cubic-bezier(.18,1,.56,1)' };
    main.animate([{ opacity: 0, transform: 'translateY(10px)' }, { opacity: 1, transform: 'none' }], opt);
    aside.animate([{ opacity: 0 }, { opacity: 1 }], opt);
  }
  window.addEventListener('popstate', function (e) {
    if (bare(location.pathname) === shownPath) return;   // a jump within the page shown: the browser's own
    var st = e.state || {};
    show(new URL(location.href), false, st.y >= 0 ? st.y : -1);
  });

  document.addEventListener('mintlify-assistant:navigate', function (e) {
    var d = e.detail || {}, to = ours(d.path || d.url || '');
    if (!to) return;   // not one of these pages: the widget handles it
    e.preventDefault();
    place(to);
  });
  // The widget's links with a whole address (https://…) are plain new-tab links that send no event, and its panel is
  // a closed shadow root that hides from the page which link was clicked. The root is kept as the widget makes it
  // (for its own element, only while it starts), and a click on a link to a page here is answered in it first.
  function keepRoot() {
    var proto = Element.prototype, attach = proto.attachShadow;
    if (!attach) return noop;
    proto.attachShadow = function (init) {
      var r = attach.apply(this, arguments);
      if (this.localName === 'mintlify-assistant') { shadow = r; r.addEventListener('click', onLink, true); }
      return r;
    };
    return function () { proto.attachShadow = attach; };
  }
  function onLink(e) {
    if (e.defaultPrevented || e.button !== 0 || e.ctrlKey || e.metaKey || e.shiftKey || e.altKey) return;   // a new tab asked for
    var a = e.target.closest ? e.target.closest('a[href]') : null;
    if (!a || a.getAttribute('href').charAt(0) === '#') return;   // a footnote: the widget scrolls its own panel
    var to = ours(a.href);
    if (!to) return;
    e.preventDefault();
    e.stopPropagation();   // the widget's own handler would send the event and open the page a second time
    place(to);
  }
  function folder() {   // this language's folder of the site, as a path
    var dir = (site.dirs || {})[site.lang];
    return new URL(((site.root || '') + (dir ? dir + '/' : '')) || './', location.href).pathname;
  }
  function place(to) {
    var f = folder(), here = bare(to.pathname) === shownPath;
    var near = !here && to.pathname.indexOf(f) === 0 && to.pathname.slice(f.length).indexOf('/') < 0 && !!window.fetch && !!window.DOMParser;
    if (!here && !near) {   // a page in another language: in a new tab, this one stays as it is
      var w = window.open(to.href, '_blank');
      if (w) w.opener = null; else click(to.href);   // a blocked new tab: this one goes instead
      return;
    }
    if (!side.matches && api) api.close();   // the sheet covers the page: it makes way, Ask AI brings it back
    if (near) { show(to, true); return; }
    var h = to.hash && heading(to.hash);
    if (to.hash) click(h ? '#' + h.id : to.hash);
  }
  (function arrive() {   // an anchor in Mintlify's form: go to our heading of that name at once (no history entry)
    if (location.hash.length < 2) return;
    var h = heading(location.hash);
    if (h) location.replace('#' + h.id);
  })();

  /* ---------------------------------------------------------------- the reader moves on, the conversation stays */
  // Once the widget has started, a page load would start it again with an empty panel. A link to another page of this
  // language (the sidebar, the tabs, the text, a search result, the previous and next pages) is then followed the way
  // an answer's link is: the page is put in place of this one and the panel stays as it is, open or closed, with the
  // conversation. Before the widget starts, and for links to other languages or sites and clicks that ask for a new
  // tab, links go as usual.
  function pageOf(href) {   // a link to a page of this language, as an address; null for anything else
    var u, f = folder();
    try { u = new URL(href, location.href); } catch (e) { return null; }
    var rest = u.pathname.slice(f.length);
    if (u.origin !== location.origin || u.search || u.pathname.indexOf(f) !== 0 || (rest && !/^[^\/]+\.html$/.test(rest))) return null;
    return (cfg.pages || []).indexOf(rest ? rest.slice(0, -5) : 'index') >= 0 ? u : null;
  }
  function toTop() {   // the program's SmoothScrollTo, as site.js glides to a heading: a scroll by the reader takes over
    if (still() || !window.requestAnimationFrame) { window.scrollTo(0, 0); return; }
    var y = window.scrollY, last = y, then = performance.now();
    requestAnimationFrame(function step(now) {
      if (Math.abs(window.scrollY - last) > 1.5) return;
      var dt = Math.min(Math.max((now - then) / 1000, 0), 0.05);
      then = now;
      y -= y * (1 - Math.exp(-18 * dt));
      window.scrollTo(0, y > 0.5 ? y : 0);
      last = window.scrollY;
      if (y > 0.5) requestAnimationFrame(step);
    });
  }
  document.addEventListener('click', function (e) {   // first, before the site's own handlers
    if (!mounted || e.defaultPrevented || e.button || e.ctrlKey || e.metaKey || e.shiftKey || e.altKey) return;
    var a = e.target.closest ? e.target.closest('a[href]') : null;
    if (!a || (a.target && a.target !== '_self') || a.hasAttribute('download')) return;
    var to = pageOf(a.href);
    if (!to) return;
    if (bare(to.pathname) === shownPath) {   // this page: a heading glides (site.js), the page itself goes to its top
      if (to.hash && to.pathname === location.pathname) return;
      e.preventDefault();
      if (to.hash) click(to.hash); else toTop();
      return;
    }
    e.preventDefault();
    show(to, true);
  }, true);

  /* ---------------------------------------------------------------- the page makes room */
  // Docked at the side, the panel would cover the right of the page. Where the window leaves the page at least 760 px
  // beside it, the page makes room instead: it keeps clear of the panel (--ask-room, ask.css) and is laid out for the
  // width left, with three columns, two (the contents list moves into the text) or one (the sidebar becomes the sheet
  // behind the menu button), through html.ask-r2 / ask-r1. The page slides over with the panel, on its 0.45 s curve,
  // and back as it leaves, on the program's 0.28 s. When the text would be laid out again (fewer columns, a narrower
  // column), it fades out instead, is laid out with the line read kept where it was, and fades back in while the top
  // bar slides, as when another page is put in its place. A narrower window keeps the panel over the page.
  var ROOM_MIN = 760, SLIDE_IN = 'cubic-bezier(.32,.72,0,1)', EASE = 'cubic-bezier(.18,1,.56,1)';
  var room = 0, roomLevel = 3, roomRuns = [], roomSeq = 0, goal = 0, goalLevel = 3;   // goal: where the latest change is heading
  function level(w) { return w >= 1240 ? 3 : w >= 1000 ? 2 : 1; }   // the site's columns at a width (site.css)
  function panelRoom() {   // the panel with its margin at the window's edge, and as much again beside the page
    var d = shadow && shadow.querySelector('[role="dialog"]'), v = d && d.offsetParent;
    if (!v || !d.offsetWidth) return 496;   // 480 + 8 + 8, before it is drawn
    return Math.round(doc.getBoundingClientRect().width - v.getBoundingClientRect().left - d.offsetLeft) + 8;   // at rest, wherever its slide is
  }
  function want() {
    var p = isOpen && side.matches ? panelRoom() : 0;
    return p && window.innerWidth - p >= ROOM_MIN ? p : 0;
  }
  function setRoom(px, quiet) {
    var l = px ? level(window.innerWidth - px) : 3;
    room = px;
    roomLevel = l;
    doc.classList.toggle('ask-room', px > 0);
    doc.classList.toggle('ask-r2', l < 3);
    doc.classList.toggle('ask-r1', l < 2);
    if (px) doc.style.setProperty('--ask-room', px + 'px'); else doc.style.removeProperty('--ask-room');
    if (!quiet) document.dispatchEvent(new CustomEvent('vdc:room'));   // site.js: the sidebar sheet, when no longer one
  }
  function relayout(px) {   // the new width, with the first block of the text in view kept where it stands
    var top = (document.querySelector('.topbar') || doc).getBoundingClientRect().bottom, at = null, r, i;
    var els = window.scrollY < 1 ? [] : document.querySelectorAll('#content h1, #content h2, #content h3, #content h4, #content p, #content li, #content tr, #content figure, #content pre');
    for (i = 0; i < els.length && !at; i++) { r = els[i].getBoundingClientRect(); if (r.bottom > top) at = { el: els[i], y: r.top }; }
    doc.style.overflowAnchor = 'none';   // the browser would keep a line of its own choosing
    setRoom(px);
    if (at) { r = at.el.getBoundingClientRect().top - at.y; if (Math.abs(r) >= 1) window.scrollBy(0, r); }
    requestAnimationFrame(function () { doc.style.overflowAnchor = ''; });
  }
  function left(el) { return el ? el.getBoundingClientRect().left : 0; }
  function slideFrom(els, x0, T, E) {   // each block from where it stood to where it stands now
    els.forEach(function (el, i) {
      var dx = el ? x0[i] - left(el) : 0;
      if (Math.abs(dx) >= 1) roomRuns.push(el.animate([{ transform: 'translateX(' + dx + 'px)' }, { transform: 'none' }], { duration: T, easing: E }));
    });
  }
  function makeRoom(animate) {
    var px = want(), was = room, w = window.innerWidth, l = px ? level(w - px) : 3;
    if (px === goal && l === goalLevel) return;   // there already, or on the way
    goal = px;
    goalLevel = l;
    roomSeq++;
    roomRuns.forEach(function (a) { a.cancel(); });
    roomRuns = [];
    var q = function (s) { return document.querySelector(s); }, main = q('#content'), bar = q('.topbar-in');
    var text = [q('.layout'), q('.footer-in')], blocks = [bar, q('.sidenav'), main, q('.aside-toc'), text[1]];   // each column on its own: the gaps may change
    if (!animate || still() || document.visibilityState !== 'visible' || !doc.animate || !main) { relayout(px); return; }
    var T = px > was ? 450 : 280, E = px > was ? SLIDE_IN : EASE, w0 = main.getBoundingClientRect().width, x0 = blocks.map(left);
    setRoom(px, true);   // tried first: does the text keep its columns and its width?
    if (level(w - px) === level(w - was) && Math.abs(main.getBoundingClientRect().width - w0) < 1) {
      document.dispatchEvent(new CustomEvent('vdc:room'));
      slideFrom(blocks, x0, T, E);
      return;
    }
    setRoom(was, true);
    var id = roomSeq, outs = text.map(function (el) {
      return el && el.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 120, easing: 'cubic-bezier(.32,0,.67,0)', fill: 'forwards' });
    });
    roomRuns = outs.filter(Boolean);
    Promise.all(roomRuns.map(function (a) { return a.finished ? a.finished.catch(noop) : new Promise(function (r) { setTimeout(r, 120); }); })).then(function () {
      if (id !== roomSeq) return;
      var xb = left(bar);
      relayout(px);
      slideFrom([bar], [xb], T - 120, E);
      text.forEach(function (el) { if (el) roomRuns.push(el.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 280, easing: EASE })); });
      outs.forEach(function (a) { if (a) a.cancel(); });   // the fade-ins draw from here on
    });
  }
  window.addEventListener('resize', function () { if (mounted) makeRoom(false); });

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
