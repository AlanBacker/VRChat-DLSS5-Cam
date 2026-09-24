#!/usr/bin/env python3
"""Draws the program icon (a lens ring with a gold sparkle on a navy tile, flat colours only) and writes
resources/app.ico (PNG entries, 16-256 px) and resources/app-256.png.

The small sizes are drawn, not scaled: at 24 px and below the tile has no margin, the lens is larger, the reflection
dot and the second sparkle are left out and the sparkle is chunkier, so the mark stays readable in the taskbar.
The in-app logo (DrawLogo in src/ui/Theme.cpp) draws the same shapes with the same colours.

Needs pycairo and Pillow. Run from the repository root: python3 tools/make_app_icon.py
"""
import io
import math
import os
import struct

import cairo

TILE = '#1E2449'      # the navy tile
GLASS = '#11152E'     # the lens glass
INNER = '#1A2046'     # the inner lens element (48 px and up)
RING = '#6F84FF'      # the lens ring
SHINE = '#B7C4FF'     # the reflection dot (32 px and up)
GOLD = '#FFD166'      # the sparkles

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)


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


def sparkle(ctx, cx, cy, R, pinch=0.13):
    # a four-pointed star with curved sides; "pinch" sets how fat the waist is
    c = R * pinch
    ctx.new_path()
    ctx.move_to(cx, cy - R)
    ctx.curve_to(cx + c * 0.6, cy - c * 1.6, cx + c * 1.6, cy - c * 0.6, cx + R, cy)
    ctx.curve_to(cx + c * 1.6, cy + c * 0.6, cx + c * 0.6, cy + c * 1.6, cx, cy + R)
    ctx.curve_to(cx - c * 0.6, cy + c * 1.6, cx - c * 1.6, cy + c * 0.6, cx - R, cy)
    ctx.curve_to(cx - c * 1.6, cy - c * 0.6, cx - c * 0.6, cy - c * 1.6, cx, cy - R)
    ctx.close_path()


def draw(ctx, s):
    small = s <= 24
    m = 0 if s <= 32 else s * 0.035
    squircle(ctx, m, m, s - 2 * m, s * 0.235)
    ctx.set_source_rgba(*rgba(TILE))
    ctx.fill()
    cx, cy, R = (s * 0.45, s * 0.57, s * 0.3) if small else (s * 0.455, s * 0.56, s * 0.255)
    lw = max(2.0 if small else 1.8, s * 0.08)
    ctx.new_path()
    ctx.arc(cx, cy, R, 0, 2 * math.pi)
    ctx.set_source_rgba(*rgba(GLASS))
    ctx.fill()
    if s >= 48:
        ctx.new_path()
        ctx.arc(cx, cy, R * 0.56, 0, 2 * math.pi)
        ctx.set_source_rgba(*rgba(INNER))
        ctx.fill()
    ctx.set_line_width(lw)
    ctx.new_path()
    ctx.arc(cx, cy, R, 0, 2 * math.pi)
    ctx.set_source_rgba(*rgba(RING))
    ctx.stroke()
    if s >= 32:
        ctx.new_path()
        ctx.arc(cx - R * 0.36, cy - R * 0.36, R * 0.15, 0, 2 * math.pi)
        ctx.set_source_rgba(*rgba(SHINE))
        ctx.fill()
    if small:
        sparkle(ctx, s * 0.75, s * 0.26, s * (0.23 if s <= 20 else 0.21), pinch=0.2)
    else:
        sparkle(ctx, s * 0.735, s * 0.265, s * 0.15)
    # a knockout in the tile colour where the sparkle crosses the ring keeps the two shapes apart
    ctx.set_line_width(max(1.0, s * 0.045))
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.set_source_rgba(*rgba(TILE))
    ctx.stroke_preserve()
    ctx.set_source_rgba(*rgba(GOLD))
    ctx.fill()
    if s >= 40:
        sparkle(ctx, s * 0.84, s * 0.47, s * 0.055)
        ctx.set_source_rgba(*rgba(GOLD))
        ctx.fill()


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
