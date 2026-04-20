# Reflex OS: A Purpose-Aware Ternary Operating System for Embedded Mesh Networks

## Abstract

We present Reflex OS, a microcontroller operating system that introduces three novel architectural primitives absent from existing embedded operating systems: (1) balanced ternary state propagation as the fundamental scheduling signal, (2) purpose-modulated kernel scheduling where user-declared intent directly adjusts task priorities through a domain-matching policy engine, and (3) Hebbian reinforcement learning integrated into the scheduler itself, enabling the OS to learn which task-routing patterns best serve the user's evolving goals. The system runs on ESP32-C6 (RISC-V) and ESP32 (Xtensa) hardware, owns its interrupt context switching via linker-level wraps on the preemptive scheduler, and has been validated end-to-end: purpose declaration → domain-biased routing → amplified learning → priority adjustment → NVS persistence → reboot recovery.

---

## 1. Ternary State Propagation

### 1.1 Motivation

Binary operating systems model task state as running/blocked, signal values as 0/1, and routing decisions as yes/no. This forces three-state conditions (agree/neutral/disagree, excite/inhibit/observe) into two-state encodings that lose information.

Reflex OS uses **balanced ternary** (-1, 0, +1) as the atomic state value throughout the substrate. Every cell, route, and scheduling decision operates on trits, not bits.

### 1.2 Implementation

The substrate consists of **cells** (state holders) connected by **routes** (directional edges). State propagation is a single ternary multiplication:

```c
// goose_runtime.c:585-587
reflex_trit_t effective_orient = r->learned_orientation
    ? r->learned_orientation : r->orientation;
reflex_trit_t control = r->cached_control
    ? (reflex_trit_t)r->cached_control->state : effective_orient;
r->cached_sink->state = (int8_t)((int)r->cached_source->state * (int)control);
```

The multiplication produces the full ternary truth table:

| Source | Control | Sink |
|--------|---------|------|
| +1 | +1 | +1 |
| +1 | -1 | -1 |
| -1 | +1 | -1 |
| -1 | -1 | +1 |
| any | 0 | 0 |

This single operation encodes excitation, inhibition, and indifference — the three fundamental responses of a routing element.

### 1.3 Why This Matters

The `learned_orientation` field is mutable. When Hebbian learning commits a new sign, it changes how future signals propagate — the substrate physically rewires its routing topology through learned experience, not explicit programming.

---

## 2. Purpose-Modulated Kernel Scheduling

### 2.1 Motivation

Existing RTOSes assign static priorities at task creation. Dynamic priority adjustment exists (e.g., priority inheritance for mutex deadlock avoidance) but is reactive — it responds to contention, not intent.

Reflex OS introduces **purpose**: a user-declared domain string (e.g., "photography", "mesh", "liberated") that the kernel reads at runtime and uses to modulate task priorities. When purpose is active, tasks serving that domain get more CPU time.

### 2.2 Implementation

The kernel supervisor runs at highest priority and calls a policy function registered by the substrate layer:

```c
// reflex_freertos_compat.c:76-80
static void reflex_kernel_supervisor(void *arg) {
    (void)arg;
    vTaskDelay(MS_TO_TICKS(3000));
    while (1) {
        if (s_policy_fn) s_policy_fn(s_kernel_tick);
        vTaskDelay(MS_TO_TICKS(1000));
    }
}
```

The policy function resolves route endpoint names against the purpose domain:

```c
// goose_supervisor.c:96-107
for (size_t r = 0; r < field->route_count && !has_domain_route; r++) {
    const char *src = goonies_resolve_name_by_coord(field->routes[r].source_coord);
    const char *snk = goonies_resolve_name_by_coord(field->routes[r].sink_coord);
    if ((src && strstr(src, s_last_purpose)) ||
        (snk && strstr(snk, s_last_purpose))) {
        has_domain_route = true;
    }
}
if (has_domain_route) priority += PULSE_PURPOSE_BOOST;
```

Fields with routes touching the purpose domain receive a +3 priority boost. Non-matching fields receive +1. Deactivated holons are forced to base priority.

### 2.3 Architectural Pattern

The kernel provides a **policy hook** via weak/strong symbol linkage:

```c
// goose_supervisor.c:49-50 (weak default)
__attribute__((weak))
void reflex_kernel_set_policy(reflex_kernel_policy_fn fn) { (void)fn; }

// reflex_freertos_compat.c:63-65 (strong override when kernel active)
void reflex_kernel_set_policy(reflex_kernel_policy_fn fn) {
    s_policy_fn = fn;
}
```

This decouples the kernel from the substrate — the kernel doesn't know about GOOSE, cells, or routes. It just calls a function pointer. The substrate registers its own policy implementation. When the kernel is disabled (`CONFIG_REFLEX_KERNEL_SCHEDULER=n`), the weak no-op compiles cleanly.

---

## 3. Geometric Coordinate Addressing

### 3.1 Motivation

Traditional OSes identify resources by integer IDs (PIDs, file descriptors) or string names (paths). Both are flat — they encode no spatial or topological relationship between resources.

Reflex OS uses **9-trit geometric coordinates** as the primary identifier for every cell in the substrate:

```c
// reflex_ternary.h:54-57
typedef struct {
    reflex_trit_t trits[9];
} reflex_tryte9_t;
```

### 3.2 Addressing Scheme

Coordinates encode a three-level hierarchy: `field.region.cell`, each a 3-trit value:

```c
// goose_runtime.c:465-470
reflex_tryte9_t goose_make_coord(int8_t field, int8_t region, int8_t cell) {
    reflex_tryte9_t t = {{0}};
    t.trits[0] = (reflex_trit_t)field;
    t.trits[3] = (reflex_trit_t)region;
    t.trits[6] = (reflex_trit_t)cell;
    return t;
}
```

Each coordinate level uses balanced ternary (-1, 0, +1), giving 3^3 = 27 positions per level and 27^3 = 19,683 addressable cells in the full 9-trit space.

### 3.3 Properties

- **Spatial locality**: cells with similar coordinates share routing topology
- **O(1) lattice lookup**: coordinates hash directly to fabric array positions
- **Multi-resolution**: the first 3 trits identify the field (coarse), next 3 the region (medium), last 3 the cell (fine)
- **Ternary arithmetic**: coordinate comparison, distance, and adjacency use trit operations, not integer math

---

## 4. Hebbian Scheduling

### 4.1 Motivation

Machine learning in operating systems typically exists as a user-space application. The OS itself doesn't learn. Reflex OS embeds a **reward-gated Hebbian learning rule** directly in the scheduler's regulation pass.

### 4.2 The Learning Rule

"Fire together, wire together" — when two cells connected by a route are co-active under a reward signal, the route's connection strength increases:

```c
// goose_supervisor.c:293-310
if (rewarded) {
    int s = route->cached_source->state;
    int k = route->cached_sink->state;
    if (s == 0 || k == 0) continue;
    int delta = ((s == k) ? 1 : -1) * purpose_multiplier;
    route->hebbian_counter += delta;

    if (route->hebbian_counter >= HEBBIAN_COMMIT_THRESHOLD) {
        route->learned_orientation = REFLEX_TRIT_POS;
        route->hebbian_counter = 0;
    } else if (route->hebbian_counter <= -HEBBIAN_COMMIT_THRESHOLD) {
        route->learned_orientation = REFLEX_TRIT_NEG;
        route->hebbian_counter = 0;
    }
} else if (pained) {
    if (route->hebbian_counter > 0) route->hebbian_counter--;
    else if (route->hebbian_counter < 0) route->hebbian_counter++;
}
```

### 4.3 Key Properties

- **Purpose amplification**: `purpose_multiplier = 2` when purpose is active — learning is faster under declared intent
- **Commitment threshold**: the counter must reach ±8 before the orientation commits — this prevents single-event overreaction
- **Pain decay**: under pain signal, counters decay toward zero (unlearning) without catastrophic erasure
- **Scheduling feedback**: fields with `learned_orientation != 0` routes get a +2 priority boost — the scheduler rewards maturity

### 4.4 Interaction with Priority

The Hebbian state feeds back into scheduling via the kernel policy:

```c
// goose_supervisor.c:109-115
int learned_routes = 0;
for (size_t r = 0; r < field->route_count; r++) {
    if (field->routes[r].learned_orientation != 0) learned_routes++;
}
if (learned_routes > 0) priority += PULSE_HEBBIAN_BOOST;
```

This creates a **positive feedback loop**: mature routes get more CPU time, which means they evaluate faster, which means they respond to stimuli quicker. The OS becomes more responsive in domains where it has learned.

---

## 5. Holonic Lifecycle Management

### 5.1 Motivation

Tasks in traditional RTOSes are independent units. There's no concept of "this group of tasks forms a coherent functional unit that should be managed together." Holons fill this gap.

### 5.2 Implementation

A holon is a named group of fields with a domain tag:

```c
// goose_supervisor.c:37-43
typedef struct {
    char name[16];
    char domain[16];
    goose_field_t *fields[MAX_HOLON_FIELDS];
    size_t field_count;
    bool active;
} reflex_holon_t;
```

### 5.3 Lifecycle Behavior

The kernel policy tick activates/deactivates holons based on purpose:

```c
// goose_supervisor.c:73-86
bool should_be_active = !holon->domain[0] ||
    (purpose_active && strstr(purpose, holon->domain));
if (holon->active != should_be_active) {
    holon->active = should_be_active;
}
```

- Holons with no domain tag (`domain[0] == '\0'`) are always active
- Holons with a domain tag are active only when purpose contains that domain
- When a holon deactivates, all its fields drop to base priority
- When a holon activates, its fields regain their earned boosts

### 5.4 Scheduling Interaction

```c
// goose_supervisor.c:117-130
bool in_any_holon = false;
bool in_active_holon = false;
for (size_t h = 0; h < s_holon_count; h++) {
    for (size_t hf = 0; hf < s_holons[h].field_count; hf++) {
        if (s_holons[h].fields[hf] == field) {
            in_any_holon = true;
            if (s_holons[h].active) in_active_holon = true;
        }
    }
}
if (in_any_holon && !in_active_holon) priority = PULSE_BASE_PRIORITY;
```

A field keeps its boost if ANY containing holon is active. Only when ALL are inactive does it lose priority. This enables overlapping functional groups.

---

## 6. Cooperative-on-Preemptive Architecture

### 6.1 Motivation

Embedded operating systems are either cooperative (simple, predictable, no preemption) or preemptive (complex, interrupt-driven, full context switching). Reflex OS is both: a **cooperative policy layer** riding on a **preemptive scheduling HAL**.

### 6.2 Mechanism: Linker-Level Interception

The kernel owns interrupt context switching without modifying FreeRTOS source:

```asm
; reflex_portasm.S — replaces FreeRTOS's rtos_int_enter/exit via --wrap
.global __wrap_rtos_int_enter
__wrap_rtos_int_enter:
    lw      a0, port_xSchedulerRunning
    beqz    a0, .Lenter_end
    ; Increment nesting, save SP to TCB, switch to ISR stack
    ; Manage hardware stack guard (ASSIST_DEBUG peripheral)
    ...
    ret

.global __wrap_rtos_int_exit
__wrap_rtos_int_exit:
    mv      s10, ra          ; Save return address
    mv      s11, a0          ; Save mstatus
    ; Check xPortSwitchFlag, call vTaskSwitchContext if set
    ; Restore SP from (new) TCB, set stack guard bounds
    mv      a0, s11          ; Return mstatus
    mv      ra, s10
    ret
```

Linker flags: `--wrap=rtos_int_enter`, `--wrap=rtos_int_exit`, `--wrap=xPortStartScheduler`

### 6.3 Layered Design

| Layer | Responsibility | Code |
|-------|---------------|------|
| Hardware | Interrupts, register save/restore | ESP-IDF vectors.S |
| Port Assembly | TCB pivot, ISR stack, stack guard | reflex_portasm.S |
| Scheduler HAL | Task ready lists, queues, timers | FreeRTOS (unchanged) |
| Kernel Supervisor | Policy tick, purpose/Hebbian/holons | reflex_freertos_compat.c |
| Substrate | GOOSE cells, routes, fields, learning | goose_supervisor.c |

This is architecturally analogous to Linux (policy layer on top of a preemptive microkernel) but at MCU scale. The key insight: **we decide WHEN to switch, FreeRTOS handles HOW to switch.**

### 6.4 Complete Standalone Backend

For environments without FreeRTOS, a complete task backend exists:

```
kernel/reflex_task_kernel.c — 13 functions:
  create, delete, delay, yield, get_by_name, set/get_priority,
  queue create/send/recv, critical enter/exit, mutex init
```

Selectable via `CONFIG_REFLEX_KERNEL_SCHEDULER`. Uses setjmp/longjmp cooperative scheduling with SYSTIMER tick preemption capability.

---

## 7. Hardware Validation

### 7.1 Platform

- **Primary**: Seeed XIAO ESP32-C6 (RISC-V single-core, 160MHz, 320KB SRAM, 4MB flash)
- **Secondary**: ESP32 (Xtensa dual-core, 240MHz) — validated as mesh peer
- **Custom bootloader** (Boot0): 5.2KB, zero ESP-IDF bootloader_support calls, own partition table reader and MMU mapper

### 7.2 Closed-Loop Demonstration

Validated end-to-end on hardware, captured via serial:

```
[reflex.boot0] Reflex OS bootloader
[reflex.boot0] factory partition at 0x10000
[reflex.boot0] jumping to 0x408002e6

  ╔══════════════════════════════════════╗
  ║       Reflex OS Kernel Active        ║
  ╚══════════════════════════════════════╝

[reflex.kernel] tick=1000Hz supervisor=active
purpose restored from NVS: "mesh"
[reflex.kernel] supervisor: policy=registered
[reflex.kernel] policy: purpose=mesh fields=1
```

The loop: **purpose set** → routing biased (weave_sync resolves capabilities in purpose domain first) → **learning amplified** (2× Hebbian under purpose) → **priorities adjusted** (domain-matching fields +3) → **persist to NVS** → **reboot** → purpose restored → policy re-fires → priorities re-adjusted.

### 7.3 FreeRTOS Isolation

FreeRTOS is fully contained behind `reflex_task.h`:
- **Zero** FreeRTOS references in substrate, shell, services, VM, core, net
- **One** backend file touches FreeRTOS: `platform/esp32c6/reflex_task_esp32c6.c`
- **Complete** standalone backend: `kernel/reflex_task_kernel.c` (13 functions, zero FreeRTOS)
- **Interrupt context switching** owned by Reflex port assembly

---

## 8. Related Work

| System | Ternary | Purpose-aware scheduling | Hebbian in kernel | Holonic lifecycle |
|--------|---------|--------------------------|-------------------|-------------------|
| FreeRTOS | No | No | No | No |
| Zephyr | No | No | No | No |
| Linux CFS | No | Nice/ionice (reactive) | No | cgroups (manual) |
| RIOT | No | No | No | No |
| Reflex OS | **Yes** | **Yes** (proactive) | **Yes** | **Yes** (automatic) |

No existing operating system combines ternary state propagation, proactive purpose-driven scheduling, embedded learning in the scheduler, and automatic holonic lifecycle management.

---

## 9. Conclusion

Reflex OS demonstrates that operating system design has unexplored territory at the intersection of ternary logic, neuromorphic learning, and intentional computing. The system is not a simulation — it runs on commodity hardware, handles real interrupts, manages real tasks, and learns from real signals. The entire substrate, VM, and kernel are original work: ~15,000 lines of C and assembly, hardware-validated on multiple architectures.

The key insight: **an operating system that knows what the user is trying to do can allocate resources better than one that only reacts to contention.** Purpose is not a hint — it's a first-class kernel primitive that reshapes scheduling topology in real time.
