#!/usr/bin/env python3
"""LoomScript compiler (loomc).

Translates .ls (declarative manifolds) into .loom (binary fragments).

This is the producer side of a trust boundary: goose_weave_loom treats every
field of a .loom as hostile and now validates the payload fields as well as the
structural ones (see goose_policy_loom_route_acceptable). A compiler that emits
a semantically wrong fragment while printing "Success!" is what makes that
validation the first place a mistake is noticed, which is far too late and on
the wrong machine. So this refuses instead of guessing:

  - an unknown verb is an error, not a silently skipped line
  - a doubled verb or a name containing whitespace is an error, not a name
    quietly rewritten by a substring scrub
  - a `weave` without `->` is an error, not an IndexError traceback
  - a cell name that does not fit the 23-byte wire field is an error, not a
    truncation that aliases two distinct cells onto one entry
  - every failure exits non-zero

Wire format (little-endian, packed):
  header: magic(4) version(4) name_hash(4) cell_count(2) route_count(2) trans_count(2)
  cell:   name(24, NUL-padded, 23 usable bytes + terminator)
  route:  src_idx(2) snk_idx(2) orientation(1, signed) coupling(1, unsigned)
"""
import struct
import sys

LOOM_MAGIC = 0x4D4F4F4C
LOOM_VERSION = 1

# goose_weave_loom accepts only SOFTWARE from the wire; HARDWARE routes silicon
# through the GPIO matrix and RADIO turns each pulse into a mesh transmission.
COUPLING_SOFTWARE = 1
ORIENTATION_DEFAULT = 1

# loom_cell_entry_t.name is char[24] and the loader forces a terminator into the
# last byte, so 23 bytes are usable.
CELL_NAME_MAX = 23

# Quotas enforced by goose_weave_loom. Emitting past them produces a file the
# firmware refuses, so refuse here where the line number is still known.
# Mirrored from goose_library.c; tests/host/test_loomc.py checks them against
# that file so the two cannot drift silently.
MAX_CELLS_PER_FRAGMENT = 64
MAX_ROUTES_PER_FRAGMENT = 32


class LoomError(Exception):
    """A diagnosable problem in the source, reported with its line number."""


def parse(text):
    """Parse LoomScript into (cells, routes). Raises LoomError on any problem."""
    cells = []
    routes = []

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("//"):
            continue

        # Tokenise on whitespace rather than scrubbing the verb with
        # str.replace, which removed "weave " from anywhere in the line --
        # including from inside a cell name -- and left the result looking valid.
        parts = line.split(None, 1)
        verb = parts[0]
        if verb != "weave":
            raise LoomError(
                f"Line {lineno}: unknown directive '{verb}' (expected 'weave')")
        if len(parts) < 2:
            raise LoomError(f"Line {lineno}: 'weave' needs '<src> -> <snk>'")

        operand = parts[1]
        if "->" not in operand:
            raise LoomError(f"Line {lineno}: 'weave' needs '->' between two cells")

        halves = operand.split("->")
        if len(halves) != 2:
            raise LoomError(
                f"Line {lineno}: expected one '->', found {len(halves) - 1}")

        src_name = halves[0].strip()
        snk_name = halves[1].strip()
        for name, side in ((src_name, "source"), (snk_name, "sink")):
            _check_name(name, side, lineno)

        for name in (src_name, snk_name):
            if name not in cells:
                cells.append(name)

        routes.append({
            "src_idx": cells.index(src_name),
            "snk_idx": cells.index(snk_name),
            "orientation": ORIENTATION_DEFAULT,
            "coupling": COUPLING_SOFTWARE,
        })

    if not routes:
        raise LoomError("no routes: the fragment would weave to nothing")
    if len(cells) > MAX_CELLS_PER_FRAGMENT:
        raise LoomError(
            f"{len(cells)} cells exceeds the firmware quota of {MAX_CELLS_PER_FRAGMENT}")
    if len(routes) > MAX_ROUTES_PER_FRAGMENT:
        raise LoomError(
            f"{len(routes)} routes exceeds the firmware quota of {MAX_ROUTES_PER_FRAGMENT}")

    return cells, routes


def _check_name(name, side, lineno):
    if not name:
        raise LoomError(f"Line {lineno}: empty {side} cell name")
    # Cell names are dotted identifiers, and the SDK's _token rejects all
    # whitespace for the same reason. This also catches a doubled verb:
    # `weave weave x -> y` yields a source named "weave x" rather than being
    # quietly accepted, which is what the old str.replace-based parser did.
    if any(ch.isspace() for ch in name):
        raise LoomError(
            f"Line {lineno}: {side} name '{name}' contains whitespace")
    try:
        encoded = name.encode("ascii")
    except UnicodeEncodeError:
        raise LoomError(
            f"Line {lineno}: {side} name '{name}' is not ASCII; the wire field is bytes")
    # Truncating instead of refusing let two distinct cells whose first 23 bytes
    # agree become two entries carrying identical names, which the firmware then
    # resolves to the same cell -- routes silently pointing somewhere else.
    if len(encoded) > CELL_NAME_MAX:
        raise LoomError(
            f"Line {lineno}: {side} name '{name}' is {len(encoded)} bytes, "
            f"over the {CELL_NAME_MAX}-byte wire field")


def build(cells, routes, name_hash=0xABCDEF01):
    """Serialise a parsed fragment to .loom bytes."""
    out = bytearray()
    out += struct.pack("<IIIHHH", LOOM_MAGIC, LOOM_VERSION, name_hash,
                       len(cells), len(routes), 0)
    for cname in cells:
        out += cname.encode("ascii").ljust(24, b"\0")
    for r in routes:
        # coupling is uint8_t on the wire; packing it as signed 'b' happened to
        # work only because every value emitted is small.
        out += struct.pack("<HHbB", r["src_idx"], r["snk_idx"],
                           r["orientation"], r["coupling"])
    return bytes(out)


def compile_loom(input_file, output_file):
    print(f"LoomScript: Compiling {input_file}...")
    with open(input_file, "r") as f:
        cells, routes = parse(f.read())
    with open(output_file, "wb") as f:
        f.write(build(cells, routes))
    print(f"Success! Fragment woven into {output_file} "
          f"({len(routes)} routes, {len(cells)} cells).")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python loomc.py <input.ls> <output.loom>")
        sys.exit(1)
    try:
        compile_loom(sys.argv[1], sys.argv[2])
    except (LoomError, OSError) as e:
        print(f"Error: {e}")
        sys.exit(1)
