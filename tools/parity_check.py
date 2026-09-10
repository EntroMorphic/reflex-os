#!/usr/bin/env python3
"""Measure what a running Reflex OS board can actually do.

The independence ratchet in check_independence.py counts one thing: ESP-IDF
includes removed, per tier, never allowed to grow. It measures subtraction. It
cannot see whether the Reflex code that replaced a dependency still does the
job — and twice now it has scored a replacement as progress while that
replacement did nothing at all. `reflex_task_reflex.c` could not wake a task
from a delay; `reflex_kv_flash.c` persists nothing across a reboot. Both passed
every gate, both lowered the count.

So this measures the other half: run the same battery against two builds and
diff. A capability the default build has and the independence build does not is
a regression the tier count will happily call an improvement.

Deliberately shell-driven and hardware-only. The host suite mocks flash as RAM
and cannot model persistence; the thing that fooled it is exactly what this is
for.

Usage:
    tools/parity_check.py --port /dev/cu.usbmodemXXXX --label default
    tools/parity_check.py --port /dev/cu.usbmodemYYYY --label independence
    tools/parity_check.py --diff default.json independence.json
"""
import argparse, json, os, re, sys, time

try:
    import serial
except ImportError:
    sys.exit("pyserial required: pip install pyserial")

BAUD = 115200


def open_port(port, tries=8):
    """Opening resets the board. Deasserting DTR/RTS first stops the USB-JTAG
    device re-enumerating out from under us, which is what made every earlier
    two-connection experiment on this bench fail."""
    for _ in range(tries):
        if not os.path.exists(port):
            time.sleep(2.0)
            continue
        try:
            s = serial.Serial(port, BAUD, timeout=0.3)
            s.dtr = False
            s.rts = False
            time.sleep(3.5)
            s.reset_input_buffer()
            return s
        except Exception:
            time.sleep(2.0)
    return None


SILENT = []


def ask(s, cmd, wait=2.5):
    """Records commands that produced nothing.

    A silent command and an absent capability are not the same thing, and this
    tool conflated them: every probe returned None either way, so one dead
    connection reported as a dozen capability regressions. It very nearly went
    into the record that way. A run with silent commands is untrustworthy as a
    whole, not selectively wrong, and says so."""
    try:
        s.write((cmd + "\r\n").encode())
        s.flush()
    except Exception as e:
        return "<<write failed: %s>>" % e
    end = time.time() + wait
    out = b""
    while time.time() < end:
        try:
            d = s.read(4096)
        except Exception as e:
            return out.decode("utf8", "replace") + "<<read failed: %s>>" % e
        if d:
            out += d
    text = out.decode("utf8", "replace")
    if not text.strip():
        SILENT.append(cmd)
    return text


def probe(text, pattern, cast=str):
    m = re.search(pattern, text)
    if not m:
        return None
    try:
        return cast(m.group(1))
    except Exception:
        return None


def run_battery(port, label):
    """Two sessions on purpose: persistence can only be measured across a
    reboot, and opening the port is the reboot."""
    results = {"label": label, "port": port}

    s = open_port(port)
    if not s:
        results["error"] = "port unavailable"
        return results

    txt = ask(s, "status")
    results["boots"] = probe(txt, r"reflex-os uptime=([\d.]+)s", float) is not None
    results["mac"] = probe(txt, r"mac=([0-9a-f:]+)")

    ask(s, "auth admin")
    results["auth_admin"] = "role: admin" in ask(s, "auth")

    tick = ask(s, "kernel tick", 4.0)
    results["tick_hz"] = probe(tick, r"-> (\d+) Hz", int)

    tasks = ask(s, "kernel tasks")
    results["sched_slots_used"] = probe(tasks, r"kernel tasks: (\d+) of", int)
    results["sched_slots_dead"] = probe(tasks, r"in use, (\d+) dead", int)

    results["temp_c"] = probe(ask(s, "temp"), r"temp=([\d.]+)C", float)
    results["lp_heartbeat"] = probe(ask(s, "heartbeat"), r"lp_pulse_count=(\d+)", int)

    results["led_on"] = "led=on" in ask(s, "led on")
    results["led_off"] = "led=off" in ask(s, "led off")

    # Read the reply to `connect` itself. This used to ask `bonsai exp4 status`
    # — not a subcommand, so the match always failed — and then `or True`ed the
    # result, which made it a check that could not fail. In a tool written to
    # catch exactly that.
    results["pwm_attach"] = "orient=rising" in ask(s, "bonsai exp4 connect")
    results["pwm_detach"] = "orient=unchanged" in ask(s, "bonsai exp4 detach")

    mesh = ask(s, "mesh status")
    results["mesh_tx"] = probe(mesh, r"tx=(\d+)", int)
    results["mesh_rx"] = probe(mesh, r"rx=(\d+)", int)
    results["mesh_peers"] = probe(mesh, r"peers=(\d+)", int)

    results["vm_programs"] = len(re.findall(r"^\s+\w+ \(\d+ bytes\)", ask(s, "vm list"), re.M))

    # Persistence: write now, read after the reopen below reboots the board.
    ask(s, "purpose set photography")
    results["purpose_set_ok"] = "photography" in ask(s, "purpose get")
    s.close()

    time.sleep(1.0)
    s2 = open_port(port)
    if not s2:
        results["persist_across_reboot"] = None
        return results
    after = ask(s2, "purpose get")
    results["persist_across_reboot"] = "photography" in after
    # Put it back. The persistence probe is the one part of this battery that
    # writes to the device, and on a build where persistence works it therefore
    # leaves a purpose set behind — observed on a board that came up
    # `purpose=photography` long after a run. A measuring tool that changes what
    # it measures is only acceptable if it changes it back.
    ask(s2, "auth admin")
    ask(s2, "purpose clear")
    results["cleaned_up"] = "photography" not in ask(s2, "purpose get")
    s2.close()
    results["silent_commands"] = list(SILENT)
    results["trustworthy"] = not SILENT
    return results


# What a difference means has to be declared, because most of these numbers are
# not comparable between builds. Reporting every field with a verdict made a
# temperature reading look like a capability check and, worse, let a real
# numeric regression print "same": tick_hz could fall from 999 to 100 and the
# old logic only ever flagged booleans and None.
BOOL_CHECKS = (
    "boots", "auth_admin", "led_on", "led_off",
    "pwm_attach", "pwm_detach", "purpose_set_ok", "persist_across_reboot",
    "cleaned_up",
)
# Numeric checks, with what a regression means for each.
NUM_CHECKS = {
    "tick_hz": ("at least 95% of the baseline", lambda base, other: other >= 0.95 * base),
    "vm_programs": ("no fewer than the baseline", lambda base, other: other >= base),
    "sched_slots_dead": ("no unreclaimed task slots", lambda base, other: other == 0),
}
# Reported for context and never given a verdict: they legitimately differ
# between builds, boards and moments.
OBSERVATIONS = ("temp_c", "lp_heartbeat", "mesh_tx", "mesh_rx", "mesh_peers",
                "sched_slots_used")


def diff(a, b):
    for run in (a, b):
        if not run.get("trustworthy", True):
            print(f"WARNING: '{run['label']}' had silent commands "
                  f"{run.get('silent_commands')} — treat this run as a transport "
                  f"failure, not as capability data.\n")
    keys = list(BOOL_CHECKS) + list(NUM_CHECKS) + list(OBSERVATIONS)
    width = max(len(k) for k in keys)
    print(f"{'capability'.ljust(width)}  {a['label'][:18].ljust(18)}  {b['label'][:18].ljust(18)}  verdict")
    print("-" * (width + 48))
    regressions = 0
    for k in keys:
        va, vb = a.get(k), b.get(k)
        if k in OBSERVATIONS:
            verdict = "(observed)"
        elif va is None or vb is None:
            verdict = "same" if va == vb else ("REGRESSION" if vb is None else "gain")
        elif k in BOOL_CHECKS:
            verdict = "same" if va == vb else ("REGRESSION" if va and not vb else "gain")
        else:
            rule, ok = NUM_CHECKS[k]
            verdict = "same" if ok(va, vb) else "REGRESSION (%s)" % rule
        if verdict.startswith("REGRESSION"):
            regressions += 1
        print(f"{k.ljust(width)}  {str(va)[:18].ljust(18)}  {str(vb)[:18].ljust(18)}  {verdict}")
    print()
    print(f"{regressions} capability regression(s) in '{b['label']}' relative to '{a['label']}'")
    return 1 if regressions else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--label", default="build")
    ap.add_argument("--out")
    ap.add_argument("--diff", nargs=2, metavar=("BASE", "OTHER"))
    args = ap.parse_args()

    if args.diff:
        with open(args.diff[0]) as f:
            a = json.load(f)
        with open(args.diff[1]) as f:
            b = json.load(f)
        sys.exit(diff(a, b))

    if not args.port:
        ap.error("--port required unless --diff")
    res = run_battery(args.port, args.label)
    text = json.dumps(res, indent=2, sort_keys=True)
    print(text)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)


if __name__ == "__main__":
    main()
