# ESP-IDF Independence: Dependency Map

What Reflex OS actually needs ESP-IDF for, measured from the linked image rather
than reasoned from the source tree, and in what order those needs can be retired.

Companion to [`prd-full-independence.md`](prd-full-independence.md) and
[`v3-independence-plan.md`](v3-independence-plan.md). Where this document and
those disagree, this one carries its evidence.

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
| **A1** | 52 register constants from `soc/*` headers | **Owned.** Generated from the SVD, proved against ESP-IDF |
| **A2** | 14 mask-ROM entry points | **Owned.** Declared by Reflex, addresses verified |
| **B** | Deep sleep entry; heap reporting | Sleep constants owned; entry still borrowed. Heap belongs to F |
| **C** | FreeRTOS as the scheduler | Not started — but see §3, it is closer than it looks |
| **D** | Radio (802.15.4 or ESP-NOW) | 802.15.4 is one component and creates no tasks |
| **E** | Console RX, peripheral drivers | TX already owned; RX needs a queue primitive |
| **F** | Build system, startup, heap, linker/memory layout, image format | Untouched. The real dependency |

Tiers A and B are retired in commits `4548445`, `8d707d1` and `aff2784`. What
follows is about what is left.

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

What C1 does *not* do is change who implements `reflex_task.h`.
`reflex_task_kernel.c` still delegates to FreeRTOS. That cutover is C3.

**C2 — startup, trap, vectors.** The path that makes `reflex_sched_start`
actually run. `reflex_startup.c` already calls `reflex_sched_init`,
`create_task` and `start`; nothing calls `reflex_startup.c`.

**C3 — cut `reflex_task_kernel.c` over** from FreeRTOS delegation to
`reflex_sched_*`. This is the point at which Reflex owns its scheduling, and
the point at which a bench stops being optional: a scheduler that is wrong does
not fail to build.

**Then D, E, F.** FreeRTOS cannot leave the image while newlib pulls in
`esp_timer` and `pthread`, so its final removal is a Tier F matter. Tier E
matters to Tier C only through the queue primitive; console TX is already a
direct register write (`usj_write_bytes`), and only RX uses the driver.

---

## 6. Honest limits

- No hardware was attached for any of this. Everything above is a property of
  builds and link maps, which is exactly the right evidence for a dependency
  question and exactly the wrong evidence for a scheduler that runs.
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
