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
  * runs a valid VM program. `vm run` executes the VM synchronously in the
    shell task for up to 100,000 steps, and a program that logs each iteration
    blocks on the serial write at ~2 lines/sec — the board stops answering for
    something like an hour and `vm stop` is unreachable, because the shell is
    inside `vm run`. Only `vm run <unknown-name>` is exercised here.
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

# Output the board emits on its own schedule, not in answer to a command.
#
# ESP-IDF log lines ("I (GOOSE_SUPERVISOR) snapshot saved: ...") and the
# kernel's supervisor notices arrive whenever the supervisor runs, which can be
# in the middle of any exchange. Several checks compare a response for exact
# equality — a denial must be the *whole* answer, so nothing leaks past the
# guard — and that strictness is worth keeping. What it must not also assert is
# that the board stayed silent about everything else, which it never promised.
ASYNC_LINE_RE = re.compile(r"^(?:[IWEDV] \([^)]*\)|\[reflex\.[a-z]+\])\s")

# A cell the fabric always seeds, used as a benign signal target.
LIVE_CELL = "agency.led.intent"
# A supervisor cell that `tapestry signal` must refuse.
SYS_CELL = "sys.kernel.disposition"
# A shadow-catalog register behind the Sanctuary Guard, which `goonies read`
# must refuse. IO_MUX sits at 0x60090000, outside the whitelist of pages
# non-system code may touch, so the read is rejected before any dereference —
# nothing is sampled and no peripheral state is disturbed by running this.
#
# Verified against the generated catalog rather than chosen by eye:
# goose_shadow_atlas.c carries {"agency.io_mux.date", 0x600900FC, ...}, and
# 0x600900FC & 0xFFFFF000 = 0x60090000 is absent from the allow-list in
# goose_fabric_addr_is_sanctuary, so the predicate returns true. It is not
# boot-woven, so the read takes the shadow-resolve branch.
SANCTUARY_CELL = "agency.io_mux.date"


# How long to wait for a board to reach its shell before giving up. A cold
# ESP32 takes several seconds to boot, and longer with a radio to bring up.
READY_TIMEOUT_S = 25.0


class BoardNotReady(Exception):
    """A board never reached a usable shell.

    Deliberately not SystemExit. This suite takes several ports on one command
    line and validates them in turn, so killing the process on the first
    unresponsive board would skip every board after it and print no summary at
    all. One dead board is one recorded failure, not the end of the run.
    """


class Board:
    def __init__(self, port, baud=115200):
        self.port = port
        # Short serial timeout; the deadline is enforced by the read loop.
        self.ser = serial.Serial(port, baud, timeout=0.1)
        self.last_outcome = None
        time.sleep(0.6)
        self.ser.reset_input_buffer()
        self._wait_ready()

    def _wait_ready(self):
        """Block until the board answers, rather than assuming it is up.

        A fixed 0.6s settle was enough for a board that had been running and
        far too short for one that had just been flashed. Running the suite
        immediately after `idf.py flash` produced 99 passed and 71 failed on a
        classic ESP32 — not a regression, just every check racing a board that
        was still booting. A test suite that reports 71 failures for a timing
        reason is worse than one that waits: it invites someone to go looking
        for a defect that is not there.

        Polls `status` until the outcome marker comes back, so readiness means
        "the shell parsed a command and answered", not "some bytes arrived".

        Two consecutive answers are required, not one. A single answer proves
        the shell is alive; it does not prove the console is done settling. On
        a classic ESP32 the first run after a flash answered `status` and then
        failed the 1022-character line check — the boundary case that most
        depends on the UART ring being fully up — while three later runs passed
        it. Requiring stability rather than mere liveness removes that.
        """
        deadline = time.time() + READY_TIMEOUT_S
        consecutive = 0
        while time.time() < deadline:
            self.ser.reset_input_buffer()
            self.ser.write(b"status\n")
            buf = b""
            inner = time.time() + 2.0
            answered = False
            while time.time() < inner:
                chunk = self.ser.read(self.ser.in_waiting or 1)
                if chunk:
                    buf += chunk
                    if b"#R:" in buf:
                        answered = True
                        break
            if answered:
                consecutive += 1
                if consecutive >= 2:
                    time.sleep(0.3)  # let the last response drain fully
                    self.ser.reset_input_buffer()
                    return
            else:
                consecutive = 0
            time.sleep(0.5)
        raise BoardNotReady(
            f"no shell response within {READY_TIMEOUT_S:g}s. Either the board "
            f"is still booting, is halted by Boot0 loop protection and needs a "
            f"power cycle, or is running firmware older than the `#R:` outcome "
            f"marker this suite requires."
        )

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
        # Identify the echo by its content, not by its position.
        #
        # The board emits supervisor telemetry on its own schedule, so a line
        # like "I (GOOSE_SUPERVISOR) snapshot saved" can legitimately land
        # between the write and the echo. Stripping only a leading echo then
        # leaves the echo in the response, and checks that were reading the
        # board's answer start reading the command back instead. That is the
        # suite asserting something the system never promised — asynchronous
        # output is not a fault, and a console that owns its own transmit path
        # times it differently than ESP-IDF's buffered driver did.
        lines = text.split("\n")
        for i, line in enumerate(lines):
            if line.rstrip("\r") == cmd:
                del lines[i]
                break
        text = "\n".join(lines)
        text = text.replace(PROMPT, "")
        self.last_outcome = None
        kept = []
        for line in text.splitlines():
            m = RESULT_RE.match(line.strip())
            if m:
                self.last_outcome = (int(m.group(1)), m.group(2))
            elif not ASYNC_LINE_RE.match(line.strip()):
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
        self.skipped = 0

    def check(self, desc, cond, got=""):
        if cond:
            self.passed += 1
            print(f"  PASS  {desc}")
        else:
            self.failed += 1
            print(f"  FAIL  {desc}\n          got: {got!r}")

    def skip(self, desc, why):
        """Record a check that does not apply to this target.

        Counted and printed, never silent. A check that quietly disappears on
        one target is indistinguishable from a check that passes, which is the
        failure mode this suite exists to catch elsewhere."""
        self.skipped += 1
        print(f"  SKIP  {desc}\n          {why}")


def board_catalog_size(b):
    """How many MMIO registers this board's shadow atlas actually carries.

    `atlas verify` reports `ok=N/M`; M is the catalog size. A C6 answers
    12738/12738, and a target built with goose_shadow_atlas_stub.c answers 0/0.
    Returns 0 when the count cannot be read, which routes the caller to skip
    rather than to assert against a catalog whose size is unknown.
    """
    out = b.raw("atlas verify")
    m = re.search(r"ok=\d+/(\d+)", out)
    return int(m.group(1)) if m else 0


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

    print("--- bonsai: no silent success, and no leaked units ---")
    # Every unmatched subcommand used to fall off the end of the dispatcher and
    # report #R:+1,ok having done nothing.
    for cmd in ("bonsai", "bonsai nosuchthing", "bonsai exp4 bogus", "bonsai exp5"):
        b.raw(cmd)
        r.check(f"`{cmd}` reports usage, not success",
                b.last_outcome == (0, "usage"), b.last_outcome)

    # exp5 allocates a PCNT unit, a channel and an RMT channel per run and used
    # to free the unit while the channel was still attached, so pcnt_del_unit
    # failed and the unit leaked. The C6 has four; the fifth run would fail for
    # a reason with nothing to do with the experiment. Five runs is the check.
    # exp4 configured its LEDC channel with gpio_num = -1, which LEDC rejects,
    # and reported success anyway. A failure is now reported as one, so the
    # check is that it succeeds — and that detaching gives the pin back, since
    # it routes the LED away from plain GPIO to do its work.
    got = b.raw("bonsai exp4 connect")
    r.check("`bonsai exp4 connect` configures LEDC without failing",
            b.last_outcome == (1, "ok") and "orient=" in got, got[:80])

    # The C6 configures LEDC through Reflex's own driver rather than
    # driver/ledc.h. What makes that trustworthy is not that it compiles: it is
    # that the peripheral ends up in the same state. These are the exact
    # registers ESP-IDF's driver left behind, captured from this board before
    # the swap, so a divergence in the divider arithmetic, the clock source or
    # the latch bits fails here rather than becoming a pin quietly running at
    # the wrong frequency.
    IDF_LEDC = ("timer=0x00138808 ch0=0x00000004 duty=0x00000800 "
                "pcr=0x00000001 sclk=0x00700000")
    #
    # Four states, not two. Collapsing them is how this failed the V3 for
    # months over something the firmware was reporting correctly: the ESP32
    # prints `ledc readback=unavailable (no snapshot on this target)` and emits
    # no `ledc timer=` line at all, so the match ran against an empty string
    # and blamed the board for the test's assumption. The same mistake, and the
    # same fix, as the atlas probe above.
    ledc_line = next((l for l in got.splitlines() if "ledc timer=" in l), "")
    if "ledc readback=unavailable" in got:
        r.skip("Reflex LEDC matches ESP-IDF register for register",
               "this target reports no LEDC snapshot, which is the honest "
               "answer where Reflex does not own the peripheral")
    elif "sclk=0x00000000" in ledc_line:
        r.skip("Reflex LEDC matches ESP-IDF register for register",
               "this target still uses ESP-IDF's LEDC, so there is no Reflex "
               "configuration to compare")
    elif not ledc_line:
        # Neither a snapshot nor a stated absence. That is a test problem or a
        # broken command, and it should say which rather than reporting a
        # register mismatch against nothing.
        r.check("Reflex LEDC matches ESP-IDF register for register", False,
                "no `ledc timer=` line and no stated unavailability: "
                f"{got[:80]!r}")
    else:
        r.check("Reflex LEDC matches ESP-IDF register for register",
                IDF_LEDC in ledc_line, ledc_line)
    was_on = "led=on" in b.raw("led status")
    b.raw("bonsai exp4 detach")
    # Detach routes the pin back from the LEDC signal to plain GPIO, so the
    # test is that it can be driven again — both ways, since a pin stuck at one
    # level would still satisfy a single read.
    off_ok = "led=off" in b.raw("led off") and "led=off" in b.raw("led status")
    on_ok = "led=on" in b.raw("led on") and "led=on" in b.raw("led status")
    r.check("the LED is driveable again after exp4 detaches", off_ok and on_ok,
            f"off_ok={off_ok} on_ok={on_ok}")
    if not was_on:
        b.raw("led off")

    first = b.raw("bonsai exp5 run")

    # PCNT is Reflex's own on the C6, checked the same way LEDC was: these are
    # the exact registers ESP-IDF's driver left behind, captured from this board
    # before the swap. The first attempt at the driver counted correctly while
    # leaving the filter and all four watch-event enables set — they come up
    # that way out of reset — and only this comparison showed it.
    IDF_PCNT = ("conf0=0x00040010 conf1=0x00000000 conf2=0xfc1803e8 "
                "ctrl=0x00000054 pcr=0x00000001")
    IDF_RMT = ("conf0=0x00525050 sys=0x05000011 lim=0x00200030 "
               "pcr=0x00000001 sclk=0x00500040")
    rmt_line = next((l for l in first.splitlines() if "rmt conf0=" in l), "")
    if not rmt_line or "sclk=0x00000000" in rmt_line:
        r.skip("Reflex RMT matches ESP-IDF register for register",
               "this target does not run the experiment, so there is no Reflex "
               "RMT configuration to compare")
    else:
        r.check("Reflex RMT matches ESP-IDF register for register",
                IDF_RMT in rmt_line, rmt_line)

    # The experiment counts every pulse it sends now. It read 2 of 10 for as
    # long as ESP-IDF drove the transmit side, with both peripherals configured
    # identically — the pad's input buffer was never enabled, so the counter
    # could not see the signal. Isolated by removing that one bit from the
    # Reflex driver, which drops the count to 0 while every register stays the
    # same. Asserting the number keeps that from silently regressing.
    if "overlap=" in first:
        r.check("`bonsai exp5` counts every pulse it sends",
                "overlap=10 (expected 10)" in first, first[:90])

        # Check the clock, which no register comparison can.
        #
        # The RMT register check pins the channel divider at 80 and the count
        # above proves ten edges arrived — and both would still pass if the
        # source feeding that divider were not the 80 MHz the driver assumes,
        # because the pulses would simply come out the wrong width. Ten symbols
        # of 1000 + 1000 ticks at 1 MHz is 20 ms. Measured at 20024 us.
        m = re.search(r"tx_us=(\d+)", first)
        r.check("the transmission takes the time 1 MHz implies",
                m is not None and 19000 <= int(m.group(1)) <= 21500,
                m.group(1) if m else first[:90])
    pcnt_line = next((l for l in first.splitlines() if "pcnt conf0=" in l), "")
    if not pcnt_line or "conf0=0x00000000" in pcnt_line:
        # Say so rather than vanish. A check that disappears on one target
        # without reporting it is indistinguishable from one that passed, and
        # the counted SKIP exists precisely so coverage cannot go quiet.
        r.skip("Reflex PCNT matches ESP-IDF register for register",
               "this target does not run the experiment, so there is no Reflex "
               "PCNT configuration to compare")
    else:
        r.check("Reflex PCNT matches ESP-IDF register for register",
                IDF_PCNT in pcnt_line, pcnt_line)

    if "not wired for this target" in first:
        # The experiment hardcodes C6 pins, and GPIO 6 is a flash pin on the
        # classic ESP32 — running it there resets the board, so it refuses.
        r.check("`bonsai exp5 run` refuses on a target it is not wired for",
                b.last_outcome == (-1, "failed"), b.last_outcome)
    else:
        leaked = None if "overlap=" in first else f"run 1: {first[:80]}"
        for i in range(4):
            got = b.raw("bonsai exp5 run")
            if leaked is None and "overlap=" not in got:
                leaked = f"run {i + 2}: {got[:80]}"
        r.check("five `bonsai exp5 run` in a row all reach the count (no leaked unit)",
                leaked is None, leaked or "")

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

    got = b.raw(f"tapestry signal {SYS_CELL} 1")
    r.check("tapestry refuses a sys.* cell", "refusing to signal" in got, got)

    # Establish that the cell is quiet before trusting it to stay put.
    #
    # The supervisor rewrites sys.kernel.disposition on every policy pass, from
    # live engaged/withheld counts (goose_supervisor.c) — so a bare
    # before/after comparison is asserting something the system never promised,
    # and fails whenever a pass lands between the two reads. The old form was
    # worse than it looked: it sampled the cell a second time for the label and
    # a third for the comparison, so it compared the first read against the
    # third with two command round-trips in between. It passed by being lucky
    # about timing, and a change that shifted timing at all could unmask it
    # without breaking anything.
    #
    # A trial only counts when the cell reads the same either side of a control
    # round-trip; otherwise the supervisor is active and the trial says nothing.
    # Then the same comparison is made across the refused signal, which is the
    # property actually under test: a refusal must not write.
    for _ in range(5):
        first = b.cell_state(SYS_CELL)
        b.raw("status")               # a round-trip that must not change it
        if first != b.cell_state(SYS_CELL):
            continue                  # supervisor wrote; trial void
        b.raw(f"tapestry signal {SYS_CELL} 1")
        after = b.cell_state(SYS_CELL)
        r.check(f"a refused sys.* signal does not write ({first} -> {after})",
                first == after, after)
        break
    else:
        r.skip("a refused sys.* signal does not write",
               "the supervisor rewrote sys.kernel.disposition during every "
               "attempt, so no trial could isolate the signal's effect")
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
    # Three states have to be told apart here, and the suite previously
    # collapsed the last two.
    #
    #   1. The probe resolves          -> assert the Sanctuary Guard.
    #   2. The catalog exists but this name does not -> TEST PROBLEM: the
    #      catalog was regenerated and the probe went stale. `goonies read`
    #      answers notfound, which reads as the guard regressing when in fact
    #      the test needs a new name.
    #   3. This target has no catalog at all -> not applicable.
    #
    # State 3 is real, not hypothetical: the shadow atlas is generated from
    # tools/esp32c6.svd, and components/goose/CMakeLists.txt deliberately
    # substitutes goose_shadow_atlas_stub.c on every other target so they do
    # not claim C6 hardware knowledge. On the classic ESP32 mesh peer the
    # catalog is empty by design, notfound is the honest answer, and reporting
    # a failure there blames the firmware for the test's assumption.
    catalog_size = board_catalog_size(b)
    if catalog_size == 0:
        r.skip(f"Sanctuary Guard via {SANCTUARY_CELL}",
               "this target has no scraped MMIO catalog (atlas reports 0 entries), "
               "so no shadow register exists to guard — see "
               "components/goose/CMakeLists.txt")
    else:
        probe = b.raw(f"goonies find {SANCTUARY_CELL}")
        if "not" in probe.lower() and "found" in probe.lower():
            r.check(f"TEST PROBLEM: sanctuary probe {SANCTUARY_CELL} no longer resolves "
                    f"in a catalog of {catalog_size} entries "
                    f"— pick another guarded catalog name", False, probe)

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
        # The Sanctuary Guard is the other policy guard SHELL_GUARD names, and
        # it reported `+1,ok` while refusing: an agent could not tell a
        # rejected MMIO read from a successful one. Asserted here for the same
        # reason the sys.* guard is — but only where a catalog exists to guard.
        *([(f"goonies read {SANCTUARY_CELL}", (-1, "guard"))] if catalog_size else []),
        ("tapestry signal no.such.cell 1", (-1, "notfound")),
        ("no_such_command_xyz", (-1, "notfound")),
        # Rejections in the mesh peer parser. A 17-character string with the
        # wrong separators passed the length check and then reported success.
        ("mesh peer add p aa-bb-cc-dd-ee-ff", (-1, "invalid")),
        # Over-long peer names are refused rather than silently truncated.
        ("mesh peer add thisnameiswaytoolong aa:bb:cc:dd:ee:ff", (-1, "invalid")),
        # Bare invocations print usage and do nothing; both reported `+1,ok`.
        ("snapshot", (0, "usage")),
        ("purpose", (0, "usage")),
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
        try:
            validate(port, r)
        except (BoardNotReady, serial.SerialException) as e:
            # Recorded against this port and the run continues, so a dead board
            # costs one failure rather than every result after it. SerialException
            # is caught alongside: a port that does not exist raises from
            # serial.Serial() before readiness is ever consulted, and aborting
            # there skips every later board just as surely.
            # validate() has already printed the port header before the
            # failure point, so printing it again here would double it.
            r.check(f"{port} reachable", False, str(e))
        except Exception as e:  # noqa: BLE001 - deliberate, see below
            # A board that dies *partway* through validation aborts the rest of
            # the run exactly as a dead one did, just at a different point —
            # the checks after it never run and the boards after it are never
            # touched. Anything unhandled is therefore recorded against this
            # port and the run continues.
            #
            # Broad on purpose, and visible rather than swallowed: the
            # exception type and message are reported as a failed check, so a
            # bug in the suite surfaces as a named failure instead of being
            # quietly absorbed.
            r.check(f"{port} completed without an unhandled error", False,
                    f"{type(e).__name__}: {e}")
    tail = f", {r.skipped} skipped" if r.skipped else ""
    print(f"\n=== {r.passed} passed, {r.failed} failed{tail} ===")
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main())
