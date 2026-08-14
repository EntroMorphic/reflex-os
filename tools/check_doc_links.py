#!/usr/bin/env python3
"""Verify every relative link in every tracked Markdown file resolves.

    python3 tools/check_doc_links.py     # or: make doc-links

This replaced a grep pipeline in CI that checked README.md only. Two things
were wrong with the narrower version: links inside `docs/` were never checked
at all, and it resolved every link against the repository root, when a link in
`docs/foo.md` is relative to `docs/`. Extending the old approach would have
reported false breakage on the majority of the tree.

Exits non-zero if any link is broken, listing each one.
"""

import os
import re
import subprocess
import sys

LINK = re.compile(r"\[([^\]]*)\]\(([^)]+)\)")
SKIP_PREFIXES = ("http://", "https://", "mailto:", "#")


def tracked_markdown():
    out = subprocess.check_output(["git", "ls-files", "*.md"]).decode()
    return [f for f in out.split() if f]


def main():
    broken = []
    checked = 0
    files = tracked_markdown()

    for path in files:
        base = os.path.dirname(path)
        try:
            text = open(path, encoding="utf-8").read()
        except OSError as e:
            broken.append(f"{path}: unreadable ({e})")
            continue

        for match in LINK.finditer(text):
            target = match.group(2).split("#")[0].strip()
            if not target or target.startswith(SKIP_PREFIXES):
                continue
            checked += 1
            # Relative to the containing file, not the repo root.
            resolved = os.path.normpath(os.path.join(base, target))
            if not os.path.exists(resolved):
                broken.append(f"{path} -> {target}")

    for b in broken:
        print(f"BROKEN LINK: {b}")
    print(f"{checked} relative links checked across {len(files)} files, "
          f"{len(broken)} broken")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
