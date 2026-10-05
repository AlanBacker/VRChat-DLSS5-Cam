#!/usr/bin/env python3
"""Checks the places an Ask AI answer can point at (src/ui/JumpTargets.h).

  python3 tools/check_jump_targets.py [--names]

Errors (exit status 1):
  - a target's documentation page or anchor is missing in one of the four languages of the docs site
    (tools/site/content/<lang>/<page>.md, an explicit {#id} or a heading's slug);
  - a target has no marker in src/ui/MainUI.cpp (JumpBegin/JumpItem/JumpRect with its id, Section with a header's
    section code), or a marker names no target;
  - a row of the settings page has no target and is not in EXCLUDED below, or the rows differ between languages;
  - a target names a string that I18n.h does not have, a fallback that does not exist, or a fallback loop.

Notes (no failure): names two targets share in a language (the earlier target takes the name, which is what the
table's order is for), names too short or too long for the page to mark, targets with no name in a language.
--names lists every name.
"""
import re
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANGS = ('en', 'zh', 'ja', 'ko')
SITE = ROOT / 'tools' / 'site' / 'content'
sys.path.insert(0, str(ROOT / 'tools' / 'site'))
from md import slug  # noqa: E402  (the site's own slugs, for headings without an explicit id)

# Rows of the settings page that deliberately have no target.
EXCLUDED = {
    'edition': 'the button shows only on the other maker\'s card, in a notice or the About section; an answer that '
               'names it is left to its page link',
}

# Names two targets share on purpose (the earlier one is the right one), by the set of target ids.
EXPECTED_SHARED = [
    {'save-button', 'capture'},   # zh/ja/ko: one word for the button and the section; "... section" picks the section
]

errors = []
notes = []


def err(msg):
    errors.append(msg)


def parse_i18n():
    text = (ROOT / 'src' / 'core' / 'I18n.h').read_text(encoding='utf-8')
    table = {}
    for m in re.finditer(r'\bX\((\w+),((?:\s*"(?:[^"\\]|\\.)*"\s*,?){4})\s*\)', text):
        vals = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
        table[m.group(1)] = [v.replace('\\"', '"').replace('\\n', '\n').replace('\\\\', '\\') for v in vals]
    return table


def parse_targets():
    text = (ROOT / 'src' / 'ui' / 'JumpTargets.h').read_text(encoding='utf-8')
    body = text[text.index('kJumpTargets[] = {'):]
    body = body[:body.index('\n};')]
    targets = []
    pat = re.compile(r'\{\s*"([\w-]+)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*([^,]+),\s*([^,]+),\s*\{([^}]*)\},\s*"([^"]*)"\s*\}')
    for m in pat.finditer(body):
        tid, doc, section, fallback, flags, modes, labels, aliases = m.groups()
        targets.append({
            'id': tid, 'doc': doc, 'section': section, 'fallback': fallback,
            'flags': set(f.strip() for f in flags.split('|') if f.strip() not in ('0', '')),
            'labels': re.findall(r'Str::(\w+)', labels),
            'aliases': [a for a in aliases.split('|') if a],
        })
    count = len(re.findall(r'^\s*\{\s*"[\w-]+",\s*"', body, re.M))
    if count != len(targets):
        err('JumpTargets.h: %d entries, %d understood (the pattern of this script needs a look)' % (count, len(targets)))
    return targets


def anchors(lang, page):
    """The ids of a page of the site in a language, or None when the page is missing."""
    path = SITE / lang / (page[:-5] + '.md' if page.endswith('.html') else page)
    if not path.is_file():
        return None
    text = path.read_text(encoding='utf-8')
    ids = set(re.findall(r'\{#([A-Za-z0-9_-]+)\}', text))
    for m in re.finditer(r'^#{2,4}\s+(.*?)\s*$', text, re.M):
        if '{#' not in m.group(1):
            ids.add(slug(m.group(1)))
    return ids


def jkey(text):
    """The page's key for a name (jkey in piece 3 of kPageJs, src/core/AskPanel.cpp)."""
    s = unicodedata.normalize('NFKC', text).lower()
    s = re.sub(r'[\s​]+', '', s)
    s = re.sub(r'^["\'“”‘’「」『』]+|["\'“”‘’「」『』]+$', '', s)
    s = re.sub(r'[.:]+$', '', s)
    return s


def names(t, i18n, lang):
    """A target's names as App::AskConfig hands them to the page."""
    col = LANGS.index(lang)
    out = []
    for s in t['labels']:
        if s not in i18n:
            continue
        v = i18n[s][col].split('##')[0].rstrip(' ')
        if v and '%' not in v and '\n' not in v and v not in out:
            out.append(v)
    return out + t['aliases']


def main():
    list_names = '--names' in sys.argv
    i18n = parse_i18n()
    targets = parse_targets()
    ids = [t['id'] for t in targets]
    by_id = {t['id']: t for t in targets}
    for tid in sorted(set(i for i in ids if ids.count(i) > 1)):
        err('target %s is in the table twice' % tid)

    # Strings, fallbacks.
    for t in targets:
        for s in t['labels']:
            if s not in i18n:
                err('%s: I18n.h has no string %s' % (t['id'], s))
        if t['fallback'] and t['fallback'] not in by_id:
            err('%s: its fallback %s is not a target' % (t['id'], t['fallback']))
        seen, cur = set(), t
        while cur and cur['fallback']:
            if cur['id'] in seen:
                err('%s: its fallbacks go round in a loop' % t['id'])
                break
            seen.add(cur['id'])
            cur = by_id.get(cur['fallback'])
        if 'JumpNoPlace' in t['flags'] and not t['fallback']:
            err('%s: has no place of its own and no fallback' % t['id'])

    # Documentation: every page and anchor in every language.
    cache = {}
    for t in targets:
        page, _, anchor = t['doc'].partition('#')
        for lang in LANGS:
            if (lang, page) not in cache:
                cache[(lang, page)] = anchors(lang, page)
            got = cache[(lang, page)]
            if got is None:
                err('%s: the %s documentation has no page %s' % (t['id'], lang, page))
            elif anchor and anchor not in got:
                err('%s: %s/%s has no anchor #%s' % (t['id'], lang, page, anchor))

    # Markers in MainUI.cpp.
    ui = (ROOT / 'src' / 'ui' / 'MainUI.cpp').read_text(encoding='utf-8')
    marked = set(re.findall(r'\bJump(?:Begin|Item|Rect)\("([\w-]+)"', ui))
    for arr in re.findall(r'kModeJumps\[\]\s*=\s*\{([^}]*)\}', ui):
        marked |= set(re.findall(r'"([\w-]+)"', arr))
    sections = set(re.findall(r'\bSection\(TR\(\w+\),\s*"([\w-]+)"', ui))
    for t in targets:
        if t['section'] and t['section'] not in sections:
            err('%s: its section %s is not a section of the sidebar' % (t['id'], t['section']))
        if 'JumpNoPlace' in t['flags']:
            continue
        if 'JumpHeader' in t['flags']:
            if t['section'] not in sections:
                err('%s: no Section(..., "%s", ...) in MainUI.cpp' % (t['id'], t['section']))
        elif t['id'] not in marked:
            err('%s: no JumpBegin/JumpItem/JumpRect("%s") in MainUI.cpp' % (t['id'], t['id']))
    for m in sorted(marked - set(ids)):
        err('MainUI.cpp marks "%s", which is not a target' % m)

    # The settings page: every row has a target, the same rows in every language.
    docs = {t['doc'].partition('#')[2] for t in targets if t['doc'].startswith('settings.html#')}
    rows = {}
    for lang in LANGS:
        text = (SITE / lang / 'settings.md').read_text(encoding='utf-8')
        rows[lang] = re.findall(r'^\|[^\n]*\{#([\w-]+)\}\s*\|', text, re.M)
        rows[lang] += re.findall(r'^#{2,4}\s[^\n]*\{#([\w-]+)\}', text, re.M)
    for lang in LANGS[1:]:
        if set(rows[lang]) != set(rows['en']):
            err('settings.md: the %s rows differ from the English ones (%s)' % (
                lang, ', '.join(sorted(set(rows[lang]) ^ set(rows['en'])))))
    for r in rows['en']:
        if r not in docs and r not in EXCLUDED:
            err('settings.md #%s has no target in JumpTargets.h (add one, or add it to EXCLUDED with the reason)' % r)
    for r in EXCLUDED:
        if r not in rows['en']:
            err('EXCLUDED names #%s, which the settings page no longer has' % r)

    # Names, per language.
    for lang in LANGS:
        owners = {}
        for i, t in enumerate(targets):
            n = names(t, i18n, lang)
            if not n:
                if t['labels'] or t['aliases']:
                    notes.append('%s: %s has no usable name (the page cannot mark it)' % (lang, t['id']))
                continue
            for v in n:
                k = jkey(v)
                if len(k) < 2 or len(k) > 60:
                    notes.append('%s: %s: "%s" is outside the 2-60 characters the page marks' % (lang, t['id'], v))
                    continue
                o = owners.setdefault(k, [])
                if t['id'] not in [x[1] for x in o]:
                    o.append((i, t['id'], v))
                if list_names:
                    print('%s  %-20s %s' % (lang, t['id'], v))
        for k, o in sorted(owners.items()):
            if len(o) < 2:
                continue
            group = {x[1] for x in o}
            kind = 'meant' if any(group == e for e in EXPECTED_SHARED) else 'not meant'
            notes.append('%s: "%s" is shared (%s) by %s; %s takes it' % (
                lang, o[0][2], kind, ', '.join(x[1] for x in o), o[0][1]))

    for n in notes:
        print('note:', n)
    for e in errors:
        print('error:', e)
    print('%d targets, %d settings rows (%d left out on purpose), %d errors, %d notes' % (
        len(targets), len(rows['en']), len(EXCLUDED), len(errors), len(notes)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
