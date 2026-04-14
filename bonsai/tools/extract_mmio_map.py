#!/usr/bin/env python3

from pathlib import Path
import re
import sys


ROOT = Path("/Users/aaronjosserand-austin/Projects/esp-idf/components/soc/esp32c6")
REG_BASE_FILES = [
    ROOT / "register/soc/reg_base.h",
    ROOT / "include/modem/reg_base.h",
]
REG_HEADERS = ROOT / "register/soc"


BASE_PATTERN = re.compile(r"#define\s+([A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+)\b")
REG_PATTERN = re.compile(r"#define\s+([A-Z0-9_]+_REG(?:\([^)]*\))?)\s*\(([^\n]+)\)")


def load_bases():
    bases = {}
    for path in REG_BASE_FILES:
        for line in path.read_text().splitlines():
            match = BASE_PATTERN.match(line.strip())
            if match and "BASE" in match.group(1):
                bases[match.group(1)] = int(match.group(2), 16)
    return bases


def main():
    bases = load_bases()
    rows = []

    for path in sorted(REG_HEADERS.glob("*_reg.h")):
        for line in path.read_text().splitlines():
            match = REG_PATTERN.match(line.strip())
            if not match:
                continue

            reg_name, expr = match.groups()
            base_symbol = ""
            for candidate in sorted(bases.keys(), key=len, reverse=True):
                if candidate in expr:
                    base_symbol = candidate
                    break

            offset = ""
            off_matches = re.findall(r"\+\s*0x([0-9A-Fa-f]+)", expr)
            if off_matches:
                offset = f"0x{int(off_matches[-1], 16):X}"

            rows.append((path.name, reg_name, base_symbol, offset, expr.strip()))

    out = sys.stdout
    out.write("header\tregister\tbase_symbol\toffset\texpression\n")
    for row in rows:
        out.write("\t".join(row) + "\n")


if __name__ == "__main__":
    main()
