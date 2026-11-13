"""The decks the firmware supports, as the generator tools see them.

Screen size, byte order and coin logo sizes are read from each deck's own headers
(src/decks/<DECK>/include/: board_pins.h, lv_conf.h, deck_config.h), so they never disagree with
the firmware. What only the tools need (image quality, where Today's text sits on the weather
picture) is in TOOLS below. Adding a deck: its folder in src/decks/ and an entry here.
"""
from __future__ import annotations

import os
import re

FW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(FW, "src")

# Per-deck settings of the generators.
#   wallpaper_quality  JPEG quality of the screensaver wallpapers (gen_wallpapers.py)
#   today              Today page area behind the weather picture: (W, H), and the scrim baked into the
#                      picture where text sits on it without a card: boxes (x0, x1, y0, y1, alpha)
#                      feathered over `feather` px (gen_weather_bg.py)
TOOLS = {
    "JC1060P470C": dict(
        wallpaper_quality=90,
        # Content area under the top bar, right of the rail. Left column: clock, date, current
        # conditions, stats (no card).
        today=dict(size=(920, 548), scrim=[(0, 470, 0, 548, 0.55)], feather=90),
    ),
    "JC4827W543": dict(
        # ~150 KB for the eight images at 4:2:0; the RGB565 panel hides anything above this quality.
        wallpaper_quality=94,
        # Content area under the top bar, right of the rail. Top of the left column: clock and date
        # (no card; the rest sits in cards).
        today=dict(size=(416, 242), scrim=[(0, 200, 0, 92, 0.55)], feather=40),
    ),
    "JC3248W535": dict(
        # The skies are smooth dark gradients; the eight images are still only ~100 KB together.
        wallpaper_quality=92,
        # Portrait: content area between the top bar and the tab bar. Top of the page: big clock and
        # date (no card; the rest sits in cards).
        today=dict(size=(320, 374), scrim=[(0, 320, 0, 128, 0.55)], feather=50),
    ),
    "ESP32_4848S040": dict(
        wallpaper_quality=92,
        # Square: content area between the top bar and the tab bar. Top of the page: big clock and
        # date (no card; the rest sits in cards).
        today=dict(size=(480, 374), scrim=[(0, 480, 0, 128, 0.55)], feather=50),
    ),
}


def all_decks() -> list[str]:
    return sorted(d for d in os.listdir(os.path.join(SRC, "decks")) if os.path.isdir(os.path.join(SRC, "decks", d)))


def deck_dir(deck: str) -> str:
    path = os.path.join(SRC, "decks", deck)
    if not os.path.isdir(path):
        raise SystemExit("unknown deck %r (known: %s)" % (deck, ", ".join(all_decks())))
    return path


def define(deck: str, header: str, name: str) -> int:
    path = os.path.join(deck_dir(deck), "include", header)
    m = re.search(r"^#define\s+%s\s+(\d+)" % name, open(path, encoding="utf-8").read(), re.M)
    if not m:
        raise SystemExit("%s: no #define %s" % (path, name))
    return int(m.group(1))


def screen(deck: str) -> tuple[int, int]:
    return define(deck, "board_pins.h", "PCD_LCD_H_RES"), define(deck, "board_pins.h", "PCD_LCD_V_RES")


def big_endian(deck: str) -> bool:
    """RGB565 byte order of the deck's LVGL build (LV_COLOR_16_SWAP=1: big-endian, QSPI panels)."""
    return define(deck, "lv_conf.h", "LV_COLOR_16_SWAP") == 1


def coin_logo_sizes(deck: str) -> dict[str, int]:
    return {"s": define(deck, "deck_config.h", "PCD_COIN_LOGO_S"), "m": define(deck, "deck_config.h", "PCD_COIN_LOGO_M")}


def selected(arg: str) -> list[str]:
    """`--deck` value: one deck key, or "all"."""
    return all_decks() if arg == "all" else [arg if os.path.isdir(os.path.join(SRC, "decks", arg)) else deck_dir(arg)]
