#!/usr/bin/env python3
"""Measure the Reflex scheduler tick across repeated cold starts.

Written because this tick was characterised three separate ways from single
samples in one session, and all three readings were wrong: "the derived period
does not produce the derived rate" (it does, on the build that was not being
measured), "the first arming after boot works and re-arming does not" (fitted
four runs, failed on the next boot), and two hypotheses about interrupt
allocation that measurement rejected only after the code had been changed.

Delivery is intermittent. One run tells you nothing. This runs N cold starts and
reports the distribution, so a change can be judged against a rate rather than
against an anecdote.

Cold starts are performed with the shell's own `reboot`, and each one is
*verified* by reading uptime back before arming the tick.

An earlier version of this script assumed that opening a C6's USB-JTAG port
resets the board, and every measurement it produced was therefore of re-arming
on a continuously running board rather than of a cold start. Checked directly:
six consecutive opens read uptime 925.9, 934.5, 943.1, 951.7, 960.4 and 969.0
seconds — monotonic, no reset. The assumption came from watching boards
re-enumerate during a different experiment and was never tested. This script now
tests it on every run, and reports a run as non-cold rather than counting it.

Usage:  python3 tools/measure_tick.py <port> [runs]
"""
import re
import statistics
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial required: pip install pyserial")

RATE_RE = re.compile(r"kernel tick:\s+(\d+) ticks in (\d+) us -> (\d+) Hz")


UPTIME_RE = re.compile(r"uptime=([0-9.]+)s")
# A board that has just rebooted reports single-digit seconds. Anything past
# this means the reboot did not happen and the run is not a cold start.
COLD_UPTIME_MAX_S = 20.0


def _open(port, attempts=8):
    """Open the port, retrying while USB re-enumerates after a reboot."""
    for _ in range(attempts):
        try:
            return serial.Serial(port, 115200, timeout=2.0)
        except Exception:
            time.sleep(1.5)
    return None


def one_cold_run(port, boot_wait):
    """Reboot the board, reconnect, verify it rebooted, then arm the tick once.

    The reconnect is not optional: `reboot` tears down the USB-JTAG endpoint, so
    the descriptor held across it is stale and every read through it fails.
    """
    s = _open(port)
    if s is None:
        return None, "open failed"
    try:
        time.sleep(1.0)
        s.reset_input_buffer()
        s.write(b"reboot\n")
        time.sleep(0.5)
    except Exception:
        pass
    finally:
        try:
            s.close()
        except Exception:
            pass

    time.sleep(boot_wait)

    s = _open(port)
    if s is None:
        return None, "did not re-enumerate after reboot"
    try:
        time.sleep(0.5)
        s.reset_input_buffer()

        # Verify coldness rather than assuming it.
        s.write(b"status\n")
        time.sleep(1.8)
        st = s.read(s.in_waiting or 1).decode(errors="replace")
        um = UPTIME_RE.search(st)
        if not um:
            return None, "no uptime line (board not responding)"
        uptime = float(um.group(1))
        if uptime > COLD_UPTIME_MAX_S:
            return None, f"NOT COLD: uptime={uptime}s — reboot did not take"

        s.reset_input_buffer()
        s.write(b"kernel tick\n")
        time.sleep(4.5)
        out = s.read(s.in_waiting or 1).decode(errors="replace")
    except Exception as e:
        return None, f"io failed: {e}"
    finally:
        try:
            s.close()
        except Exception:
            pass

    if "kernel <" in out or "unknown command" in out.lower():
        return None, "kernel tick unavailable (not a KERNEL_SCHEDULER build)"
    m = RATE_RE.search(out)
    if not m:
        return None, "no rate line"
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), uptime), None


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    port = sys.argv[1]
    runs = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    boot_wait = 9.0

    rates, failures = [], 0
    for i in range(runs):
        res, err = one_cold_run(port, boot_wait)
        if res is None:
            failures += 1
            print(f"  run {i+1:2d}: {err}")
        else:
            ticks, us, hz, uptime = res
            rates.append((ticks, hz))
            print(f"  run {i+1:2d}: cold(uptime={uptime:.1f}s)  "
                  f"{ticks:5d} ticks in {us} us -> {hz} Hz")
        time.sleep(1.0)

    print()
    if not rates:
        print("no successful reads")
        return 1
    fired = [(t, h) for (t, h) in rates if t > 0]
    sustained = [h for (t, h) in rates if t > 10]  # more than a lone stray tick
    print(f"  verified cold starts: {len(rates)}   ({failures} not counted)")
    print(f"  any tick at all:      {len(fired)}/{len(rates)} "
          f"({100.0*len(fired)/len(rates):.0f}%)")
    print(f"  sustained (>10 ticks):{len(sustained)}/{len(rates)} "
          f"({100.0*len(sustained)/len(rates):.0f}%)")
    if sustained:
        print(f"  when sustained:       min={min(sustained)} max={max(sustained)} "
              f"median={int(statistics.median(sustained))} Hz")
    if fired and not sustained:
        print("  every firing was a lone tick — delivery starts and stops")
    print("\n  A single run proves nothing here. Compare distributions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
