#!/usr/bin/env python3
"""Draws the program icon and writes resources/app.ico (PNG entries, 16-256 px) and resources/app-256.png.

The mark shows what the program does: a sphere on a dark tile, split down the middle. The left half is the game
picture - low-poly facets, each one flat colour; the right half is the same sphere after DLSS 5 - smooth light,
drawn as flat bands. The light comes from the upper right, and the gap between the halves is the split line of the
program's before/after wipe. From 40 px on, the four corners of a camera's viewfinder frame the sphere, which is drawn
smaller to make room for them. Flat colours only.

The small sizes are drawn, not scaled: below 40 px the sphere has the 20 facets of an icosahedron, three bands and no
viewfinder corners, below 24 px two bands, and up to 48 px the gap sits on whole pixels, so the mark stays sharp in the
taskbar.
The in-app logo (DrawLogo in src/ui/Theme.cpp) draws the same shapes with the same colours.

Needs pycairo. Run from the repository root: python3 tools/make_app_icon.py
"""
import io
import math
import os
import struct

import cairo

TILE = '#121526'                                                 # the dark tile
TONES = ('#FFFFFF', '#CBD1FF', '#97A3FF', '#6272F2', '#3A46B4')  # the light, from lit to shaded
ENDS = (0.8, 0.58, 0.33, 0.08)                                   # where each band ends, as the Lambert term n.L
LIGHT = (0.55, 0.62, 0.56)                                       # from the upper right, towards the viewer
SPIN = 0.3                                                       # the sphere's turn about its upright axis
CORNERS = '#7F8BFF'                                              # the viewfinder corners round the sphere

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
PHI = (1 + 5 ** 0.5) / 2


def rgba(h):
    h = h.lstrip('#')
    return (int(h[0:2], 16) / 255, int(h[2:4], 16) / 255, int(h[4:6], 16) / 255, 1.0)


def squircle(ctx, x, y, s, r):
    # a rounded square whose corners flow into the sides (cubic curves pulled past the circular 0.5523)
    k = 0.5523 * 1.28
    ctx.new_path()
    ctx.move_to(x + r, y)
    ctx.line_to(x + s - r, y)
    ctx.curve_to(x + s - r + r * k, y, x + s, y + r - r * k, x + s, y + r)
    ctx.line_to(x + s, y + s - r)
    ctx.curve_to(x + s, y + s - r + r * k, x + s - r + r * k, y + s, x + s - r, y + s)
    ctx.line_to(x + r, y + s)
    ctx.curve_to(x + r - r * k, y + s, x, y + s - r + r * k, x, y + s - r)
    ctx.line_to(x, y + r)
    ctx.curve_to(x, y + r - r * k, x + r - r * k, y, x + r, y)
    ctx.close_path()


def norm(v):
    l = math.sqrt(sum(c * c for c in v))
    return tuple(c / l for c in v)


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def facets(sub):
    """The facets of an icosphere (an icosahedron split "sub" times) that face the viewer, as (corners, normal) in
    unit space, y up. A vertex of the icosahedron sits at the top, so both poles lie on the split line."""
    V = [(-1, PHI, 0), (1, PHI, 0), (-1, -PHI, 0), (1, -PHI, 0), (0, -1, PHI), (0, 1, PHI), (0, -1, -PHI), (0, 1, -PHI),
         (PHI, 0, -1), (PHI, 0, 1), (-PHI, 0, -1), (-PHI, 0, 1)]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7),
         (9, 8, 1)]
    tris = [tuple(norm(V[i]) for i in f) for f in F]
    for _ in range(sub):
        split = []
        for a, b, c in tris:
            ab, bc, ca = (norm(tuple(p[i] + q[i] for i in range(3))) for p, q in ((a, b), (b, c), (c, a)))
            split += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        tris = split
    pole, cs, sn = -math.atan(1 / PHI), math.cos(SPIN), math.sin(SPIN)

    def turn(v):
        x, y, z = v
        x, y = x * math.cos(pole) - y * math.sin(pole), x * math.sin(pole) + y * math.cos(pole)
        return (x * cs + z * sn, y, -x * sn + z * cs)

    out = []
    for tri in tris:
        a, b, c = (turn(p) for p in tri)
        u = tuple(b[i] - a[i] for i in range(3))
        w = tuple(c[i] - a[i] for i in range(3))
        n = norm((u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]))
        if dot(n, (a[0] + b[0] + c[0], a[1] + b[1] + c[1], a[2] + b[2] + c[2])) < 0:
            n = tuple(-x for x in n)   # outwards
        if n[2] > 0:
            out.append(((a, b, c), n))
    return out


def band_outline(end, count=180):
    """The outline of the part of the sphere the light reaches at n.L >= end, in unit space, y up: a circle on the
    sphere around the light; where it runs round the back, the sphere's own outline takes over."""
    L = norm(LIGHT)
    ax = norm((L[1], -L[0], 0.0))
    ay = (L[1] * ax[2] - L[2] * ax[1], L[2] * ax[0] - L[0] * ax[2], L[0] * ax[1] - L[1] * ax[0])
    r = math.sqrt(max(0.0, 1.0 - end * end))
    pts = []
    for i in range(count):
        a = 2 * math.pi * i / count
        p = tuple(L[j] * end + (ax[j] * math.cos(a) + ay[j] * math.sin(a)) * r for j in range(3))
        if p[2] < 0:
            l = math.hypot(p[0], p[1])
            p = (p[0] / l, p[1] / l, 0.0)
        pts.append((p[0], p[1]))
    return pts


def tier(s):
    """The tones, the band ends and the facets' split count at a size of s px: fewer below 40 px and 24 px."""
    if s >= 40:
        return TONES, ENDS, 1
    if s >= 24:
        return (TONES[0], TONES[2], TONES[4]), (ENDS[0] - 0.1, ENDS[-1] + 0.1), 0
    return (TONES[0], TONES[-1]), (0.3,), 0


def geom(s):
    """The sphere's centre and radius and the gap's width at a size of s px."""
    k = 0.4 if s <= 24 else 0.35 if s >= 32 else 0.4 - 0.05 * (s - 24) / 8
    if s >= 40:   # inside the viewfinder corners
        k *= 0.84
    cx = cy = s / 2
    gap = max(1.0, s * 0.026)
    if s <= 48:   # on whole pixels, so the gap stays sharp
        gap = 1.0 if s >= 20 else 0.0
        cx = round(cx) + (0.5 if gap else 0.0)
    return cx, cy, s * k, gap


def ramp(tones, ends, d):
    """A facet's colour for its Lambert term d: the band colours, blended between the middles of the bands."""
    edges = (1.0,) + tuple(ends) + (-1.0,)
    mids = [(edges[k] + max(edges[k + 1], -0.2)) / 2 for k in range(len(tones))]
    if d >= mids[0]:
        return rgba(tones[0])
    for k in range(len(tones) - 1):
        if mids[k] >= d >= mids[k + 1]:
            t = (mids[k] - d) / (mids[k] - mids[k + 1])
            a, b = rgba(tones[k]), rgba(tones[k + 1])
            return tuple(a[i] + (b[i] - a[i]) * t for i in range(4))
    return rgba(tones[-1])


def viewfinder(ctx, s):
    """The four corners of a viewfinder round the sphere: strokes with round ends and a round bend."""
    near, far, arm = s * 0.16, s * 0.84, s * 0.12
    ctx.set_line_width(s * 0.04)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.set_source_rgba(*rgba(CORNERS))
    for x, y, sx, sy in ((near, near, 1, 1), (far, near, -1, 1), (near, far, 1, -1), (far, far, -1, -1)):
        ctx.new_path()
        ctx.move_to(x, y + sy * arm)
        ctx.line_to(x, y)
        ctx.line_to(x + sx * arm, y)
        ctx.stroke()


def draw(ctx, s):
    m = 0 if s <= 32 else s * 0.035
    squircle(ctx, m, m, s - 2 * m, s * 0.235)
    ctx.set_source_rgba(*rgba(TILE))
    ctx.fill()
    tones, ends, sub = tier(s)
    cx, cy, R, gap = geom(s)
    L = norm(LIGHT)
    # the left half: flat facets, each one stroked in its own colour too so no seam shows between two
    ctx.save()
    ctx.rectangle(0, 0, cx - gap / 2, s)
    ctx.clip()
    ctx.set_line_width(0.5)
    for corners, n in facets(sub):
        ctx.new_path()
        for p in corners:
            ctx.line_to(cx + p[0] * R, cy - p[1] * R)
        ctx.close_path()
        ctx.set_source_rgba(*ramp(tones, ends, dot(n, L)))
        ctx.fill_preserve()
        ctx.stroke()
    ctx.restore()
    # the right half: the shaded disc, then the bands on it from the widest to the brightest
    ctx.save()
    ctx.rectangle(cx + gap / 2, 0, s, s)
    ctx.clip()
    ctx.new_path()
    ctx.arc(cx, cy, R, 0, 2 * math.pi)
    ctx.clip_preserve()
    ctx.set_source_rgba(*rgba(tones[-1]))
    ctx.fill()
    for k in range(len(ends) - 1, -1, -1):
        ctx.new_path()
        for x, y in band_outline(ends[k]):
            ctx.line_to(cx + x * R, cy - y * R)
        ctx.close_path()
        ctx.set_source_rgba(*rgba(tones[k]))
        ctx.fill()
    ctx.restore()
    if s >= 40:
        viewfinder(ctx, s)


def png(size):
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
    ctx = cairo.Context(surf)
    ctx.set_antialias(cairo.ANTIALIAS_BEST)
    draw(ctx, size)
    out = io.BytesIO()
    surf.write_to_png(out)
    return out.getvalue()


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'resources')
    blobs = [png(s) for s in SIZES]
    head = struct.pack('<HHH', 0, 1, len(SIZES))
    offset = 6 + 16 * len(SIZES)
    entries = b''
    for s, blob in zip(SIZES, blobs):
        entries += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(os.path.join(root, 'app.ico'), 'wb') as f:
        f.write(head + entries + b''.join(blobs))
    with open(os.path.join(root, 'app-256.png'), 'wb') as f:
        f.write(blobs[SIZES.index(256)])
    print('app.ico: %d sizes, app-256.png written' % len(SIZES))


if __name__ == '__main__':
    main()
