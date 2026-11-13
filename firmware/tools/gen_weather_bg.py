#!/usr/bin/env python3
"""
Embed the Today backgrounds by weather of a deck: firmware/src/decks/<DECK>/assets/weather_bg.c / .h.

Sources: firmware/tools/weather_backgrounds/<group>_<day|night>.jpg, 9 weather groups x day / night
(see PROMPTS.md there: one alpine valley under every kind of weather). Each image is fitted to the
Today page of this deck ("cover": scaled and centre-cropped to the content area under the top bar)
and darkened where the page puts text straight on it (the scrim below), so the clock and the
conditions stay readable with no runtime cost. Stored as baseline JPEG; the firmware decodes the
current one with JPEGDEC when the weather changes.

The page size and where text sits on the picture (the scrim) are per deck in tools/decks.py.

Usage (from the repository root):
    python firmware/tools/gen_weather_bg.py --deck all      # every source image must exist
    python firmware/tools/gen_weather_bg.py --deck JC3248W535 --placeholder
                                                  # draw stand-ins for missing ones (local tests
                                                  # only: the firmware warns at build)
Requires Pillow.
"""
import argparse
import io
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

import decks

FW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(FW, "tools", "weather_backgrounds")
QUALITY = 84
# Set per deck by main() from tools/decks.py: output files, Today page size, and where text sits
# straight on the picture: (x0, x1, y0, y1, darkness 0..1) boxes with a soft edge of FEATHER px.
OUT_C = OUT_H = ""
W = H = FEATHER = 0
SCRIM = []
GLOBAL_DIM = 0.80  # the whole picture sits behind glass cards and white text

GROUPS = ["clear", "partly", "cloudy", "fog", "drizzle", "rain", "storm", "snow", "sleet"]


def fit(img):
    """Scale to cover W x H, centre crop."""
    s = max(W / img.width, H / img.height)
    img = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    x, y = (img.width - W) // 2, (img.height - H) // 2
    return img.crop((x, y, x + W, y + H))


def scrim_mask():
    """Brightness multiplier per pixel (L image, 255 = untouched)."""
    m = Image.new("L", (W, H), 255)
    d = ImageDraw.Draw(m)
    for x0, x1, y0, y1, dark in SCRIM:
        d.rectangle([x0, y0, x1, y1], fill=round(255 * (1 - dark)))
    m = m.filter(ImageFilter.GaussianBlur(FEATHER / 2))
    return m.point(lambda v: round(v * GLOBAL_DIM))


def apply_scrim(img, mask):
    black = Image.new("RGB", img.size, (0, 0, 0))
    return Image.composite(img, black, mask)


# ---- stand-ins (development only) ------------------------------------------------------------------
SKY = {  # (top, horizon) per group, day / night
    "clear": [((40, 92, 165), (150, 185, 220)), ((6, 10, 32), (28, 38, 78))],
    "partly": [((62, 102, 150), (165, 180, 196)), ((14, 18, 40), (44, 50, 78))],
    "cloudy": [((92, 97, 108), (140, 144, 150)), ((24, 27, 34), (48, 52, 60))],
    "fog": [((150, 155, 162), (196, 198, 200)), ((40, 42, 50), (78, 80, 88))],
    "drizzle": [((82, 92, 106), (128, 136, 146)), ((20, 25, 36), (44, 52, 64))],
    "rain": [((54, 64, 80), (98, 108, 122)), ((12, 16, 28), (32, 40, 56))],
    "storm": [((38, 38, 62), (86, 80, 110)), ((14, 10, 34), (50, 34, 86))],
    "snow": [((168, 178, 194), (222, 226, 236)), ((26, 36, 62), (66, 80, 112))],
    "sleet": [((118, 128, 140), (172, 176, 184)), ((24, 30, 46), (58, 64, 80))],
}


def stand_in(group, night):
    """A rough valley under the weather: sky, mountains, forest, lake (+ stars, rain, snow, bolt)."""
    rnd = random.Random(f"{group}{night}")
    w, h = 1536, 1024
    top, hor = SKY[group][1 if night else 0]
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = min(1.0, y / (h * 0.62))
        d.line([(0, y), (w, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, hor)))
    if night and group in ("clear", "partly"):
        for _ in range(260):
            x, y = rnd.randrange(w), rnd.randrange(int(h * 0.55))
            v = rnd.randrange(120, 255)
            d.point((x, y), fill=(v, v, v))
    if group in ("partly", "cloudy", "drizzle", "rain", "storm"):
        cl = Image.new("L", (w, h), 0)
        cd = ImageDraw.Draw(cl)
        n = 14 if group == "partly" else 40
        for _ in range(n):
            x, y, r = rnd.randrange(-100, w), rnd.randrange(0, int(h * 0.5)), rnd.randrange(60, 200)
            cd.ellipse([x - r * 2, y - r, x + r * 2, y + r], fill=rnd.randrange(60, 140))
        cl = cl.filter(ImageFilter.GaussianBlur(40))
        shade = (210, 215, 225) if not night else (70, 76, 92)
        img = Image.composite(Image.new("RGB", (w, h), shade), img, cl)
        d = ImageDraw.Draw(img)
    base = int(h * 0.60)
    mountain = [(0, base)]
    x = 0
    while x < w:
        x += rnd.randrange(60, 160)
        mountain.append((x, base - rnd.randrange(60, 260) - (120 if w * 0.45 < x < w * 0.8 else 0)))
    mountain += [(w, base), (w, h), (0, h)]
    mcol = tuple(round(c * 0.55) for c in hor) if group != "snow" else tuple(min(255, round(c * 0.95)) for c in hor)
    d.polygon(mountain, fill=mcol)
    forest = tuple(round(c * 0.28) for c in hor) if group != "snow" else tuple(round(c * 0.7) for c in hor)
    for x in range(0, w, 14):
        tall = rnd.randrange(30, 90)
        d.polygon([(x, base + 40), (x + 7, base + 40 - tall), (x + 14, base + 40)], fill=forest)
    d.rectangle([0, base + 40, w, h], fill=tuple(round(c * 0.5) for c in hor))
    lake = img.crop((0, base - 300, w, base)).transpose(Image.FLIP_TOP_BOTTOM).point(lambda v: v * 0.55)
    img.paste(lake, (0, base + 60))
    d = ImageDraw.Draw(img)
    if group in ("drizzle", "rain", "storm", "sleet"):
        n = {"drizzle": 260, "sleet": 300, "rain": 600, "storm": 700}[group]
        for _ in range(n):
            x, y = rnd.randrange(w), rnd.randrange(h)
            ln = rnd.randrange(14, 40)
            d.line([(x, y), (x - ln // 5, y + ln)], fill=(170, 180, 195), width=1)
    if group in ("snow", "sleet"):
        for _ in range(500 if group == "snow" else 180):
            x, y, r = rnd.randrange(w), rnd.randrange(h), rnd.randrange(1, 4)
            d.ellipse([x - r, y - r, x + r, y + r], fill=(240, 244, 250))
    if group == "storm":
        x, y = int(w * 0.72), 120
        pts = [(x, y)]
        while y < base - 60:
            x += rnd.randrange(-40, 40)
            y += rnd.randrange(40, 80)
            pts.append((x, y))
        d.line(pts, fill=(235, 230, 255), width=5)
    if group == "fog":
        fog = Image.new("RGB", (w, h), hor)
        img = Image.blend(img, fog, 0.45)
    return img


# ---- output ---------------------------------------------------------------------------------------
def c_bytes(data, per_line=32):
    return "\n".join(
        "    " + ",".join("0x%02x" % b for b in data[i : i + per_line]) + "," for i in range(0, len(data), per_line)
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--deck", default="all", help="deck key (src/decks/<KEY>) or all")
    ap.add_argument("--placeholder", action="store_true", help="draw stand-ins for missing source images")
    args = ap.parse_args()
    for deck in decks.selected(args.deck):
        build(deck, args)


def build(deck, args):
    global OUT_C, OUT_H, W, H, SCRIM, FEATHER
    OUT_C = os.path.join(decks.deck_dir(deck), "assets", "weather_bg.c")
    OUT_H = os.path.join(decks.deck_dir(deck), "assets", "weather_bg.h")
    t = decks.TOOLS[deck]["today"]
    (W, H), SCRIM, FEATHER = t["size"], t["scrim"], t["feather"]
    mask = scrim_mask()
    c = ["/* Generated by firmware/tools/gen_weather_bg.py - do not edit. */", '#include "weather_bg.h"', ""]
    rows, total, stand_ins = [], 0, 0
    for g in GROUPS:
        for night in (False, True):
            name = f"{g}_{'night' if night else 'day'}"
            path = os.path.join(SRC, name + ".jpg")
            if os.path.exists(path):
                src = Image.open(path).convert("RGB")
            elif args.placeholder:
                src = stand_in(g, night)
                stand_ins += 1
            else:
                raise SystemExit(f"missing {os.path.relpath(path, FW)} (see tools/weather_backgrounds/PROMPTS.md)")
            buf = io.BytesIO()
            apply_scrim(fit(src), mask).save(buf, "JPEG", quality=QUALITY, optimize=True, progressive=False, subsampling=2)
            data = buf.getvalue()
            total += len(data)
            c.append("static const uint8_t wb_%s_jpg[] __attribute__((aligned(4))) = {\n%s\n};\n" % (name, c_bytes(data)))
            rows.append('    {"%s", wb_%s_jpg, sizeof(wb_%s_jpg)},' % (name, name, name))
            print("%-14s %6d bytes%s" % (name, len(data), "" if os.path.exists(path) else "  (stand-in)"))
    c.append("/* index = group * 2 + night; groups: " + ", ".join(GROUPS) + " */")
    c.append("const weather_bg_t WEATHER_BG[] = {")
    c += rows
    c.append("};")
    c.append("const int WEATHER_BG_COUNT = sizeof(WEATHER_BG) / sizeof(WEATHER_BG[0]);")
    os.makedirs(os.path.dirname(OUT_C), exist_ok=True)
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(c) + "\n")
    h = f"""/* Generated by firmware/tools/gen_weather_bg.py - do not edit */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

typedef struct {{
    const char *id;     /* <group>_<day|night> */
    const uint8_t *jpg; /* baseline JPEG, WEATHER_BG_W x WEATHER_BG_H, scrim baked in */
    uint32_t jpg_size;
}} weather_bg_t;

extern const weather_bg_t WEATHER_BG[];  /* index = group * 2 + night */
extern const int WEATHER_BG_COUNT;

#define WEATHER_BG_W {W}
#define WEATHER_BG_H {H}
#define WEATHER_BG_STAND_INS {stand_ins}  /* > 0: development stand-ins, not for release */

#ifdef __cplusplus
}}
#endif
"""
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(h)
    print("done: %d backgrounds (%d stand-ins), %d KB of JPEG -> %s" % (len(rows), stand_ins, total // 1024, os.path.relpath(OUT_C, FW)))


if __name__ == "__main__":
    main()
