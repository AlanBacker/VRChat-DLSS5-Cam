#!/usr/bin/env python3
"""Builds src/ui/IconFont.h and IconFont.inc: the Lucide icon font (ISC licence) cut down to the glyphs the interface draws,
plus a few glyphs of our own drawn from Lucide's shapes (CUSTOM).

    npm pack lucide-static        (or download the tarball from the npm registry) and unpack it
    python3 tools/make_icon_font.py path/to/package/font

Needs fontTools and skia-pathops. Add a name to GLYPHS, run the script again and use the constant it writes
(lucide::name_with_underscores).
"""
import json
import os
import sys

import pathops
from fontTools import subset
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path
from fontTools.ttLib import TTFont

GLYPHS = [
    "chevron-down", "chevron-up", "chevron-left", "chevron-right", "rotate-ccw", "refresh-cw", "external-link", "x",
    "lock", "undo-2", "redo-2", "maximize", "minimize", "history", "circle-question-mark", "save", "pencil", "plus",
    "search", "rotate-ccw-square", "rotate-cw-square", "flip-horizontal-2", "flip-vertical-2", "crop", "image", "film",
    "radio", "sparkles", "download", "layers", "grid-2x2", "eye", "plug", "gauge", "info", "folder-open", "check",
    "camera", "triangle-alert", "sliders-horizontal", "circle-check", "circle-x", "image-plus", "languages", "trash-2",
    "keyboard", "monitor", "wand-sparkles", "settings", "columns-2", "zoom-in", "zoom-out", "terminal", "key-round",
    "upload", "copy", "images", "video", "cpu", "loader-circle", "import", "minus", "ellipsis", "list-checks",
    "square-check", "arrow-right", "sun", "moon", "bell", "scissors", "flag", "repeat", "file-image", "file-video",
    "mouse", "hand", "move", "focus", "aperture", "shield-check", "wifi", "hard-drive", "panel-left", "panel-right",
]

# Our own glyphs, drawn the way Lucide draws (its 24 grid, 2 wide strokes with round caps and joins) from Lucide's
# shapes: (code point in the private use area, the parts as SVG paths, each with a scale and a move).
# - book-question-mark: Lucide's book with the question mark of circle-question-mark on its cover (the documentation).
# - message-square-ai: Lucide's message-square with the letters A and I in it (the AI Q&A); the A is Lucide's own
#   (book-a), a little narrower.
_BOOK = "M4 19.5v-15A2.5 2.5 0 0 1 6.5 2H19a1 1 0 0 1 1 1v18a1 1 0 0 1-1 1H6.5a1 1 0 0 1 0-5H20"
_QUESTION = ["M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3", "M12 17h.01"]   # its middle is (12, 11.75)
_MESSAGE = "M22 17a2 2 0 0 1-2 2H6.828a2 2 0 0 0-1.414.586l-2.202 2.202A.71.71 0 0 1 2 21.286V5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2z"
CUSTOM = {
    "book-question-mark": (0xF8F0, [(_BOOK, 1.0, (0, 0), (0, 0))] + [(d, 0.78, (12, 11.75), (12, 9.6)) for d in _QUESTION]),
    "message-square-ai": (0xF8F1, [
        (_MESSAGE, 1.0, (0, 0), (0, 0)),
        ("M6.45 14.5 L10.2 7.5 L13.95 14.5", 1.0, (0, 0), (0, 0)),   # A
        ("M7.87 12.5 H12.53", 1.0, (0, 0), (0, 0)),                  # its bar
        ("M17.55 7.5 V14.5", 1.0, (0, 0), (0, 0)),                   # I
    ]),
}


def StrokedGlyph(parts):
    """A TrueType glyph of the parts' strokes (the 24 grid, y down, to font units of a 1000 em, y up)."""
    total = pathops.Path()
    for d, scale, about, to in parts:
        p = pathops.Path()
        parse_path(d, p.getPen())
        if scale != 1.0 or about != to:
            p = p.transform(scale, 0, 0, scale, to[0] - about[0] * scale, to[1] - about[1] * scale)
        p.stroke(2.0, pathops.LineCap.ROUND_CAP, pathops.LineJoin.ROUND_JOIN, 4.0)
        p.convertConicsToQuads()
        total.addPath(p)
    total.simplify(fix_winding=True)
    pen = TTGlyphPen(None)
    unit = 1000.0 / 24.0
    total.draw(TransformPen(Cu2QuPen(pen, 1.0, reverse_direction=True), (unit, 0, 0, -unit, 0, 24 * unit)))
    return pen.glyph()


def AddCustom(font):
    glyf, hmtx = font["glyf"], font["hmtx"]
    order = list(font.getGlyphOrder())
    names = list(CUSTOM)
    font.setGlyphOrder(order + names)
    glyf.glyphOrder = order + names
    for name in names:
        g = StrokedGlyph(CUSTOM[name][1])
        g.recalcBounds(glyf)
        glyf.glyphs[name] = g
        hmtx.metrics[name] = (1000, g.xMin)
    for table in font["cmap"].tables:
        if table.isUnicode():
            for name in names:
                table.cmap[CUSTOM[name][0]] = name
    font["maxp"].numGlyphs = len(order) + len(names)


# Names that would read as C++ keywords (or the module "import") get a trailing underscore.
RESERVED = {"import", "export", "module", "delete", "new", "default", "switch", "case", "class", "private", "public"}


def CName(glyph):
    name = glyph.replace("-", "_")
    return name + "_" if name in RESERVED else name


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "package/font"
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    codepoints = json.load(open(os.path.join(src, "codepoints.json"), encoding="utf-8"))
    missing = [g for g in GLYPHS if g not in codepoints]
    if missing:
        sys.exit("not in this Lucide build: " + ", ".join(missing))

    font = TTFont(os.path.join(src, "lucide.ttf"))
    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6]
    options.notdef_outline = False
    options.glyph_names = False
    sub = subset.Subsetter(options)
    sub.populate(unicodes=[codepoints[g] for g in GLYPHS])
    sub.subset(font)
    AddCustom(font)
    out_ttf = os.path.join(src, "lucide-subset.ttf")
    font.save(out_ttf)
    data = open(out_ttf, "rb").read()

    head = [
        "// Lucide icons (https://lucide.dev, ISC licence - see THIRD_PARTY_NOTICES.md): the code points of the glyphs",
        "// in IconFont.inc. Generated by tools/make_icon_font.py; do not edit.",
        "#pragma once",
        "#include \"imgui.h\"",
        "",
        "namespace lucide {",
    ]
    for g in GLYPHS:
        head.append("constexpr ImWchar %s = 0x%04X;" % (CName(g), codepoints[g]))
    for g in CUSTOM:
        head.append("constexpr ImWchar %s = 0x%04X;   // ours, from Lucide's shapes" % (CName(g), CUSTOM[g][0]))
    head.append("}")
    head.append("")
    data_lines = [
        "// Lucide icons (https://lucide.dev, ISC licence - see THIRD_PARTY_NOTICES.md), cut down to the glyphs the",
        "// interface draws (their code points are in IconFont.h). Generated by tools/make_icon_font.py; do not edit.",
        "alignas(4) static const unsigned char kIconFontData[%d] = {" % len(data),
    ]
    for i in range(0, len(data), 24):
        data_lines.append("    " + ",".join("0x%02x" % b for b in data[i:i + 24]) + ",")
    data_lines.append("};")
    data_lines.append("")
    ui = os.path.join(root, "src", "ui")
    for name, body in (("IconFont.h", head), ("IconFont.inc", data_lines)):
        with open(os.path.join(ui, name), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(body))
    path = os.path.join(ui, "IconFont.inc")
    print("%s: %d glyphs, %d bytes" % (path, len(GLYPHS) + len(CUSTOM), len(data)))


if __name__ == "__main__":
    main()
