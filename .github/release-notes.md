Firmware **{{version}}** for every DECKY deck. The easiest way to install or update it is the
**DECKY Agent** app on the computer ([DECKY-Agent releases](https://github.com/crazyramirez/DECKY-Agent/releases)):
*Update deck firmware* recognises the deck on USB, picks the right file and flashes it. The deck
needs the app anyway for the PC pages.

To flash by hand from the browser, take the files of **your** deck:

| Deck | Chip | First install / recovery (`0x0`) | Update (`0x10000`) |
|---|---|---|---|
| Guition **JC1060P470C** · 7" 1024×600 | ESP32-P4 **v1.x** (every board shipped so far) | `pcdeck-JC1060P470C-{{version}}-merged.bin` | `pcdeck-JC1060P470C-{{version}}.bin` |
| Guition **JC1060P470C** · 7" 1024×600 | ESP32-P4 **v3.x** | `pcdeck-JC1060P470C_r3-{{version}}-merged.bin` | `pcdeck-JC1060P470C_r3-{{version}}.bin` |
| Guition **JC4827W543** · 4.3" 480×272 | ESP32-S3 | `pcdeck-JC4827W543-{{version}}-merged.bin` | `pcdeck-JC4827W543-{{version}}.bin` |
| Guition **JC3248W535** · 3.5" 320×480 | ESP32-S3 | `pcdeck-JC3248W535-{{version}}-merged.bin` | `pcdeck-JC3248W535-{{version}}.bin` |

- The **merged** image (bootloader + partition table + app) goes at **`0x0`**: it erases Wi-Fi,
  pairing and settings, and the setup assistant opens.
- The app-only image goes at **`0x10000`** on a deck that already runs DECKY (or PC Control Deck): everything
  is kept.
- Download the `.bin` from **Assets** below (not *Source code*). The JC1060P470C's chip revision:
  ESP Tool prints `Chip is ESP32-P4 (revision v…)` when it connects.

## Flash from the browser with ESP Tool

1. Open **[ESP Tool by Espressif](https://espressif.github.io/esptool-js/)** in **Chrome or Edge**
   on a computer (it needs Web Serial: Firefox and Safari don't have it). Connect the deck by USB-C.
2. Leave *Baudrate* at `115200`, click **Connect** and pick the deck's port (*USB JTAG/serial debug
   unit*; on Windows a COM port). If it doesn't connect: hold **BOOT**, press and release **RST**,
   release **BOOT** and click **Connect** again.
3. Under *Program* keep **one row** (delete any other):
   - *Flash Address*: **`0x0`** for the `-merged.bin`, **`0x10000`** for the app-only `.bin`. The
     tool pre-fills `0x1000` (the address of the original ESP32): change it, or the deck reboots in
     a loop.
   - *File*: the `.bin` you downloaded.
   - *Flash Mode / Frequency / Size*: leave `keep`.
4. Click **Program** and wait until the log says it is done.
5. Click **Disconnect** and press **RST** on the deck (or unplug the USB cable and plug it back in).

After a first install the **setup assistant** walks through language, Wi-Fi, pairing (the 6-digit
code from *Pair device* in the PC app) and the city for the weather.

**Command line** (esptool): `esptool.py --chip esp32s3 write_flash 0x0 pcdeck-JC4827W543-{{version}}-merged.bin`
(`--chip esp32p4` for the JC1060P470C).

| Problem | Fix |
|---|---|
| *Serial data stream stopped* | Use Espressif's ESP Tool above; close other tabs or programs using the port; enter download mode with BOOT + RST before Connect |
| The deck reboots in a loop | The merged image went to `0x1000`: flash it again at `0x0` |
| Black screen or a crash right after flashing | The file of another deck: flash the one with your deck's name |
| `Image requires chip rev …` on the JC1060P470C | Wrong silicon image: `JC1060P470C` for v1.x chips, `JC1060P470C_r3` for v3.x |
| Serial port repeats `invalid header: 0x04034b50` | A `.zip` was flashed: flash the `.bin` |

## Changes

{{changes}}
