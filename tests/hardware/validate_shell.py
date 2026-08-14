#!/usr/bin/env python3
"""Shell validation against real hardware.

The host suite covers the shell's pure logic — role decisions and input
parsing — but the parts that only exist on a board (the wire format, the
dispatcher, the guards firing against live cells) had no repeatable check.
This is that check.

    python3 tests/hardware/validate_shell.py /dev/cu.usbmodem1101 [more ports]
    make hw-test PORT=/dev/cu.usbmodem1101

NON-DESTRUCTIVE BY CONSTRUCTION. It deliberately never:

  * provisions or clears an Aura key — `aura setkey` would overwrite the mesh
    secret and `aura clear` regenerates a *random* per-board key rather than
    restoring the old one, so either would silently un-pair a working bench.
    Only the *rejection* path is exercised, which by definition writes nothing.
  * reboots, sleeps, or loads a VM image or Loom fragment that could take.
  * leaves the session role, vitals overrides or purpose changed.

Requires pyserial. Exits non-zero if any check fails.
"""

import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial required: pip install pyserial")

sys.path.insert(0, "sdk/python")
try:
    from reflex import ReflexNode, AccessDenied
except ImportError:
    ReflexNode = None

PROMPT = "reflex> "
RESULT_RE = re.compile(r"^#R:([+-]?\d+),(\w+)$")

# A cell the fabric always seeds, used as a benign signal target.
LIVE_CELL = "agency.led.intent"
# A supervisor cell that `tapestry signal` must refuse.
SYS_CELL = "sys.kernel.disposition"


class Board:
    def __init__(self, port, baud=115200):
        self.port = port
        # Short serial timeout; the deadline is enforced by the read loop.
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.last_outcome = None
        time.sleep(0.6)
        self.ser.reset_input_buffer()

    def _send(self, cmd, timeout):
        """Write a command and read until the prompt returns.

        Reading to the prompt rather than sleeping a fixed interval is not a
        nicety: `loom list` prints ~118 rows, and any fixed wait short enough
        to keep the suite quick will truncate it, leaving the remainder in the
        buffer to be misread as the *next* command's response. Every check
        after it then compares against the wrong output.
        """
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode())
        # Accumulate bytes and decode once at the end. Decoding each chunk
        # separately splits any multi-byte character that straddles a read
        # boundary — the shell's em-dashes arrive as U+FFFD otherwise.
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                buf += chunk
                if PROMPT.encode() in buf:
                    break
        return buf.decode("utf-8", "replace")

    def raw(self, cmd, timeout=6.0):
        """Send a command, return the response with echo and `#R:` stripped.

        The ternary outcome marker is consumed into `self.last_outcome` rather
        than returned, so assertions keep comparing against the human text.
        """
        text = self._send(cmd, timeout)
        for marker in (cmd + "\r\n", cmd + "\n", cmd):
            if text.startswith(marker):
                text = text[len(marker):]
                break
        text = text.replace(PROMPT, "")
        self.last_outcome = None
        kept = []
        for line in text.splitlines():
            m = RESULT_RE.match(line.strip())
            if m:
                self.last_outcome = (int(m.group(1)), m.group(2))
            else:
                kept.append(line)
        return "\n".join(kept).strip()

    def wire(self, cmd, timeout=6.0):
        """Send a command, return the raw text as received."""
        return self._send(cmd, timeout)

    def cell_state(self, name):
        for line in self.raw("loom list").splitlines():
            if line.strip().startswith(name):
                return line.split("|")[2].strip()
        return None

    def close(self):
        self.ser.close()


class Results:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, desc, cond, got=""):
        if cond:
            self.passed += 1
            print(f"  PASS  {desc}")
        else:
            self.failed += 1
            print(f"  FAIL  {desc}\n          got: {got!r}")


# (command, role required) — every denial the policy table promises.
DENIALS = [
    ("tapestry signal " + LIVE_CELL + " 1", "operator"),
    ("goonies read " + LIVE_CELL, "operator"),
    ("led on", "operator"),
    ("telemetry on", "operator"),
    ("mesh emit 1", "operator"),
    ("mesh ping", "operator"),
    ("bonsai runtime", "operator"),
    ("purpose set nav", "agent"),
    ("snapshot save", "agent"),
    ("config set a b", "admin"),
    ("loom load AA", "admin"),
    ("vitals override temp 1", "admin"),
    ("reboot", "admin"),
    ("sleep 1", "admin"),
    ("aura clear", "admin"),
    ("aura setkey 000102030405060708090a0b0c0d0e0f", "admin"),
    ("snapshot clear", "admin"),
    ("mesh peer add x 00:00:00:00:00:00", "admin"),
    ("vm loadhex AA", "admin"),
]

OBSERVER_ALLOWED = [
    "status", "goonies ls", "goonies find " + LIVE_CELL, "temp", "loom list",
    "loom evictions", "loom fragments", "kernel", "mesh peer ls", "mesh stat",
    "mesh status", "vm info", "vm list", "led status", "purpose get",
    "services", "heartbeat", "vitals", "config get x", "telemetry", "help", "auth",
]


def validate(port, r):
    print(f"\n########## {port} ##########")
    b = Board(port)
    b.raw("auth role admin")

    print("--- wire format: echo and response must not share a line ---")
    b.raw("auth role observer")
    w = b.wire("led on")
    r.check("newline echoed before dispatch", "led on\r\ndenied: requires operator" in w, w)
    b.raw("auth role admin")

    print("--- role gating: denials ---")
    b.raw("auth role observer")
    for cmd, need in DENIALS:
        got = b.raw(cmd)
        r.check(f"observer denied `{cmd}` -> {need}", got == f"denied: requires {need}", got)

    print("--- role gating: observer must still work ---")
    for cmd in OBSERVER_ALLOWED:
        got = b.raw(cmd)
        r.check(f"observer allowed `{cmd}`", not got.startswith("denied:"), got[:70])

    print("--- role gating: agent and operator boundaries ---")
    b.raw("auth role agent")
    for cmd in ("snapshot save", "snapshot load", "purpose set nav", "purpose clear"):
        got = b.raw(cmd)
        r.check(f"agent allowed `{cmd}`", not got.startswith("denied:"), got[:70])
    for cmd, need in (("snapshot clear", "admin"), ("led on", "operator")):
        got = b.raw(cmd)
        r.check(f"agent denied `{cmd}` -> {need}", got == f"denied: requires {need}", got)

    b.raw("auth role operator")
    for cmd in ("led status", "goonies read " + LIVE_CELL, f"tapestry signal {LIVE_CELL} 0"):
        got = b.raw(cmd)
        r.check(f"operator allowed `{cmd}`", not got.startswith("denied:"), got[:70])
    for cmd, need in (("loom load AA", "admin"), ("aura clear", "admin")):
        got = b.raw(cmd)
        r.check(f"operator denied `{cmd}` -> {need}", got == f"denied: requires {need}", got)
    b.raw("auth role admin")

    print("--- tapestry: usage, trit range, sys guard ---")
    r.check("bare tapestry prints usage", "tapestry signal" in b.raw("tapestry"), "")
    r.check("missing state prints usage", "tapestry signal" in b.raw(f"tapestry signal {LIVE_CELL}"), "")
    for bad in ("99", "-99", "2", "abc", "1abc", "0x1"):
        got = b.raw(f"tapestry signal {LIVE_CELL} {bad}")
        r.check(f"tapestry rejects state {bad!r}", "must be -1, 0 or 1" in got, got)

    before = b.cell_state(SYS_CELL)
    got = b.raw(f"tapestry signal {SYS_CELL} 1")
    r.check("tapestry refuses a sys.* cell", "refusing to signal" in got, got)
    r.check(f"sys cell unchanged ({before} -> {b.cell_state(SYS_CELL)})",
            before == b.cell_state(SYS_CELL), before)
    got = b.raw("tapestry signal sys.does.not.exist 1")
    r.check("sys refusal precedes resolution", "refusing to signal" in got and "not found" not in got, got)

    for state in ("1", "-1", "0"):
        got = b.raw(f"tapestry signal {LIVE_CELL} {state}")
        r.check(f"tapestry signals a non-sys cell ({state})", "Signal sent" in got, got)
        r.check(f"cell state is {state}", b.cell_state(LIVE_CELL) == state, b.cell_state(LIVE_CELL))
    b.raw(f"tapestry signal {LIVE_CELL} 0")

    print("--- hex parsing: non-hex must be refused, not coerced to zero ---")
    # `aura setkey` is the one with no downstream validation: these 16 bytes are
    # the mesh HMAC key. Only rejections are exercised, so no key is ever written.
    for bad, label in (
        ("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", "all non-hex (the all-zero key)"),
        ("deadbeefzzzzzzzzdeadbeefzzzzzzzz", "partial non-hex"),
        ("0x0102030405060708090a0b0c0d0e0f", "0x prefix"),
    ):
        got = b.raw(f"aura setkey {bad}")
        # The success message is "aura: key provisioned"; the refusal is
        # "aura: key not provisioned", so match the success string exactly.
        r.check(f"aura setkey refuses {label}",
                "invalid hex" in got and "aura: key provisioned" not in got, got)
    got = b.raw("aura setkey deadbeef")
    r.check("aura setkey refuses wrong length", "expect 32 hex" in got, got)

    for cmd, label in (("loom load", "loom load"), ("vm loadhex", "vm loadhex")):
        got = b.raw(f"{cmd} zzzz")
        r.check(f"{label} refuses non-hex", "invalid hex" in got, got)
        got = b.raw(f"{cmd} abc")
        r.check(f"{label} refuses odd length", "even" in got.lower(), got)
        # Valid hex that is not a valid payload must reach the validator, not
        # die in the parser — this is what distinguishes the two layers.
        got = b.raw(f"{cmd} AABB")
        r.check(f"{label} passes valid hex to the validator",
                "invalid hex" not in got and got != "", got)

    print("--- mesh: operator input must not be coerced ---")
    # `mesh posture` state goes on the radio and is multiplied into every
    # peer's swarm accumulator, so an out-of-range value is a one-packet
    # consensus flip. These must be refused before anything is transmitted.
    for bad, why in (("99 4", "out-of-range state"), ("-99 4", "negative out-of-range state"),
                     ("abc 4", "non-numeric state"), ("2 4", "state just past the trit range")):
        got = b.raw(f"mesh posture {bad}")
        r.check(f"mesh posture refuses {why}", "state must be -1, 0 or 1" in got, got)
    for bad, why in (("1 abc", "non-numeric weight"), ("1 256", "weight past a byte"),
                     ("1 -1", "negative weight")):
        got = b.raw(f"mesh posture {bad}")
        r.check(f"mesh posture refuses {why}", "weight must be" in got, got)
    got = b.raw("mesh posture 1 4")
    r.check("mesh posture accepts a valid trit and weight", "mesh posture: state=1" in got, got)
    b.raw("mesh posture -1 4")
    b.raw("mesh posture 0 4")

    for bad in ("abc", "99", "2"):
        got = b.raw(f"mesh emit {bad}")
        r.check(f"mesh emit refuses {bad!r}", "state must be -1, 0, or 1" in got, got)

    got = b.raw("mesh peer add zzpeer zz:zz:zz:zz:zz:zz")
    r.check("mesh peer add refuses non-hex MAC", "mac format" in got, got)
    got = b.raw("mesh peer add zzpeer 00:11:22:33:44:zz")
    r.check("mesh peer add refuses one bad MAC octet", "mac format" in got, got)

    got = b.raw("config set log_level abc")
    r.check("config set refuses non-numeric log_level", "expected an integer" in got, got)
    got = b.raw("config set boot_count xyz")
    r.check("config set refuses non-numeric boot_count", "expected an integer" in got, got)

    got = b.raw("mesh stat")
    r.check("mesh stat surfaces the malformed counter", "malformed=" in got, got)

    # Rejection tests alone would pass against a parser that refuses
    # everything, so each changed command needs its accepting path exercised
    # too. Only the non-mutating ones belong here: `mesh peer add` would grow a
    # registry capped at 8 entries, and `aura setkey` would overwrite the mesh
    # key, so their happy paths are covered by tests/host/test_shell_parse.c
    # against the decoder itself.
    for state in ("-1", "0", "1"):
        got = b.raw(f"mesh emit {state}")
        r.check(f"mesh emit accepts {state}", "rc=" in got and "must be" not in got, got)

    original = b.raw("config get log_level")
    got = b.raw("config set log_level 3")
    r.check("config set accepts a valid integer", "ok" in got, got)
    r.check("config get reflects the new value", "3" in b.raw("config get log_level"), "")
    got = b.raw("config set log_level -1")
    r.check("config set accepts a negative integer", "ok" in got, got)
    if "=" in original:  # put it back exactly as found
        b.raw("config set log_level " + original.split("=")[1].strip())
        r.check("config restored to its original value",
                original.strip() == b.raw("config get log_level").strip(), original)

    print("--- ternary outcome marker ---")
    # The marker is the SDK's contract now, so each outcome class is asserted
    # against a command known to produce it.
    for cmd, want in (
        ("status", (1, "ok")),
        ("temp", (1, "ok")),
        (f"tapestry signal {LIVE_CELL} 0", (1, "ok")),
        ("tapestry", (0, "usage")),
        ("config", (0, "usage")),
        ("atlas", (0, "usage")),
        ("led", (0, "usage")),
        (f"tapestry signal {LIVE_CELL} 99", (-1, "invalid")),
        ("mesh posture 99 4", (-1, "invalid")),
        ("aura setkey zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", (-1, "invalid")),
        ("vm loadhex zzzz", (-1, "invalid")),
        (f"tapestry signal {SYS_CELL} 1", (-1, "guard")),
        ("tapestry signal no.such.cell 1", (-1, "notfound")),
        ("no_such_command_xyz", (-1, "notfound")),
    ):
        b.raw(cmd)
        sign = "+1" if want[0] > 0 else ("-1" if want[0] < 0 else "0")
        r.check(f"`{cmd}` -> #R:{sign},{want[1]}", b.last_outcome == want, b.last_outcome)

    b.raw("auth role observer")
    b.raw("reboot")
    r.check("a role refusal is #R:-1,denied", b.last_outcome == (-1, "denied"), b.last_outcome)
    b.raw("auth role admin")
    r.check("every command emits a marker", b.last_outcome is not None, b.last_outcome)
    b.raw(f"tapestry signal {LIVE_CELL} 0")

    print("--- raised line bound: payloads past the old 122-byte ceiling ---")
    # 1000 hex chars = 500 bytes, four times what `len < 255` allowed. The
    # transport buffer had to grow with the line buffer: at the driver's
    # default 256-byte RX buffer the tail of a long line was silently dropped
    # and the payload arrived as odd-length hex. An "even-length" complaint
    # here means characters were lost in transit, not that the test is wrong.
    for nbytes in (200, 500):
        got = b.raw("vm loadhex " + ("AB" * nbytes), timeout=25.0)
        r.check(f"a {nbytes}-byte payload is not truncated in transit",
                "even-length" not in got and "invalid hex" not in got, got[:80])
        r.check(f"...and a {nbytes}-byte bad image reports failed",
                b.last_outcome == (-1, "failed"), b.last_outcome)

    print("--- extra arguments are refused, not silently dropped ---")
    # `purpose set my purpose` used to store "my", discard the rest, and report
    # success — silent truncation plus a false +1,ok. There is no quoting, so
    # refusing is the only honest answer.
    for cmd in ("purpose set my purpose", "config set a b c", "goonies find a b",
                f"tapestry signal {LIVE_CELL} 1 extra", "auth role admin extra",
                "mesh emit 1 extra", "mesh posture 1 4 extra",
                "loom load AABB extra", "vitals override temp 1 extra",
                "aura setkey 000102030405060708090a0b0c0d0e0f extra"):
        got = b.raw(cmd)
        r.check(f"`{cmd}` refused",
                "too many arguments" in got and b.last_outcome == (-1, "invalid"),
                (got[:60], b.last_outcome))
    r.check("a refused `purpose set` did not take effect",
            "inactive" in b.raw("purpose get"), "")
    b.raw("auth role admin")

    # ...and the arity check must not reject valid invocations.
    for cmd in ("purpose set nav", "purpose clear", "config get log_level",
                f"goonies find {LIVE_CELL}", f"tapestry signal {LIVE_CELL} 0",
                "mesh emit 1", "mesh posture 1 4", "loom fragments"):
        b.raw(cmd)
        r.check(f"`{cmd}` still accepted", b.last_outcome == (1, "ok"), b.last_outcome)

    b.raw("vm run definitely_not_a_program")
    r.check("vm run on an unknown program -> notfound",
            b.last_outcome == (-1, "notfound"), b.last_outcome)
    b.raw("")
    r.check("a bare newline emits no marker", b.last_outcome is None, b.last_outcome)

    print("--- over-long input is refused, not silently truncated ---")
    # Past the line buffer the excess used to be dropped and the truncated
    # line dispatched as though complete. For `loom load` that would have fed
    # a *partial* fragment to a parser hardened against exactly this.
    for n, want_overflow in ((900, False), (1009, False), (1200, True), (2000, True)):
        b.raw("goonies find " + ("A" * n), timeout=15.0)
        got = b.last_outcome
        if want_overflow:
            r.check(f"a {13 + n}-char line is refused as overflow",
                    got == (-1, "overflow"), got)
        else:
            r.check(f"a {13 + n}-char line still dispatches",
                    got is not None and got != (-1, "overflow"), got)
    r.check("board still responsive after overflow", "uptime" in b.raw("status"), "")

    print("--- config usage ---")
    r.check("bare config prints usage", "config <get" in b.raw("config"), "")
    r.check("partial config set prints usage", "config <get" in b.raw("config set"), "")

    print("--- help lists every documented command ---")
    h = b.raw("help")
    for name in ("tapestry", "kernel", "aura", "vitals", "auth", "loom", "mesh", "vm"):
        r.check(f"help mentions `{name}`", name in h, "")

    b.close()

    if ReflexNode is not None:
        print("--- SDK contract: AccessDenied (SECURITY.md 2) ---")
        n = ReflexNode(port, timeout=3.0)
        time.sleep(0.4)
        n.cmd("auth role observer")
        for cmd, need in (("reboot", "admin"), ("led on", "operator"), ("purpose set x", "agent")):
            try:
                out = n.cmd(cmd)
                r.check(f"AccessDenied for `{cmd}`", False, f"no exception, returned {out!r}")
            except AccessDenied as e:
                r.check(f"AccessDenied for `{cmd}` -> {need}", f"requires {need}" in str(e), str(e))
        n.cmd("auth role admin")
        st = n.status()
        r.check("SDK output carries no echo prefix", not st.startswith("status"), st[:60])
        r.check("temp() parses", isinstance(n.temp(), float), "")
        n.close()

        g = ReflexNode(port, timeout=3.0, role="agent")
        try:
            g.cmd("reboot")
            r.check("role='agent' constructor blocks reboot", False, "no exception")
        except AccessDenied as e:
            r.check("role='agent' constructor blocks reboot", "requires admin" in str(e), str(e))
        g.cmd("auth role admin")
        g.close()

    print("--- board health ---")
    b = Board(port)
    b.raw("auth role admin")
    b.raw("vitals clear")
    b.raw("purpose clear")
    st = b.raw("status")
    r.check("board healthy", "reflex-os uptime" in st, st[:70])
    for line in st.splitlines()[:2]:
        print(f"       {line}")
    b.close()


def main():
    ports = sys.argv[1:]
    if not ports:
        sys.exit(__doc__)
    r = Results()
    for port in ports:
        validate(port, r)
    print(f"\n=== {r.passed} passed, {r.failed} failed ===")
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main())
