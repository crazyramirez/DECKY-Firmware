# Changelog

Firmware for every DECKY deck (Guition JC1060P470C, JC4827W543, JC3248W535), one version for all
of them. Up to 1.0 each deck had its own repository and versions: their history is in
[JC1060P470C](decks/JC1060P470C/changelog-0.x.md) (up to 0.13.0),
[JC4827W543](decks/JC4827W543/changelog-0.x.md) (up to 0.13.0) and
[JC3248W535](decks/JC3248W535/changelog-0.x.md) (up to 0.14.0).

## firmware 1.0.0 — DECKY: one firmware for every deck

- **New name: DECKY.** The deck says *DECKY* where it said *PC Control Deck* (the JC3248W535's top
  bar, the name the deck announces to the PC: *DECKY 1A2B*) and the pairing steps name the desktop
  app by its new name, **DECKY Agent**.
- **One firmware, one version, one release** for the three decks, built from one source tree
  ([ADR 0008](adr/0008-one-firmware-repository.md)). The release carries every deck's images,
  named after the deck: `pcdeck-JC1060P470C-1.0.0-merged.bin`, `pcdeck-JC1060P470C_r3-…`,
  `pcdeck-JC4827W543-…`, `pcdeck-JC3248W535-…` (DECKY Agent 1.0 picks the right one by itself).
- Everything the decks had in common now runs the same code. On screen nothing else changes; the
  differences that remain are each panel's layout and each board's hardware.
- **USB console:** `status` and `saver` answer alike on every deck (the screensaver command now
  prints its state on the S3 decks too); `help` lists the deck's own commands (`dbg` on the
  JC1060P470C, `sleep` on the S3 decks, `setup` on the JC3248W535); a screenshot (`shot`) stops
  cleanly when the PC stops reading on every deck.
- JC3248W535: the setup wizard no longer opens in demo mode (developer aid), as on the other decks.
