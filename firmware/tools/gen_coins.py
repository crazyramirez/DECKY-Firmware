#!/usr/bin/env python3
"""
Generate a deck's Crypto coin catalog + embedded coin logos (LVGL 8 LV_IMG_CF_RGB565A8 images).

Logos come from the CoinGecko public API (image URLs of /coins/markets), cached in
firmware/tools/.cache/logos (git-ignored), masked to an anti-aliased circle when needed and
resized to two sizes. A brand colour is extracted from each logo for UI accents.

LVGL reads RGB565A8 colour planes as lv_color_t, so the colour plane follows the deck's byte order:
big-endian with LV_COLOR_16_SWAP=1 (QSPI panels), little-endian with 0 (the JC1060P470C's MIPI-DSI
frame buffer). The logo sizes are the deck's PCD_COIN_LOGO_S / _M (deck_config.h).

Usage (from the repo root):
    python firmware/tools/gen_coins.py --deck all        # or --deck JC1060P470C

Outputs:
    firmware/src/decks/<DECK>/crypto/coin_catalog.c
    firmware/src/core/crypto/coin_catalog.h (the same for every deck)
"""
import argparse
import colorsys
import io
import json
import os
import sys
import urllib.request

from PIL import Image, ImageDraw

import decks

FW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(FW, "tools", ".cache", "logos")
OUT_H = os.path.join(FW, "src", "core", "crypto", "coin_catalog.h")

# symbol, display name, Binance pair, CoinGecko id   (ordered by market cap, Oct 2026)
COINS = [
    ("BTC", "Bitcoin", "BTCUSDT", "bitcoin"),
    ("ETH", "Ethereum", "ETHUSDT", "ethereum"),
    ("BNB", "BNB", "BNBUSDT", "binancecoin"),
    ("XRP", "XRP", "XRPUSDT", "ripple"),
    ("SOL", "Solana", "SOLUSDT", "solana"),
    ("TRX", "TRON", "TRXUSDT", "tron"),
    ("HYPE", "Hyperliquid", "HYPEUSDT", "hyperliquid"),
    ("DOGE", "Dogecoin", "DOGEUSDT", "dogecoin"),
    ("LINK", "Chainlink", "LINKUSDT", "chainlink"),
    ("ADA", "Cardano", "ADAUSDT", "cardano"),
    ("NEAR", "NEAR Protocol", "NEARUSDT", "near"),
    ("XLM", "Stellar", "XLMUSDT", "stellar"),
    ("BCH", "Bitcoin Cash", "BCHUSDT", "bitcoin-cash"),
    ("LTC", "Litecoin", "LTCUSDT", "litecoin"),
    ("UNI", "Uniswap", "UNIUSDT", "uniswap"),
    ("AVAX", "Avalanche", "AVAXUSDT", "avalanche-2"),
    ("SUI", "Sui", "SUIUSDT", "sui"),
    ("HBAR", "Hedera", "HBARUSDT", "hedera-hashgraph"),
    ("TAO", "Bittensor", "TAOUSDT", "bittensor"),
    ("SHIB", "Shiba Inu", "SHIBUSDT", "shiba-inu"),
    ("ENA", "Ethena", "ENAUSDT", "ethena"),
    ("AAVE", "Aave", "AAVEUSDT", "aave"),
    ("ONDO", "Ondo", "ONDOUSDT", "ondo-finance"),
    ("PEPE", "Pepe", "PEPEUSDT", "pepe"),
    ("DOT", "Polkadot", "DOTUSDT", "polkadot"),
    ("WLD", "Worldcoin", "WLDUSDT", "worldcoin-wld"),
    ("ICP", "Internet Computer", "ICPUSDT", "internet-computer"),
    ("ETC", "Ethereum Classic", "ETCUSDT", "ethereum-classic"),
    ("ARB", "Arbitrum", "ARBUSDT", "arbitrum"),
    ("ALGO", "Algorand", "ALGOUSDT", "algorand"),
    ("POL", "Polygon", "POLUSDT", "polygon-ecosystem-token"),
    ("JUP", "Jupiter", "JUPUSDT", "jupiter-exchange-solana"),
    ("RENDER", "Render", "RENDERUSDT", "render-token"),
    ("ATOM", "Cosmos", "ATOMUSDT", "cosmos"),
    ("INJ", "Injective", "INJUSDT", "injective-protocol"),
    ("FIL", "Filecoin", "FILUSDT", "filecoin"),
    ("CAKE", "PancakeSwap", "CAKEUSDT", "pancakeswap-token"),
    ("APT", "Aptos", "APTUSDT", "aptos"),
    ("VET", "VeChain", "VETUSDT", "vechain"),
    ("STX", "Stacks", "STXUSDT", "blockstack"),
    ("TIA", "Celestia", "TIAUSDT", "celestia"),
    ("PENGU", "Pudgy Penguins", "PENGUUSDT", "pudgy-penguins"),
    ("TRUMP", "Official Trump", "TRUMPUSDT", "official-trump"),
    ("VIRTUAL", "Virtuals", "VIRTUALUSDT", "virtual-protocol"),
    ("SEI", "Sei", "SEIUSDT", "sei-network"),
    ("LDO", "Lido DAO", "LDOUSDT", "lido-dao"),
    ("BONK", "Bonk", "BONKUSDT", "bonk"),
    ("OP", "Optimism", "OPUSDT", "optimism"),
    ("GRT", "The Graph", "GRTUSDT", "the-graph"),
    ("FLOKI", "FLOKI", "FLOKIUSDT", "floki"),
    ("EIGEN", "EigenCloud", "EIGENUSDT", "eigenlayer"),
    ("S", "Sonic", "SUSDT", "sonic-3"),
    ("WIF", "dogwifhat", "WIFUSDT", "dogwifcoin"),
    ("IMX", "Immutable", "IMXUSDT", "immutable-x"),
]

UA = {"User-Agent": "PCControlDeck asset tool"}


def http_get(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def fetch_image_urls():
    ids = ",".join(c[3] for c in COINS)
    url = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&per_page=250&ids=" + ids
    data = json.loads(http_get(url))
    return {d["id"]: d["image"] for d in data}


def load_logo(cg_id, urls):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, cg_id + ".img")
    if not os.path.exists(path):
        if urls is None:
            raise SystemExit("logo for %s not cached and no network list" % cg_id)
        print("  download", cg_id)
        with open(path, "wb") as fh:
            fh.write(http_get(urls[cg_id]))
    return Image.open(io.BytesIO(open(path, "rb").read())).convert("RGBA")


def circle_mask(size, ss=8):
    big = Image.new("L", (size * ss, size * ss), 0)
    ImageDraw.Draw(big).ellipse((0, 0, size * ss - 1, size * ss - 1), fill=255)
    return big.resize((size, size), Image.LANCZOS)


def square_logo(img):
    w, h = img.size
    s = max(w, h)
    canvas = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    canvas.paste(img, ((s - w) // 2, (s - h) // 2))
    return canvas


def needs_circle(img):
    """JPEG-like logos (opaque corners) get clipped to a circle."""
    a = img.getchannel("A")
    w, h = img.size
    corners = [a.getpixel((1, 1)), a.getpixel((w - 2, 1)), a.getpixel((1, h - 2)), a.getpixel((w - 2, h - 2))]
    return min(corners) > 200


def is_dark_logo(img):
    """True when the logo has no bright areas -> invisible on the dark UI."""
    small = img.resize((48, 48), Image.LANCZOS)
    lum = sorted(0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b, a in small.getdata() if a > 128)
    if not lum:
        return False
    return lum[int(len(lum) * 0.9)] < 70


def on_light_disc(img):
    s = img.size[0]
    disc = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(disc).ellipse((0, 0, s - 1, s - 1), fill=(242, 244, 247, 255))
    inset = img.resize((int(s * 0.78), int(s * 0.78)), Image.LANCZOS)
    off = (s - inset.size[0]) // 2
    disc.alpha_composite(inset, (off, off))
    return disc


def brand_color(img):
    small = img.resize((48, 48), Image.LANCZOS)
    best = {}
    for r, g, b, a in small.getdata():
        if a < 200:
            continue
        h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
        if s < 0.35 or l < 0.18 or l > 0.85:
            continue
        key = int(h * 24)
        e = best.setdefault(key, [0, 0, 0, 0])
        e[0] += 1
        e[1] += r
        e[2] += g
        e[3] += b
    if not best:
        return 0x8A94A6
    n, r, g, b = max(best.values(), key=lambda e: e[0])
    r, g, b = r / n, g / n, b / n
    # normalise lightness so accents are visible on a dark UI
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    l = min(max(l, 0.50), 0.66)
    s = max(s, 0.55)
    r, g, b = colorsys.hls_to_rgb(h, l, s)
    return (int(r * 255) << 16) | (int(g * 255) << 8) | int(b * 255)


def to_rgb565a8(img, big_endian):
    """LVGL 8 RGB565A8: colour plane (in the deck's byte order) then alpha plane."""
    rgb = bytearray()
    alpha = bytearray()
    for r, g, b, a in img.getdata():
        if a == 0:
            r = g = b = 0
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb += bytes((v >> 8, v & 0xFF)) if big_endian else bytes((v & 0xFF, v >> 8))
        alpha.append(a)
    return bytes(rgb + alpha)


def c_bytes(data, per_line=24):
    out = []
    for i in range(0, len(data), per_line):
        out.append("    " + ",".join("0x%02x" % b for b in data[i : i + per_line]) + ",")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--deck", default="all", help="deck key (src/decks/<KEY>) or all")
    for deck in decks.selected(ap.parse_args().deck):
        build(deck)


def build(deck):
    out_c = os.path.join(decks.deck_dir(deck), "crypto", "coin_catalog.c")
    sizes, big_endian = decks.coin_logo_sizes(deck), decks.big_endian(deck)
    urls = None  # every logo cached: no network needed
    if not all(os.path.exists(os.path.join(CACHE, cg + ".img")) for _, _, _, cg in COINS):
        try:
            urls = fetch_image_urls()
        except Exception as e:  # offline: rely on the cache
            print("warning: could not query CoinGecko (%s), using cache" % e)

    c = [
        "/* Generated by firmware/tools/gen_coins.py - do not edit.",
        " * Coin logos are trademarks of their respective projects. */",
        '#include "crypto/coin_catalog.h"',
        "",
    ]
    rows = []
    for sym, name, pair, cg in COINS:
        img = square_logo(load_logo(cg, urls))
        circ = needs_circle(img)
        color = brand_color(img)
        if not circ and is_dark_logo(img):
            img = on_light_disc(img)
        ident = sym.lower()
        for key, size in sizes.items():
            im = img.resize((size, size), Image.LANCZOS)
            if circ:
                m = circle_mask(size)
                a = Image.composite(im.getchannel("A"), Image.new("L", (size, size), 0), m)
                im.putalpha(a)
            data = to_rgb565a8(im, big_endian)
            c.append("static const uint8_t logo_%s_%s_map[] = {\n%s\n};" % (ident, key, c_bytes(data)))
            c.append("static const lv_img_dsc_t logo_%s_%s = {" % (ident, key))
            c.append("    .header = {.cf = LV_IMG_CF_RGB565A8, .always_zero = 0, .reserved = 0, .w = %d, .h = %d}," % (size, size))
            c.append("    .data_size = %d," % len(data))
            c.append("    .data = logo_%s_%s_map," % (ident, key))
            c.append("};\n")
        rows.append(
            '    {"%s", "%s", "%s", "%s", 0x%06X, &logo_%s_s, &logo_%s_m},' % (sym, name, pair, cg, color, ident, ident)
        )
        print("%-8s color #%06X circle=%s" % (sym, color, circ))

    c.append("const coin_info_t COIN_CATALOG[] = {")
    c += rows
    c.append("};")
    c.append("const int COIN_CATALOG_COUNT = sizeof(COIN_CATALOG) / sizeof(COIN_CATALOG[0]);")
    os.makedirs(os.path.dirname(out_c), exist_ok=True)
    with open(out_c, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(c) + "\n")

    h = """/* Generated by firmware/tools/gen_coins.py - do not edit */
#pragma once
#include <lvgl.h>
#include <stdint.h>

#include "deck_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *sym;              /* ticker, e.g. "BTC" (also the id stored in the settings) */
    const char *name;             /* display name */
    const char *pair;             /* Binance spot pair against USDT */
    const char *cg_id;            /* CoinGecko id (market cap / rank / ATH) */
    uint32_t color;               /* brand accent colour 0xRRGGBB */
    const lv_img_dsc_t *logo_s;   /* PCD_COIN_LOGO_S px */
    const lv_img_dsc_t *logo_m;   /* PCD_COIN_LOGO_M px */
} coin_info_t;

extern const coin_info_t COIN_CATALOG[];
extern const int COIN_CATALOG_COUNT;

#define COIN_LOGO_S PCD_COIN_LOGO_S
#define COIN_LOGO_M PCD_COIN_LOGO_M

#ifdef __cplusplus
}
#endif
"""
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(h)
    print("done: %d coins -> %s" % (len(COINS), os.path.relpath(out_c, FW)))


if __name__ == "__main__":
    sys.exit(main())
