"""Desktop UI simulator: builds one deck's UI (LVGL 8.4 + its screens + the shared core it uses) for
Windows with zig, renders every page with sample data into a framebuffer of the deck's resolution
and saves PNG screenshots.

No hardware, no PC app: hardware, network and storage calls are stubbed (sim/stubs.cpp, every deck);
the scenes are in sim/decks/<DECK>/main.cpp.

Requirements (any Python 3.10+):  pip install ziglang==0.13.0 pillow
LVGL and JPEGDEC come from PlatformIO: run `pio pkg install -e <DECK>` (or build it) once so
.pio/libdeps/<DECK>/{lvgl,JPEGDEC} exist.

Usage (from the repository root):
    python firmware/sim/build.py JC1060P470C        # Spanish UI -> firmware/sim/out/JC1060P470C/es/*.png
    python firmware/sim/build.py JC3248W535 en      # English UI -> firmware/sim/out/JC3248W535/en/*.png
    python firmware/sim/build.py all                # every deck, Spanish and English
Set PCD_SIM_TIME=<unix seconds> to pin the clock (reproducible screenshots), PCD_ZIG_PYTHON to
another interpreter if ziglang is installed elsewhere.
"""

import concurrent.futures as cf
import glob
import os
import re
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
ZIG = [os.environ.get("PCD_ZIG_PYTHON", sys.executable), "-m", "ziglang"]


def decks():
    return sorted(d for d in os.listdir(os.path.join(HERE, "decks")) if os.path.isdir(os.path.join(HERE, "decks", d)))


def define(path, name):
    m = re.search(r"^#define\s+%s\s+(\d+)" % name, open(path, encoding="utf-8").read(), re.M)
    if not m:
        sys.exit("%s: no #define %s" % (path, name))
    return int(m.group(1))


def build(deck, lang):
    src = os.path.join(FW, "src")
    core, own = os.path.join(src, "core"), os.path.join(src, "decks", deck)
    inc = os.path.join(own, "include")
    lv_conf = os.path.join(inc, "lv_conf.h")
    w, h = define(os.path.join(inc, "board_pins.h"), "PCD_LCD_H_RES"), define(os.path.join(inc, "board_pins.h"), "PCD_LCD_V_RES")
    swap = define(lv_conf, "LV_COLOR_16_SWAP")
    lvgl = os.path.join(FW, ".pio", "libdeps", deck, "lvgl")
    jpegdec = os.path.join(FW, ".pio", "libdeps", deck, "JPEGDEC", "src")
    for lib in (lvgl, jpegdec):
        if not os.path.isdir(lib):
            sys.exit("%s not found: run `pio pkg install -e %s` first" % (lib, deck))
    obj_dir = os.path.join(HERE, "obj", deck)
    out = os.path.join(HERE, "out", deck, lang)
    os.makedirs(obj_dir, exist_ok=True)
    os.makedirs(out, exist_ok=True)

    inc_flags = ["-I", os.path.join(HERE, "stubs"), "-I", inc, "-I", os.path.join(FW, "include"), "-I", core, "-I", own,
                 "-I", lvgl, "-I", jpegdec]
    # NO_SIMD: JPEGDEC's x86-64 SSE path mis-decodes these images (the device has no such path).
    defs = ["-DPCD_APP_DECK=1", "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_LVGL_H_INCLUDE_SIMPLE", "-DNO_SIMD"]
    common = ["-target", "x86_64-windows-gnu", "-O1", "-g0", "-w", "-include", os.path.join(HERE, "stubs", "compat.h")]
    common += defs + inc_flags

    c_src = glob.glob(os.path.join(lvgl, "src", "**", "*.c"), recursive=True)
    c_src += glob.glob(os.path.join(own, "ui", "fonts", "*.c"))
    c_src += glob.glob(os.path.join(own, "crypto", "*.c")) + glob.glob(os.path.join(own, "assets", "*.c"))
    cpp_src = []
    for root in (core, own):
        cpp_src += glob.glob(os.path.join(root, "ui", "*.cpp"))
        cpp_src += glob.glob(os.path.join(root, "ui", "screens", "*.cpp"))
        cpp_src += glob.glob(os.path.join(root, "ui", "theme", "*.cpp"))
    for rel in ("crypto/watchlist.cpp", "crypto/crypto_store.cpp", "app/state_store.cpp", "alerts/thermal_alerts.cpp"):
        hit = [p for p in (os.path.join(core, rel), os.path.join(own, rel)) if os.path.exists(p)]
        if not hit:
            sys.exit("missing %s" % rel)
        cpp_src += hit
    cpp_src += [os.path.join(jpegdec, "JPEGDEC.cpp"), os.path.join(HERE, "stubs.cpp"),
                os.path.join(HERE, "decks", deck, "main.cpp")]

    def obj_name(path):
        rel = os.path.relpath(path, FW)
        return os.path.join(obj_dir, rel.replace(os.sep, "_").replace(".", "_") + ".o")

    def compile_one(path):
        o = obj_name(path)
        # C sources (LVGL, fonts, logos, images) are cached until they or lv_conf.h change; the C++ UI
        # is small and rebuilt every time.
        newest = max(os.path.getmtime(path), os.path.getmtime(lv_conf))
        if path.endswith(".c") and os.path.exists(o) and os.path.getmtime(o) > newest:
            return o, None
        if path.endswith(".cpp"):
            cmd = ZIG + ["c++", "-std=gnu++17", "-fno-exceptions", "-fno-rtti"] + common + ["-c", path, "-o", o]
        else:
            cmd = ZIG + ["cc", "-std=gnu11"] + common + ["-c", path, "-o", o]
        r = subprocess.run(cmd, capture_output=True, text=True)
        return o, (r.stderr if r.returncode else None)

    objs, errors = [], []
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        for o, err in ex.map(compile_one, c_src + cpp_src):
            objs.append(o)
            if err:
                errors.append(err)
    if errors:
        for e in errors[:6]:
            print(e[:3000])
        sys.exit("%s: compile errors" % deck)
    exe = os.path.join(obj_dir, "sim.exe")
    rsp = os.path.join(obj_dir, "objs.rsp")  # the object list is longer than a Windows command line
    with open(rsp, "w") as fh:
        fh.write(" ".join('"%s"' % o.replace(os.sep, "/") for o in objs))
    r = subprocess.run(ZIG + ["c++", "-target", "x86_64-windows-gnu", "@" + rsp, "-o", exe], capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stderr[:6000])
    for f in glob.glob(os.path.join(out, "*")):
        os.remove(f)
    r = subprocess.run([exe, out, lang], capture_output=True, text=True, timeout=300)
    if r.returncode:
        sys.exit("simulator failed:\n" + r.stdout[-2000:] + r.stderr[-2000:])
    checks = [l for l in r.stdout.splitlines() if not l.startswith("shot ")]
    if checks:
        print("\n".join(checks))
    if any(l.startswith("FAIL") for l in checks):
        sys.exit("%s: simulator checks failed" % deck)
    for raw in sorted(glob.glob(os.path.join(out, "*.rgb"))):
        data = open(raw, "rb").read()
        px = []
        for i in range(0, len(data), 2):
            # RGB565 as the panel takes it: big-endian with LV_COLOR_16_SWAP=1, else little-endian.
            v = (data[i] << 8) | data[i + 1] if swap else data[i] | (data[i + 1] << 8)
            r_, g_, b_ = (v >> 11) & 31, (v >> 5) & 63, v & 31
            px.append(((r_ << 3) | (r_ >> 2), (g_ << 2) | (g_ >> 4), (b_ << 3) | (b_ >> 2)))
        img = Image.new("RGB", (w, h))
        img.putdata(px)
        img.save(raw[:-4] + ".png")
        os.remove(raw)
    print("%s: %d screenshots in %s" % (deck, len(glob.glob(os.path.join(out, "*.png"))), os.path.relpath(out, FW)))


def main():
    args = sys.argv[1:]
    known = decks()
    if not args or args[0] not in known + ["all"]:
        sys.exit("usage: build.py <%s|all> [es|en]" % "|".join(known))
    if args[0] == "all":
        for d in known:
            for lang in ("es", "en"):
                build(d, lang)
    else:
        build(args[0], args[1] if len(args) > 1 else "es")


if __name__ == "__main__":
    main()
