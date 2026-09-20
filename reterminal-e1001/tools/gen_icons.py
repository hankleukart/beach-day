#!/usr/bin/env python3
"""Icon source of truth: small vector programs in a 48x48 box.

    .venv/bin/python tools/gen_icons.py

Emits src/icons_data.h - a command table per icon that src/icons.cpp
interprets on the device at any pixel size - and an honest preview
(../docs-design/icons-preview.png) drawn at the sizes the 800x480 layout
uses, without anti-aliasing, so it looks like the panel will.

Strokes are always INK. Fills may carry a colour role - SUN (yellow), WATER
(blue), LEAF (green), WARM (red) - that a colour panel paints and a mono
panel renders as a light dot pattern. PAPER is a knockout.
"""
import math, pathlib
from PIL import Image, ImageDraw

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = ROOT / "src" / "icons_data.h"
ROLE_ID = { "ink": 0, "paper": 1, "sun": 2, "water": 3, "leaf": 4, "warm": 5 }
PREVIEW = ROOT.parent / "docs-design" / "icons-preview.png"

SIZES = { "hero": 104, "tile": 44, "stat": 22 }
SS = 1              # preview is drawn 1:1, aliased, like the device
INK, SUN, WATER, LEAF, WARM, PAPER = "ink", "sun", "water", "leaf", "warm", "paper"
ACCENTS = (SUN, WATER, LEAF, WARM)


def stroke_px(size):
    # spec: 2.6 units of 48. Thin lines vanish at 1-bit, so floor it.
    return max(2, round(2.6 / 48 * size * 1.05))


class Canvas:
    def __init__(self, size):
        self.cmds = []          # (op, role, [int16 values]) - coords in half-units
        self.size = size
        self.k = size * SS / 48.0
        self.w = stroke_px(size) * SS
        self.layers = { r: Image.new("L", (size * SS, size * SS), 0) for r in (INK,) + ACCENTS }
        self.draw = { r: ImageDraw.Draw(im) for r, im in self.layers.items() }

    def rec(self, op, role, vals):
        self.cmds.append((op, ROLE_ID[role], [int(round(v)) for v in vals]))
    def h(self, *vals): return [v * 2 for v in vals]       # half-unit coordinates

    def _d(self, role): return self.draw[INK if role == PAPER else role]
    def _fill(self, role): return 0 if role == PAPER else 255
    def s(self, v): return v * self.k

    # -- primitives (48-unit coordinates) --
    def line(self, pts, role=INK, w=None):
        if w is None: self.rec("LINE", role, self.h(*[c for p in pts for c in p]))
        else:         self.rec("LINEW", role, [int(round(w * 10))] + self.h(*[c for p in pts for c in p]))
        w = self.w if w is None else w * SS
        d = self._d(role); f = self._fill(role)
        P = [(self.s(x), self.s(y)) for x, y in pts]
        if len(P) > 1:
            d.line(P, fill=f, width=int(w), joint="curve")
        for x, y in P:
            d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=f)

    def circle(self, cx, cy, r, role=INK):
        self.rec("CIRCLE", role, self.h(cx, cy, r))
        d = self._d(role); w = self.w
        x, y, R = self.s(cx), self.s(cy), self.s(r)
        d.ellipse([x - R, y - R, x + R, y + R], outline=self._fill(role), width=int(w))

    def disc(self, cx, cy, r, role=INK):
        self.rec("DISC", role, self.h(cx, cy, r))
        x, y, R = self.s(cx), self.s(cy), self.s(r)
        self._d(role).ellipse([x - R, y - R, x + R, y + R], fill=self._fill(role))

    def ellipse(self, x0, y0, x1, y1, role=INK, fill=False):
        self.rec("ELLIPSE_FILL" if fill else "ELLIPSE", role, self.h(x0, y0, x1, y1))
        b = [self.s(x0), self.s(y0), self.s(x1), self.s(y1)]
        if fill: self._d(role).ellipse(b, fill=self._fill(role))
        else:    self._d(role).ellipse(b, outline=self._fill(role), width=int(self.w))

    def arc(self, cx, cy, r, a0, a1, role=INK):
        self.rec("ARC", role, self.h(cx, cy, r) + [int(round(a0)), int(round(a1))])
        x, y, R = self.s(cx), self.s(cy), self.s(r)
        self._d(role).arc([x - R, y - R, x + R, y + R], a0, a1, fill=self._fill(role), width=int(self.w))
        for a in (a0, a1):   # round caps
            px, py = x + R * math.cos(math.radians(a)), y + R * math.sin(math.radians(a))
            self._d(role).ellipse([px - self.w / 2, py - self.w / 2, px + self.w / 2, py + self.w / 2], fill=self._fill(role))

    def rrect(self, x0, y0, x1, y1, rad, role=INK, fill=False):
        self.rec("RRECT_FILL" if fill else "RRECT", role, self.h(x0, y0, x1, y1, rad))
        b = [self.s(x0), self.s(y0), self.s(x1), self.s(y1)]
        if fill: self._d(role).rounded_rectangle(b, radius=self.s(rad), fill=self._fill(role))
        else:    self._d(role).rounded_rectangle(b, radius=self.s(rad), outline=self._fill(role), width=int(self.w))

    def poly(self, pts, role=INK, fill=False):
        if fill: self.rec("POLY_FILL", role, self.h(*[c for p in pts for c in p]))
        else:    self.rec("POLY", role, self.h(*[c for p in pts for c in p]))
        P = [(self.s(x), self.s(y)) for x, y in pts]
        if fill: self._d(role).polygon(P, fill=self._fill(role))
        else:
            saved = self.cmds; self.cmds = []
            self.line(pts + [pts[0]], role)
            self.cmds = saved

    def pie(self, cx, cy, r, a0, a1, role):
        self.rec("PIE", role, self.h(cx, cy, r) + [int(round(a0)), int(round(a1))])
        x, y, R = self.s(cx), self.s(cy), self.s(r)
        self._d(role).pieslice([x - R, y - R, x + R, y + R], a0, a1, fill=self._fill(role))

    def _shape_vals(self, shapes):
        v = [len(shapes)]
        for sh in shapes:
            if sh[0] == "disc": v += [0] + self.h(*sh[1:])
            else:               v += [1] + self.h(*sh[1:])
        return v

    def union_outline(self, shapes, role=INK):
        """Outline of a union of ('disc',cx,cy,r) / ('rrect',x0,y0,x1,y1,rad) shapes."""
        self.rec("UNION_OUTLINE", role, self._shape_vals(shapes))
        big = Image.new("L", self.layers[INK].size, 0); db = ImageDraw.Draw(big)
        small = Image.new("L", self.layers[INK].size, 0); ds = ImageDraw.Draw(small)
        w = self.w
        for sh in shapes:
            if sh[0] == "disc":
                _, cx, cy, r = sh; x, y, R = self.s(cx), self.s(cy), self.s(r)
                db.ellipse([x - R, y - R, x + R, y + R], fill=255)
                ds.ellipse([x - R + w, y - R + w, x + R - w, y + R - w], fill=255)
            else:
                _, x0, y0, x1, y1, rad = sh; b = [self.s(x0), self.s(y0), self.s(x1), self.s(y1)]
                db.rounded_rectangle(b, radius=self.s(rad), fill=255)
                ib = [b[0] + w, b[1] + w, b[2] - w, b[3] - w]
                if ib[2] > ib[0] and ib[3] > ib[1]:
                    ds.rounded_rectangle(ib, radius=max(0, self.s(rad) - w), fill=255)
        from PIL import ImageChops
        ring = ImageChops.subtract(big, small)
        self.layers[role].paste(255, mask=ring)

    def union_fill(self, shapes, role):
        if shapes: self.rec("UNION_FILL", role, self._shape_vals(shapes))
        d = self._d(role)
        for sh in shapes:
            if sh[0] == "disc":
                _, cx, cy, r = sh; x, y, R = self.s(cx), self.s(cy), self.s(r)
                d.ellipse([x - R, y - R, x + R, y + R], fill=255)
            else:
                _, x0, y0, x1, y1, rad = sh
                d.rounded_rectangle([self.s(x0), self.s(y0), self.s(x1), self.s(y1)], radius=self.s(rad), fill=255)

    def finish(self):
        """-> dict role -> 1-bit PIL image at target size; ink knocks out accents."""
        out = {}
        for r, im in self.layers.items():
            out[r] = im.point(lambda v: 255 if v >= 128 else 0).convert("1")
        # accents never show through ink
        from PIL import ImageChops
        inkmask = out[INK].convert("L")
        for r in ACCENTS:
            out[r] = ImageChops.subtract(out[r].convert("L"), inkmask).convert("1")
        return out


# ---------------------------------------------------------------------------
# The icons. Coordinates in a 48x48 box. Draw accents (fills) before ink.
# ---------------------------------------------------------------------------
def sun_core(c, cx, cy, r, ray0, ray1, n=8):
    c.disc(cx, cy, r - 1, SUN)
    c.circle(cx, cy, r)
    for i in range(n):
        a = math.radians(i * 360 / n)
        c.line([(cx + ray0 * math.cos(a), cy + ray0 * math.sin(a)), (cx + ray1 * math.cos(a), cy + ray1 * math.sin(a))])

def wave(c, y, x0=8, x1=40, amp=2.5, role=INK):
    pts = [(x, y + amp * math.sin((x - x0) / (x1 - x0) * 2 * math.pi * 1.5)) for x in [x0 + i * (x1 - x0) / 24 for i in range(25)]]
    c.line(pts, role)

CLOUD = [("disc", 17, 26, 7), ("disc", 25, 21, 9), ("disc", 33, 26, 6.5), ("rrect", 10, 26, 39.5, 33, 3.5)]

def i_sun(c):            sun_core(c, 24, 24, 8, 13, 18)
def i_sun_and_waves(c):
    sun_core(c, 24, 17, 6.5, 10.5, 14.5)
    wave(c, 34, role=WATER); wave(c, 41, role=WATER)
    wave(c, 34); wave(c, 41)
def i_cloud(c):          c.union_outline(CLOUD)
def i_rain_cloud(c):
    shapes = [("disc", 17, 20, 7), ("disc", 25, 15, 9), ("disc", 33, 20, 6.5), ("rrect", 10, 20, 39.5, 27, 3.5)]
    c.union_outline(shapes)
    for x in (17, 25, 33):
        c.line([(x + 1, 32), (x - 1, 40)], WATER); c.line([(x + 1, 32), (x - 1, 40)])
def i_raindrop(c):
    c.union_fill([("disc", 24, 29, 9)], WATER); c.poly([(24, 9), (15.5, 26), (32.5, 26)], WATER, fill=True)
    c.arc(24, 29, 9, -20, 200)
    c.line([(15.6, 25), (24, 9), (32.4, 25)])
def i_wind(c):
    c.line([(8, 17), (30, 17)]); c.arc(30, 13, 4, 90, 360)
    c.line([(8, 25), (36, 25)]); c.arc(36, 29, 4, -90, 180)
    c.line([(8, 33), (24, 33)]); c.arc(24, 37, 4, 90, 360)
def i_leaf(c):
    # lens between (11,37) and (37,11)
    c.poly([(11, 37), (16, 18), (37, 11), (32, 30)], LEAF, fill=True)
    c.arc(11.5, 11.5, 25.5, 0, 90); c.arc(36.5, 36.5, 25.5, 180, 270)
    c.line([(14, 34), (32, 16)])
def i_thermometer(c):
    c.rrect(19.5, 6, 28.5, 33, 4.5)
    c.line([(24, 18), (24, 32)], WARM, w=3.2); c.disc(24, 38, 6.5, WARM)
    c.circle(24, 38, 7)
def i_sun_hat(c):
    c.ellipse(6, 29, 42, 37); c.union_fill([("disc", 24, 29, 10)], SUN)
    c.arc(24, 30, 10, 180, 360); c.line([(14, 30), (34, 30)])
def i_sunscreen(c):
    c.rrect(17, 17, 31, 43, 3); c.rrect(20.5, 11, 27.5, 17, 1.5); c.line([(19, 7.5), (29, 7.5)])
    c.line([(21, 27), (27, 27)]); c.line([(21, 33), (27, 33)])
def i_swim_trunks(c):
    pts = [(13, 11), (35, 11), (37.5, 39), (27.5, 39), (24, 23), (20.5, 39), (10.5, 39)]
    c.poly(pts, WATER, fill=True); c.poly(pts)
    c.line([(13.5, 17), (34.5, 17)])
def i_towel(c):
    c.rrect(12, 9, 36, 41, 5); c.line([(12, 20), (36, 20)]); c.line([(12, 30), (36, 30)])
    c.line([(12, 25), (36, 25)], WATER, w=4)
def i_water_bottle(c):
    c.rrect(17, 17, 31, 43, 3.5, WATER, fill=True); c.rrect(17, 17, 31, 43, 3.5)
    c.rrect(20, 9, 28, 17, 1.5); c.line([(19, 6), (29, 6)]); c.line([(20.5, 30), (27.5, 30)], PAPER)
def i_tshirt(c):
    pts = [(9, 12), (17.5, 7), (30.5, 7), (39, 12), (35.5, 20), (31, 18), (31, 41), (17, 41), (17, 18), (12.5, 20)]
    c.poly(pts); c.arc(24, 7.5, 5, 0, 180)
def i_shorts(c):
    pts = [(11, 10), (37, 10), (39, 38), (27.5, 38), (24, 23), (20.5, 38), (9, 38)]
    c.poly(pts); c.line([(11.5, 15.5), (36.5, 15.5)])
def i_sneaker(c):
    pts = [(6, 33), (7, 25), (16, 21), (24, 21), (33, 27), (42, 30), (42, 35), (6, 35)]
    c.poly(pts); c.line([(6, 35), (42, 35)]); c.line([(17, 26), (20, 23)]); c.line([(21, 28), (24, 25)])
def i_sweater(c):
    pts = [(5, 15), (16, 8), (32, 8), (43, 15), (40.5, 27), (34.5, 24.5), (34.5, 41), (13.5, 41), (13.5, 24.5), (7.5, 27)]
    c.poly(pts); c.arc(24, 8.5, 5.5, 0, 180); c.line([(13.5, 36), (34.5, 36)])
def i_hoodie(c):
    i_sweater(c); c.arc(24, 10, 9, 190, 350); c.line([(20, 15), (19, 27)]); c.line([(28, 15), (29, 27)])
def i_jacket(c):
    pts = [(5, 15), (16, 8), (32, 8), (43, 15), (40.5, 27), (34.5, 24.5), (34.5, 41), (13.5, 41), (13.5, 24.5), (7.5, 27)]
    c.poly(pts); c.line([(24, 13), (24, 41)]); c.line([(16, 8), (24, 15), (32, 8)])
def i_long_pants(c):
    pts = [(13, 7), (35, 7), (37, 41), (27, 41), (24, 20), (21, 41), (11, 41)]
    c.poly(pts); c.line([(13.5, 12.5), (34.5, 12.5)])
def i_raincoat(c):
    pts = [(5, 16), (16, 9), (32, 9), (43, 16), (40.5, 28), (35, 25.5), (35, 43), (13, 43), (13, 25.5), (7.5, 28)]
    c.poly(pts, SUN, fill=True); c.poly(pts); c.arc(24, 11, 9, 190, 350); c.line([(24, 16), (24, 43)])
def i_rain_boot(c):
    pts = [(13, 6), (27, 6), (27, 27), (42, 34.5), (42, 41), (11, 41), (11, 6)]
    c.poly(pts, SUN, fill=True); c.poly(pts); c.line([(11, 11.5), (27, 11.5)]); c.line([(11, 36), (42, 36)])
def i_umbrella(c):
    c.pie(24, 25, 18, 180, 360, WARM)
    c.arc(24, 25, 18, 180, 360)
    for x0 in (6, 18, 30): c.arc(x0 + 6, 25, 6, 0, 180)
    c.line([(24, 7), (24, 25)]); c.line([(24, 25), (24, 38)]); c.arc(20.5, 38, 3.5, 0, 180)
def i_puffy_coat(c):
    pts = [(5, 16), (16, 9), (32, 9), (43, 16), (40.5, 28), (35, 25.5), (35, 42), (13, 42), (13, 25.5), (7.5, 28)]
    c.poly(pts, WARM, fill=True); c.poly(pts)
    for y in (22, 29, 36): c.line([(13.5, y), (34.5, y)])
    c.line([(16, 9), (24, 16), (32, 9)])
def i_beanie(c):
    c.pie(24, 27, 15, 180, 360, WARM)
    c.arc(24, 27, 15, 180, 360); c.rrect(7, 26, 41, 35, 3); c.disc(24, 10, 3.5, WARM); c.circle(24, 10, 4)
def i_mittens(c):
    shapes = [("rrect", 15, 12, 35, 42, 9), ("disc", 12, 26, 6)]
    c.union_fill(shapes, WARM); c.union_outline(shapes); c.line([(15.5, 35), (34.5, 35)])
def i_scarf(c):
    c.rrect(10, 12, 38, 22, 5, WARM, fill=True); c.rrect(10, 12, 38, 22, 5)
    c.poly([(15, 22), (21.5, 22), (23.5, 43), (17, 43)], WARM, fill=True); c.poly([(15, 22), (21.5, 22), (23.5, 43), (17, 43)])
    c.poly([(24.5, 22), (31, 22), (30, 43), (23.5, 43)], WARM, fill=True); c.poly([(24.5, 22), (31, 22), (30, 43), (23.5, 43)])
    for x in (18, 20.5, 26, 28.5): c.line([(x, 43), (x, 46)])

ICONS = {
    "sun": i_sun, "sun_and_waves": i_sun_and_waves, "cloud": i_cloud, "rain_cloud": i_rain_cloud,
    "raindrop": i_raindrop, "wind": i_wind, "leaf": i_leaf, "thermometer": i_thermometer,
    "sun_hat": i_sun_hat, "sunscreen": i_sunscreen, "swim_trunks": i_swim_trunks, "towel": i_towel,
    "water_bottle": i_water_bottle, "tshirt": i_tshirt, "shorts": i_shorts, "sneaker": i_sneaker,
    "sweater": i_sweater, "hoodie": i_hoodie, "jacket": i_jacket, "long_pants": i_long_pants,
    "raincoat": i_raincoat, "rain_boot": i_rain_boot, "umbrella": i_umbrella, "puffy_coat": i_puffy_coat,
    "beanie": i_beanie, "mittens": i_mittens, "scarf": i_scarf,
}


OPS = ["LINE", "LINEW", "ARC", "CIRCLE", "DISC", "ELLIPSE", "ELLIPSE_FILL", "RRECT", "RRECT_FILL",
       "POLY", "POLY_FILL", "PIE", "UNION_OUTLINE", "UNION_FILL"]


def main():
    lines = ["// GENERATED by tools/gen_icons.py - do not edit. The vector sources live in that script.",
             "// Coordinates are in half-units of a 48x48 box (so 24.5 -> 49); angles in degrees;",
             "// LINEW's first value is a stroke multiplier x10. Roles: 0 ink 1 paper 2 sun 3 water 4 leaf 5 warm.",
             "#pragma once", "#include <stdint.h>", "#include <pgmspace.h>", "",
             "enum IcOp : uint8_t { " + ", ".join(f"IC_{o} = {i}" for i, o in enumerate(OPS)) + " };",
             "struct IcCmd { uint8_t op; uint8_t role; uint8_t n; const int16_t* v; };",
             "struct IconDef { const char* name; const IcCmd* cmds; uint8_t count; };", ""]
    defs = []
    total_vals = 0
    preview_size = SIZES["hero"]
    cols = 9
    rows = math.ceil(len(ICONS) / cols)
    cell_w = preview_size + 60
    cell_h = preview_size + SIZES["tile"] + 40
    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), "white")
    colours = { SUN: (245, 200, 0), WATER: (40, 110, 230), LEAF: (40, 160, 70), WARM: (220, 50, 40) }

    for idx, (name, fn) in enumerate(ICONS.items()):
        # commands are size-independent; record them once from a hero-size pass
        c = Canvas(preview_size); fn(c)
        vals, cmds = [], []
        for op, role, v in c.cmds:
            cmds.append((op, role, len(v), len(vals)))
            vals.extend(v)
        total_vals += len(vals)
        lines.append(f"static const int16_t IC_{name}_v[] PROGMEM = {{ " + ", ".join(str(v) for v in vals) + " };")
        lines.append(f"static const IcCmd IC_{name}[] PROGMEM = {{")
        for op, role, n, off in cmds:
            lines.append(f"  {{ IC_{op}, {role}, {n}, IC_{name}_v + {off} }},")
        lines.append("};")
        defs.append(f'  {{ "{name}", IC_{name}, {len(cmds)} }},')

        # preview: hero + tile + stat, aliased, accents in colour with ink knocked out
        col, row = idx % cols, idx // cols
        x0, y0 = col * cell_w + 8, row * cell_h + 8
        for sname, pos in (("hero", (x0, y0)), ("tile", (x0, y0 + preview_size + 6)),
                           ("stat", (x0 + SIZES["tile"] + 10, y0 + preview_size + 6 + (SIZES["tile"] - SIZES["stat"]) // 2))):
            size = SIZES[sname]
            cc = Canvas(size); fn(cc); layers = cc.finish()
            cell = Image.new("RGB", (size, size), "white")
            for r in ACCENTS:
                if layers[r].getbbox():
                    cell.paste(colours[r], mask=layers[r].convert("L"))
            cell.paste((17, 17, 17), mask=layers[INK].convert("L"))
            sheet.paste(cell, pos)
        ImageDraw.Draw(sheet).text((x0, y0 + preview_size + SIZES["tile"] + 12), name, fill=(0, 0, 0))

    lines.append("")
    lines.append("static const IconDef ICON_DEFS[] PROGMEM = {")
    lines.extend(defs)
    lines.append("};")
    lines.append("static const int ICON_DEF_COUNT = sizeof(ICON_DEFS) / sizeof(ICON_DEFS[0]);")
    OUT.write_text("\n".join(lines) + "\n")
    sheet.save(PREVIEW)
    print(f"{len(ICONS)} icons, {total_vals} int16 values ({total_vals * 2} bytes) -> {OUT.relative_to(ROOT)}")
    print(f"preview: {PREVIEW}")


if __name__ == "__main__":
    main()
