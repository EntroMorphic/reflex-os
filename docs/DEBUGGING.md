# Debugging Reflex OS

## Reading a Panic Dump

When the board crashes, you see:

```
Guru Meditation Error: Core  0 panic'ed (Store access fault). Exception was unhandled.

Core  0 register dump:
MEPC    : 0x42012abc  RA      : 0x42012a90  SP      : 0x4082f100
...
Stack memory:
4082f100: 0x00000000 0x4082f120 ...

Backtrace: 0x42012abc:0x4082f100 0x42012a90:0x4082f120 0x4200ff00:0x4082f140
```

### Key Fields

- **MEPC** — the instruction that faulted
- **RA** — return address (the caller)
- **Backtrace** — call chain (most recent first)

## Decoding the Backtrace

```bash
riscv32-esp-elf-addr2line -e build/reflex_os.elf 0x42012abc 0x42012a90 0x4200ff00
```

Output:
```
/path/to/reflex-os/components/goose/goose_runtime.c:587
/path/to/reflex-os/components/goose/goose_runtime.c:540
/path/to/reflex-os/kernel/reflex_freertos_compat.c:84
```

## Common Crash Types

### Stack Overflow
**Symptom:** `Stack canary watchpoint triggered` or random crashes in deep call chains.
**Cause:** Task stack too small for the call depth.
**Fix:** Increase stack size in `reflex_task_create` (try 4096 → 8192).

### NULL Dereference
**Symptom:** `Store access fault` or `Load access fault` at address 0x00000000.
**Cause:** Uninitialized pointer or failed allocation.
**Fix:** Check the MEPC line — that's where the NULL was dereferenced.

### Watchdog Timeout
**Symptom:** `Interrupt wdt timeout on CPU0`.
**Cause:** A task blocked interrupts too long, or an ISR handler isn't clearing its source.
**Fix:** Check if your code disables interrupts with csrci/portENTER_CRITICAL and forgot to re-enable.

### IRAM Placement Error
**Symptom:** `Cache disabled but cached memory region accessed` or silent hang during interrupt.
**Cause:** ISR code is in flash (0x4200xxxx) instead of IRAM (0x4080xxxx).
**Fix:** Add `__attribute__((section(".iram1")))` to ISR handlers.

### Flash Access During ISR
**Symptom:** Hang or crash when WiFi/radio is active (flash cache may be disabled).
**Cause:** ISR handler reads from flash-mapped memory while cache is off.
**Fix:** Move handler + all data it touches to IRAM/DRAM.

## Using idf.py Monitor

```bash
idf.py -p /dev/cu.usbmodem1101 monitor
```

This auto-decodes backtraces and colorizes output. Requires a TTY (won't work in non-interactive shells).

## Python SDK Health Check

```python
from reflex import ReflexNode

node = ReflexNode("/dev/cu.usbmodem1101")
print(node.services())    # All services started?
print(node.heartbeat())   # LP core alive?
print(node.purpose_get()) # Purpose restored?
node.close()
```

## USB-JTAG Recovery

If the board doesn't respond on serial:

1. Unplug for 60 seconds
2. Hold BOOT button while plugging in
3. Try `esptool.py --chip esp32c6 chip_id` — if it responds, flash normally
4. If still dead: use a USB-to-UART adapter on TX/RX header pins

## The board prints but ignores input

A different failure, and the steps above are the wrong tree for it. The board is
**healthy**: it emits a full boot log, prints `reflex>`, and keeps emitting
supervisor telemetry — it simply never answers anything you type, and
`make hw-test` reports `no shell response within 25s`.

This is **host-side USB-CDC state on the C6's native USB-serial-JTAG port**, not
the firmware. It appears after heavy flash-and-reset churn, and it is immune to
everything that looks like it should help: reflashing, `esptool` resets,
rebuilding, even reflashing a known-good image. That immunity is exactly what
makes the firmware look guilty.

**Recovery** — close and reopen the port with a short settle:

```python
import serial, time
ser = serial.Serial('/dev/cu.usbmodem11101', 115200, timeout=0.2)
time.sleep(2.0); ser.reset_input_buffer(); ser.write(b"\r"); time.sleep(1.0)
print(ser.read(ser.in_waiting or 1)); ser.close()
```

Then run `make hw-test PORT=…` normally. `tests/hardware/validate_shell.py`
opens the port once and polls for 25s, so it cannot rescue a port already in
this state — wake it first.

**The classic ESP32 ("the V3") is the control.** It reaches the console through
a CP210x bridge rather than native USB-serial-JTAG, so it is unaffected. *If the
V3 passes and both C6s are deaf on the same image, suspect the host, not the
substrate.* On 2026-09-11 that distinction cost six wrong hypotheses — boot-loop
protection, a bad image, USB re-enumeration, hand-toggled DTR/RTS, an
intervening `esptool chip_id`, and a stale process holding the port — and a
firmware change was briefly recorded as a suspected regression before both
boards came back at 183/0 on that same firmware.

Do not hand-toggle DTR/RTS from pyserial to force a reset. It is not the cure,
and it can put the port into this state.
