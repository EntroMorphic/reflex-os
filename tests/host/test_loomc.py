#!/usr/bin/env python3
"""Tests for the LoomScript compiler (tools/loomc.py).

loomc is the producer side of goose_weave_loom's trust boundary. Every case
below is a way the old parser produced either a traceback or a valid-but-wrong
fragment while reporting success -- the second being the dangerous one, because
the firmware then accepts the file and weaves something the author did not
write.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
import loomc


def _expect_error(src, needle):
    try:
        loomc.parse(src)
    except loomc.LoomError as e:
        assert needle in str(e), f"expected {needle!r} in {str(e)!r}"
        return str(e)
    raise AssertionError(f"expected LoomError containing {needle!r}, got success")


def test_missing_arrow():
    """`weave a` indexed parts[1] and raised IndexError."""
    _expect_error("weave a\n", "needs '->'")
    _expect_error("weave\n", "needs '<src> -> <snk>'")
    print("PASS: test_missing_arrow")


def test_unknown_verb_rejected():
    """A misspelled directive was skipped, emitting a valid but EMPTY fragment
    while printing 'Success!' -- a broken source producing a file the firmware
    accepts and weaves to nothing."""
    msg = _expect_error("route x -> y\n", "unknown directive")
    assert "'route'" in msg, msg
    assert "Line 1" in msg, msg
    print("PASS: test_unknown_verb_rejected")


def test_empty_fragment_rejected():
    """A source with no routes must not produce a weave-to-nothing artifact."""
    _expect_error("// only a comment\n", "no routes")
    _expect_error("", "no routes")
    print("PASS: test_empty_fragment_rejected")


def test_doubled_verb_rejected():
    """`line.replace("weave ", "")` scrubbed the substring from anywhere in the
    line, including from inside a cell name."""
    msg = _expect_error("weave weave x -> y\n", "whitespace")
    assert "'weave x'" in msg, msg
    print("PASS: test_doubled_verb_rejected")


def test_long_name_rejected_not_truncated():
    """cname[:23] truncated silently, so two distinct cells agreeing in their
    first 23 bytes became two entries carrying identical names -- which the
    firmware resolves to the same cell."""
    long_a = "a" * 24 + "X"
    msg = _expect_error(f"weave {long_a} -> b\n", "over the 23-byte wire field")
    assert "25 bytes" in msg, msg

    # Exactly at the limit must still be accepted.
    at_limit = "a" * 23
    cells, routes = loomc.parse(f"weave {at_limit} -> b\n")
    assert at_limit in cells, cells
    print("PASS: test_long_name_rejected_not_truncated")


def test_no_aliasing_survives():
    """The property the truncation broke: distinct sources produce distinct
    wire names."""
    src = f"weave {'a' * 22}X -> b\nweave {'a' * 22}Y -> c\n"
    cells, _ = loomc.parse(src)
    blob = loomc.build(cells, _)
    count = struct.unpack_from("<H", blob, 12)[0]
    names = [blob[18 + i * 24: 18 + (i + 1) * 24].split(b"\0")[0] for i in range(count)]
    assert len(names) == len(set(names)), f"aliased cell names: {names}"
    print("PASS: test_no_aliasing_survives")


def test_empty_and_nonascii_names():
    _expect_error("weave  -> b\n", "empty source cell name")
    _expect_error("weave a -> \n", "empty sink cell name")
    _expect_error("weave café -> b\n", "not ASCII")
    print("PASS: test_empty_and_nonascii_names")


def test_quotas_match_firmware():
    """Emitting past goose_weave_loom's quota produces a file the firmware
    refuses; refuse here, where the line number is still known.

    The constants are mirrored by hand, so read the real ones out of
    goose_library.c rather than trusting the copy -- a silent drift would make
    loomc either reject valid fragments or emit ones the firmware throws away.
    """
    lib = os.path.abspath(os.path.join(
        os.path.dirname(__file__), '..', '..', 'components', 'goose', 'goose_library.c'))
    firmware = {}
    with open(lib) as f:
        for line in f:
            m = re.match(r'#define\s+(MAX_(?:CELLS|ROUTES)_PER_FRAGMENT)\s+(\d+)', line)
            if m:
                firmware[m.group(1)] = int(m.group(2))
    assert len(firmware) == 2, f"could not read quotas from goose_library.c: {firmware}"
    assert loomc.MAX_CELLS_PER_FRAGMENT == firmware['MAX_CELLS_PER_FRAGMENT'], \
        f"loomc says {loomc.MAX_CELLS_PER_FRAGMENT}, firmware says {firmware['MAX_CELLS_PER_FRAGMENT']}"
    assert loomc.MAX_ROUTES_PER_FRAGMENT == firmware['MAX_ROUTES_PER_FRAGMENT'], \
        f"loomc says {loomc.MAX_ROUTES_PER_FRAGMENT}, firmware says {firmware['MAX_ROUTES_PER_FRAGMENT']}"

    # Exercise each quota on its own. Fanning out from a single source keeps
    # the cell count at routes+1, so the route limit is what trips -- writing
    # `s{i} -> k{i}` would exhaust the 64-cell quota first and test the wrong
    # bound.
    over_routes = "".join(
        f"weave src -> k{i}\n" for i in range(loomc.MAX_ROUTES_PER_FRAGMENT + 1))
    msg = _expect_error(over_routes, "exceeds the firmware quota")
    assert "routes" in msg, f"expected the route quota, got {msg!r}"

    over_cells = "".join(
        f"weave s{i} -> k{i}\n" for i in range((loomc.MAX_CELLS_PER_FRAGMENT // 2) + 1))
    msg = _expect_error(over_cells, "exceeds the firmware quota")
    assert "cells" in msg, f"expected the cell quota, got {msg!r}"

    # Exactly at each quota must still be accepted.
    at_routes = "".join(
        f"weave src -> k{i}\n" for i in range(loomc.MAX_ROUTES_PER_FRAGMENT))
    cells, routes = loomc.parse(at_routes)
    assert len(routes) == loomc.MAX_ROUTES_PER_FRAGMENT, len(routes)
    assert len(cells) == loomc.MAX_ROUTES_PER_FRAGMENT + 1, len(cells)
    print("PASS: test_quotas_match_firmware")


def test_wire_format_matches_firmware():
    """Header and route encoding must match loom_header_t / loom_route_entry_t,
    and the payload fields must be what goose_policy_loom_route_acceptable
    permits: a trit orientation and SOFTWARE coupling."""
    cells, routes = loomc.parse("weave agency.led.intent -> agency.led.out\n")
    blob = loomc.build(cells, routes)

    magic, version, _hash, ncells, nroutes, ntrans = struct.unpack_from("<IIIHHH", blob, 0)
    assert magic == 0x4D4F4F4C, hex(magic)
    assert version == 1, version
    assert (ncells, nroutes) == (2, 1), (ncells, nroutes)
    # The firmware now refuses a fragment declaring transitions it cannot parse.
    assert ntrans == 0, ntrans

    assert len(blob) == 18 + ncells * 24 + nroutes * 6, len(blob)

    off = 18 + ncells * 24
    src_idx, snk_idx, orientation, coupling = struct.unpack_from("<HHbB", blob, off)
    assert (src_idx, snk_idx) == (0, 1), (src_idx, snk_idx)
    assert orientation in (-1, 0, 1), orientation
    assert coupling == loomc.COUPLING_SOFTWARE, coupling
    print("PASS: test_wire_format_matches_firmware")


def test_examples_compile():
    """The repository's own .ls examples must still compile."""
    ex = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', 'examples'))
    if not os.path.isdir(ex):
        print("SKIP: test_examples_compile (no examples dir)")
        return
    count = 0
    for fname in sorted(os.listdir(ex)):
        if not fname.endswith('.ls'):
            continue
        with open(os.path.join(ex, fname)) as f:
            cells, routes = loomc.parse(f.read())
        assert routes, f"{fname} produced no routes"
        loomc.build(cells, routes)
        count += 1
    assert count >= 2, f"expected at least 2 examples, compiled {count}"
    print(f"PASS: test_examples_compile ({count} fragments)")


if __name__ == "__main__":
    test_missing_arrow()
    test_unknown_verb_rejected()
    test_empty_fragment_rejected()
    test_doubled_verb_rejected()
    test_long_name_rejected_not_truncated()
    test_no_aliasing_survives()
    test_empty_and_nonascii_names()
    test_quotas_match_firmware()
    test_wire_format_matches_firmware()
    test_examples_compile()
    print("\nAll tests passed.")
