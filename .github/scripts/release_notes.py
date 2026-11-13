"""Write the GitHub Release body of a firmware tag: flashing guide + the CHANGELOG section.

Usage (release job, repository root):
    python .github/scripts/release_notes.py --version 0.9.0 --repo owner/name --tag v0.9.0 > notes.md

The template (.github/release-notes.md) gets {{version}}, {{repo}}, {{tag}} and {{changes}}; the
changes are the CHANGELOG heading that names "firmware <version>" and its body. Links inside the
changelog are relative to docs/: they become absolute so they work on the release page.
"""

from __future__ import annotations

import argparse
import posixpath
import re
import sys
from pathlib import Path


def changelog_section(text: str, version: str) -> str | None:
    heading = re.compile(rf"^## .*\bfirmware {re.escape(version)}(?![\w.])")
    out: list[str] = []
    inside = False
    for line in text.splitlines():
        if line.startswith("## "):
            if inside:
                break
            if heading.match(line):
                inside = True
                out.append("### " + line[3:])
                continue
        elif inside:
            out.append(line)
    return "\n".join(out).strip() if inside else None


def absolute_links(md: str, repo: str, tag: str, base: str = "docs") -> str:
    def fix(m: re.Match[str]) -> str:
        target = m.group(2)
        if re.match(r"^(?:[a-z][a-z0-9+.-]*:|#|/)", target, re.I):
            return m.group(0)
        path = posixpath.normpath(posixpath.join(base, target))
        return f"{m.group(1)}(https://github.com/{repo}/blob/{tag}/{path})"

    return re.sub(r"(\]\s*)\(([^)\s]+)\)", fix, md)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--changelog", type=Path, default=Path("docs/CHANGELOG.md"))
    ap.add_argument("--template", type=Path, default=Path(".github/release-notes.md"))
    args = ap.parse_args()

    changes = changelog_section(args.changelog.read_text(encoding="utf-8"), args.version)
    if changes is None:
        print(f"{args.changelog} has no '## ... firmware {args.version}' section", file=sys.stderr)
        return 1
    body = args.template.read_text(encoding="utf-8")
    for key, value in {"version": args.version, "repo": args.repo, "tag": args.tag,
                       "changes": absolute_links(changes, args.repo, args.tag)}.items():
        body = body.replace("{{" + key + "}}", value)
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stdout.write(body)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
