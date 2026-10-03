#!/usr/bin/env python3
"""Draws the program's mark as SVG for the documentation site, from the geometry of tools/make_app_icon.py.

Writes, into tools/site/:
  assets/logo.svg           the full icon (tile, split sphere, viewfinder corners), as at 256 px; also the favicon
  assets/mark.svg           the small icon (tile, 20 facets, three bands), as the app draws it in its top bar
  assets/icon-32.png, assets/icon-180.png   bitmap icons for browsers that take no SVG icon
  partials/sphere-facets.svg, partials/sphere-bands.svg
                            the whole sphere in both looks (facets: the picture before, bands: after DLSS 5), in a
                            200 x 200 box, for the hero picture and the wipe illustration, which split them themselves

The outputs are committed, so building the site does not need pycairo; run this again only when the icon changes.
Needs pycairo (make_app_icon.py imports it) and Pillow.  Run: python3 tools/site/logo.py
"""
import io
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import make_app_icon as ic  # noqa: E402


def hexrgb(c):
    return '#%02X%02X%02X' % tuple(int(round(v * 255)) for v in c[:3])


def f(v):
    s = ('%.2f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def squircle_path(x, y, s, r):
    k = 0.5523 * 1.28
    pts = [
        ('M', (x + r, y)), ('L', (x + s - r, y)),
        ('C', (x + s - r + r * k, y, x + s, y + r - r * k, x + s, y + r)), ('L', (x + s, y + s - r)),
        ('C', (x + s, y + s - r + r * k, x + s - r + r * k, y + s, x + s - r, y + s)), ('L', (x + r, y + s)),
        ('C', (x + r - r * k, y + s, x, y + s - r + r * k, x, y + s - r)), ('L', (x, y + r)),
        ('C', (x, y + r - r * k, x + r - r * k, y, x + r, y)),
    ]
    return ''.join(op + ' '.join(f(v) for v in vals) for op, vals in pts) + 'Z'


def facet_polys(cx, cy, R, tones, ends, sub, stroke):
    L = ic.norm(ic.LIGHT)
    out = []
    for corners, n in ic.facets(sub):
        col = hexrgb(ic.ramp(tones, ends, ic.dot(n, L)))
        pts = ' '.join('%s,%s' % (f(cx + p[0] * R), f(cy - p[1] * R)) for p in corners)
        out.append('<polygon points="%s" fill="%s" stroke="%s" stroke-width="%s" stroke-linejoin="round"/>'
                   % (pts, col, col, f(stroke)))
    return out


def band_paths(cx, cy, R, tones, ends):
    out = ['<circle cx="%s" cy="%s" r="%s" fill="%s"/>' % (f(cx), f(cy), f(R), tones[-1])]
    for k in range(len(ends) - 1, -1, -1):
        pts = ic.band_outline(ends[k], 120)
        d = 'M' + 'L'.join('%s %s' % (f(cx + x * R), f(cy - y * R)) for x, y in pts) + 'Z'
        out.append('<path d="%s" fill="%s"/>' % (d, tones[k]))
    return out


def icon_svg(s, title):
    """The icon as make_app_icon.draw() paints it at s px, as an SVG of viewBox 0 0 s s."""
    m = 0 if s <= 32 else s * 0.035
    tones, ends, sub = ic.tier(s)
    cx, cy, R, gap = ic.geom(s)
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" role="img" aria-label="%s">' % (s, s, title),
             '<defs><clipPath id="l%d"><rect x="0" y="0" width="%s" height="%d"/></clipPath>' % (s, f(cx - gap / 2), s),
             '<clipPath id="r%d"><rect x="%s" y="0" width="%s" height="%d"/></clipPath></defs>' % (s, f(cx + gap / 2), f(s - cx - gap / 2), s),
             '<path d="%s" fill="%s"/>' % (squircle_path(m, m, s - 2 * m, s * 0.235), ic.TILE),
             '<g clip-path="url(#l%d)">' % s]
    parts += facet_polys(cx, cy, R, tones, ends, sub, max(0.3, s / 512))
    parts.append('</g><g clip-path="url(#r%d)">' % s)
    parts += band_paths(cx, cy, R, tones, ends)
    parts.append('</g>')
    if s >= 40:
        near, far, arm, w = s * 0.16, s * 0.84, s * 0.12, s * 0.04
        d = ''
        for x, y, sx, sy in ((near, near, 1, 1), (far, near, -1, 1), (near, far, 1, -1), (far, far, -1, -1)):
            d += 'M%s %sL%s %sL%s %s' % (f(x), f(y + sy * arm), f(x), f(y), f(x + sx * arm), f(y))
        parts.append('<path d="%s" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round"/>'
                     % (d, ic.CORNERS, f(w)))
    parts.append('</svg>')
    return ''.join(parts) + '\n'


def write(rel, text):
    path = os.path.join(HERE, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(text)
    print('wrote', rel, len(text), 'bytes')


def main():
    write('assets/logo.svg', icon_svg(256, 'VRChat DLSS5 Cam'))
    write('assets/mark.svg', icon_svg(32, 'VRChat DLSS5 Cam'))
    # the whole sphere in either look, centre (100, 100), radius 88, the icon's full tones
    tones, ends, sub = ic.tier(256)
    write('partials/sphere-facets.svg', '<g class="sphere-facets">' + ''.join(facet_polys(100, 100, 88, tones, ends, sub, 0.5)) + '</g>\n')
    write('partials/sphere-bands.svg', '<g class="sphere-bands">' + ''.join(band_paths(100, 100, 88, tones, ends)) + '</g>\n')
    # bitmap icons: the 32 px entry is drawn for its size, the 180 px one is scaled from the 256 px drawing
    from PIL import Image
    Image.open(io.BytesIO(ic.png(32))).save(os.path.join(HERE, 'assets', 'icon-32.png'), optimize=True)
    Image.open(io.BytesIO(ic.png(256))).resize((180, 180), Image.LANCZOS).save(os.path.join(HERE, 'assets', 'icon-180.png'), optimize=True)
    print('wrote assets/icon-32.png, assets/icon-180.png')


if __name__ == '__main__':
    main()
