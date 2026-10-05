/* VRChat DLSS5 Cam documentation: everything here is an extra. Every page reads and works without it. */
(function () {
  'use strict';
  var doc = document.documentElement;
  var body = document.body;
  var site = {};
  try { site = JSON.parse(body.getAttribute('data-site') || '{}'); } catch (e) { /* keep the defaults */ }
  var calm = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  function $(sel, root) { return (root || document).querySelector(sel); }
  function $$(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }
  function store(key, value) {
    try {
      if (value === undefined) return localStorage.getItem(key);
      if (value === null) localStorage.removeItem(key); else localStorage.setItem(key, value);
    } catch (e) { return null; }
    return null;
  }
  function el(tag, cls, html) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (html !== undefined) n.innerHTML = html;
    return n;
  }
  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; });
  }

  /* ---------------------------------------------------------------------------------------------- menus */
  function closeMenus(except) {
    $$('details.menu[open]').forEach(function (d) { if (d !== except) d.removeAttribute('open'); });
  }
  $$('details.menu').forEach(function (d) {
    d.addEventListener('toggle', function () { if (d.open) closeMenus(d); });
  });
  document.addEventListener('click', function (e) {
    if (!e.target.closest('details.menu')) closeMenus();
  });
  document.addEventListener('keydown', function (e) {
    if (e.key !== 'Escape') return;
    var open = $('details.menu[open]');
    if (open) { open.removeAttribute('open'); var s = $('summary', open); if (s) s.focus(); }
  });

  /* ---------------------------------------------------------------------------------------------- theme */
  function currentTheme() { return doc.getAttribute('data-theme') || 'system'; }
  function showTheme() {
    var t = currentTheme();
    $$('[data-theme-set]').forEach(function (b) { b.setAttribute('aria-pressed', b.getAttribute('data-theme-set') === t ? 'true' : 'false'); });
  }
  function setTheme(t) {
    if (t === 'system') { doc.removeAttribute('data-theme'); store('vdc-theme', null); }
    else { doc.setAttribute('data-theme', t); store('vdc-theme', t); }
    showTheme();
  }
  // The pictures taken in the theme t that are on screen, loaded and decoded first (at most 400 ms), so the page
  // blends over to them rather than to empty frames. The others load as they come into view.
  function themePictures(t) {
    if (t === 'system') t = window.matchMedia && matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    var waits = [];
    $$('.shot img.for-' + t).forEach(function (im) {
      var r = im.parentNode.getBoundingClientRect();
      if (r.bottom < -200 || r.top > window.innerHeight + 200) return;
      im.loading = 'eager';
      if (im.decode) waits.push(im.decode().catch(function () {}));
    });
    if (!waits.length) return Promise.resolve();
    return Promise.race([Promise.all(waits), new Promise(function (r) { setTimeout(r, 400); })]);
  }
  $$('[data-theme-set]').forEach(function (b) {
    b.addEventListener('click', function () {
      var t = b.getAttribute('data-theme-set');
      closeMenus();   // the menu closes at once, as the program's popups do; the page then blends to the new palette
      if (t === currentTheme()) return;
      themePictures(t).then(function () {
        if (calm || !document.startViewTransition) { setTheme(t); return; }
        document.startViewTransition(function () { setTheme(t); });
      });
    });
  });
  showTheme();

  /* ---------------------------------------------------------------------------------------------- language */
  function langUrl(code) {
    var dir = (site.dirs || {})[code];
    return (site.root || '') + (dir ? dir + '/' : '') + (site.slug || 'index') + '.html' + location.hash;
  }
  document.addEventListener('click', function (e) {
    var a = e.target.closest('a[data-lang]');
    if (!a) return;
    var code = a.getAttribute('data-lang');
    store('vdc-lang', code);
    if (location.hash && !e.metaKey && !e.ctrlKey && !e.shiftKey) { e.preventDefault(); location.href = langUrl(code); }
  });
  (function suggest() {
    if (store('vdc-lang') || store('vdc-suggest-done') || !site.suggest) return;
    var langs = navigator.languages || [navigator.language || ''];
    var pref = null;
    for (var i = 0; i < langs.length && !pref; i++) {
      var l = String(langs[i]).toLowerCase();
      if (l.indexOf('zh') === 0) pref = 'zh';
      else if (l.indexOf('ja') === 0) pref = 'ja';
      else if (l.indexOf('ko') === 0) pref = 'ko';
      else if (l.indexOf('en') === 0) pref = 'en';
    }
    if (!pref || pref === site.lang || !site.suggest[pref] || !site.suggest[pref].ready) return;
    var t = site.suggest[pref];
    var bar = el('div', 'suggest');
    bar.setAttribute('role', 'region');
    bar.setAttribute('lang', pref === 'zh' ? 'zh-CN' : pref);
    bar.setAttribute('aria-label', t.text);
    bar.innerHTML = '<p>' + esc(t.text) + '</p><span class="suggest-actions"><a class="btn btn-accent btn-sm" href="' +
      esc(langUrl(pref)) + '">' + esc(t['switch']) + '</a><button type="button" class="btn btn-sm btn-flat">' + esc(t.dismiss) + '</button></span>';
    function leave() {
      store('vdc-suggest-done', '1');
      if (calm) { bar.remove(); return; }
      bar.classList.add('is-leaving');
      setTimeout(function () { bar.remove(); }, 170);
    }
    $('a', bar).addEventListener('click', function () { store('vdc-lang', pref); });
    $('button', bar).addEventListener('click', leave);
    setTimeout(function () { body.appendChild(bar); }, 600);
  })();

  /* ---------------------------------------------------------------------------------------------- jumps on the page glide */
  // The program's SmoothScrollTo: each frame closes the gap by 1 - e^(-18 dt), and a scroll by the reader takes over.
  // The browser makes the jump first (the address, the history and :target stay its own), the view is put back
  // before anything is painted, and then it glides there; the spotlight waits for the arrival.
  (function jumps() {
    if (calm || !window.requestAnimationFrame) return;
    var raf = 0, last = 0;
    function stop() {
      if (raf) cancelAnimationFrame(raf);
      raf = 0;
      doc.classList.remove('is-scrolling');
    }
    var glideTo = function (hash) {
      var id = decodeURIComponent(hash.slice(1)), target = id && document.getElementById(id);
      if (!target) return false;
      var y0 = window.scrollY;
      stop();
      doc.classList.add('is-scrolling');
      if (location.hash === hash) target.scrollIntoView(); else location.hash = hash;
      var y1 = window.scrollY;
      window.scrollTo(0, y0);
      if (Math.abs(y1 - y0) < 1) { window.scrollTo(0, y1); stop(); return true; }
      var y = y0, then = performance.now();
      last = y0;
      function step(now) {
        if (Math.abs(window.scrollY - last) > 1.5) { stop(); return; }   // the reader scrolled: theirs now
        var dt = Math.min(Math.max((now - then) / 1000, 0), 0.05);
        then = now;
        y += (y1 - y) * (1 - Math.exp(-18 * dt));
        if (Math.abs(y1 - y) <= 0.5) { window.scrollTo(0, y1); stop(); return; }
        window.scrollTo(0, y);
        last = window.scrollY;
        raf = requestAnimationFrame(step);
      }
      raf = requestAnimationFrame(step);
      return true;
    };
    document.addEventListener('click', function (e) {
      if (e.defaultPrevented || e.button || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
      var a = e.target.closest('a[href*="#"]');
      if (!a || a.matches('.skip, .menu-btn, .nav-close') || (a.target && a.target !== '_self')) return;
      var url = new URL(a.href, location.href);
      if (!url.hash || url.origin !== location.origin || url.pathname !== location.pathname || url.search !== location.search) return;
      if (glideTo(url.hash)) e.preventDefault();
    });
    ['wheel', 'touchstart', 'keydown'].forEach(function (type) { window.addEventListener(type, function () { if (raf) stop(); }, { passive: true }); });
  })();

  /* ---------------------------------------------------------------------------------------------- search */
  (function search() {
    var box = $('.search');
    if (!box) return;
    var input = $('.search-input', box), pop = $('.search-pop', box), list = $('ul', pop), status = $('.search-status', pop);
    var topbar = $('.topbar');
    var data = null, loading = false, sel = -1, items = [];
    var txt = site.search || {};

    function load(then) {
      if (data) { then(); return; }
      if (window.VDC_SEARCH) { data = window.VDC_SEARCH; then(); return; }
      if (loading) return;
      loading = true;
      var s = document.createElement('script');
      s.src = (site.root || '') + 'assets/search-' + (site.lang || 'en') + '.js';
      s.onload = function () { data = window.VDC_SEARCH || []; loading = false; then(); };
      s.onerror = function () { loading = false; };
      document.head.appendChild(s);
    }
    function norm(s) { return String(s).toLowerCase().normalize('NFKC'); }
    function mark(text, words) {
      var out = esc(text);
      words.forEach(function (w) {
        if (!w) return;
        var re = new RegExp('(' + esc(w).replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'ig');
        out = out.replace(re, '<mark>$1</mark>');
      });
      return out;
    }
    function run() {
      var q = norm(input.value.trim());
      if (!q) { hide(); return; }
      var words = q.split(/\s+/).filter(Boolean);
      var found = [];
      data.forEach(function (row) {
        var t = norm(row[0]), hay = t + ' ' + norm(row[2]);
        for (var i = 0; i < words.length; i++) if (hay.indexOf(words[i]) < 0) return;
        var score = t === q ? 100 : t.indexOf(q) === 0 ? 60 : t.indexOf(q) >= 0 ? 35 : 10;
        score += [0, 16, 10, 6, 3][row[3]] || 0;
        found.push([score, row]);
      });
      found.sort(function (a, b) { return b[0] - a[0]; });
      items = found.slice(0, 12).map(function (f) { return f[1]; });
      list.innerHTML = items.map(function (row, i) {
        var ic = row[3] === 1 ? site.icons.page : site.icons.hash;
        return '<li role="option" id="sr-' + i + '" aria-selected="false"><a href="' + esc(row[1]) + '" tabindex="-1">' + ic +
          '<span><span class="r-title">' + mark(row[0], words) + '</span>' + (row[2] ? '<span class="r-page">' + esc(row[2]) + '</span>' : '') +
          '</span></a></li>';
      }).join('');
      var n = found.length;
      status.textContent = n === 0 ? (txt.none || '') : n === 1 ? (txt.result || '') : (txt.results || '').replace('{n}', n);
      sel = -1;
      pop.hidden = false;
      input.setAttribute('aria-expanded', 'true');
      if (items.length) select(0);
    }
    function select(i) {
      var lis = $$('li', list);
      if (!lis.length) return;
      sel = (i + lis.length) % lis.length;
      lis.forEach(function (li, k) { li.setAttribute('aria-selected', k === sel ? 'true' : 'false'); });
      input.setAttribute('aria-activedescendant', 'sr-' + sel);
      lis[sel].scrollIntoView({ block: 'nearest' });
    }
    function hide() {
      pop.hidden = true;
      input.setAttribute('aria-expanded', 'false');
      input.removeAttribute('aria-activedescendant');
    }
    function closeMobile() {
      topbar.classList.remove('is-searching');
      hide();
    }
    input.addEventListener('focus', function () { load(function () { if (input.value) run(); }); });
    input.addEventListener('input', function () { load(run); });
    input.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowDown') { e.preventDefault(); select(sel + 1); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); select(sel - 1); }
      else if (e.key === 'Enter') {
        var a = sel >= 0 ? $('#sr-' + sel + ' a', list) : null;
        if (a) {   // followed as if clicked: a heading on this page glides, another page is remembered or put in place
          e.preventDefault();
          a.click();
          hide();
          if (topbar.classList.contains('is-searching')) closeMobile();
        }
      } else if (e.key === 'Escape') {
        if (input.value) { input.value = ''; hide(); } else { input.blur(); closeMobile(); }
      }
    });
    list.addEventListener('click', function () { hide(); closeMobile(); });
    document.addEventListener('click', function (e) { if (!e.target.closest('.search')) hide(); });
    document.addEventListener('keydown', function (e) {
      var t = e.target, typing = t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable);
      if ((e.key === '/' && !typing) || ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k')) {
        e.preventDefault();
        if (getComputedStyle($('.search-box', box)).display === 'none') topbar.classList.add('is-searching');
        input.focus();
      }
    });
    $('.search-open', box).addEventListener('click', function () { topbar.classList.add('is-searching'); input.focus(); });
    $('.search-close', box).addEventListener('click', function () { input.value = ''; closeMobile(); $('.search-open', box).focus(); });
  })();

  /* ---------------------------------------------------------------------------------------------- copy buttons */
  function copyButtons() {
    $$('.code').forEach(function (block) {
      var b = el('button', 'btn copy-btn', site.icons.copy + '<span>' + esc(site.code.copy) + '</span>');
      b.type = 'button';
      b.addEventListener('click', function () {
        var text = $('code', block).textContent;
        function done() {
          b.classList.add('is-done', 'is-swap');
          b.innerHTML = site.icons.check + '<span>' + esc(site.code.copied) + '</span>';
          clearTimeout(b._back);
          b._back = setTimeout(function () { b.classList.remove('is-done'); b.innerHTML = site.icons.copy + '<span>' + esc(site.code.copy) + '</span>'; }, 1600);
        }
        if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(text).then(done, function () {});
        else {
          var ta = el('textarea'); ta.value = text; ta.style.position = 'fixed'; ta.style.opacity = '0';
          body.appendChild(ta); ta.select();
          try { document.execCommand('copy'); done(); } catch (e) { /* nothing to do */ }
          ta.remove();
        }
      });
      block.appendChild(b);
    });
  }

  /* ---------------------------------------------------------------------------------------------- contents: where the reader is */
  function spy() {
    var links = $$('.toc a');
    if (!links.length || !('IntersectionObserver' in window)) return null;
    var map = {};
    links.forEach(function (a) { map[a.getAttribute('href').slice(1)] = a; });
    var heads = $$('.doc-body h2[id]').filter(function (h) { return map[h.id]; });
    var visible = {};
    // one bar for the whole list, gliding to the section's link (the program's segmented thumb)
    var toc = $('.toc'), thumb = el('span', 'toc-thumb'), shown = null;
    thumb.setAttribute('aria-hidden', 'true');
    if (toc) { toc.appendChild(thumb); toc.classList.add('has-thumb'); }
    function place(a) {
      if (!toc || !a || !a.offsetHeight) return;
      thumb.style.transform = 'translate(' + a.offsetLeft + 'px,' + a.offsetTop + 'px)';
      thumb.style.height = a.offsetHeight + 'px';
      if (!toc.classList.contains('is-ready')) {
        void thumb.offsetWidth;   // the first place is taken without a glide
        toc.classList.add('is-ready');
      }
    }
    function mark() {
      var cur = null;
      for (var i = 0; i < heads.length; i++) {
        if (heads[i].getBoundingClientRect().top < window.innerHeight * 0.35) cur = heads[i].id;
      }
      if (!cur && heads.length) cur = heads[0].id;
      links.forEach(function (a) { if (a === map[cur]) a.setAttribute('aria-current', 'true'); else a.removeAttribute('aria-current'); });
      if (map[cur] !== shown || !toc.classList.contains('is-ready')) { shown = map[cur]; place(shown); }
    }
    function resized() { place(shown); }
    function scrolled() { window.requestAnimationFrame(mark); }
    window.addEventListener('resize', resized);
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) { visible[en.target.id] = en.isIntersecting; });
      mark();
    }, { rootMargin: '0px 0px -55% 0px' });
    heads.forEach(function (h) { io.observe(h); });
    window.addEventListener('scroll', scrolled, { passive: true });
    mark();
    return function () { io.disconnect(); window.removeEventListener('resize', resized); window.removeEventListener('scroll', scrolled); };
  }

  /* ---------------------------------------------------------------------------------------------- one motion per figure */
  function arm() {
    var figs = $$('[data-anim]');
    if (calm || !figs.length) return null;
    figs.forEach(function (f) {
      if (f.getAttribute('data-anim') !== 'flow') return;
      $$('.flow-link', f).forEach(function (l, k) {
        l.style.setProperty('--d', (k * 0.45).toFixed(2));
        var next = l.nextElementSibling;
        if (next) next.style.setProperty('--d', (k * 0.45).toFixed(2));
      });
    });
    if (!('IntersectionObserver' in window)) return null;
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (!en.isIntersecting) return;
        var f = en.target;
        f.classList.add(f.getAttribute('data-anim') === 'flow' ? 'is-in' : 'is-armed');
        io.unobserve(f);
      });
    }, { threshold: 0.45 });
    figs.forEach(function (f) { io.observe(f); });
    return function () { io.disconnect(); };
  }

  /* ---------------------------------------------------------------------------------------------- the wipe you can drag */
  function wipes() {
    $$('.wipe[data-drag]').forEach(function (w) {
      var stage = $('.wipe-stage', w);
      var dragging = false;
      function set(p) {
        p = Math.max(0, Math.min(100, p));
        w.classList.remove('is-armed');
        w.style.setProperty('--split', p.toFixed(1) + '%');
        w.setAttribute('aria-valuenow', Math.round(p));
      }
      function at(e) {
        var r = stage.getBoundingClientRect();
        set((e.clientX - r.left) / r.width * 100);
      }
      w.addEventListener('pointerdown', function (e) {
        dragging = true;
        w.setPointerCapture(e.pointerId);
        at(e);
      });
      w.addEventListener('pointermove', function (e) { if (dragging) at(e); });
      w.addEventListener('pointerup', function () { dragging = false; });
      w.addEventListener('pointercancel', function () { dragging = false; });
      w.addEventListener('keydown', function (e) {
        var now = parseFloat(w.getAttribute('aria-valuenow')) || 50;
        var step = { ArrowLeft: -5, ArrowDown: -5, ArrowRight: 5, ArrowUp: 5, PageDown: -20, PageUp: 20 }[e.key];
        if (step) { e.preventDefault(); set(now + step); }
        else if (e.key === 'Home') { e.preventDefault(); set(0); }
        else if (e.key === 'End') { e.preventDefault(); set(100); }
      });
    });
  }

  /* ---------------------------------------------------------------------------------------------- pictures at full size */
  var dlg = null, img = null, opener = null;   // one dialog for every page shown in this tab
  function lightbox() {
    var links = $$('a[data-zoom]');
    if (!links.length || typeof HTMLDialogElement !== 'function') return;
    if (!dlg) {
      dlg = el('dialog', 'lightbox');
      dlg.innerHTML = '<img alt=""><button type="button" class="btn btn-icon" aria-label="' + esc(site.figure.close) + '">' + site.icons.x + '</button>';
      body.appendChild(dlg);
      img = $('img', dlg);
      var close = function () {
        if (!dlg.open || dlg.classList.contains('is-closing')) return;
        if (calm) { dlg.close(); return; }
        dlg.classList.add('is-closing');
        setTimeout(function () { dlg.classList.remove('is-closing'); dlg.close(); }, 160);
      };
      dlg.addEventListener('click', close);
      dlg.addEventListener('cancel', function (e) { e.preventDefault(); close(); });
      dlg.addEventListener('close', function () { if (opener && opener.isConnected) opener.focus(); });
    }
    links.forEach(function (a) {
      a.addEventListener('click', function (e) {
        if (e.metaKey || e.ctrlKey || e.shiftKey || e.button) return;
        e.preventDefault();
        var src = $$('img', a).filter(function (i) { return i.offsetWidth; })[0] || $('img', a);   // the theme's picture
        img.src = src ? src.getAttribute('src') : a.getAttribute('href');
        img.alt = src ? src.alt : '';
        opener = a;
        dlg.showModal();
      });
    });
  }

  /* ---------------------------------------------------------------------------------------------- the page's own parts */
  // Run for the page as it loads, and again when a script puts another page of the site in its place without a page
  // load: that script swaps the page's parts, then sends vdc:swap.
  var undo = [];
  function parts() {
    copyButtons();
    undo = [spy(), arm()];
    wipes();
    lightbox();
  }
  parts();
  document.addEventListener('vdc:swap', function () {
    undo.forEach(function (f) { if (f) f(); });
    try { site = JSON.parse(body.getAttribute('data-site') || '{}'); } catch (e) { /* keep the page before's */ }
    parts();
  });

  /* ---------------------------------------------------------------------------------------------- the sidebar remembers itself */
  // For this tab only: which cards are folded, and, as the reader leaves for another page of the site, where the
  // sidebar was scrolled and which page it marked. The next page's nav-early script (layout) puts the sidebar back
  // and glides the highlight across before its first paint.
  (function memory() {
    var nav = $('#site-nav');
    if (!nav) return;
    var cards = $$('.nav-card', nav);
    function session(key, value) { try { sessionStorage.setItem(key, value); } catch (e) { /* private mode: nothing kept */ } }
    function folds() { return cards.map(function (c) { return c.open ? '1' : '0'; }).join(''); }
    function here() { return location.pathname.replace(/index\.html$/, ''); }
    session('vdc-folds', folds());
    cards.forEach(function (c) { c.addEventListener('toggle', function () { session('vdc-folds', folds()); }); });
    var saved = '';
    function path(u) { return new URL(u, location.href).pathname.replace(/index\.html$/, ''); }
    function save(to) {
      var tab = -1;
      $$('.top-links a').forEach(function (a, i) { if (a.hasAttribute('aria-current')) tab = i; });
      session('vdc-nav', JSON.stringify({ from: here(), to: to, t: Date.now(), scroll: Math.round(nav.scrollTop),
        navTop: Math.round(nav.getBoundingClientRect().top), tab: tab }));
      saved = to;
    }
    document.addEventListener('click', function (e) {
      var a = e.target.closest('a[href]');
      if (!a || e.defaultPrevented || e.button || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
      if ((a.target && a.target !== '_self') || a.hasAttribute('download')) return;
      var url = new URL(a.href, location.href);
      if (url.origin === location.origin && url.pathname !== location.pathname) save(url.href);
    });
    // Only a link or a search result followed in the site glides: any other way out of the page (an address typed in
    // this tab, the back button, a reload) forgets the record, so the next page simply shows.
    window.addEventListener('pageswap', function (e) {
      var act = e.activation;
      var ours = act && (act.navigationType === 'push' || act.navigationType === 'replace') && act.entry && saved && path(act.entry.url) === path(saved);
      if (!ours) session('vdc-nav', '');
    });
    window.addEventListener('pagehide', function () { if (!saved && !('onpageswap' in window)) session('vdc-nav', ''); });
    window.addEventListener('pageshow', function (e) { if (e.persisted) saved = ''; });
  })();

  /* ---------------------------------------------------------------------------------------------- the sidebar as a sheet on narrow screens */
  (function sheet() {
    var btn = $('.menu-btn'), nav = $('#site-nav'), closeBtn = $('.nav-close');
    if (!btn || !nav) return;
    var scrim = el('div', 'nav-scrim');
    body.appendChild(scrim);
    function narrow() { return getComputedStyle(btn).display !== 'none'; }
    function open() {
      doc.classList.remove('nav-closing');
      doc.classList.add('nav-open');
      btn.setAttribute('aria-expanded', 'true');
      var cur = $('[aria-current="page"]', nav) || closeBtn;
      setTimeout(function () { (cur || nav).focus({ preventScroll: false }); }, 30);
    }
    function close(back) {
      if (!doc.classList.contains('nav-open')) return;
      doc.classList.add('nav-closing');
      doc.classList.remove('nav-open');
      btn.setAttribute('aria-expanded', 'false');
      setTimeout(function () { doc.classList.remove('nav-closing'); }, 230);
      if (back) btn.focus();
    }
    btn.setAttribute('aria-expanded', 'false');
    btn.addEventListener('click', function (e) { if (narrow()) { e.preventDefault(); open(); } });
    closeBtn.addEventListener('click', function (e) { if (narrow()) { e.preventDefault(); close(true); } });
    scrim.addEventListener('click', function () { close(true); });
    document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && doc.classList.contains('nav-open')) close(true); });
    window.addEventListener('resize', function () { if (!narrow()) close(false); });
    // a link of the sheet whose page a script puts in place without a page load (ask.js) closes the sheet at once,
    // and so does a page that makes room for a panel and no longer needs the sheet (vdc:room)
    nav.addEventListener('click', function (e) { if (e.defaultPrevented && e.target.closest('a[href]')) close(false); });
    document.addEventListener('vdc:room', function () { if (!narrow()) close(false); });
  })();
})();
