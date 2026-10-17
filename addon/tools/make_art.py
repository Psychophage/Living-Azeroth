#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Draws the addon's art into addon/LivingAzeroth/Art as 32-bit TGA files the 3.3.5 client reads.

Glyphs come from glyphs.json (24x24 SVG bodies drawn in currentColor, from the design mockups). They are drawn
white, with their shading in black and a soft shadow baked in, so the game can tint them with SetVertexColor.
Shapes (tiles, medal, ring disc) are drawn with cairo at about their size on screen. Window frames, buttons and
other surfaces are drawn by the game itself from plain textures (Draw.lua), with the corner atlas and the grain
from here shown at one texel per screen pixel.

Needs rsvg-convert, pycairo, numpy and Pillow. Run it from anywhere: python3 addon/tools/make_art.py
"""
import io
import json
import math
import os
import subprocess

import cairo
import numpy as np
from PIL import Image, ImageChops, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "LivingAzeroth", "Art")

# Tile colours from the mockup: light, middle and dark stops of the radial gradient.
TONES = {
    "gold": ("#d9a948", "#82571a", "#2c1b06"),
    "bronze": ("#b07a44", "#5f3a17", "#211206"),
    "steel": ("#7f9fc8", "#33507a", "#0e1a2c"),
    "green": ("#79c062", "#2f6d2b", "#0b220b"),
    "red": ("#d0583e", "#7d2116", "#2a0805"),
    "violet": ("#a479cc", "#52307c", "#1a0c2c"),
    "blue": ("#56aee2", "#215f92", "#0a1f33"),
    "teal": ("#5cbfb1", "#216f66", "#0a2724"),
    "stone": ("#948e82", "#4c4840", "#1a1814"),
}

# A few plain shapes the mockup drew with text or the browser: folding arrows, scroll arrows.
EXTRA_GLYPHS = {
    "down": '<path d="M5 8.5h14L12 16.5z" fill="currentColor"/>',
    "right": '<path d="M8.5 5v14l8-7z" fill="currentColor"/>',
    "up": '<path d="M5 15.5h14L12 7.5z" fill="currentColor"/>',
    # A ghost: the Run back switch.
    "spirit": '<path d="M12 2.5c-4.2 0-7.3 3.2-7.3 7.4V21l2.4-1.9 2.4 1.9 2.5-1.9 2.5 1.9 2.4-1.9 2.4 1.9V9.9'
              'c0-4.2-3.1-7.4-7.3-7.4z" fill="currentColor"/>'
              '<path d="M12 2.5c-4.2 0-7.3 3.2-7.3 7.4V21l2.4-1.9V9.9c0-3.6 2-6.3 4.9-7.4z" fill="#000" '
              'fill-opacity="0.16"/>'
              '<circle cx="9.5" cy="10" r="1.5" fill="#000" fill-opacity="0.6"/>'
              '<circle cx="14.5" cy="10" r="1.5" fill="#000" fill-opacity="0.6"/>',
}


def rgb(hex_colour, alpha=1.0):
    h = hex_colour.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)) + (alpha,)


def save(surface_or_image, name):
    if isinstance(surface_or_image, cairo.ImageSurface):
        buffer = io.BytesIO()
        surface_or_image.write_to_png(buffer)
        buffer.seek(0)
        image = Image.open(buffer).convert("RGBA")
    else:
        image = surface_or_image
    w, h = image.size
    assert w & (w - 1) == 0 and h & (h - 1) == 0, name + ": the client needs power-of-two sizes"
    image.save(os.path.join(OUT, name + ".tga"))


def canvas(w, h):
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, h)
    return surface, cairo.Context(surface)


def rounded(cr, x, y, w, h, r):
    r = min(r, w / 2, h / 2)
    cr.new_sub_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    cr.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    cr.close_path()


def glyph(name, body, size=64):
    """White glyph, black shading, soft shadow below: tinted in game."""
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="{0}" height="{0}" viewBox="0 0 24 24" '
           'color="#ffffff">{1}</svg>').format(size, body)
    png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size)], input=svg.encode(),
                         capture_output=True, check=True).stdout
    shape = Image.open(io.BytesIO(png)).convert("RGBA")
    alpha = shape.getchannel("A")
    shadow_alpha = alpha.filter(ImageFilter.GaussianBlur(size / 40)).point(lambda a: int(a * 0.85))
    shadow = Image.new("RGBA", shape.size, (0, 0, 0, 0))
    shadow.putalpha(ImageChops.offset(shadow_alpha, 0, max(1, size // 32)))
    save(Image.alpha_composite(shadow, shape), "glyph-" + name)


def tile(tone, size=64):
    """The mockup's tile: a lit radial gradient, a top sheen, a faint inner edge and a dark rim."""
    light, middle, dark = (rgb(c) for c in TONES[tone])
    s, cr = canvas(size, size)
    edge, radius = size * 0.03, size * 0.12
    rounded(cr, edge, edge, size - 2 * edge, size - 2 * edge, radius)
    cr.save()
    cr.clip()
    gradient = cairo.RadialGradient(size * 0.34, size * 0.26, 0, size * 0.34, size * 0.26, size * 0.99)
    gradient.add_color_stop_rgba(0, *light)
    gradient.add_color_stop_rgba(0.54, *middle)
    gradient.add_color_stop_rgba(1, *dark)
    cr.set_source(gradient)
    cr.paint()
    sheen = cairo.LinearGradient(0, 0, 0, size * 0.44)
    sheen.add_color_stop_rgba(0, 1, 1, 1, 0.2)
    sheen.add_color_stop_rgba(1, 1, 1, 1, 0)
    cr.set_source(sheen)
    cr.paint()
    shade = cairo.LinearGradient(0, size * 0.75, 0, size)
    shade.add_color_stop_rgba(0, 0, 0, 0, 0)
    shade.add_color_stop_rgba(1, 0, 0, 0, 0.5)
    cr.set_source(shade)
    cr.paint()
    cr.restore()
    rounded(cr, edge * 2, edge * 2, size - 4 * edge, size - 4 * edge, radius - edge)
    cr.set_source_rgba(1, 0.94, 0.78, 0.25)
    cr.set_line_width(size / 64)
    cr.stroke()
    rounded(cr, edge / 2, edge / 2, size - edge, size - edge, radius)
    cr.set_source_rgba(0, 0, 0, 1)
    cr.set_line_width(edge)
    cr.stroke()
    save(s, "tile-" + tone)


def tile_glow(size=128):
    """Drawn over a tile that is the current order: a gold edge and a soft glow (twice the tile's size)."""
    s, cr = canvas(size, size)
    inner = size / 2
    x = (size - inner) / 2
    for spread, alpha in ((10, 0.08), (7, 0.14), (4, 0.24)):
        rounded(cr, x - spread, x - spread, inner + 2 * spread, inner + 2 * spread, inner * 0.12 + spread)
        cr.set_source_rgba(1, 0.82, 0, alpha)
        cr.set_line_width(spread)
        cr.stroke()
    rounded(cr, x - 1.5, x - 1.5, inner + 3, inner + 3, inner * 0.14)
    cr.set_source_rgba(1, 0.82, 0, 1)
    cr.set_line_width(2.4)
    cr.stroke()
    save(s, "tile-glow")


def medal_glow(size=128):
    """Round light for a medal: hovered, chosen, or waiting (twice the medal's size, so it shows around it)."""
    s, cr = canvas(size, size)
    c = size / 2
    r = size / 4  # the medal's edge
    for spread, alpha in ((14, 0.07), (10, 0.12), (6, 0.2), (3, 0.3)):
        cr.arc(c, c, r + spread / 2, 0, 2 * math.pi)
        cr.set_source_rgba(1, 0.82, 0, alpha)
        cr.set_line_width(spread)
        cr.stroke()
    cr.arc(c, c, r + 1, 0, 2 * math.pi)
    cr.set_source_rgba(1, 0.86, 0.3, 1)
    cr.set_line_width(2.2)
    cr.stroke()
    save(s, "medal-glow")


def medal(size=128):
    """A dark coin with a gold rim, for role and bot marks; the glyph on it is tinted by the game."""
    s, cr = canvas(size, size)
    c, r = size / 2, size / 2 - 1
    gradient = cairo.RadialGradient(c, size * 0.34, 0, c, size * 0.34, r * 1.3)
    gradient.add_color_stop_rgba(0, *rgb("#3d3428"))
    gradient.add_color_stop_rgba(0.72, *rgb("#15100a"))
    gradient.add_color_stop_rgba(1, *rgb("#15100a"))
    cr.arc(c, c, r, 0, 2 * math.pi)
    cr.set_source(gradient)
    cr.fill_preserve()
    cr.set_source_rgba(0, 0, 0, 1)
    cr.set_line_width(2)
    cr.stroke()
    cr.arc(c, c, r - size * 0.06, 0, 2 * math.pi)
    cr.set_source_rgba(*rgb("#d4b062"))
    cr.set_line_width(size * 0.06)
    cr.stroke()
    cr.arc(c, c, r - size * 0.11, 0, 2 * math.pi)
    cr.set_source_rgba(*rgb("#2a1f0c"))
    cr.set_line_width(size * 0.04)
    cr.stroke()
    save(s, "medal")


def disc(size=256):
    """The orders ring's centre: a dark disc with a gold rim and a soft shadow."""
    s, cr = canvas(size, size)
    c = size / 2
    r = size / 2 - size * 0.06
    for spread, alpha in ((12, 0.05), (8, 0.1), (4, 0.18)):
        cr.arc(c, c + 4, r + spread / 2, 0, 2 * math.pi)
        cr.set_source_rgba(0, 0, 0, alpha)
        cr.set_line_width(spread)
        cr.stroke()
    gradient = cairo.RadialGradient(c, size * 0.42, 0, c, size * 0.42, r * 1.1)
    gradient.add_color_stop_rgba(0, 46 / 255, 36 / 255, 20 / 255, 0.97)
    gradient.add_color_stop_rgba(0.72, 10 / 255, 8 / 255, 5 / 255, 0.97)
    gradient.add_color_stop_rgba(1, 10 / 255, 8 / 255, 5 / 255, 0.97)
    cr.arc(c, c, r, 0, 2 * math.pi)
    cr.set_source(gradient)
    cr.fill()
    for radius, colour, width in ((r, "#000000", 3), (r - 3.5, "#c9a24a", 4), (r - 7.5, "#2a1f0c", 4)):
        cr.arc(c, c, radius, 0, 2 * math.pi)
        cr.set_source_rgba(*rgb(colour))
        cr.set_line_width(width)
        cr.stroke()
    save(s, "disc")


# The orders ring's segments, in units of the ring (Ring.lua uses the same numbers): an annulus cut into equal parts
# with RING_GAP between them and corners rounded by RING_ROUND, drawn at `scale` texels per unit with the ring's centre
# at (width / 2, centre) of a texture RING_HEIGHT tall, 512 wide when a part is too wide for 256 (the same rule as
# Ring.lua's ArtWidth). Bands: "ring" (one ring of orders), and "ring-in" and "ring-out" (the two-ring layout: the
# bots' state inside, actions outside).
RING_GAP, RING_ROUND, RING_HEIGHT = 3, 3, 256
RING_BANDS = {
    "ring": dict(inner=43, outer=75, scale=2.56, centre=194),
    "ring-in": dict(inner=43, outer=66, scale=2.56, centre=194),
    "ring-out": dict(inner=69, outer=100, scale=2.0, centre=208),
}


def art_width(count, outer, scale):
    return 512 if (outer * math.sin(min(math.pi / count, math.pi / 2)) + 3) * scale > 126 else 256


def ring_segment(count, band="ring"):
    """One part of a ring, pointing up: <band>-<count>-fill (white, with a dark outline) and <band>-<count>-edge
    (the white band inside the outline). The game tints both and turns them to each part's place with texture
    coordinates."""
    spec = RING_BANDS[band]
    inner, outer, k, centre = spec["inner"], spec["outer"], spec["scale"], spec["centre"]
    width = art_width(count, outer, k)
    ys, xs = np.mgrid[0:RING_HEIGHT, 0:width] + 0.5
    x, y = (xs - width / 2) / k, (centre - ys) / k
    half = math.pi / count
    r = np.hypot(x, y)
    annulus = np.maximum(inner - r, r - outer)
    # The two sides, each RING_GAP / 2 in from the line between neighbouring parts.
    side_a = x * math.cos(half) - y * math.sin(half) + RING_GAP / 2
    side_b = -(x * math.cos(half) + y * math.sin(half)) + RING_GAP / 2
    cone = np.maximum(side_a, side_b)
    # Rounded where the sides meet the arcs (hg_sdf's rounded intersection), in texels.
    c = RING_ROUND
    u = np.hypot(np.maximum(annulus + c, 0), np.maximum(cone + c, 0))
    depth = -(np.minimum(-c, np.maximum(annulus, cone)) + u) * k

    def cover(inset):
        return np.clip(depth - inset + 0.5, 0, 1)

    outline, band_width = 1.3, 1.6
    alpha = cover(0)
    light = cover(outline)
    fill = np.dstack([light, light, light, alpha])
    edge = np.dstack([np.ones_like(alpha)] * 3 + [cover(outline) - cover(outline + band_width)])
    for name, layers in (("fill", fill), ("edge", edge)):
        image = Image.fromarray((layers * 255 + 0.5).astype(np.uint8), "RGBA")
        save(image, "%s-%d-%s" % (band, count, name))


def portrait_ring(size=128):
    """Rings around a round portrait: dark, gold, dark. The portrait fills the middle 104/128."""
    s, cr = canvas(size, size)
    c = size / 2
    inner = size * 52 / 128
    for radius, colour, width in ((inner + 2, "#120c04", 4), (inner + 5.5, "#c9a24a", 3.5),
                                  (inner + 8.5, "#2e2109", 3)):
        cr.arc(c, c, radius, 0, 2 * math.pi)
        cr.set_source_rgba(*rgb(colour))
        cr.set_line_width(width)
        cr.stroke()
    save(s, "portrait-ring")


def corners():
    """Corners of rounded boxes, drawn at one texel per screen pixel so the game shows them 1:1. Cell (r-1, row) of
    32x32 holds the top-left corner of a rounded rectangle of radius r px from texel (1, 1): row 0 filled, rows
    1-3 only its edge band 1-3 px wide. Around the r x r corner the cell carries on with the shape (the straight
    edges to the right and below, nothing to the left and above), because the game samples half a texel off and
    would otherwise blend the corner's inner edges with empty texels. White, antialiased; the game tints them."""
    sub = 8
    cell = 32
    atlas = np.zeros((4 * cell, 16 * cell))
    offsets = (np.arange(sub) + 0.5) / sub

    def filled(x, y, r):
        corner = (x < r) & (y < r)
        inside = (x >= 0) & (y >= 0)
        return inside & (~corner | (np.hypot(r - x, r - y) <= r))

    for r in range(1, 17):
        for row in range(4):
            for ty in range(cell):
                for tx in range(cell):
                    x = tx - 1 + offsets[None, :]
                    y = ty - 1 + offsets[:, None]
                    shape = filled(x, y, r)
                    if row:
                        inner = filled(x - row, y - row, max(r - row, 0)) if r > row else (x >= row) & (y >= row)
                        shape = shape & ~inner
                    atlas[row * cell + ty, (r - 1) * cell + tx] = shape.mean()
    image = Image.new("RGBA", (16 * cell, 4 * cell), (255, 255, 255, 0))
    image.putalpha(Image.fromarray((atlas * 255 + 0.5).astype(np.uint8)))
    save(image, "corners")


def grain(size=128):
    """A tileable grain for window fills: soft blotches over fine speckle, grey around the middle. The game lays
    it over the fill at a low alpha, tiled at one texel per pixel."""
    rng = np.random.default_rng(7)
    total = np.zeros((size, size))
    for cells, weight in ((8, 0.45), (16, 0.3), (32, 0.15)):
        small = rng.random((cells, cells))
        tiled = np.tile(small, (3, 3))
        big = Image.fromarray((tiled * 255).astype(np.uint8)).resize((size * 3, size * 3), Image.BICUBIC)
        total += np.asarray(big, dtype=np.float64)[size:2 * size, size:2 * size] / 255 * weight
    total += rng.random((size, size)) * 0.25
    total = (total - total.min()) / (total.max() - total.min())
    grey = (total * 255).astype(np.uint8)
    image = Image.merge("RGBA", [Image.fromarray(grey)] * 3 + [Image.new("L", (size, size), 255)])
    save(image, "grain")


def field(size=128):
    """The ground formations are drawn on: a dim green, lighter in the middle, with a dark rim."""
    s, cr = canvas(size, size)
    gradient = cairo.RadialGradient(size / 2, size * 0.45, 0, size / 2, size * 0.45, size * 0.75)
    gradient.add_color_stop_rgba(0, *rgb("#3c5236"))
    gradient.add_color_stop_rgba(0.6, *rgb("#24321f"))
    gradient.add_color_stop_rgba(1, *rgb("#172014"))
    cr.rectangle(0, 0, size, size)
    cr.set_source(gradient)
    cr.fill()
    save(s, "field")


def dot(size=16):
    """A small round marker with a black rim, tinted in the game."""
    s, cr = canvas(size, size)
    cr.arc(size / 2, size / 2, size / 2 - 1, 0, 2 * math.pi)
    cr.set_source_rgba(0, 0, 0, 1)
    cr.fill()
    cr.arc(size / 2, size / 2, size / 2 - 2.5, 0, 2 * math.pi)
    cr.set_source_rgba(1, 1, 1, 1)
    cr.fill()
    save(s, "dot")


def main():
    os.makedirs(OUT, exist_ok=True)
    glyphs = json.load(open(os.path.join(HERE, "glyphs.json")))
    glyphs.update(EXTRA_GLYPHS)
    for name, body in sorted(glyphs.items()):
        glyph(name, body)
    for tone in TONES:
        tile(tone)
    tile_glow()
    medal()
    medal_glow()
    disc()
    for count in range(2, 9):
        for band in RING_BANDS:
            ring_segment(count, band)
    portrait_ring()
    corners()
    grain()
    field()
    dot()
    print("Wrote", len(os.listdir(OUT)), "files to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
