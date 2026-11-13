#!/usr/bin/env python3
"""
Generate a deck's UI fonts (src/decks/<DECK>/ui/fonts/pcd_font_*.c) with lv_font_conv.

Every deck uses the same glyph sets: Latin-1 (á é í ó ú ñ ¿ ¡ ° ·), a few typographic marks, the
LVGL built-in symbols and the extra FontAwesome icons (weather, crypto); the text sizes that also
render song titles and PC names carry every script Montserrat covers (Latin Extended, Cyrillic,
Vietnamese). What differs per deck is the type scale (FONTS below).

Sources: Montserrat-Medium.ttf (SIL OFL 1.1) and FontAwesome5 Free (icons CC BY 4.0), both shipped
inside the LVGL 8.4.0 package that PlatformIO downloads to .pio/libdeps/<DECK>/lvgl.
Requires Node.js (npx); lv_font_conv is pinned.

Usage (from the repository root):
    python firmware/tools/gen_fonts.py --deck JC3248W535      # or --deck all
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

import decks

LV_FONT_CONV = "lv_font_conv@1.5.2"

TEXT = "0x20-0x7E,0xA0-0xFF,0x2014,0x2022,0x2026,0x20AC"  # + euro (Crypto prices)
# Text sizes that also render song titles and PC names carry every script that Montserrat covers:
# Latin Extended-A/B, Cyrillic, Vietnamese, typographic punctuation, euro, TM. The PC app folds
# anything else (emoji, CJK, Greek) before sending; keep this list in sync with the PC app's
# src/pcdeck_agent/textfit.py (FONT_RANGES).
TEXT_EXT = TEXT + ",0x100-0x24F,0x400-0x45F,0x1EA0-0x1EF9,0x2010-0x203A,0x2122"
# Weather condition icons: sun, cloud, cloud-sun, cloud-moon, cloud-rain, cloud-showers-heavy,
# cloud-sun-rain, cloud-moon-rain, snowflake, smog.
WEATHER = "61829,61634,63172,63171,63293,63296,63299,63292,62172,63327"
WEATHER_ICONS = WEATHER + ",61830,61671"  # + moon, bolt (used by the conditions)
# Crypto tab: bitcoin (brand), coins, chart-line, chart-bar, caret-up, caret-down.
CRYPTO = "62329,62750,61953,61568,61656,61655"
# LVGL built-in symbols (lvgl/scripts/built_in_font/built_in_font_gen.py, v8.4.0) + extras:
# 62153 thermometer-half, 61475 lock, 61918 sliders-h, 62461 tachometer-alt, 61830 moon, the weather
# icons, wind, umbrella, map-marker-alt, search, clock, and the crypto icons.
SYMS = ("61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,"
        "61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,"
        "61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,"
        "62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650,62153,61475,61918,62461,"
        "61830," + WEATHER + ",63278,61673,62405,61442,61463," + CRYPTO)
DIGITS = "0x20,0x2D,0x30-0x3A"  # clock: space, dash ("--:--"), digits, colon
DIGITS_DEG = DIGITS + ",0xB0"  # + degree sign (Today temperature)

# Type scale per deck.
#   text         text sizes (pcd_font_<size>): Montserrat + icons
#   labels_only  the smallest size, tab / rail labels: text only, no icons
#   ext          (lo, hi]: text sizes with the extended scripts (song titles, PC names)
#   syms_max     sizes up to this one carry the symbols (the largest: player transport icons); the
#                bigger ones only the weather icons
#   digits       (size, with degree sign): clock / temperature fonts, digits only
#   icons        weather-icon-only fonts (pcd_font_wx<size>)
FONTS = {
    # 1024x600, ~170 dpi: the 480x272 type scale x1.5. 15: navigation-rail labels, 18: captions.
    "JC1060P470C": dict(text=[15, 18, 21, 24, 30, 42, 60], labels_only=15, ext=(18, 30), syms_max=42,
                        digits=[(78, True), (150, True)],  # Today temperature; screensaver / Today clock
                        icons=[92, 34, 26]),  # Today: "now", next hours, next days (a notch under the text)
    # 480x272. 10: navigation-rail labels, 12: captions.
    "JC4827W543": dict(text=[10, 12, 14, 16, 20, 28, 40], labels_only=10, ext=(12, 20), syms_max=28,
                       digits=[(52, False), (84, False)],  # Today clock; screensaver clock
                       icons=[]),
    # 320x480 portrait. 12: bottom-bar labels.
    "JC3248W535": dict(text=[12, 14, 16, 20, 28, 40], labels_only=12, ext=(12, 20), syms_max=28,
                       digits=[(72, False), (90, False)],  # Today clock; screensaver clock ("20:48" in 288 px)
                       icons=[]),
}


def convert(src: str, out_dir: str, rel_out: str, name: str, size: int, ranges: list[tuple[str, str]]) -> None:
    """ranges: (font file, -r ranges) pairs, in order. The C symbol is the file name (pcd_font_...)."""
    path = os.path.join(out_dir, name + ".c")
    print("->", name)
    args = ["npx", "-y", LV_FONT_CONV, "--no-compress", "--bpp", "4", "--size", str(size)]
    for font, r in ranges:
        args += ["--font", os.path.join(src, font), "-r", r]
    args += ["--format", "lvgl", "--lv-include", "lvgl.h", "--force-fast-kern-format", "-o", path]
    subprocess.run(args, check=True, shell=sys.platform == "win32")
    # Keep machine-specific absolute paths out of the committed sources.
    with open(path, encoding="utf-8") as fh:
        c = fh.read()
    c = c.replace(src, "<lvgl-8.4.0>/scripts/built_in_font").replace(out_dir, rel_out)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(c)


def build(deck: str) -> None:
    spec = FONTS[deck]
    src = os.path.join(decks.FW, ".pio", "libdeps", deck, "lvgl", "scripts", "built_in_font")
    if not os.path.isdir(src):
        sys.exit("%s not found: run `pio pkg install -e %s` first" % (src, deck))
    out_dir = os.path.join(decks.deck_dir(deck), "ui", "fonts")
    os.makedirs(out_dir, exist_ok=True)
    rel_out = "src/decks/%s/ui/fonts" % deck
    mont, fa = "Montserrat-Medium.ttf", "FontAwesome5-Solid+Brands+Regular.woff"
    lo, hi = spec["ext"]
    for s in spec["text"]:
        ranges = [(mont, TEXT_EXT if lo < s <= hi else TEXT)]
        if s == spec["labels_only"]:
            pass
        elif s <= spec["syms_max"]:
            ranges.append((fa, SYMS))
        else:
            ranges.append((fa, WEATHER_ICONS))
        convert(src, out_dir, rel_out, "pcd_font_%d" % s, s, ranges)
    for s, degree in spec["digits"]:
        convert(src, out_dir, rel_out, "pcd_font_%d" % s, s, [(mont, DIGITS_DEG if degree else DIGITS)])
    for s in spec["icons"]:
        convert(src, out_dir, rel_out, "pcd_font_wx%d" % s, s, [(fa, WEATHER_ICONS)])
    print("fonts of %s written to %s" % (deck, os.path.relpath(out_dir, decks.FW)))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--deck", default="all", help="deck key (src/decks/<KEY>) or all")
    for deck in decks.selected(ap.parse_args().deck):
        if deck not in FONTS:
            sys.exit("no font spec for %s in tools/gen_fonts.py (FONTS)" % deck)
        build(deck)


if __name__ == "__main__":
    main()
