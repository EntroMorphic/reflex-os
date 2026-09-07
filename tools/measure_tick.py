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

Cold start is free here: opening a C6's USB-JTAG port resets the board. That
property wasted an hour of this session when it silently zeroed counters that
were being read; used deliberately it is exactly the reset this needs.

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


def one_cold_run(port, boot_wait):
    """Open (which resets the board), wait for boot, arm the tick once."""
    try:
        s = serial.Serial(port, 115200, timeout=2.0)
    except Exception as e:
        return None, f"open failed: {e}"
    try:
        time.sleep(boot_wait)
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
    m = RATE_RE.search(out)
    if not m:
        return None, "no rate line"
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))), None


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
            ticks, us, hz = res
            rates.append(hz)
            print(f"  run {i+1:2d}: {ticks:5d} ticks in {us} us -> {hz} Hz")
        time.sleep(1.0)

    print()
    if not rates:
        print("no successful reads")
        return 1
    fired = [r for r in rates if r > 0]
    print(f"  cold starts:      {len(rates)} read, {failures} unreadable")
    print(f"  tick fired:       {len(fired)}/{len(rates)} "
          f"({100.0*len(fired)/len(rates):.0f}%)")
    if fired:
        print(f"  when it fired:    min={min(fired)} max={max(fired)} "
              f"median={int(statistics.median(fired))} Hz")
    print("\n  A single run proves nothing here. Compare distributions.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
