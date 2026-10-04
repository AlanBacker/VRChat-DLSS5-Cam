#!/usr/bin/env python3
"""Builds the documentation site into site/ from tools/site/ (see tools/site/README.md).

    python3 tools/site/build.py            build every language
    python3 tools/site/build.py --check    build, then check every internal link and picture of the output

Each build also rewrites mintlify/, the copy of the pages for Mintlify's assistant (tools/site/mintlify.py).

Python 3.8+ with Pillow (for the screenshots' WebP copies); nothing else.
"""
import argparse
import html
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'site')
sys.path.insert(0, HERE)
from md import Markdown, plain  # noqa: E402

WARNINGS = []


def warn(msg):
    if msg not in WARNINGS:
        WARNINGS.append(msg)


def read(path):
    with open(path, encoding='utf-8') as fh:
        return fh.read()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(text)


def esc(s):
    return html.escape(s, quote=True)


def merge(base, over):
    out = dict(base)
    for k, v in over.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = merge(out[k], v)
        else:
            out[k] = v
    return out


CONFIG = json.loads(read(os.path.join(HERE, 'site.json')))
IMAGES = {k: v for k, v in json.loads(read(os.path.join(HERE, 'images.json'))).items() if not k.startswith('_')}
LANGS = CONFIG['languages']
PAGES = [dict(p, section=s['id']) for s in CONFIG['sections'] for p in s['pages']]
SLUGS = [p['slug'] for p in PAGES]
STEP_PAGES = [p['slug'] for p in PAGES if p.get('step')]
REPO = CONFIG['repo']
# AI Q&A: the public ID of a Mintlify widget. Empty: the pages carry no trace of the feature (see README).
ASK_ID = CONFIG.get('askWidget', '').strip()

# ------------------------------------------------------------------------------------------------ icons
_ICONS = {}


def icon(name, cls=''):
    if name not in _ICONS:
        path = os.path.join(HERE, 'icons', name + '.svg')
        if not os.path.exists(path):
            raise SystemExit('icon missing: tools/site/icons/%s.svg (copy it from the Lucide package)' % name)
        svg = read(path).strip()
        svg = re.sub(r'\s(width|height)="24"', '', svg)
        _ICONS[name] = svg.replace('<svg ', '<svg class="icon{cls}" aria-hidden="true" focusable="false" ', 1)
    return _ICONS[name].replace('{cls}', (' ' + cls) if cls else '')


# ------------------------------------------------------------------------------------------------ strings and pages
def load_strings():
    en = json.loads(read(os.path.join(HERE, 'strings', 'en.json')))
    out = {}
    for L in LANGS:
        path = os.path.join(HERE, 'strings', L['code'] + '.json')
        data = json.loads(read(path)) if os.path.exists(path) else {}
        if not data:
            warn('strings/%s.json is missing; English strings are used' % L['code'])
        out[L['code']] = merge(en, data)
    return out


STRINGS = load_strings()


def S(lang, key, **fmt):
    node = STRINGS[lang]
    for part in key.split('.'):
        if not isinstance(node, dict) or part not in node:
            warn('string missing: %s' % key)
            return key
        node = node[part]
    if fmt:
        for k, v in fmt.items():
            node = node.replace('{%s}' % k, str(v))
    return node


FRONT = re.compile(r'^---\s*\n(.*?)\n---\s*\n', re.S)
# A passage that holds only while a feature is on:  <!-- if askWidget --> … [<!-- else --> …] <!-- endif -->,
# each marker on a line of its own. With the feature off the page reads exactly as the else part (or without it).
IF_BLOCK = re.compile(r'^<!-- if (\w+) -->\n(.*?)^(?:<!-- else -->\n(.*?))?^<!-- endif -->\n', re.S | re.M)
FEATURES = {'askWidget': bool(ASK_ID)}


def features(text, where):
    def rep(m):
        if m.group(1) not in FEATURES:
            warn('%s: unknown feature in <!-- if %s -->' % (where, m.group(1)))
        return m.group(2) if FEATURES.get(m.group(1)) else (m.group(3) or '')
    text = IF_BLOCK.sub(rep, text)
    if re.search(r'^<!-- (if \w+|else|endif) -->$', text, re.M):
        warn('%s: an <!-- if --> block is not closed' % where)
    return text


def load_page(lang, slug):
    path = os.path.join(HERE, 'content', lang, slug + '.md')
    fallback = False
    if not os.path.exists(path):
        path = os.path.join(HERE, 'content', 'en', slug + '.md')
        fallback = True
        warn('content/%s/%s.md is missing; the English page is used' % (lang, slug))
    text = read(path)
    meta = {}
    m = FRONT.match(text)
    if m:
        for line in m.group(1).split('\n'):
            if ':' in line:
                k, v = line.split(':', 1)
                meta[k.strip()] = v.strip()
        text = text[m.end():]
    text = features(text, os.path.relpath(path, HERE))
    if fallback:
        meta['status'] = 'to-be-translated'
    meta.setdefault('status', 'translated' if lang == 'en' else 'to-be-translated')
    meta.setdefault('nav', meta.get('title', slug))
    return meta, text


# ------------------------------------------------------------------------------------------------ pictures
SHOT_LANGS = ['en'] + [L['code'] for L in LANGS if L['code'] != 'en']


def image_options(name, lang):
    """The entry of images.json for a picture, with the language's own crop/cover/width when it has them."""
    opt = dict(IMAGES[name])
    over = (opt.pop('langs', None) or {}).get(lang) or {}
    opt.update(over)
    return opt


def build_images():
    """WebP copies of the screenshots (made when the source or images.json is newer). Each language can have its own
    screenshots: the English sources are tools/site/shots/<name>.png, the others tools/site/shots/<lang>/<name>.png,
    with their copies in site/assets/img/gen/ and gen/<lang>/. A language without its own source of a picture shows
    the English one. The sources are read from tools/site/shots/ when a picture is there, otherwise (English only)
    from site/assets/img/; only the latter are published as they are, so sources with private pixels belong in
    tools/site/shots/, which is not committed (.gitignore). The copies in gen/ are committed; without a source they
    are kept as they are. Returns {lang: {name: info}}."""
    shots_dir = os.path.join(HERE, 'shots')
    src_dir = os.path.join(OUT, 'assets', 'img')
    gen_root = os.path.join(src_dir, 'gen')
    published = sorted(f for f in os.listdir(src_dir) if f.endswith('.png')) if os.path.isdir(src_dir) else []
    if published:
        warn('%d PNG source(s) in site/assets/img/ are published unpainted; move them to tools/site/shots/ '
             'to publish only the painted WebP copies' % len(published))
    try:
        from PIL import Image
    except ImportError:
        Image = None
        warn('Pillow is not installed: the screenshots are not converted (pip install pillow)')
    cfg_time = os.path.getmtime(os.path.join(HERE, 'images.json'))
    found = {}
    for lang in SHOT_LANGS:
        en = lang == 'en'
        gen_dir = gen_root if en else os.path.join(gen_root, lang)
        os.makedirs(gen_dir, exist_ok=True)
        found[lang] = {}
        keep = set()
        for name in IMAGES:
            opt = image_options(name, lang)
            full = os.path.join(gen_dir, name + '.webp')
            small = os.path.join(gen_dir, name + '-800.webp')
            meta_path = os.path.join(gen_dir, name + '.json')
            src = os.path.join(shots_dir if en else os.path.join(shots_dir, lang), name + '.png')
            if en and not os.path.exists(src):
                src = os.path.join(src_dir, name + '.png')
            if not os.path.exists(src):
                # No source on this computer (the sources are not in the repository): a copy made by an earlier
                # build is kept as it is, so a clone builds the same pages.
                if os.path.exists(full) and os.path.exists(meta_path):
                    found[lang][name] = json.loads(read(meta_path))
                    keep.update({name + '.webp', name + '.json'})
                    if found[lang][name].get('small'):
                        keep.add(name + '-800.webp')
                continue
            fresh = (os.path.exists(full) and os.path.exists(meta_path) and
                     os.path.getmtime(full) >= max(os.path.getmtime(src), cfg_time))
            if not fresh and Image is not None:
                im = Image.open(src).convert('RGB')
                for c in opt.get('cover', []):
                    l, t, r, b = c[:4]
                    at = (c[4], c[5]) if len(c) >= 6 else (min(r + 4, im.width - 1), (t + b) // 2)
                    im.paste(im.getpixel(at), (l, t, r, b))
                if opt.get('crop'):
                    im = im.crop(tuple(opt['crop']))
                cap = opt.get('width', 1600)
                if im.width > cap:
                    im = im.resize((cap, round(im.height * cap / im.width)), Image.LANCZOS)
                im.save(full, 'WEBP', quality=86, method=6)
                info = {'w': im.width, 'h': im.height, 'small': False}
                if im.width > 1000:
                    sm = im.resize((800, round(im.height * 800 / im.width)), Image.LANCZOS)
                    sm.save(small, 'WEBP', quality=84, method=6)
                    info['small'] = True
                write(meta_path, json.dumps(info))
            if os.path.exists(meta_path):
                found[lang][name] = json.loads(read(meta_path))
                keep.update({name + '.webp', name + '.json'})
                if found[lang][name].get('small'):
                    keep.add(name + '-800.webp')
        for f in os.listdir(gen_dir):
            if f not in keep and os.path.isfile(os.path.join(gen_dir, f)):
                os.remove(os.path.join(gen_dir, f))
    return found


# ------------------------------------------------------------------------------------------------ components
def tile(name):
    return '<span class="tile">%s</span>' % icon(name)


def flow(lang, label_key, nodes, extra_cls=''):
    """A row of steps joined by arrows (a column on a narrow screen). nodes: dicts with icon, t, s, optional
    'takes' (list of string keys), 'optional' (True)."""
    parts = ['<figure class="diagram flow %s" data-anim="flow" aria-label="%s"><ol class="flow-row">' % (extra_cls, esc(S(lang, label_key)))]
    for i, nd in enumerate(nodes):
        if i:
            parts.append('<li class="flow-link" aria-hidden="true"><span class="flow-line"></span>%s</li>' % icon('chevron-right', 'flow-arrow'))
        cls = 'flow-node' + (' is-optional' if nd.get('optional') else '')
        logo = nd.get('logo')
        head = ('<span class="tile tile-logo"><img src="{root}assets/mark.svg" alt="" width="28" height="28"></span>' if logo else tile(nd['icon']))
        body = '<span class="flow-title">%s</span>' % esc(S(lang, nd['t']))
        if nd.get('s'):
            body += '<span class="flow-sub">%s</span>' % esc(S(lang, nd['s']))
        if nd.get('optional'):
            body += '<span class="badge badge-adv">%s</span>' % esc(S(lang, 'diagram.pipeOptional'))
        if nd.get('takes'):
            body += '<span class="flow-takes"><span class="flow-takes-label">%s</span>%s</span>' % (
                esc(S(lang, 'diagram.pipeTakes')),
                ''.join('<span class="chip chip-%s">%s</span>' % (k.split('.')[-1].lower(), esc(S(lang, k))) for k in nd['takes']))
        parts.append('<li class="%s" style="--i:%d">%s<span class="flow-text">%s</span></li>' % (cls, i, head, body))
    parts.append('</ol></figure>')
    return ''.join(parts)


DIAGRAMS = {
    'live': ('diagram.liveLabel', [
        {'icon': 'camera', 't': 'diagram.liveCamera', 's': 'diagram.liveCameraSub'},
        {'icon': 'radio', 't': 'diagram.liveSpout', 's': 'diagram.liveSpoutSub'},
        {'logo': True, 't': 'diagram.liveApp', 's': 'diagram.liveAppSub'},
        {'icon': 'file-image', 't': 'diagram.liveFile', 's': 'diagram.liveFileSub'},
    ]),
    'pipeline': ('diagram.pipeLabel', [
        {'icon': 'image', 't': 'diagram.pipeInput', 's': 'diagram.pipeInputSub'},
        {'icon': 'scan-eye', 't': 'diagram.pipeDlaa', 's': 'diagram.pipeDlaaSub', 'optional': True,
         'takes': ['diagram.pipePicture', 'diagram.pipeMotion', 'diagram.pipeDepth']},
        {'icon': 'sparkles', 't': 'diagram.pipeNeural', 's': 'diagram.pipeNeuralSub',
         'takes': ['diagram.pipePicture', 'diagram.pipeMotion']},
        {'icon': 'blend', 't': 'diagram.pipeComposite', 's': 'diagram.pipeCompositeSub'},
        {'icon': 'save', 't': 'diagram.pipeOutput', 's': 'diagram.pipeOutputSub'},
    ]),
    'update': ('diagram.updateLabel', [
        {'icon': 'refresh-cw', 't': 'diagram.updateCheck', 's': 'diagram.updateCheckSub'},
        {'icon': 'bell', 't': 'diagram.updateWindow', 's': 'diagram.updateWindowSub'},
        {'icon': 'download', 't': 'diagram.updateDownload', 's': 'diagram.updateDownloadSub'},
        {'icon': 'circle-check', 't': 'diagram.updateRestart', 's': 'diagram.updateRestartSub'},
    ]),
}


def sphere_layers():
    return (read(os.path.join(HERE, 'partials', 'sphere-facets.svg')).strip(),
            read(os.path.join(HERE, 'partials', 'sphere-bands.svg')).strip())


def wipe(lang, hero=False):
    facets, bands = sphere_layers()
    corners = ''
    if hero:
        # the viewfinder corners of the program icon, in the 200 x 200 box of the sphere drawing
        d = 'M14 38V14H38M162 14H186V38M14 162V186H38M186 162V186H162'
        corners = '<svg class="wipe-corners" viewBox="0 0 200 200" aria-hidden="true"><path d="%s"/></svg>' % d
    box = '<svg class="wipe-layer" viewBox="0 0 200 200" aria-hidden="true">%s</svg>'
    attrs = 'class="wipe%s" data-anim="wipe"' % (' wipe-hero' if hero else ' wipe-demo')
    if not hero:
        attrs += ' data-drag role="slider" tabindex="0" aria-valuemin="0" aria-valuemax="100" aria-valuenow="50" aria-label="%s"' % esc(S(lang, 'wipe.label'))
    else:
        attrs += ' aria-hidden="true"'
    return ('<div %s><div class="wipe-stage">%s<div class="wipe-before">%s</div><div class="wipe-after">%s</div>'
            '<div class="wipe-line"><span class="wipe-handle"></span></div>'
            '<span class="wipe-label wipe-label-l">%s</span><span class="wipe-label wipe-label-r">%s</span></div></div>'
            % (attrs, corners, box % facets, box % bands, esc(S(lang, 'wipe.original')), esc(S(lang, 'wipe.output'))))


def downloads(lang):
    d = CONFIG['downloads']
    cards = [
        ('geforce', 'gpu', 'install.html#package'),
        ('radeon', 'gpu', 'radeon.html'),
        ('linux', 'package', 'linux.html'),
    ]
    out = ['<div class="dl-grid">']
    for key, ic, more in cards:
        url = '%s/releases/latest/download/%s' % (REPO, d[key])
        out.append(
            '<div class="card dl-card dl-%s">'
            '<div class="dl-head">%s<div><h3 class="dl-title">%s</h3><p class="dl-cards">%s</p></div></div>'
            '<p class="dl-note">%s</p><p class="dl-file"><code>%s</code></p>'
            '<div class="dl-actions"><a class="btn %s" href="%s" rel="noopener">%s<span>%s</span></a>'
            '<a class="btn btn-quiet" href="%s"><span>%s</span>%s</a></div></div>'
            % (key, tile(ic), esc(S(lang, 'downloads.%sTitle' % key)), esc(S(lang, 'downloads.%sCards' % key)),
               esc(S(lang, 'downloads.%sNote' % key)), esc(d[key]),
               'btn-accent' if key == 'geforce' else 'btn-ghost', esc(url), icon('download'), esc(S(lang, 'downloads.button')),
               more, esc(S(lang, 'downloads.more')), icon('arrow-right')))
    out.append('</div><p class="dl-all"><a class="ext" href="%s/releases" rel="noopener">%s</a></p>' % (REPO, esc(S(lang, 'home.allReleases'))))
    return ''.join(out)


def tree(lang):
    def row(ic, name, desc, cls=''):
        return ('<li class="tree-row %s">%s<code>%s</code><span class="tree-desc">%s</span></li>'
                % (cls, icon(ic), esc(name), esc(S(lang, desc))))

    def card(ic, title, sub, path, rows):
        return ('<div class="card tree-card"><div class="tree-head">%s<div><p class="tree-title">%s</p><p class="tree-sub">%s</p></div></div>'
                '<p class="tree-path"><code>%s</code></p><ul class="tree-list">%s</ul></div>'
                % (tile(ic), esc(S(lang, title)), esc(S(lang, sub)), esc(path), ''.join(rows)))

    return ('<figure class="diagram tree">' +
            card('folder-open', 'tree.programTitle', 'tree.programSub', 'VRChatDLSS5Cam-win64\\', [
                row('app-window', 'VRChatDLSS5Cam.exe', 'tree.exe', 'is-key'),
                row('folder', 'runtimes\\', 'tree.runtimes'),
                row('folder', 'models\\', 'tree.models'),
                row('folder', 'licenses\\', 'tree.licenses'),
                row('folder', 'docs\\', 'tree.docs'),
                row('file-text', 'README.md', 'tree.readme'),
            ]) +
            card('folder-cog', 'tree.settingsTitle', 'tree.settingsSub', '%LOCALAPPDATA%\\VRChatDLSS5Cam\\', [
                row('file-text', 'settings.ini', 'tree.settingsIni'),
                row('file-text', 'presets.txt', 'tree.presetsTxt'),
                row('file-text', 'log.txt, log-1.txt … log-5.txt', 'tree.logTxt'),
                row('file-text', 'log-crash.txt, crash.txt', 'tree.crashTxt'),
            ]) +
            card('images', 'tree.picturesTitle', 'tree.picturesSub', 'Pictures\\VRChat DLSS5 Cam\\', [
                row('file-image', '*.png, *.mp4, *.webp …', 'tree.pictures'),
            ]) +
            '</figure>')


def pagelist(lang, section, metas):
    pages = [p for p in PAGES if p['section'] == section and p['slug'] != 'index']
    if section == 'guide':
        out = ['<ol class="path">']
        for k, p in enumerate(pages, 1):
            m = metas[p['slug']]
            out.append('<li><a class="card path-card" href="%s.html"><span class="path-num">%d</span>'
                       '<span class="path-text"><span class="path-title">%s</span><span class="path-desc">%s</span></span>%s</a></li>'
                       % (p['slug'], k, esc(m['nav']), esc(m.get('description', '')), icon('arrow-right', 'path-arrow')))
        out.append('</ol>')
    else:
        out = ['<ul class="ref-grid">']
        for p in pages:
            m = metas[p['slug']]
            out.append('<li><a class="card ref-card" href="%s.html">%s<span class="ref-text"><span class="ref-title">%s</span><span class="ref-desc">%s</span></span></a></li>'
                       % (p['slug'], tile(p['icon']), esc(m['nav']), esc(m.get('description', ''))))
        out.append('</ul>')
    return ''.join(out)


def version_key(name):
    m = re.match(r'v(\d+)\.(\d+)\.(\d+)', name)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def changelog_md(lang):
    """The release notes of docs/releases/ as Markdown: per version its lead and the bold first words of every item
    (the full notes stay on GitHub). The files table at the end of a release's notes is left out."""
    rel = os.path.join(ROOT, 'docs', 'releases')
    suffix = next(L['notes'] for L in LANGS if L['code'] == lang)
    names = sorted({m.group(1) for f in os.listdir(rel) for m in [re.match(r'^(v\d+\.\d+\.\d+)\.md$', f)] if m},
                   key=version_key, reverse=True)
    out = []
    for v in names:
        path = os.path.join(rel, v + suffix + '.md')
        if not os.path.exists(path):
            path = os.path.join(rel, v + '.md')
        text = read(path)
        blocks = re.split(r'\n(?=## )', text.strip())
        lead = blocks[0].strip().split('\n\n')[0].replace('\n', ' ')
        out.append('## %s {#%s}' % (S(lang, 'changelog.version', v=v[1:]), v.replace('.', '-')))
        out.append('')
        out.append(lead)
        out.append('')
        for n, b in enumerate(blocks[1:], 1):
            head, _, body = b.partition('\n')
            title = head[3:].strip()
            items = re.findall(r'^- (.*)$', body, re.M)
            if not items:
                continue   # a table (the files) or prose only
            # The id counts the section within the version, so the anchor is the same in every language
            # (the translated notes keep the English order of the sections).
            out.append('### %s {#%s-%d}' % (title, v.replace('.', '-'), n))
            out.append('')
            for it in items:
                m = re.match(r'\*\*(.+?)\*\*', it)
                short = m.group(1) if m else re.split(r'(?<=[.。])\s', it)[0]
                short = short.strip().rstrip(':;,：；，、')
                if short and short[-1] not in '.!?。！？':
                    short += '。' if lang in ('zh', 'ja') else '.'
                out.append('- ' + short)
            out.append('')
        out.append('[%s](%s/releases/tag/%s)' % (S(lang, 'changelog.full'), REPO, v))
        out.append('')
    out.append('[%s](%s/releases)' % (S(lang, 'changelog.earlier'), REPO))
    return '\n'.join(out)


class Ctx:
    def __init__(self, lang, root, images, metas):
        self.lang, self.root, self.images, self.metas = lang, root, images, metas
        self.md = None

    def icon(self, name, cls=''):
        return icon(name, cls)

    def string(self, key):
        return S(self.lang, key)

    def warn(self, msg):
        warn(msg)

    def figure(self, name, alt, caption):
        if name not in IMAGES:
            warn('figure "%s" is not listed in images.json' % name)
            return ''
        sub = '' if self.lang == 'en' else self.lang + '/'
        info = self.images.get(self.lang, {}).get(name)
        if not info:
            sub = ''
            info = self.images.get('en', {}).get(name)
        if not info:
            warn('screenshot missing: %s.png in tools/site/shots/ or site/assets/img/ (left out of the pages)' % name)
            return ''
        opt = image_options(name, self.lang if sub else 'en')
        alt = alt or S(self.lang, 'img.' + name)
        base = '%sassets/img/gen/%s%s' % (self.root, sub, name)
        srcset = ''
        if info.get('small'):
            srcset = ' srcset="%s-800.webp 800w, %s.webp %dw" sizes="(min-width: 900px) 800px, 100vw"' % (base, base, info['w'])
        style = ''
        if opt.get('look') == 'panel':
            style = ' style="--show:%dpx"' % min(opt.get('show', info['w']), info['w'])
        cap = '<figcaption>%s</figcaption>' % caption if caption else ''
        return ('<figure class="shot shot-%s"%s><a class="shot-link" href="%s.webp" data-zoom aria-label="%s">'
                '<img src="%s.webp"%s width="%d" height="%d" alt="%s" loading="lazy" decoding="async"></a>%s</figure>'
                % (opt.get('look', 'window'), style, base, esc(S(self.lang, 'figure.open')), base, srcset,
                   info['w'], info['h'], esc(alt), cap))

    def shortcode(self, name, arg):
        if name == 'diagram':
            label, nodes = DIAGRAMS[arg]
            return flow(self.lang, label, nodes, 'flow-' + arg).replace('{root}', self.root)
        if name == 'wipe':
            return '<figure class="diagram wipe-figure">%s</figure>' % wipe(self.lang)
        if name == 'downloads':
            return downloads(self.lang)
        if name == 'tree':
            return tree(self.lang)
        if name == 'pagelist':
            return pagelist(self.lang, arg, self.metas)
        if name == 'changelog':
            return self.md.blocks(changelog_md(self.lang).split('\n'), top=True)
        warn('unknown shortcode {{%s}}' % name)
        return ''


# ------------------------------------------------------------------------------------------------ page frame
def lang_root(lang):
    return '' if lang == 'en' else '../'


def page_url(from_lang, to_lang, slug):
    """A link from a page of from_lang to the page slug of to_lang."""
    to_dir = next(L['dir'] for L in LANGS if L['code'] == to_lang)
    prefix = lang_root(from_lang) + (to_dir + '/' if to_dir else '')
    return prefix + slug + '.html'


def nav_html(lang, slug, metas):
    out = []
    for sec in CONFIG['sections']:
        items = []
        for p in sec['pages']:
            m = metas[p['slug']]
            cur = ' aria-current="page"' if p['slug'] == slug else ''
            label = S(lang, 'nav.home') if p['slug'] == 'index' else m['nav']
            items.append('<li><a href="%s.html"%s>%s<span>%s</span></a></li>' % (p['slug'], cur, icon(p['icon']), esc(label)))
        out.append('<details class="nav-card" open><summary>%s<span>%s</span>%s</summary><ul>%s</ul></details>'
                   % (icon(sec['icon'], 'sec-icon'), esc(S(lang, 'nav.' + sec['id'])), icon('chevron-down', 'fold'), ''.join(items)))
    return ''.join(out)


def lang_menu(lang, slug):
    cur = STRINGS[lang]['langName']
    items = []
    for L in LANGS:
        here = L['code'] == lang
        items.append('<a href="%s" hreflang="%s" lang="%s" data-lang="%s"%s>%s<span>%s</span></a>'
                     % (page_url(lang, L['code'], slug), L['htmlLang'], L['htmlLang'], L['code'],
                        ' aria-current="true"' if here else '', icon('check', 'mark'), esc(STRINGS[L['code']]['langName'])))
    return ('<details class="menu lang-menu"><summary class="btn btn-flat" aria-label="%s">%s<span class="lang-cur">%s</span>%s</summary>'
            '<div class="menu-pop">%s</div></details>' % (esc(S(lang, 'nav.language')), icon('languages'), esc(cur), icon('chevron-down', 'caret'), ''.join(items)))


def theme_menu(lang):
    opts = [('system', 'monitor'), ('dark', 'moon'), ('light', 'sun')]
    btns = ''.join('<button type="button" data-theme-set="%s" aria-pressed="false">%s<span>%s</span>%s</button>'
                   % (k, icon(ic), esc(S(lang, 'theme.' + k)), icon('check', 'mark')) for k, ic in opts)
    return ('<details class="menu theme-menu js-only"><summary class="btn btn-icon" aria-label="%s" title="%s">'
            '<span class="theme-ic theme-ic-system">%s</span><span class="theme-ic theme-ic-dark">%s</span><span class="theme-ic theme-ic-light">%s</span></summary>'
            '<div class="menu-pop" role="group" aria-label="%s">%s</div></details>'
            % (esc(S(lang, 'theme.label')), esc(S(lang, 'theme.label')), icon('monitor'), icon('moon'), icon('sun'), esc(S(lang, 'theme.label')), btns))


def ask_button(lang):
    """The AI Q&A button beside the search box (only with a widget ID; ask.js runs it)."""
    if not ASK_ID:
        return ''
    return ('<button type="button" class="btn btn-flat ask-btn js-only" aria-expanded="false">%s'
            '<svg class="ask-spin" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><circle cx="12" cy="12" r="9"/></svg>'
            '<span class="ask-label">%s</span></button>' % (icon('message-circle-question-mark', 'ask-ic'), esc(S(lang, 'ask.button'))))


def ask_data(lang):
    """What ask.js needs: the widget ID, Mintlify's code for the language, the panel's words and the page names."""
    import mintlify
    return {
        'id': ASK_ID,
        'language': mintlify.MINT_LANG[lang],
        'labels': {k: S(lang, 'ask.' + k) for k in ('title', 'trigger', 'placeholder', 'disclaimer', 'suggestions')},
        'questions': list(S(lang, 'ask.questions'))[:3],
        'text': {k: S(lang, 'ask.' + k) for k in ('search', 'loading', 'failed')},
        'pages': SLUGS,
        'icon': icon('message-circle-question-mark'),
    }


def toc_html(lang, headings):
    h2 = [(hid, text) for level, hid, text in headings if level == 2]
    if len(h2) < 2:
        return ''
    items = ''.join('<li><a href="#%s">%s</a></li>' % (hid, esc(text)) for hid, text in h2)
    return '<nav class="toc" aria-label="%s"><p class="toc-title">%s</p><ul>%s</ul></nav>' % (
        esc(S(lang, 'nav.onThisPage')), esc(S(lang, 'nav.onThisPage')), items)


def toc_inline(lang, headings):
    """The same list folded at the top of the article, for screens too narrow for the side column."""
    h2 = [(hid, text) for level, hid, text in headings if level == 2]
    if len(h2) < 3:
        return ''
    items = ''.join('<li><a href="#%s">%s</a></li>' % (hid, esc(text)) for hid, text in h2)
    return '<details class="toc-inline"><summary>%s<span>%s</span>%s</summary><ul>%s</ul></details>' % (
        icon('list-checks'), esc(S(lang, 'nav.onThisPage')), icon('chevron-down', 'fold'), items)


def prevnext(lang, slug, metas):
    i = SLUGS.index(slug)
    out = []
    for j, key, ic, cls in ((i - 1, 'nav.prev', 'arrow-left', 'pn-prev'), (i + 1, 'nav.next', 'arrow-right', 'pn-next')):
        if 0 <= j < len(SLUGS):
            s = SLUGS[j]
            title = S(lang, 'nav.home') if s == 'index' else metas[s]['nav']
            out.append('<a class="card pn %s" href="%s.html"><span class="pn-label">%s%s</span><span class="pn-title">%s</span></a>'
                       % (cls, s, icon(ic), esc(S(lang, key)), esc(title)))
        else:
            out.append('<span></span>')
    return '<nav class="prevnext" aria-label="%s / %s">%s</nav>' % (esc(S(lang, 'nav.prev')), esc(S(lang, 'nav.next')), ''.join(out))


def footer_html(lang, slug):
    langs = ' '.join('<a href="%s" hreflang="%s" lang="%s" data-lang="%s"%s>%s</a>'
                     % (page_url(lang, L['code'], slug), L['htmlLang'], L['htmlLang'], L['code'],
                        ' aria-current="true"' if L['code'] == lang else '', esc(STRINGS[L['code']]['langName'])) for L in LANGS)
    return ('<footer class="site-footer"><div class="footer-in">'
            '<div class="footer-brand"><img src="{root}assets/mark.svg" alt="" width="24" height="24"><div>'
            '<p>%s</p><p>%s</p></div></div>'
            '<ul class="footer-links"><li><a class="ext" href="%s" rel="noopener">%s</a></li><li><a class="ext" href="%s/releases" rel="noopener">%s</a></li>'
            '<li><a class="ext" href="%s/issues/new/choose" rel="noopener">%s</a></li><li><a href="privacy.html">%s</a></li>'
            '<li><a href="{root}assets/licenses/lucide-LICENSE.txt">%s</a></li></ul>'
            '<p class="footer-langs">%s %s</p></div></footer>'
            % (esc(S(lang, 'footer.license')), esc(S(lang, 'footer.notAffiliated')), REPO, esc(S(lang, 'footer.source')),
               REPO, esc(S(lang, 'footer.releases')), REPO, esc(S(lang, 'footer.report')), esc(S(lang, 'footer.privacy')),
               esc(S(lang, 'footer.icons')), icon('languages'), langs))


def suggest_data():
    """The suggestion bar's text in every language, for the script (shown in the reader's own language)."""
    out = {}
    for L in LANGS:
        st = STRINGS[L['code']]
        ready = lang_ready(L['code'])
        out[L['code']] = {'text': st['suggest']['text'], 'switch': st['suggest']['switch'],
                          'dismiss': st['suggest']['dismiss'], 'ready': bool(ready)}
    return out


LAYOUT = read(os.path.join(HERE, 'templates', 'layout.html'))


def fill(template, values):
    def rep(m):
        k = m.group(1)
        if k not in values:
            raise SystemExit('layout slot without a value: {{%s}}' % k)
        return values[k]
    return re.sub(r'\{\{([a-z_]+)\}\}', rep, template)


def render_page(L, page, metas, images, search):
    lang, slug = L['code'], page['slug']
    meta, text = load_page(lang, slug)
    root = lang_root(lang)
    ctx = Ctx(lang, root, images, metas)
    md = Markdown(ctx, index_rows=page.get('indexRows', False))
    ctx.md = md
    body = md.render(text)
    pending = meta['status'] != 'translated' and lang != 'en'
    home = page.get('layout') == 'home'

    title = meta.get('title', slug)
    head_title = S(lang, 'site.homeTitle') if home else '%s · %s' % (title, S(lang, 'site.titleSuffix'))
    # search engines want the language versions as full addresses (a link element loads nothing)
    def absolute(code):
        d = next(X['dir'] for X in LANGS if X['code'] == code)
        return CONFIG['siteUrl'] + (d + '/' if d else '') + ('' if slug == 'index' else slug + '.html')
    alternates = '<link rel="canonical" href="%s">' % absolute(lang)
    alternates += ''.join('<link rel="alternate" hreflang="%s" href="%s">' % (X['htmlLang'], absolute(X['code'])) for X in LANGS)
    alternates += '<link rel="alternate" hreflang="x-default" href="%s">' % absolute('en')

    eyebrow = ''
    if page.get('step'):
        n = STEP_PAGES.index(slug) + 1
        eyebrow = '<p class="eyebrow">%s<span>%s · %s</span></p>' % (icon(page['icon']), esc(S(lang, 'nav.guide')), esc(S(lang, 'nav.step', n=n, m=len(STEP_PAGES))))
    elif not home:
        eyebrow = '<p class="eyebrow">%s<span>%s</span></p>' % (icon(page['icon']), esc(S(lang, 'nav.' + page['section'])))

    pending_html = '<p class="pending">%s<span>%s</span></p>' % (icon('languages'), esc(S(lang, 'pending'))) if pending else ''
    if home:
        header = ('<header class="hero"><div class="hero-text"><p class="eyebrow eyebrow-plain">%s</p><h1>%s</h1><p class="lead">%s</p>'
                  '<div class="hero-actions"><a class="btn btn-accent btn-lg" href="#download">%s<span>%s</span></a>'
                  '<a class="btn btn-ghost btn-lg" href="install.html"><span>%s</span>%s</a></div></div>'
                  '<div class="hero-art">%s</div></header>'
                  % (esc(S(lang, 'home.eyebrow')), esc(title), md.inline(meta.get('description', '')), icon('download'),
                     esc(S(lang, 'home.download')), esc(S(lang, 'home.start')), icon('arrow-right'), wipe(lang, hero=True)))
    else:
        header = '<header class="doc-head">%s<h1>%s</h1>%s</header>' % (
            eyebrow, esc(title), '<p class="lead">%s</p>' % md.inline(meta['description']) if meta.get('description') else '')

    article = '%s%s%s<div class="doc-body">%s</div>%s' % (pending_html, header, toc_inline(lang, md.headings), body,
                                                         prevnext(lang, slug, metas))

    for level, hid, text_ in md.headings:
        if level in (2, 3):
            search.append([text_, '%s.html#%s' % (slug, hid), meta.get('nav', title), level])
    for rid, text_ in md.rows:
        search.append([text_, '%s.html#%s' % (slug, rid), meta.get('nav', title), 4])
    search.append([title, slug + '.html', '', 1])

    values = {
        'lang': L['htmlLang'],
        'code': lang,
        'title': esc(head_title),
        'description': esc(plain(md.inline(meta.get('description', '')))),
        'root': root,
        'alternates': alternates,
        'skip': esc(S(lang, 'nav.skip')),
        'site_name': esc(CONFIG['name']),
        'docs': esc(S(lang, 'site.docs')),
        'guide': esc(S(lang, 'nav.guide')),
        'reference': esc(S(lang, 'nav.reference')),
        'guide_current': ' aria-current="true"' if page['section'] == 'guide' else '',
        'reference_current': ' aria-current="true"' if page['section'] == 'reference' else '',
        'first_reference': next(p['slug'] for p in PAGES if p['section'] == 'reference'),
        'download_url': '%s/releases/latest' % REPO,
        'download': esc(S(lang, 'nav.download')),
        'icon_download': icon('download'),
        'icon_menu': icon('menu'),
        'icon_close': icon('x'),
        'icon_search': icon('search'),
        'menu': esc(S(lang, 'nav.menu')),
        'close_menu': esc(S(lang, 'nav.closeMenu')),
        'contents': esc(S(lang, 'nav.contents')),
        'search_placeholder': esc(S(lang, 'search.placeholder')),
        'search_clear': esc(S(lang, 'search.clear')),
        'lang_menu': lang_menu(lang, slug),
        'theme_menu': theme_menu(lang),
        'ask_button': ask_button(lang),
        'nav': nav_html(lang, slug, metas),
        'article': article,
        'article_lang': ' lang="en"' if pending else '',
        'toc': toc_html(lang, md.headings),
        'footer': footer_html(lang, slug),
        'body_class': 'page-%s%s' % (slug, ' is-home' if home else ''),
        'site_data': esc(json.dumps({
            'lang': lang, 'slug': slug, 'root': root,
            'dirs': {X['code']: X['dir'] for X in LANGS},
            'suggest': suggest_data(),
            'search': {'none': S(lang, 'search.none'), 'results': S(lang, 'search.results'), 'result': S(lang, 'search.result')},
            'code': {'copy': S(lang, 'code.copy'), 'copied': S(lang, 'code.copied')},
            'figure': {'close': S(lang, 'figure.close')},
            'icons': {'copy': icon('copy'), 'check': icon('check'), 'x': icon('x'), 'page': icon('file-text'), 'hash': icon('hash')},
            **({'ask': ask_data(lang)} if ASK_ID else {}),
        }, ensure_ascii=False)),
    }
    out = fill(LAYOUT, values).replace('{root}', root)
    target = os.path.join(OUT, L['dir'], slug + '.html')
    write(target, out)
    return meta


def lang_ready(code):
    """True when a language's strings are translated (English always is)."""
    if code == 'en':
        return True
    path = os.path.join(HERE, 'strings', code + '.json')
    own = json.loads(read(path)) if os.path.exists(path) else {}
    return bool(own) and own.get('_status') != 'to-be-translated'


def not_found_page():
    """site/404.html: GitHub Pages serves it at any missing address, so it sets its own base and carries its style.
    The message stands in English and in every translated language; the row under it leads to the start page of
    each language, named in that language, so every reader finds their own."""
    css = read(os.path.join(HERE, 'assets', 'site.css'))
    blocks = []
    for L in LANGS:
        c = L['code']
        if not lang_ready(c) or (c != 'en' and S(c, 'notFound.title') == S('en', 'notFound.title')):
            continue
        tag = 'h1' if c == 'en' else 'h2'
        blocks.append('<section lang="%s" class="nf-block"><%s>%s</%s><p>%s</p></section>'
                      % (L['htmlLang'], tag, esc(S(c, 'notFound.title')), tag, esc(S(c, 'notFound.text'))))
    links = []
    for L in LANGS:
        c = L['code']
        href = (L['dir'] + '/' if L['dir'] else '') + 'index.html'
        links.append('<a class="btn btn-ghost" lang="%s" hreflang="%s" href="%s">%s</a>'
                     % (L['htmlLang'], L['htmlLang'], href, esc(S(c, 'langName'))))
    homes = ' <span class="nf-dot" aria-hidden="true">·</span> '.join(
        '<span lang="%s">%s</span>' % (L['htmlLang'], esc(S(L['code'], 'notFound.home')))
        for L in LANGS if lang_ready(L['code']))
    langs = ('<nav class="nf-langs" aria-label="%s"><p class="nf-home">%s%s</p><div class="nf-links">%s</div></nav>'
             % (esc(S('en', 'notFound.home')), icon('languages'), homes, ''.join(links)))
    base = CONFIG['basePath']
    return ('<!doctype html><html lang="en" class="no-js"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width, initial-scale=1"><meta name="color-scheme" content="light dark">'
            '<base href="%s"><script>(function(){var d=document.documentElement;d.className="js";'
            'if(location.protocol==="file:"||location.pathname.indexOf(%s)!==0){var b=document.querySelector("base");if(b)b.parentNode.removeChild(b);}'
            'try{var t=localStorage.getItem("vdc-theme");if(t==="dark"||t==="light")d.setAttribute("data-theme",t);}catch(e){}})();</script>'
            '<title>%s · %s</title><link rel="icon" href="assets/logo.svg" type="image/svg+xml"><style>%s</style></head>'
            '<body class="page-404"><main class="nf"><img class="nf-logo" src="assets/logo.svg" alt="%s" width="72" height="72">%s%s</main></body></html>\n'
            % (base, json.dumps(base), esc(S('en', 'notFound.title')), esc(CONFIG['name']), css, esc(CONFIG['name']),
               ''.join(blocks), langs))


def build():
    images = build_images()
    os.makedirs(OUT, exist_ok=True)
    # assets
    adir = os.path.join(OUT, 'assets')
    for f in ('site.css', 'site.js', 'logo.svg', 'mark.svg', 'icon-32.png', 'icon-180.png'):
        shutil.copyfile(os.path.join(HERE, 'assets', f), os.path.join(adir, f))
    if ASK_ID:   # the AI Q&A rides in site.css and site.js: no request of its own, nothing at all without an ID
        for f, extra in (('site.css', 'ask.css'), ('site.js', 'ask.js')):
            write(os.path.join(adir, f), read(os.path.join(adir, f)) + read(os.path.join(HERE, 'assets', extra)))
    os.makedirs(os.path.join(adir, 'licenses'), exist_ok=True)
    shutil.copyfile(os.path.join(HERE, 'icons', 'LICENSE'), os.path.join(adir, 'licenses', 'lucide-LICENSE.txt'))
    write(os.path.join(OUT, '.nojekyll'), '')
    # pages
    for L in LANGS:
        metas = {p['slug']: load_page(L['code'], p['slug'])[0] for p in PAGES}
        search = []
        for page in PAGES:
            render_page(L, page, metas, images, search)
        write(os.path.join(adir, 'search-%s.js' % L['code']),
              'window.VDC_SEARCH=%s;\n' % json.dumps(search, ensure_ascii=False, separators=(',', ':')))
        # pages of a language that no longer exist in site.json
        ldir = os.path.join(OUT, L['dir'])
        for f in os.listdir(ldir):
            if f.endswith('.html') and f[:-5] not in SLUGS and not (L['code'] == 'en' and f == '404.html'):
                os.remove(os.path.join(ldir, f))
    write(os.path.join(OUT, '404.html'), not_found_page())
    missing = [n for n in IMAGES if n not in images['en']]
    return images, missing


def check():
    """Every href/src of the output that points inside the site must exist."""
    bad = []
    for dirpath, _, files in os.walk(OUT):
        for f in files:
            if not f.endswith('.html') or f == '404.html':
                continue
            path = os.path.join(dirpath, f)
            text = read(path)
            ids = set(re.findall(r'\sid="([^"]+)"', text))
            for attr, url in re.findall(r'\s(href|src)="([^"]+)"', text):
                if re.match(r'^[a-z]+:', url) or url.startswith('//'):
                    continue
                target, _, frag = url.partition('#')
                if not target:
                    if frag and frag not in ids:
                        bad.append('%s: #%s' % (os.path.relpath(path, OUT), frag))
                    continue
                full = os.path.normpath(os.path.join(dirpath, target))
                if not os.path.exists(full):
                    bad.append('%s: %s' % (os.path.relpath(path, OUT), url))
                elif frag and full.endswith('.html'):
                    if ('id="%s"' % frag) not in read(full):
                        bad.append('%s: %s (no such anchor)' % (os.path.relpath(path, OUT), url))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='check the internal links and pictures after the build')
    a = ap.parse_args()
    images, missing = build()
    n = sum(1 for L in LANGS for _ in PAGES)
    print('built %d pages in %d languages into %s' % (n, len(LANGS), os.path.relpath(OUT, ROOT)))
    print('screenshots: %d of %d present (English); own pictures: %s' % (len(images['en']), len(IMAGES),
          ', '.join('%s %d' % (l, len(images[l])) for l in SHOT_LANGS if l != 'en')))
    for m in missing:
        print('  missing: %s.png (tools/site/shots/ or site/assets/img/)' % m)
    other = [w for w in WARNINGS if not w.startswith('screenshot missing')]
    for w in other:
        print('warning:', w)
    import mintlify   # the copy of the pages that Mintlify's assistant reads (see mintlify.py)
    print('Mintlify copy: %d pages into %s' % (mintlify.export(), os.path.relpath(mintlify.OUT, ROOT)))
    if a.check:
        bad = check()
        for b in bad:
            print('broken link:', b)
        print('link check: %s' % ('%d broken' % len(bad) if bad else 'all internal links and pictures resolve'))
        if bad:
            sys.exit(1)


if __name__ == '__main__':
    main()
