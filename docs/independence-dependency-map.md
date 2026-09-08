# ESP-IDF Independence: Dependency Map

**This is the single source of truth for independence status.** What Reflex OS
actually needs ESP-IDF for, measured from the linked image rather than reasoned
from the source tree, and in what order those needs can be retired.

Supersedes the tier discussion in
[`prd-full-independence.md`](prd-full-independence.md) and
[`v3-independence-plan.md`](v3-independence-plan.md) for questions of *status*.
Those remain the record of the abstraction migration and its reasoning; where
they disagree with this document about what is done, this one carries the
evidence.

**Two tier schemes exist and they answer different questions. Do not conflate
them.**

- `v3-independence-plan.md` Tier A/B/C is the **abstraction migration**: Types,
  HAL, Tasks, KV, then Radio and Crypto, then Drivers and LP. It asks *does
  Reflex code call ESP-IDF directly.* It is essentially complete —
  `vm/`, `core/`, `storage/`, `services/` and `drivers/` carry zero ESP-IDF
  includes, and `components/goose/` carries one.
- This document's Tier A1/A2/B/C/D/E/F is the **dependency map**: it asks *what
  does ESP-IDF still provide as scaffolding*, which is the harder question and
  the one that decides independence. Section 1 says why: counted as API calls
  the surface is small; counted as scaffolding it is the whole build.

**The status table below is generated, not remembered.** Run
`make independence` for the current measurement and `make independence-check`
for the ratchet, which CI enforces. Prose drifts — this file had drifted three
ways by 2026-09-07, each corrected in place and noted.

**Method.** Every claim below comes from one of: the link map of a real build,
`nm` over the archives that build linked, or the ESP-IDF headers as the
toolchain preprocesses them. Two configurations were built for the comparison —
the default ESP-NOW build and the 802.15.4 + `CONFIG_REFLEX_KERNEL_SCHEDULER`
build, which is the configuration on the independence path. Nothing here rests
on reading a `#include` list.

---

## 1. The shape of the dependency

Counted as API calls, the ESP-IDF surface is small. Counted as scaffolding, it
is the whole build. The distinction matters because rewriting drivers does not
touch the second kind.

| Tier | What | Status |
|---|---|---|
| **A1** | 47 register constants from `soc/*` headers | **Owned.** Generated from the SVD, proved against ESP-IDF |
| **A2** | 14 mask-ROM entry points | **Owned.** Declared by Reflex, addresses verified |
| **B** | Deep sleep entry; heap reporting | Sleep constants owned; entry still borrowed. Heap belongs to F |
| **C** | FreeRTOS as the scheduler | C0-C2 done, **tick proven on hardware at 1000 Hz**. C3 written and linked, blocked on nothing starting the scheduler and on `mtvec` ownership. See §5 |
| **D** | Radio (802.15.4 or ESP-NOW) | 802.15.4 is one component and creates no tasks |
| **E** | Console RX, peripheral drivers | TX already owned; the USB-JTAG register constants are now Reflex's own. **RX design settled, ISR not yet written** — see below |
| **F** | Build system, startup, heap, linker/memory layout, image format | Untouched. The real dependency |

Tier A is retired in commits `4548445` and `8d707d1`; Tier B is partly retired
in `aff2784` (sleep constants owned, entry still borrowed). What follows is
about what is left.

**Measured 2026-09-07 at `641263b`,** on the independence path (ESP32-C6 with
`CONFIG_REFLEX_RADIO_802154`) by `tools/check_independence.py`:

| Tier | Remaining ESP-IDF includes |
|---|---|
| A | **0 — clear** |
| B | 2 (`esp_sleep.h`, `esp_heap_caps.h`) |
| C | 8 (`freertos/*`, `esp_intr_alloc.h`) |
| D | 1 (`esp_ieee802154.h`) |
| E | 8 (`driver/*` — LEDC, PCNT, RMT, UART, USB-JTAG) |
| F | 4 (`nvs*`, `sdkconfig.h`, `esp_system.h`) |
| **total** | **23** |

A further 19 sit off-path — the classic-ESP32 backend, the Wi-Fi stack, the
ESP-NOW radio — deliberately borrowed and reported but not ratcheted, so that
"off-path" stays a decision rather than a hiding place.

Every one of the 23 is in a platform backend, the kernel's FreeRTOS shims, or
the shell's peripheral and console drivers. **None is in the substrate.**

`CONFIG_REFLEX_KERNEL_SCHEDULER` carries `depends on IDF_TARGET_ESP32C6` as of
`641263b`: the kernel sources are added to the build by
`platform/esp32c6/CMakeLists.txt` and by nothing else, so the independence path
is C6-only by construction rather than by convention.

---

## 2. What 802.15.4 mode already buys

The two configurations differ by **thirteen archives**, and they are not small
ones:

```
dropped: libesp_wifi libnet80211 libpp libphy libmesh liblwip libesp_netif
         libesp_event libespnow libwpa_supplicant libmbedtls libmbedx509 libnet
added:   libieee802154 libesp_hal_ieee802154 libbtbb
```

64 linked archives become 54. This is the "blob-free" claim in the README, and
it holds — but it is a claim about *binary blobs*, which is a different and
more modest achievement than independence from ESP-IDF.

---

## 3. Where the scheduler coupling actually is

The standing assumption — recorded in `reflex_task_kernel.c`'s own header — is
that "FreeRTOS remains as the task management backend because ESP-IDF drivers
(WiFi, USB-JTAG, ESP-NOW) create internal FreeRTOS tasks that require its API."

It is true, but not for the stated reason, and the difference decides the
order of work. In the 802.15.4 build, exactly four linked archives reference
`xTaskCreate*`:

| Archive | Why it is there |
|---|---|
| `libesp32c6.a` | **Reflex's own** platform component |
| `libfreertos.a` | FreeRTOS itself (idle and timer tasks) |
| `libesp_timer.a` | pulled in by `esp_phy`, `esp_coex` and `esp_hw_support` |
| `libpthread.a` | force-linked by ESP-IDF via `-u pthread_include_pthread_impl` |

### The protocol stack is scheduler-light; its PHY support is not

`libieee802154.a` and `libesp_hal_ieee802154.a` create **no tasks and use no
queues or semaphores**. `libieee802154.a` needs exactly two FreeRTOS symbols,
`vPortEnterCritical` and `vPortExitCritical` — critical sections, which any
scheduler must provide and which `reflex_sched.h` already does.

But the radio does not arrive alone. `libesp_phy.a` and `libesp_coex.a` come
with it, and they reference `esp_timer_create` and `ets_timer_setfn`, which
pulls in `libesp_timer.a` — and `esp_timer` **runs a task**: `s_timer_task`,
`init_timer_task` and `deinit_timer_task` are all present in the linked image.

So the radio, taken as the set of components it actually drags in, does
transitively require task creation. The coupling is real but narrow: it runs
through **one component, `esp_timer`**, rather than through the driver set. A
Reflex-owned scheduler that can host `esp_timer`'s task satisfies it.

> An earlier revision of this document claimed `esp_timer` and `pthread` were
> pulled in by newlib and concluded that Tier C was not gated on Tier D at all.
> That was read from the wrong lines of the link map. The referencing objects
> are `esp_phy/phy_common.c`, `esp_coex/esp_coex_adapter.c` and
> `esp_hw_support/sleep_modes.c`. `pthread` is the one that is not a real
> dependency — ESP-IDF force-links it with an undefined-symbol flag, which
> makes it a build-system artifact and therefore Tier F.

### The console coupling is heavier than "queues"

`esp_driver_usb_serial_jtag` and `vfs` need queues, mutexes **and** the task
timeout helpers (`vTaskSetTimeOutState`, `xTaskCheckForTimeOut`).

`esp_ringbuf` is heavier still: it reaches `vTaskPlaceOnEventList` and
`xTaskRemoveFromEventList`, which are FreeRTOS *internals* rather than any
portable scheduler contract. Reimplementing those against a different scheduler
is not a port, it is a reimplementation of FreeRTOS's blocking model. It is
worth noting that `esp_ringbuf` arrives through the `esp_timer`/`esp_coex`/
`esp_phy` chain rather than through the console, so owning console RX does not
by itself remove it.

The practical consequence: keeping ESP-IDF's console driver while replacing
the scheduler is the expensive path. Owning console RX (Tier E) is cheaper
than emulating FreeRTOS internals underneath a driver.

---

## 4. The scheduler is already written, and already dead

`CONFIG_REFLEX_KERNEL_SCHEDULER=y` is named after a scheduler the image does
not contain.

`reflex_sched.c` compiles into `libesp32c6.a` and the linker discards it:
`reflex_sched_init` does not appear in the ELF. Nothing references it. Its only
callers were `reflex_startup.c`, `reflex_trap.c` and `reflex_kernel_test.c` —
and until this commit **none of those were in any CMakeLists**, so they had
never been through a compiler at all.

Six of ten files under `kernel/` were in no build. Compiled for the first time
with the real toolchain at `-Wall -Wextra`, all six are **clean** — better than
the `goose_weave_loom` precedent, where code that had never been linked turned
out to have been "verified by inspection only". Two of the six are not code:

- `reflex_app_startup.c` — 21 lines of architecture notes, compiles to an empty object
- `reflex_freertos_shim.c` — a deprecation tombstone that says so itself

The four real ones — `reflex_sched.c`, `reflex_startup.c`, `reflex_trap.c`,
`reflex_vectors.S` — plus `reflex_kernel_test.c` are now all in the
kernel-scheduler build: compiled, discarded by the linker, and no longer able
to rot silently. Each defines one `reflex_`-prefixed symbol in `.text` with no
fixed placement, so they cannot collide with ESP-IDF's startup or vectors.
`reflex_vectors.S` also carried `.global reflex_context_switch` for a routine
it never defined and nothing referenced; that has been removed rather than left
as an undefined entry in the symbol table.

**What "unchanged" does and does not mean.** No symbol from these files appears
in the linked image — verified by `nm` for all five. The image is *not*
byte-identical, and cannot be: building the same configuration twice from the
same sources produces different bytes, so ESP-IDF's build is not reproducible
here and byte equality is not a test anything could pass. Building the
configuration with and without these files gives images of different size,
differing by 8 bytes in `.flash.rodata` with every section otherwise identical,
no symbol changing size, and the same set of linked objects — layout padding,
not content. The claim worth making is the one that was measured: **no code
from these files reaches the image.**

### What the Reflex scheduler is missing

`reflex_task.h` is a twelve-function contract. `reflex_sched.h` implements
eight of them and has **no queue primitive at all**:

| Needed by `reflex_task.h` | In `reflex_sched.h`? |
|---|---|
| create / delete / delay / yield / tick | yes |
| enter / exit critical | yes |
| `reflex_queue_create` / `send` / `recv` | **no** |
| `reflex_task_set_priority` / `get_priority` | no |
| `reflex_task_get_by_name` | no |

Queues have two consumers in Reflex — `core/fabric.c` and `core/event_bus.c` —
and are also what ESP-IDF's USB-JTAG driver and `vfs` require. They are the
single load-bearing gap.

---

## 5. Order of work

**C0 — make what exists reachable.** *Done in this commit.* The four orphaned
kernel files are in the build. They compile clean; they are still discarded.
This converts six files of assumption into evidence and costs nothing in the
image.

**C1 — queues.** *Done.* `kernel/reflex_kqueue.c` is the ring: fixed capacity,
allocation-free after creation, and with no dependency on the scheduler, which
is what makes it testable on a host at all. `reflex_sched.c` adds the blocking
layer on top — `reflex_sched_queue_send` and `reflex_sched_queue_recv`, which
park a caller until a peer acts or a deadline passes.

The split is deliberate and follows `reflex_log_format.c`: the part where ring
buffers actually fail — wraparound, and the `head == tail` ambiguity that reads
as both empty and full — is exercised by the host suite on every commit rather
than only on hardware. It is checked against a reference FIFO over 20,000
randomised operations, compared after *every* step rather than at the end, on
the same reasoning as `test_lattice.c`: a ring that diverges and re-converges
would pass a final-state comparison. Seven mutations were introduced to confirm
the tests fail when the code is wrong — off-by-one on the full check, head and
tail not wrapping, receiving from the wrong index, a dropped decrement, the
overflow guard removed, and destroy not releasing its pool slot. All seven were
caught. The host suite went from 355 tests to 401.

Two notes on the design. The name is `reflex_kqueue_*`, not `reflex_queue_*`,
because `reflex_task.h` already declares `reflex_queue_create` with a different
return type — the same symbol twice would be a one-definition-rule violation
that only appears once both land in one image. And an untimed wait parks with
`wake_tick = UINT32_MAX` rather than 0, because `pick_next` promotes any
BLOCKED task whose `wake_tick` has passed: a zero there would be woken on the
very next scheduling decision and spin.

Red-teaming C1 turned up three defects, two of them in the code that had just
been written and one introduced by the fix for the first.

`(timeout_ms * REFLEX_SCHED_TICK_HZ) / 1000` overflows a uint32 above
4,294,967 ms, and the wrapped value is *shorter* than requested — a timeout of
83 minutes fired after 12. Worse than a capped wait, because the caller
believes it waited. `reflex_sched_delay_ms` carried the same expression and had
since it was written.

`s_tick_count >= wake_tick` is wrong across the counter wrap. At 1000 Hz that
is every 49.7 days, and a task whose deadline landed just before it compares a
small `now` against a huge `deadline` and is never woken — a sleep that
silently becomes permanent.

Both are now `reflex_sched_ms_to_ticks` and `reflex_sched_tick_reached`,
extracted rather than fixed in place because neither was reachable from a host
test while it lived inside the scheduler.

The third was mine, made while fixing the second. Requiring `blocked_on == NULL`
before the deadline sweep would wake a task — reasoning that queue waiters are
woken by peers — quietly removed the timeout from a *timed* queue wait, which
could then only ever end if a peer acted. "No deadline" also cannot be encoded
as a `wake_tick` sentinel: under the wrap-safe comparison `UINT32_MAX` reads as
already expired. Three states need distinguishing — sleeping on a deadline,
waiting on a queue with one, waiting on a queue without one — so the deadline
now carries its own validity flag, and the sweep's decision is
`reflex_sched_should_time_wake`, extracted for the same reason as the other two
and tested against exactly that regression.

Ten mutations across the queue and the tick arithmetic; all but one killed. The
survivor is the saturation clamp in `ms_to_ticks`, which is genuinely
unreachable at 1000 Hz because the conversion is the identity there. It is kept
as defence against a tick-rate change and guarded by a `_Static_assert` that
fires above 1000 Hz, with the reason recorded in both the code and the test so
it is not later removed as dead.

What C1 does *not* do is change who implements `reflex_task.h`.
`reflex_task_kernel.c` still delegates to FreeRTOS. That cutover is C3.

**C2 — startup, trap, vectors.** *Reviewed, defects fixed, gap specified. Not
running.* These files had never been executed, so the first task was reading
them, and that turned up more than expected.

The scheduler's central decision — which task runs next — had never been
tested, because it lived inside `pick_next`, which is not compiled on the host.
It is now `reflex_sched_select`: highest priority wins, ties broken by a scan
that starts after the current task, which is what makes it round-robin instead
of always returning the lowest slot. Nineteen assertions and four mutations,
including one that only a negative start index catches.

Three defects were fixed. `reflex_trap_handler`'s exception arm was empty and
fell through to `return frame`, which restores the context and executes `mret`
with `mepc` still pointing at the faulting instruction — so any fault became a
silent infinite trap loop with no output. A stubbed exception handler that
returns is worse than none; it now reports `mcause`, `mepc` and `mtval` and
halts. `reflex_kernel_startup` discarded the return of `reflex_sched_init`,
`create_task` and `start`, so a failed task creation produced a scheduler with
nothing to run, which looks exactly like a hang; and its own comment claimed a
BSS clear as step 1 that the code does not do and should not. The same
discarded-return pattern in `reflex_kernel_test.c` is fixed too.

### Taking C2 to the tick (2026-09-04, on hardware)

The routing item turned out to be already written and unused.
`reflex_hal_intr_alloc` does the interrupt-matrix mapping, the PLIC priority
and the `mie` bit, and registers through the ROM vector table via
`intr_handler_set` — so it coexists with ESP-IDF's `mtvec` and needs no
takeover. Nothing in the firmware called it. `reflex_sched_tick_start` and
`_stop` now do: the tick is routed by Reflex's own plumbing rather than by
`esp_intr_alloc`, which is the whole distinction C2 was about.

**The interrupt source number was wrong.** `reflex_kernel_test.c` hardcoded 38.
`ETS_SYSTIMER_TARGET1_INTR_SOURCE` is **58** on the C6 — 38 is a different
peripheral, so routing it would have mapped some other device's interrupt as
the scheduler tick, and the tick would never have fired. Never caught because
the file was in no build. The number now lives in the SVD-backed header where
`make soc-bridge` proves it against ESP-IDF's own enum.

**The tick is written and routed but not proven to fire**, because of what
running the configuration actually revealed:

> ~~`CONFIG_REFLEX_KERNEL_SCHEDULER=y` **panics at boot.**~~ **Fixed 2026-09-07.**
> The panic was not a stack-size problem and not in the scheduler at all. The
> stack dump's repeating seven-frame pattern was unbounded recursion inside the
> 802.15.4 radio backend:
>
> ```
> esp_ieee802154_transmit_failed      platform/esp32c6/reflex_radio_802154.c
>   -> esp_ieee802154_receive
>     -> ieee802154_receive -> rx_init -> stop_current_operation
>       -> ieee802154_ll_clear_events -> ieee802154_inner_transmit_failed
>         -> esp_ieee802154_transmit_failed        (back to the start)
> ```
>
> Both TX callbacks called `esp_ieee802154_receive()` to put the radio back
> into RX, and the driver signals transmit-failed from inside its own event
> dispatch — so re-entering the driver from that callback made it signal again.
> The driver already returns to RX by itself: `next_operation()` calls
> `enable_rx()` when the `rx_when_idle` PIB flag is set, and that flag defaults
> to false, which is exactly what the manual re-arm was compensating for.
> `esp_ieee802154_set_rx_when_idle(true)` at init removes the re-entrancy
> rather than guarding it. Cost nothing at compile time, which is why CI built
> this configuration green for its entire existence without running it.

**The configuration now boots**, the shell is reachable, and as of 2026-09-07
**the tick is delivered.** Three defects stood between "routed" and "firing",
and none of them was visible to any gate this repository has:

1. **The comparator was never enabled.** `setup_systimer_tick` wrote
   `SYSTIMER_CONF |= 1 << 25`, but `TARGET1_WORK_EN` is **bit 23** —
   `1 << (24 - alarm_id)` in ESP-IDF's own `systimer_ll_enable_alarm`. Bit 25 is
   `TIMER_UNIT1_CORE1_STALL_EN`, which does nothing observable on a single-core
   part. Read off a board as `raw=0x00000000` with TARGET1 enabled in
   `SYSTIMER_INT_ENA`: routed the whole way to the CPU, and the peripheral never
   raised it.
2. **The comparator was armed before it was routed.** `reflex_sched_tick_start`
   called `setup_systimer_tick()` first, so TARGET1 asserted while its
   interrupt-matrix entry still held its reset value of 0 — onto CPU line 0
   rather than the line about to be claimed. Level-triggered, so it stayed
   asserted and the core stopped making progress: a task watchdog timeout with
   the idle task starved. Routing now happens first; a routed line with an
   unarmed source is inert, an armed source with no routing is not.
3. **The vector was installed after the line was made deliverable.**
   `reflex_hal_intr_alloc` enabled the PLIC line and re-enabled interrupts
   *before* calling `intr_handler_set`. Everything that can be set up while the
   line is masked now is, and the PLIC enable is last.

A routing readback was added to `kernel tick` for this, and is kept: it reports
the interrupt-matrix entry, PLIC enable/priority/threshold/type, the `mie` and
`mstatus.MIE` bits, and the SYSTIMER enable/raw/status registers, all read back
live rather than remembered. "Routed but not firing" names four possible causes
and distinguishes none; those registers distinguish all of them. The first
version of that readback ran *after* `reflex_sched_tick_stop()` and therefore
described the teardown — corrected, and the comment says so.

**The tick works. Verified: 5/5 and 6/6 cold starts at 1000-1001 Hz against a
1000 Hz target**, on the default build, measured with `make tick-measure`.

An earlier baseline in this file said the tick fired on 1 of 8 cold starts. That
was wrong, and the reason is worth more than the number: **opening a C6's
USB-JTAG port does not reset the board.** Checked directly, six consecutive
opens read uptime 925.9, 934.5, 943.1, 951.7, 960.4 and 969.0 seconds —
monotonic, no reset. The harness had assumed otherwise, so every "cold start" it
reported was a re-arming on a continuously running board. It now reboots through
the shell and *verifies* coldness by reading uptime back before arming, and
refuses to count a run it cannot confirm.

**The real defect is re-arming, and it is precisely isolated.** On one boot:

```
arming 1:  500 ticks in 499193 us -> 1001 Hz
arming 2:    1 tick
arming 3:    1 tick
arming 4:    1 tick
arming 5:    0 ticks
```

The readback now prints on success as well as failure — printing only on
failure meant the working case was never observed, and a claim about it went
into the record unmeasured. Comparing a working arming against a failing one on
the same boot:

| | arming 1 (1000 Hz) | armings 2-3 (0 ticks) |
|---|---|---|
| `intmtx` | `src=58 -> cpu_int=10` | identical |
| `plic` | `enabled=1 pri=2 thresh=1 level=1` | identical |
| `csr` | `mie=1 mip_pending=0 mstatus.MIE=1` | identical |
| `systimer` | `raw=0x0 st=0x0` | **`raw=0x2 st=0x2`** |

**Every controller register is identical. The only difference is that the
SYSTIMER source is stuck asserted in the failing case** — it fired and was never
acknowledged, because no ISR ran to acknowledge it.

That also corrects a claim made when the threshold-derived priority landed: it
was said that a cold arming "happens to run at `thresh=0`". It does not. The
threshold is 1 in both the working and failing cases, so that change is
correctness and not an explanation.

The `mip` reading needed fixing too. It was originally taken with traps
unmasked, where a forwarded interrupt is taken before the read can see it — so
`mip=0` was tautological rather than decisive. It is now read with
`mstatus.MIE` cleared, which makes `0` genuinely mean "not forwarded":

```
intmtx: src=58 -> cpu_int=10
plic:   enabled=1 pri=1 thresh=1 level=1
csr:    mie_bit=1 mip_pending=0 mstatus.MIE=1
systimer: ena=0x00000007 raw=0x00000002 st=0x00000002 (TARGET1=bit1)
```

**`mip_pending=0` with traps masked, while the source is asserting and
latched.** The core does not see the interrupt, so nothing downstream is masking
it — the trap path is irrelevant to this failure. The controller is not
forwarding an asserted, routed, enabled line whose every visible register
matches a line that works.

That narrows it to the forwarding stage and rules out most of what was
previously suspected. Two candidates remain and neither is currently
observable: an in-service or claim state latched by the first tick and never
released, and something in the interrupt-matrix or controller state that
`reflex_hal_intr_free` does not restore. Neither has a register in the readback
yet; extending it there is the next step.

Five hypotheses have been tested on hardware and all five rejected as the
cause: PLIC-aware line allocation (broke the working case outright), reordering
the arming sequence (dropped a working arming to one tick), clearing the PLIC
pending bit on the alloc path, and priority derived from the live threshold —
tested twice, once on the 802.15.4 build where the tick has never fired, and
again on the default build where `pri=2` over `thresh=1` still delivered nothing
on a re-arm.

**There are two distinct tick failures. They are not the same bug and have been
conflated more than once, including in a fix that was tested against the wrong
one:**

| | default (ESP-NOW) build | **802.15.4 (independence path)** |
|---|---|---|
| cold arming | works — 1000 Hz, 5/5 and 6/6 | **works — 1000-1001 Hz, 6/6** |
| re-arming | fails: one tick or none | fails: one tick |

**TICK-B is closed.** The Reflex-routed scheduler tick fires at 1000 Hz on the
independence configuration — ESP32-C6 with `CONFIG_REFLEX_RADIO_802154` and
`CONFIG_REFLEX_KERNEL_SCHEDULER` — through Reflex's own interrupt matrix, PLIC,
`mie` and ROM vector programming, with no `esp_intr_alloc` anywhere in the path.
Six verified cold starts, 6/6, 1000-1001 Hz.

It was never a separate defect. Earlier it appeared to be one because the test
that should have shown the threshold-derived priority working was run against a
build that *also* carried PLIC-aware line allocation, which moves off CPU line
10 and breaks delivery outright. One change masked the other, and the conclusion
drawn — that the priority fix did nothing — was wrong. Two variables, one
experiment.

**TICK-A remains**, on both builds: the first arming after a cold boot runs at
1000 Hz and every subsequent arming delivers one tick. It does not block the
scheduler, which arms once at start and never re-arms, so it is a correctness
debt rather than a gate on Tier C. Bisection has exonerated interrupt-line
management (claiming the line once and keeping it changes nothing) and left
`setup_systimer_tick` and the SYSTIMER teardown.

**Bisected 2026-09-07: the fault is on the comparator side, not the interrupt
line.** `reflex_sched_tick_stop` released the CPU interrupt line on every stop
and `tick_start` re-claimed it, so the allocate/free cycle was the obvious
suspect. Claiming the line once and keeping it across arm/disarm — leaving only
the SYSTIMER comparator to start and stop — changes nothing: arming 1 still runs
at 1000 Hz and armings 2 through 6 still deliver exactly one tick each. That
change was reverted, because its whole justification was the hypothesis it
disproved.

So `reflex_hal_intr_alloc` and `reflex_hal_intr_free` are exonerated, and what
remains is `setup_systimer_tick` and the SYSTIMER teardown. The symptom is now
sharper too: with the line held, every re-arming delivers exactly **one** tick
rather than zero or one. One tick means the comparator fires and does not
reload — in period mode it should re-arm itself — which points at
`SYSTIMER_TARGET1_CONF` or the `COMP1_LOAD` latch not taking effect on a second
arming.

Two of those changes were kept, each on its own merits and neither as a fix,
both labelled as such in the code: `reflex_hal_intr_free` now clears
`PLIC_MXINT_CLEAR` for the line it releases, because handing back an asserting
line leaves the next owner an interrupt it cannot acknowledge; and priority now
derives from the live threshold, because the controller forwards only *above*
it and `pri=1` against `thresh=1` violates that rule even though a cold arming
happens to run at `thresh=0`. Cold starts remain 5/5 at 1000-1001 Hz with both
in place.

That is the next question, and it is a good one: a first arming works perfectly
and every subsequent one does not, with no observable difference in the
hardware state that has been looked at so far. The readback needs extending —
the PLIC in-service/claim registers are the obvious gap.

The earlier blocker, for the record:The earlier blocker, for the record:The earlier blocker, for the record:The earlier blocker, for the record:

```
reflex> kernel tick
kernel tick: 0 ticks in 490684 us -> 0 Hz (target 1000)
kernel tick: no ticks — routed but not firing
```

That was a far better blocker than a panic: `make hw-test`
reports **171/171** on this configuration, the 802.15.4 radio transmits with
`aura_fail=0`, and dropping the Wi-Fi stack frees **120 KB of RAM** (heap free
289,748 against 169,880 on the ESP-NOW build). What stands in front of the tick
is now a single measurable question — the interrupt is routed and does not
fire — rather than a device that never reaches its own shell.

### Tier E: what owning console RX actually requires (2026-09-07)

"RX needs a queue primitive" understated it. C1 built the queue, and that is
necessary but not sufficient — the design is now settled and the constraints
are measured rather than assumed:

- **The ESP-IDF driver cannot be left installed.** It owns the OUT-endpoint
  FIFO once `usb_serial_jtag_driver_install` runs, so reading `EP1` directly
  alongside it races the driver for the same bytes. Owning RX means not
  installing it.
- **Polling cannot replace it.** The shell idles with `reflex_task_delay_ms(50)`
  when no byte is waiting. At 115200 baud a 64-byte FIFO fills in about 5.5ms,
  so a line arriving during an idle window overruns it several times over
  before the shell looks. This is not a prediction: it is what the CHANGELOG
  records happening before the driver was installed, and it is why the driver
  was installed.
- **So RX must be interrupt-driven into a ring**, which is now possible because
  `reflex_hal_intr_alloc` works — the same path the scheduler tick is measured
  on at 1000 Hz. Source 39, `SERIAL_OUT_RECV_PKT` (bit 2 of INT_RAW/ST/ENA/CLR),
  with `SERIAL_OUT_EP_DATA_AVAIL` (bit 2 of EP1_CONF) gating the drain.
- **stdio TX must move too.** `printf` reaches the wire through the VFS driver
  path, not through `reflex_hal_write_raw`'s direct register writes, so
  dropping the driver means routing stdio at the same time. That is why the
  Tier E include count does not fall until both halves land.

**Attempted on hardware, and it does not work as scoped — the reason is
structural, not a bug list.** Branch `tier-e-console-rx` carries the attempt.

Owning receive means applying backpressure: the ISR must stop draining the FIFO
when its ring is full, because emptying the FIFO is what ACKs the USB packet
and a receiver that always drains never slows the host down. Measured without
it: `console: 962 byte(s) DROPPED` on a 913-character line.

But backpressure on receive deadlocks against a transmit path that blocks.
`usj_write_bytes` spins until the host drains the IN endpoint, and a host
driving the shell writes a whole line before it reads. So: the device stops
ACKing OUT because its ring is full, the host blocks mid-write and therefore
never reads, the device's IN endpoint fills, and the shell blocks in its echo
and never calls the read that would re-arm the receiver. Both ends wait for the
other. Observed as a console that serves short commands, passes 87 of 171
checks, and then stops answering entirely.

The ESP-IDF driver this replaces does not avoid that by being cleverer about
receive. It avoids it by buffering **both** directions, so neither side blocks
long enough to close the loop.

So Tier E's console work is one change, not two: **receive and transmit have to
move together.** That was written above as a sequencing note — "stdio TX must
move too" — and it is stronger than that. RX alone is not a smaller first step;
it is a step that cannot stand.

What the attempt established and is worth keeping: the ISR, ring and
flow-control shape are right; `reflex_hal_intr_set_enabled` masks at the
controller rather than at the peripheral's shared INT_ENA register, which is
necessary because that register also carries the transmit interrupt ESP-IDF
uses and writing it from an ISR wedges stdout — seen as a board emitting a
single byte, `I`, and going silent.

**Done:** the register constants are Reflex's own. The SVD calls the peripheral
`USB_DEVICE` where IDF calls it `USB_SERIAL_JTAG` — the mapping table records
exactly that kind of divergence — and the C6 HAL's hand-written `USJ_BASE +
0x00 / + 0x04` literals now come from the generated header instead, so
`make soc-bridge` can prove them. The header went from 47 to 57 constants, each
`_Static_assert`ed against the ESP-IDF macro it replaces.

**Not done:** the ISR, the ring, and the stdio move.

### What C3 owes, measured 2026-09-07

With the tick proven, C3's blockers were re-read rather than assumed, and the
first one was a live defect rather than a missing call:

1. **`reflex_sched_start` armed the tick without routing it.** *Fixed.* It
   called `setup_systimer_tick()` directly — enabling the comparator and the
   peripheral interrupt and stopping there, with nothing mapping the source
   through the interrupt matrix. That is the exact hazard that stops the core:
   SYSTIMER_TARGET1's matrix entry holds its reset value of 0, so the
   comparator asserts onto CPU line 0, level-triggered, and nothing makes
   progress. Starting the scheduler would have hit it on the first tick. It now
   calls `reflex_sched_tick_start`, so the scheduler's tick and the
   diagnostic's tick are the same measured code path.

2. **The stack switch in `reflex_sched_start` is undefined behaviour.** Not
   fixed; the file's own `@warning` states the mechanism. Assigning `sp` with
   inline asm part-way through a C function leaves the compiler believing it
   owns the frame. Correct-by-construction means an assembly trampoline that
   sets `sp` and enters the task without ever returning into a C frame built on
   the old stack. It cannot be validated until the scheduler starts, which is
   blocked on (3).

3. **`mtvec` ownership, now quantified.** Taking the trap vector means
   servicing every interrupt line live at that moment, and that is not one:

   ```
   reflex> kernel tick
     plic live mask=0x0b001f24 (10 lines, incl. this one)
   ```

   **Nine of those ten belong to ESP-IDF** — lines 2, 5, 8, 9, 11, 12, 24, 25
   and 27 on the 802.15.4 build; the tenth is Reflex's tick on line 10. A
   hand-off has to quiesce or re-home all nine: the console, the radio, the
   timer and whatever else is behind them. That is the concrete size of the
   job, and it is why the map has always said this means owning startup
   outright rather than patching a vector mid-boot.

4. **Nothing calls `reflex_sched_start`.** True, and the least of the four.

### What C2 still owes, precisely

There are two tick paths and only one works. `reflex_kernel_test.c` routes
SYSTIMER TARGET1 through `esp_intr_alloc` — ESP-IDF's allocator doing the
interrupt-matrix mapping, the PLIC priority and the `mie` bit — and its ISR
calls `reflex_sched_tick` directly. That works, and it is not independence.

The standalone path is incomplete in three specific ways:

1. `setup_systimer_tick` enables the interrupt at the SYSTIMER peripheral and
   stops. Nothing maps it through the interrupt matrix to a CPU line, sets a
   PLIC priority, or enables the matching `mie` bit.
2. Nothing tells the trap handler which CPU line the tick was mapped to.

   This item was described wrongly on first writing, and the correction is
   worth keeping rather than quietly editing. The claim was that
   `reflex_trap_handler` dispatched on "`mcause == 7`, the CLINT machine timer
   interrupt". That is the textbook RISC-V encoding and it is not what this
   chip does. The C6 uses a PLIC with `RV_EXTERNAL_INT_OFFSET == 0`, and
   ESP-IDF's own dispatcher states the consequence: "mcause contains the
   interrupt number that triggered the current interrupt"
   (`components/riscv/interrupt.c`). For an interrupt, mcause is the **CPU
   interrupt line the matrix was programmed to route to** — a number chosen
   when the peripheral is wired up, not an architectural constant.

   And 7 is not spare on this chip: for an *exception* ESP-IDF reads
   `mcause == 7` as a store access fault and `mcause == 5` as a load access
   fault (`riscv/rv_utils.h`). The constant named one thing, meant another, and
   collided with a third.

   So the tick line cannot be a compile-time constant. `reflex_trap_set_tick_line`
   now receives it from the routing step, and until that step calls it the
   handler claims no interrupt as the tick — deliberately, because guessing a
   line number services some other peripheral's interrupt as though it were the
   tick and leaves that peripheral asserted forever.
3. With no tick, `reflex_sched_start` runs its first task and parks on `wfi`
   the moment everything blocks, with nothing left to wake it.

And the reason none of this can simply be switched on: `reflex_kernel_startup`
writes `mtvec`, the trap vector for *every* interrupt and exception on the
core. ESP-IDF owns that register — its interrupt allocator, the FreeRTOS tick,
the console and the radio all arrive through the vector it installed.
Overwriting it mid-boot does not degrade gracefully; it redirects the entire
machine to a handler that services a scheduler tick and nothing else. Reaching
it means either owning startup outright or a deliberate hand-off that first
quiesces those peripherals.

That is register programming validated on a board. A misrouted interrupt does
not fail to build.

**C3 — cut the task backend over** from FreeRTOS delegation to `reflex_sched_*`.
*Written, compiled, linked, and off by default.*

`kernel/reflex_task_reflex.c` implements all thirteen functions of
`reflex_task.h` on `reflex_sched_*` and `reflex_kqueue_*`, with **no ESP-IDF
and no FreeRTOS** — it compiles against nothing but Reflex's own headers, which
is why `make warn-check` builds it on every commit without it being in any
firmware configuration. `CONFIG_REFLEX_TASK_BACKEND_REFLEX` selects it in place
of `reflex_task_kernel.c`; exactly one is compiled, since both define the same
symbols.

The whole firmware links with it. That is the milestone: `reflex_task.h`, the
interface the substrate and VM use for all concurrency, can be satisfied
entirely by Reflex code.

Three gaps in the scheduler had to be closed first — it had no lookup by name
and no priority accessors, so `reflex_task_get_by_name`, `set_priority` and
`get_priority` had nothing to call. `reflex_sched_find_index` skips FREE and
DEAD slots, because a slot keeps its name pointer after the task is gone and a
lookup would otherwise resolve a name to a corpse.

**It must stay off, for a blunter reason than the tick.** *Nothing starts the
scheduler.* `reflex_sched_start` is called only from `reflex_startup.c` and
`reflex_kernel_test.c`, and nothing calls either — `nm` on a build with the
option set shows `reflex_sched_create_task` linked and `reflex_sched_start`
absent. So `reflex_task_create` files a TCB into the scheduler's table and no
task ever runs. A board with this enabled boots ESP-IDF, creates tasks that
never execute, and does nothing.

Behind that sits the tick from C2. Both have to land, and both need hardware: a
scheduler that is wrong does not fail to build, it stops responding.

### Two deliberate differences from the FreeRTOS backend

Critical sections are global. FreeRTOS's `portMUX_TYPE` is a per-object
spinlock; `reflex_sched_enter_critical` disables interrupts for the core and
counts nesting. On the single-core C6 that gives callers the mutual exclusion
they rely on, but it is coarser — two unrelated critical sections serialise
against each other. The `reflex_mutex_t` is accepted and ignored rather than
quietly reinterpreted.

Return codes match the FreeRTOS backend exactly, including its asymmetry: a
failed send is `REFLEX_ERR_TIMEOUT` and a failed receive is
`REFLEX_ERR_NOT_FOUND`. That is not obviously right, but callers compare
against `REFLEX_OK`, and changing it here would make the two backends differ in
a way no test would catch.

**Then D, E, F.** FreeRTOS cannot leave the image while newlib pulls in
`esp_timer` and `pthread`, so its final removal is a Tier F matter. Tier E
matters to Tier C only through the queue primitive; console TX is already a
direct register write (`usj_write_bytes`), and only RX uses the driver.

---

## 6. Honest limits

- **Board-to-board 802.15.4 receive is not verified, and cannot be with the
  current harness.** Transmit is: `tx_discover` advances and `aura_fail` stays
  0. Receive is not, because observing it needs two C6s running undisturbed
  while a third party reads their counters — and opening a USB-JTAG port
  *resets* a C6, which both zeroes the counters and invalidates the file
  descriptor that was just opened. Every attempt to hold a port open and watch
  discovery destroyed the state it was measuring. The classic ESP32 does not
  have this problem (it is behind an external USB-serial bridge) but cannot
  speak 802.15.4, so it cannot be the second node.

  The change that prompted the question — `esp_ieee802154_set_rx_when_idle(true)`
  replacing a manual re-arm in the TX callbacks — can only *enable* the
  receiver: `next_operation()` calls `enable_rx()`, and `reflex_radio_init`
  still calls `esp_ieee802154_receive()` explicitly at startup. So the risk is
  understood and low. It is not the same as measured, and is recorded here as
  unmeasured rather than assumed.

  Concretely, closing this needs one of: a UART bridge wired to two spare C6
  GPIOs and used as the observation channel while USB-JTAG stays untouched;
  mesh counters persisted to NVS so they survive the reset a connection causes;
  or a third C6 in promiscuous mode acting as a passive sniffer, since the
  problem is only that *reading* a node disturbs it. The first is the least
  work and the most reusable.
  It blocks any future claim about mesh behaviour on the independence path.


- **The dependency analysis** is a property of builds and link maps — exactly
  the right evidence for a dependency question and exactly the wrong evidence
  for a scheduler that runs. The original text here said "no hardware was
  attached for any of this", which was true when written and is no longer: §5's
  tick work was done on a board, and the whole tree was flashed to two C6s and
  a classic dual-core ESP32 on 2026-09-07. Corrected rather than deleted,
  because the distinction it was drawing still holds — a link map cannot tell
  you whether a scheduler schedules.
- The newly-built kernel files compile clean, under the flags of the
  configuration that actually builds them. That is all it establishes. They
  have never been linked, never executed, and C2 should treat them as a
  reviewed draft rather than working code. An earlier check compiled them with
  the *default* build's flags rather than the kernel configuration's, which
  would have missed any breakage behind a config-conditional; it has been
  redone correctly.
- ESP-IDF's build is not byte-reproducible in this environment. Two builds of
  one configuration from identical sources differ. That is worth knowing before
  anyone tries to use image equality as evidence for anything.
- Tier F is unexamined here beyond naming its parts. The heap in particular is
  load-bearing in a way this document does not explore: six files call `malloc`.
