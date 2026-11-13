#!/usr/bin/env python3
"""
Generate the Today weather source images (weather_backgrounds/*.jpg) from PROMPTS.md with the
OpenAI Images API. Only needed to redo or add a picture: the images are in the repository.

Usage (from the repository root):
    python firmware/tools/gen_weather_images.py                   # every missing image
    python firmware/tools/gen_weather_images.py rain_day snow_night   # only these (delete them first to redo)
    python firmware/tools/gen_weather_images.py --env-file path/to/.env

The key comes from OPENAI_API_KEY (environment, or a KEY=value line in --env-file); it is never
printed. Existing images are skipped. Then run gen_weather_bg.py to embed them in the firmware.
"""
import argparse
import base64
import concurrent.futures as cf
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

DIR = Path(__file__).resolve().parent / "weather_backgrounds"
MODELS = ["gpt-image-2", "gpt-image-1"]


def api_key(env_file: str | None) -> str:
    if env_file:
        for line in Path(env_file).read_text(encoding="utf-8").splitlines():
            m = re.match(r"\s*OPENAI_API_KEY\s*=\s*(.+?)\s*$", line)
            if m:
                return m.group(1).strip('"').strip("'")
    key = os.environ.get("OPENAI_API_KEY", "")
    if not key:
        raise SystemExit("OPENAI_API_KEY is not set (environment or --env-file)")
    return key


def prompts() -> list[tuple[str, str]]:
    md = (DIR / "PROMPTS.md").read_text(encoding="utf-8")
    base = re.search(r"\*\*Base \(same for all 18\):\*\*\s*\n\n((?:> .*\n)+)", md).group(1)
    base = " ".join(line[2:].strip() for line in base.splitlines())
    rows = re.findall(r"^\| `([a-z_]+)\.jpg` \| [^|]+ \| (.+?) \|$", md, re.M)
    if len(rows) != 18:
        raise SystemExit(f"expected 18 rows in PROMPTS.md, got {len(rows)}")
    return [(name, f"{base} {extra}") for name, extra in rows]


def generate(key: str, model: str, prompt: str) -> bytes:
    body = {"model": model, "prompt": prompt, "size": "1536x1024", "quality": "high", "n": 1,
            "output_format": "jpeg", "output_compression": 95}
    req = urllib.request.Request(
        "https://api.openai.com/v1/images/generations",
        data=json.dumps(body).encode(),
        headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=300) as r:
        data = json.loads(r.read())
    return base64.b64decode(data["data"][0]["b64_json"])


def one(key: str, name: str, prompt: str) -> str:
    out = DIR / f"{name}.jpg"
    if out.exists() and out.stat().st_size > 10000:
        return f"{name}: exists"
    last = ""
    for model in MODELS:
        for attempt in range(3):
            t0 = time.time()
            try:
                img = generate(key, model, prompt)
                out.write_bytes(img)
                return f"{name}: ok ({model}, {len(img) // 1024} KB, {time.time() - t0:.0f} s)"
            except urllib.error.HTTPError as e:
                msg = e.read().decode("utf-8", "replace")[:300]
                last = f"HTTP {e.code} {msg}"
                if e.code == 404 or (e.code in (400, 403) and "model" in msg.lower()):
                    break  # model not available: try the next one
                if e.code == 429 or e.code >= 500:
                    time.sleep(10 * (attempt + 1))
                    continue
                return f"{name}: FAILED {last}"
            except Exception as e:  # noqa: BLE001
                last = repr(e)
                time.sleep(5)
    return f"{name}: FAILED {last}"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="only these images (file name without .jpg)")
    ap.add_argument("--env-file", help="read OPENAI_API_KEY from this .env file")
    args = ap.parse_args()
    key = api_key(args.env_file)
    jobs = prompts()
    if args.names:
        unknown = set(args.names) - {n for n, _ in jobs}
        if unknown:
            raise SystemExit(f"unknown image(s): {', '.join(sorted(unknown))}")
        jobs = [j for j in jobs if j[0] in args.names]
    print(f"{len(jobs)} image(s) -> {DIR}", flush=True)
    failed = False
    with cf.ThreadPoolExecutor(max_workers=3) as ex:
        for res in ex.map(lambda j: one(key, *j), jobs):
            print(res, flush=True)
            failed |= "FAILED" in res
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
