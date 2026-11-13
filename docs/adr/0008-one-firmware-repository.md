# ADR 0008 — One firmware repository for every deck

Status: accepted (firmware 1.0.0).

## Context

Until firmware 0.14 each deck had its own repository (PC-Control-Deck-ESP32-JC1060P470C,
-JC4827W543, -JC3248W535), started as copies of one another. Every feature and fix was ported by
hand two more times, with its own version, CHANGELOG, CI run and release. Measured before the move:
of the 122 source files the three repositories shared, only 67 were still identical; the
application logic had drifted (the same function named differently on one deck, a console status
that existed on one deck only, a fix in one network task and not in the others), versions no longer
matched (0.13.0 / 0.13.0 / 0.14.0 for the same feature), and the same 4 MB of source images lived
in three places. More decks are planned. All three already used the same platform (pioarduino
55.03.35), LVGL 8.4.0 and libraries.

## Decision

- One repository, **DECKY-Firmware**: `firmware/src/core` is built for every deck,
  `firmware/src/decks/<DECK>` only for its own environment. A path lives in exactly one of the two.
  Deck differences that the shared code must know are capabilities in `deck_config.h`
  (`PCD_WIFI_ESP_HOSTED`, `PCD_HAS_DEEP_SLEEP`, …), not deck names in `#if`s; board-only console
  output and commands go through `app/deck_hooks.h`; per-deck wording through `ui::deck_tr()`.
- The screens stay per deck: different panels get different layouts.
- One version for all decks (1.0.0 on), one CHANGELOG, one CI run (a build matrix), one release
  with every deck's images named after its environment (`pcdeck-<DECK>-<version>[-merged].bin`).
- The generators and the simulator are single tools with `--deck`; screen size and byte order come
  from the deck's own headers.
- The desktop app keeps its own repository (DECKY-Agent): its installed copies update themselves
  from its releases, and it is a different toolchain (Python, four operating systems).

## How it was verified

The move was done in steps, each checked before the next: the desktop simulator (clock pinned
with `PCD_SIM_TIME`) rendered all 152 scenes of the three decks in Spanish and English pixel for
pixel identical to the separate repositories; the four environments built after every step; the
merged network task was checked to equal each deck's original once its `#if` branch is chosen; the
generators reproduced the committed wallpapers, weather backgrounds, coin logos and fonts byte for
byte.

## Consequences

- A change to shared code reaches every deck at once; a deck-specific change stays in its folder.
- Adding a deck: see [ARCHITECTURE.md](../ARCHITECTURE.md#adding-a-deck).
- The old repositories' history ends at their last release; their CHANGELOGs are kept in
  `docs/decks/<DECK>/changelog-0.x.md`. DECKY Agent 1.0 looks for firmware here; older app versions
  update themselves to it first.
