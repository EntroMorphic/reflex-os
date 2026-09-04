#!/usr/bin/env python3
"""Enforce: no telemetry emission while the loom lock is held.

goose_telem_* writes to the console through reflex_hal_write_raw, which spins
on a USB-JTAG FIFO one byte at a time with no bounded upper cost. The loom's
contention budget is LOOM_LOCK_TIMEOUT_US = 300us against a measured
steady-state peak hold in the hundreds of microseconds, so a console write
inside the lock is orders of magnitude over budget and drives the supervisor
pulse into LOOM_CONTENTION_FAULT.

The substrate's idiom is to snapshot names into locals inside the lock and emit
after unlocking. Two sites in fabric_alloc_internal did not, and the deviation
survived several passes of human review because it looks identical to the
correct pattern unless you track lock state while reading. That is what a
machine is for.

Method: brace-scope tracking. A naive "what was the most recent loom call"
scan does not work here and was tried first — fabric_alloc_internal releases
the lock inside an early-exit block whose text precedes the still-locked
region, so that scan reports the lock as released for the rest of the function
and misses both real defects. Held state is therefore saved on `{` and restored
on `}`: an unlock inside a nested block releases the lock for the remainder of
that block only, which is what an early return actually means.

Textual and deliberately narrow. It defends one pattern precisely and claims
nothing about any other ordering.

Usage: tools/check_lock_discipline.py   (or: make lock-check)
"""
import re
import subprocess
import sys

LOCK = re.compile(r'\bgoose_loom_try_lock\s*\(')
UNLOCK = re.compile(r'\bgoose_loom_unlock\s*\(')
TELEM = re.compile(r'\bgoose_telem_\w+\s*\(')


def clean(line, in_block):
    """Strip comments and string literals so braces and calls inside them
    cannot move the scope depth or raise a false hit."""
    out, i, n = [], 0, len(line)
    while i < n:
        if in_block:
            end = line.find('*/', i)
            if end == -1:
                return ''.join(out), True
            i, in_block = end + 2, False
            continue
        if line.startswith('/*', i):
            in_block = True; i += 2; continue
        if line.startswith('//', i):
            break
        if line[i] in '"\'':
            quote, i = line[i], i + 1
            while i < n and line[i] != quote:
                i += 2 if line[i] == '\\' else 1
            i += 1
            continue
        out.append(line[i]); i += 1
    return ''.join(out), in_block


def scan(path):
    violations = []
    held, depth, stack, in_block = False, 0, [], False
    for num, raw in enumerate(open(path, encoding='utf-8'), 1):
        code, in_block = clean(raw, in_block)

        # Resolve ordering within the line: an emission after the unlock on the
        # same line is released, one before it is not.
        t = TELEM.search(code)
        u = UNLOCK.search(code)
        l = LOCK.search(code)
        if t and held and not (u and u.start() < t.start()):
            violations.append((path, num, raw.strip()))
        if l:
            held = True
        elif u:
            held = False

        for ch in code:
            if ch == '{':
                stack.append(held); depth += 1
            elif ch == '}':
                depth -= 1
                held = stack.pop() if stack else False
                if depth <= 0:          # left the function: reset
                    depth, held, stack = 0, False, []
    return violations


def main():
    files = subprocess.check_output(
        ['git', 'ls-files', 'components/*.c', 'components/**/*.c',
         'core/*.c', 'vm/*.c', 'services/*.c']
    ).decode().split()
    found = []
    for f in files:
        found += scan(f)

    if found:
        print("Telemetry emitted while the loom lock is held:\n")
        for path, num, text in found:
            print(f"  {path}:{num}\n      {text}")
        print("\nSnapshot into a local inside the lock and emit after "
              "goose_loom_unlock(), as internal_process_transitions does.")
        return 1

    print(f"Lock discipline: {len(files)} files, no telemetry inside the loom hold.")
    return 0


if __name__ == '__main__':
    sys.exit(main())
