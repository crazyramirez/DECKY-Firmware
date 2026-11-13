"""Grab the deck's current screen over USB serial and save it as PNG.

Usage:  python firmware/tools/screenshot.py COMx [out.png]
Needs pyserial (PlatformIO's penv has it). The deck answers the console command `shot` with
base64 RGB565 between "SHOT BEGIN w h <format>" and "SHOT END"; <format> is rgb565le on the
JC1060P470C (MIPI-DSI frame buffer), rgb565be on the QSPI decks.
"""

from __future__ import annotations

import base64
import struct
import sys
import time
import zlib

import serial


def png_bytes(width: int, height: int, rgb: bytes) -> bytes:
    raw = b"".join(b"\x00" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")


def rgb565_to_rgb888(data: bytes, little: bool) -> bytes:
    out = bytearray(len(data) // 2 * 3)
    o = 0
    for i in range(0, len(data), 2):
        v = (data[i + 1] << 8) | data[i] if little else (data[i] << 8) | data[i + 1]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        out[o] = (r << 3) | (r >> 2)
        out[o + 1] = (g << 2) | (g >> 4)
        out[o + 2] = (b << 3) | (b >> 2)
        o += 3
    return bytes(out)


def grab(port: str, timeout_s: float = 60.0) -> tuple[int, int, bytes, bool]:
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = False  # never toggle DTR/RTS: on the USB-Serial/JTAG port that resets the chip
    s.rts = False
    with s:
        time.sleep(0.3)
        s.reset_input_buffer()
        s.write(b"shot\n")
        deadline = time.time() + timeout_s
        buf = bytearray()
        while time.time() < deadline and b"SHOT END" not in buf:
            buf += s.read(65536)  # bulk reads keep up with USB full speed
        if b"SHOT END" not in buf:
            raise SystemExit("timeout waiting for SHOT END")
    w = h = 0
    little = False
    b64: list[bytes] = []
    started = False
    for line in bytes(buf).splitlines():
        line = line.strip()
        if b"SHOT BEGIN" in line:  # may share a line with a log fragment
            parts = line[line.index(b"SHOT BEGIN") :].split()
            w, h = int(parts[2]), int(parts[3])
            little = len(parts) > 4 and parts[4] == b"rgb565le"
            started = True
        elif line == b"SHOT END":
            break
        elif started and line.startswith(b"#"):  # frame data; anything else is interleaved logging
            b64.append(line[1:])
    if not started:
        raise SystemExit(f"no SHOT BEGIN header; first bytes: {bytes(buf[:160])!r}")
    data = base64.b64decode(b"".join(b64))
    if len(data) != w * h * 2:
        raise SystemExit(f"short frame: {len(data)} of {w * h * 2} bytes")
    return w, h, data, little


def main() -> int:
    if len(sys.argv) < 2:
        sys.exit("usage: screenshot.py COMx [out.png]")
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "deck_screenshot.png"
    w, h, data, little = grab(port)
    with open(out, "wb") as f:
        f.write(png_bytes(w, h, rgb565_to_rgb888(data, little)))
    print(f"saved {out} ({w}x{h})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
