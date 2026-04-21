# Assessment: FreeRTOS Startup Path Removal

## Current State

ESP-IDF's boot sequence calls `app_main()` from a FreeRTOS task. The Reflex
kernel intercepts the scheduler start via `__wrap_xPortStartScheduler`
(reflex_freertos_compat.c) which:

1. Configures a 1kHz kernel tick on SYSTIMER TARGET1.
2. Creates the `reflex-kern` supervisor task at max priority.
3. Calls `__real_xPortStartScheduler()` to hand off to FreeRTOS.

From that point forward, the Reflex kernel owns:

- **Interrupt context switching**: `reflex_portasm.S` wraps `rtos_int_enter`
  and `rtos_int_exit` with Reflex's own ISR stack management and hardware
  stack guard setup.
- **Task API**: `reflex_task.h` is the only concurrency interface used by the
  substrate and VM. Two backends exist: `reflex_task_esp32c6.c` (FreeRTOS) and
  `reflex_task_kernel.c` (standalone scheduler via `reflex_sched.h`).
- **Priority management**: The supervisor policy function modulates task
  priorities based on purpose, Hebbian plasticity, holon lifecycle, and pain.
- **Scheduling policy**: The kernel tick drives the policy engine; the
  standalone backend (`reflex_task_kernel.c`) implements its own queues,
  critical sections, and task state machine.

## What FreeRTOS Still Owns

- **Initial task creation**: `app_main` runs inside a FreeRTOS-created task.
- **Idle task**: FreeRTOS creates and manages the idle task (used for
  tickless idle, stack watermark checks).
- **Timer task**: FreeRTOS software timer service task.
- **Scheduler start**: `xPortStartScheduler` initializes FreeRTOS internals
  (ready lists, tick setup, first context switch).

## Options

### A. Replace xPortStartScheduler entirely

Write a standalone scheduler start that initializes the RISC-V mtvec, creates
the idle task, starts the systimer, and performs the first context switch
without calling any FreeRTOS function.

- Requires: own idle task (power management, watchdog feeding), own timer
  infrastructure, own heap management (FreeRTOS heap_4/5 replacement), own
  TCB initialization and ready-list population.
- Pros: True independence. No FreeRTOS symbols in the binary.
- Cons: Massive scope. Every edge case FreeRTOS handles (stack overflow
  detection, tick compensation, idle hooks) must be reimplemented or
  consciously dropped.
- Effort: Weeks. High regression risk.

### B. Replace idle and timer tasks, keep scheduler start

Let FreeRTOS start the scheduler but immediately replace the idle and timer
tasks with Reflex-owned equivalents. The kernel already wraps context
switching, so this removes the last FreeRTOS runtime tasks.

- Requires: idle task replacement (must call `vApplicationIdleHook` or
  equivalent), timer replacement (or stub the timer task to do nothing).
- Pros: Incremental. Most FreeRTOS runtime behavior is eliminated.
- Cons: Still links FreeRTOS. The idle task replacement must handle chip-
  specific power management (waiti instruction, PMU interaction).
- Effort: Days. Moderate risk.

### C. Accept FreeRTOS as the scheduling backend

Keep the current architecture: FreeRTOS handles low-level scheduling
mechanics, the Reflex kernel owns everything above via `reflex_task.h` and
the `--wrap` interposition layer.

- Pros: Stable, tested, zero work. The abstraction layer (`reflex_task.h`)
  completely isolates application code. The standalone backend
  (`reflex_task_kernel.c`) already exists for future independence.
- Cons: FreeRTOS remains in the binary. Not fully independent.
- Effort: Zero (already done).

## Recommendation

**Option C.** FreeRTOS is a mature, hardware-validated scheduler. The
abstraction layer already isolates it completely -- no substrate or VM code
references FreeRTOS directly. The standalone kernel backend exists and can be
promoted to production when it has sufficient hardware validation.

Option B is a reasonable next step if reducing the FreeRTOS footprint becomes
a goal, but it should be driven by a concrete need (binary size, power
management control, or certification requirements) rather than pursued
speculatively.

## Decision

Accept FreeRTOS as the scheduling backend. The `reflex_task.h` abstraction
and `reflex_task_kernel.c` standalone backend provide the escape path. Promote
the standalone backend when hardware validation confirms parity.
