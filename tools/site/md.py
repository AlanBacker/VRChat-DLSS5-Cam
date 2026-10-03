"""The Markdown subset the documentation pages are written in (tools/site/README.md describes it for writers and
translators). No third-party package: Python's standard library only.

Blocks: ## / ### / #### headings with an optional {#id}; paragraphs; "- " and "1. " lists (ordered lists are drawn as
numbered steps), items continued by indented lines; "=> " check lines (what you should see after a step); GitHub
callouts (> [!TIP], > [!WARNING], > [!NOTE]); tables; ``` code blocks; a figure line ![alt](name "caption"); a
shortcode line {{name}} or {{name:argument}}; <!-- comments --> are dropped.
Inline: **bold**, *italic*, `code`, [text](link), [[A control's name]], ((Ctrl+Alt+P)) keys, {adv} badge.
"""
import html
import re

CJK = re.compile(r'[　-〿぀-ヿ㐀-䶿一-鿿가-힯＀-￯]')
HEADING = re.compile(r'^(#{2,4})\s+(.*?)\s*(?:\{#([A-Za-z0-9_-]+)\})?\s*$')
FIGURE = re.compile(r'^!\[(.*?)\]\(([A-Za-z0-9_./-]+?)(?:\s+"(.*)")?\)\s*$')
SHORTCODE = re.compile(r'^\{\{\s*([a-z][a-z0-9-]*)(?::\s*([^}]*?))?\s*\}\}\s*$')
OL = re.compile(r'^(\d+)\.\s+(.*)$')
UL = re.compile(r'^[-*]\s+(.*)$')
CALLOUT = re.compile(r'^\[!(TIP|WARNING|NOTE)\]\s*(.*)$', re.I)
ROW_ID = re.compile(r'\s*\{#([A-Za-z0-9_-]+)\}\s*$')


def slug(text):
    s = re.sub(r'<[^>]+>', '', text)
    s = html.unescape(s).lower()
    s = re.sub(r'[^a-z0-9]+', '-', s).strip('-')
    return s[:60]


def plain(htmltext):
    return html.unescape(re.sub(r'<[^>]+>', '', htmltext)).strip()


def join_lines(lines):
    out = ''
    for ln in lines:
        ln = ln.strip()
        if not out:
            out = ln
        elif CJK.match(out[-1:]) and CJK.match(ln[:1]):
            out += ln
        else:
            out += ' ' + ln
    return out



def glue_end(inner, tail):
    """Keeps the heading's link icon on the line of its last word (or last character in CJK text), so a
    heading that fills the line does not leave the hidden icon alone on a line of its own."""
    if inner.endswith('>'):
        return inner + tail
    m = re.search(r'(\S+)$', inner)
    if not m:
        return inner + tail
    word = m.group(1)
    if re.search(r'[\u3000-\u9fff\uac00-\ud7af\uff00-\uffef]', word):
        amp = word.rfind('&')
        cut = amp if (amp >= 0 and word.endswith(';') and ';' not in word[amp:-1]) else len(word) - 1
        word = word[cut:]
    return inner[:len(inner) - len(word)] + '<span class="h-end">' + word + tail + '</span>'

class Markdown:
    """ctx: an object with
         icon(name, cls='') -> svg markup
         string(key) -> interface string of the page's language
         figure(name, alt, caption_html) -> html ('' when the picture is missing)
         shortcode(name, arg) -> html
         warn(message)
    """

    def __init__(self, ctx, index_rows=False):
        self.ctx = ctx
        self.index_rows = index_rows
        self.headings = []      # (level, id, text)
        self.rows = []          # (id, text) of table rows with an id
        self.ids = set()

    # ---------------------------------------------------------------- inline
    def inline(self, text):
        codes = []

        def keep_code(m):
            codes.append('<code>%s</code>' % html.escape(m.group(1), quote=False))
            return '\x00%d\x00' % (len(codes) - 1)

        text = re.sub(r'`([^`]+)`', keep_code, text)
        text = html.escape(text, quote=False)

        def keys(m):
            parts = [p.strip() for p in m.group(1).split('+')]
            return '<span class="keys">' + '<span class="plus">+</span>'.join('<kbd>%s</kbd>' % p for p in parts if p) + '</span>'

        text = re.sub(r'\(\((.+?)\)\)', keys, text)
        text = re.sub(r'\[\[(.+?)\]\]', lambda m: '<span class="ui">%s</span>' % m.group(1), text)
        text = text.replace('{adv}', '<span class="badge badge-adv">%s</span>' % html.escape(self.ctx.string('settings.advancedBadge')))

        def link(m):
            label, url = m.group(1), m.group(2)
            url = url.replace('&amp;', '&')
            if re.match(r'^[a-z]+:', url):
                return '<a class="ext" href="%s" rel="noopener">%s</a>' % (html.escape(url), label)
            return '<a href="%s">%s</a>' % (html.escape(url), label)

        text = re.sub(r'\[([^\]]+)\]\(([^)\s]+)\)', link, text)
        text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
        text = re.sub(r'(?<![A-Za-z0-9_*])\*(?!\s)(.+?)(?<!\s)\*(?![A-Za-z0-9_*])', r'<em>\1</em>', text)
        text = re.sub(r'\x00(\d+)\x00', lambda m: codes[int(m.group(1))], text)
        return text

    # ---------------------------------------------------------------- blocks
    def unique(self, base):
        base = base or 'section'
        i, cand = 2, base
        while cand in self.ids:
            cand = '%s-%d' % (base, i)
            i += 1
        self.ids.add(cand)
        return cand

    def render(self, text):
        text = re.sub(r'<!--.*?-->', '', text, flags=re.S)
        lines = text.replace('\r\n', '\n').replace('\t', '    ').split('\n')
        return self.blocks(lines, top=True)

    def blocks(self, lines, top=False):
        out = []
        i, n = 0, len(lines)
        while i < n:
            line = lines[i]
            s = line.strip()
            if not s:
                i += 1
                continue
            m = HEADING.match(s)
            if m and top:
                level = len(m.group(1))
                inner = self.inline(m.group(2))
                hid = self.unique(m.group(3) or slug(m.group(2)) or 'h-%d' % (len(self.headings) + 1))
                self.headings.append((level, hid, plain(inner)))
                anchor = '<a class="anchor" href="#%s" aria-label="%s">%s</a>' % (
                    hid, html.escape(self.ctx.string('nav.linkHere')), self.ctx.icon('link'))
                out.append('<h%d id="%s">%s</h%d>' % (level, hid, glue_end(inner, anchor), level))
                i += 1
                continue
            if s.startswith('```'):
                lang = s[3:].strip()
                j = i + 1
                body = []
                while j < n and not lines[j].strip().startswith('```'):
                    body.append(lines[j])
                    j += 1
                indent = min((len(b) - len(b.lstrip()) for b in body if b.strip()), default=0)
                code = '\n'.join(b[indent:] for b in body)
                esc_code = html.escape(code, quote=False)
                if lang != 'json':
                    # a command wraps between its words, never after the hyphen of an option such as --process
                    esc_code = re.sub(r'(\S+)', r'<span class="w">\1</span>', esc_code)
                    # and a wrapped line hangs under its first word, so it does not read as the next command
                    esc_code = '\n'.join('<span class="ln">%s</span>' % ln for ln in esc_code.split('\n'))
                out.append('<div class="code"%s><pre><code>%s</code></pre></div>' % (
                    ' data-lang="%s"' % html.escape(lang) if lang else '', esc_code))
                i = j + 1
                continue
            m = FIGURE.match(s)
            if m:
                cap = self.inline(m.group(3)) if m.group(3) else ''
                out.append(self.ctx.figure(m.group(2), m.group(1), cap))
                i += 1
                continue
            m = SHORTCODE.match(s)
            if m:
                out.append(self.ctx.shortcode(m.group(1), m.group(2)))
                i += 1
                continue
            if s.startswith('>'):
                j = i
                body = []
                while j < n and lines[j].strip().startswith('>'):
                    body.append(re.sub(r'^\s*>\s?', '', lines[j]))
                    j += 1
                out.append(self.quote(body))
                i = j
                continue
            if s.startswith('=>'):
                j = i + 1
                body = [s[2:]]
                while j < n and lines[j].strip() and not self.starts_block(lines[j]):
                    body.append(lines[j])
                    j += 1
                out.append('<p class="check">%s<span>%s</span></p>' % (
                    self.ctx.icon('circle-check', 'check-icon'), self.inline(join_lines(body))))
                i = j
                continue
            if s.startswith('|') and i + 1 < n and re.match(r'^\s*\|?\s*:?-{2,}', lines[i + 1]):
                j = i
                rows = []
                while j < n and lines[j].strip().startswith('|'):
                    rows.append(lines[j].strip())
                    j += 1
                out.append(self.table(rows))
                i = j
                continue
            if OL.match(s) or UL.match(s):
                i = self.list(lines, i, out)
                continue
            # paragraph
            j = i
            body = []
            while j < n and lines[j].strip() and (j == i or not self.starts_block(lines[j])):
                body.append(lines[j])
                j += 1
            out.append('<p>%s</p>' % self.inline(join_lines(body)))
            i = j
        return '\n'.join(o for o in out if o)

    def starts_block(self, line):
        s = line.strip()
        return bool(HEADING.match(s) or s.startswith('```') or s.startswith('>') or s.startswith('=>') or
                    s.startswith('|') or OL.match(s) or UL.match(s) or FIGURE.match(s) or SHORTCODE.match(s))

    def quote(self, body):
        first = body[0].strip() if body else ''
        m = CALLOUT.match(first)
        if not m:
            return '<blockquote>%s</blockquote>' % self.blocks(body)
        kind = m.group(1).lower()
        title = m.group(2).strip() or self.ctx.string('callout.' + kind)
        icon = {'tip': 'lightbulb', 'warning': 'triangle-alert', 'note': 'info'}[kind]
        inner = self.blocks(body[1:])
        return ('<aside class="callout callout-%s"><p class="callout-title">%s<span>%s</span></p>%s</aside>'
                % (kind, self.ctx.icon(icon), self.inline(title), inner))

    def list(self, lines, i, out):
        n = len(lines)
        ordered = bool(OL.match(lines[i].strip()))
        base = len(lines[i]) - len(lines[i].lstrip())
        items = []
        start = int(OL.match(lines[i].strip()).group(1)) if ordered else 1
        while i < n:
            line = lines[i]
            s = line.strip()
            ind = len(line) - len(line.lstrip())
            m = (OL if ordered else UL).match(s) if ind == base else None
            if m:
                items.append([m.group(2) if ordered else m.group(1)])
                i += 1
                continue
            if not s:
                # a blank line ends the list unless the next line is indented under the item or is another item
                k = i + 1
                while k < n and not lines[k].strip():
                    k += 1
                if k < n and (len(lines[k]) - len(lines[k].lstrip()) > base or
                              (len(lines[k]) - len(lines[k].lstrip()) == base and (OL if ordered else UL).match(lines[k].strip()))):
                    if items:
                        items[-1].append('')
                    i += 1
                    continue
                break
            if ind > base and items:
                items[-1].append(line[base:])
                i += 1
                continue
            if items and not self.starts_block(line) and items[-1][-1].strip():
                items[-1].append(line[base:])   # a lazy continuation of the item's paragraph
                i += 1
                continue
            break
        html_items = []
        for item in items:
            # the item's own lines: dedent the continuation lines to the item's text column
            first, rest = item[0], item[1:]
            ind = min((len(r) - len(r.lstrip()) for r in rest if r.strip()), default=0)
            body = [first] + [r[ind:] if len(r) >= ind else r.strip() for r in rest]
            inner = self.blocks(body)
            if not ordered and inner.startswith('<p>') and inner.count('<p') == 1 and inner.endswith('</p>'):
                inner = inner[3:-4]
            html_items.append('<li>%s</li>' % inner)
        if ordered:
            attr = ' start="%d" style="counter-reset:step %d"' % (start, start - 1) if start != 1 else ''
            out.append('<ol class="steps"%s>%s</ol>' % (attr, ''.join(html_items)))
        else:
            out.append('<ul>%s</ul>' % ''.join(html_items))
        return i

    def table(self, rows):
        def cells(r):
            r = r.strip()
            if r.startswith('|'):
                r = r[1:]
            if r.endswith('|') and not r.endswith('\\|'):
                r = r[:-1]
            parts = re.split(r'(?<!\\)\|', r)
            return [p.strip().replace('\\|', '|') for p in parts]

        head = cells(rows[0])
        aligns = []
        for c in cells(rows[1]):
            aligns.append('right' if c.endswith(':') and not c.startswith(':') else 'center' if c.startswith(':') and c.endswith(':') else '')
        h = ['<div class="table-wrap" tabindex="0"><table><thead><tr>']
        for k, c in enumerate(head):
            h.append('<th%s>%s</th>' % (' class="ta-%s"' % aligns[k] if k < len(aligns) and aligns[k] else '', self.inline(c)))
        h.append('</tr></thead><tbody>')
        for r in rows[2:]:
            cs = cells(r)
            rid = ''
            if cs:
                m = ROW_ID.search(cs[0])
                if m:
                    rid = self.unique(m.group(1))
                    cs[0] = ROW_ID.sub('', cs[0])
                    self.rows.append((rid, plain(self.inline(cs[0]))))
            h.append('<tr%s>' % (' id="%s"' % rid if rid else ''))
            for k, c in enumerate(cs):
                tag = 'th scope="row"' if k == 0 and rid else 'td'
                endtag = 'th' if k == 0 and rid else 'td'
                h.append('<%s>%s</%s>' % (tag, self.inline(c), endtag))
            h.append('</tr>')
        h.append('</tbody></table></div>')
        return ''.join(h)
