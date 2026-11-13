"""Collect PlatformIO firmware images and build a merged, flash-at-0x0 image per environment.

Usage (after ``pio run -e <env>``):
    python .github/scripts/merge_firmware.py --project firmware --out dist JC1060P470C JC4827W543

For each environment the bootloader / partition table / boot_app0 paths and offsets are read from
``pio project metadata`` ("extra.flash_images"); the application goes to 0x10000. The chip and the
flash size come from the environment's board (``board_build.mcu`` / ``board_upload.flash_size`` or
the board JSON). Output:
    dist/pcdeck-<env>-<version>.bin          application only (flashing at 0x10000 keeps the settings)
    dist/pcdeck-<env>-<version>-merged.bin   bootloader (0x0 on the ESP32-S3, 0x2000 on the ESP32-P4)
                                             + partitions 0x8000 + boot_app0 0xe000 + app 0x10000,
                                             padded from 0x0
Flash a merged image with:  esptool.py --chip <chip> write_flash 0x0 pcdeck-<env>-<version>-merged.bin
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

APP_OFFSET = "0x10000"
# Same flash parameters PlatformIO's upload uses for these boards (QIO flash -> esptool "dio",
# the 2nd-stage bootloader switches to QIO itself), 80 MHz.
FLASH_ARGS = ["--flash_mode", "dio", "--flash_freq", "80m"]
VERSION_H = Path("src") / "core" / "app" / "version.h"


def firmware_version(project: Path) -> str:
    m = re.search(r'#define PCD_FW_VERSION "([^"]+)"', (project / VERSION_H).read_text(encoding="utf-8"))
    return m.group(1) if m else "dev"


def pio_json(*args: str) -> object:
    out = subprocess.run(["pio", *args, "--json-output"], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def env_options(project: Path, env: str) -> dict[str, str]:
    """The environment's options with `extends` and the common [env] section applied."""
    sections = dict((name, dict(opts)) for name, opts in pio_json("project", "config", "-d", str(project)))
    key = f"env:{env}"
    if key not in sections:
        raise SystemExit(f"{env}: no such environment")
    return {k: (" ".join(v) if isinstance(v, list) else str(v)) for k, v in sections[key].items()}


def board_spec(project: Path, board: str) -> dict:
    """The board JSON: the project's boards/ first, then the installed espressif32 platform."""
    for d in [project / "boards", *sorted(Path.home().glob(".platformio/platforms/espressif32*/boards"))]:
        p = d / f"{board}.json"
        if p.is_file():
            return json.loads(p.read_text(encoding="utf-8"))
    raise SystemExit(f"board {board}: JSON not found")


def chip_and_size(project: Path, env: str) -> tuple[str, str]:
    opts = env_options(project, env)
    spec = board_spec(project, opts["board"])
    mcu = opts.get("board_build.mcu") or spec["build"]["mcu"]  # "esp32s3", "esp32p4"
    size = opts.get("board_upload.flash_size") or spec["upload"]["flash_size"]
    return mcu.lower(), size.strip()


def metadata(project: Path, env: str) -> dict:
    return pio_json("project", "metadata", "-d", str(project), "-e", env)[env]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", type=Path, default=Path("firmware"))
    ap.add_argument("--out", type=Path, default=Path("dist"))
    ap.add_argument("envs", nargs="+")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    version = firmware_version(args.project)
    for env in args.envs:
        chip, size = chip_and_size(args.project, env)
        meta = metadata(args.project, env)
        app = Path(meta["prog_path"]).with_name("firmware.bin")
        images = [(img["offset"], Path(img["path"])) for img in meta.get("extra", {}).get("flash_images", [])]
        if not app.exists() or not images:
            print(f"{env}: missing firmware.bin or flash images in the metadata", file=sys.stderr)
            return 1
        for _, p in images:
            if not p.exists():
                print(f"{env}: {p} not found", file=sys.stderr)
                return 1
        base = args.out / f"pcdeck-{env}-{version}"
        shutil.copyfile(app, f"{base}.bin")
        cmd = ["esptool.py", "--chip", chip, "merge_bin", "-o", f"{base}-merged.bin", *FLASH_ARGS, "--flash_size", size]
        for offset, path in images + [(APP_OFFSET, app)]:
            cmd += [offset, str(path)]
        print(" ".join(cmd), flush=True)
        subprocess.run(cmd, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
