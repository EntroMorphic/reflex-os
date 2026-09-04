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

## 3. Tier C is not gated on Tier D

This is the correction that reorders the plan.

The standing assumption — recorded in `reflex_task_kernel.c`'s own header — is
that "FreeRTOS remains as the task management backend because ESP-IDF drivers
(WiFi, USB-JTAG, ESP-NOW) create internal FreeRTOS tasks that require its API."

In the 802.15.4 build, that is not what the linker shows. Exactly four linked
archives reference `xTaskCreate*`:

| Archive | Why it is there |
|---|---|
| `libesp32c6.a` | **Reflex's own** platform component |
| `libfreertos.a` | FreeRTOS itself (idle and timer tasks) |
| `libesp_timer.a` | pulled in by **newlib**, not by Reflex |
| `libpthread.a` | pulled in by **newlib**, not by Reflex |

**The entire 802.15.4 radio stack creates no tasks at all.** `libieee802154.a`,
`libesp_hal_ieee802154.a` and `libbtbb.a` reference no task, queue or semaphore
API. The radio is interrupt-driven and is not a scheduler dependency.

The console is a weaker coupling than the assumption implies too:
`esp_driver_usb_serial_jtag` and `vfs` need **queues, not tasks**.

And `esp_timer` and `pthread` arrive through `libnewlib.a` — the C runtime.
That is Tier F. So *removing FreeRTOS from the image* is gated on F, while
*Reflex owning its own task scheduling* is gated on neither D nor E.

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

The four real ones define exactly one `reflex_`-prefixed symbol each, in `.text`
with no fixed placement, so linking them cannot collide with ESP-IDF's own
startup or vectors. They are now in the kernel-scheduler build: compiled,
discarded, and no longer able to rot silently. The image is unchanged.

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

**C1 — queues.** The one missing primitive. Needed by `fabric.c` and
`event_bus.c` directly, and by the console driver if ESP-IDF's is kept.

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
- The four newly-built kernel files compile clean. That is all it establishes.
  They have never been linked, never executed, and C2 should treat them as a
  reviewed draft rather than working code.
- Tier F is unexamined here beyond naming its parts. The heap in particular is
  load-bearing in a way this document does not explore: six files call `malloc`.
