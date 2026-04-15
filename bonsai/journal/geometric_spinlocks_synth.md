# Synthesis: Geometric Spinlocks (Trans-Core Atomicity)

## Architecture
Geometric Spinlocks provide a hardware-backed software mutex between the HP and LP cores. It uses a single **Authority Word** in RTC RAM and a corresponding **Ternary Posture** in the Loom to coordinate access to the Global Fabric.

## Key Decisions
1.  **Authority Word**: A `uint32_t` in RTC RAM (swapped via CAS) is the source of truth for core ownership.
2.  **Peterson's Safety**: The HP core uses standard FreeRTOS critical sections when manipulating the Loom to prevent preemption by other HP tasks.
3.  **LP Assembly CAS**: The ULP RISC-V heartbeat uses a tight assembly loop for its `Compare-and-Swap` logic.
4.  **Immune Reset**: The Supervisor can forcibly clear the lock if a "Geometric Deadlock" is detected.

## Implementation Specification

### 1. Loom Authority (goose_runtime.c)
```c
static RTC_DATA_ATTR volatile uint32_t loom_authority = 0; // 0=Free, 1=HP, 2=LP
```

### 2. HP Lock Primitives
```c
void goose_loom_lock() {
    while (atomic_compare_exchange(&loom_authority, 0, 1) == false) {
        vTaskDelay(1);
    }
}
void goose_loom_unlock() {
    loom_authority = 0;
}
```

### 3. LP Lock Primitives (RISC-V ASM)
```asm
ulp_loom_lock:
    li t0, 2             # Want LP lock
    la t1, loom_authority
loop:
    lw t2, (t1)          # Load current authority
    bnez t2, loop        # If not 0, spin
    sw t0, (t1)          # Try to take it
    # Note: Real implementation needs a proper CAS loop
```

## Implementation Plan
1.  **Add `loom_authority`** to RTC RAM in `goose_runtime.c`.
2.  **Wrap `goose_process_transitions`** and all fabric-modifying functions with the lock.
3.  **Update `ulp/lp_pulse.c`** with the matching lock logic.
4.  **Add Lock Watchdog** to `goose_supervisor.c`.

## Success Criteria
- [ ] HP and LP cores can pulse the same cell (e.g., `sys_origin`) concurrently for 1 hour without a single state collision or "smudge."
- [ ] Lock overhead is <5% of the total pulse time.
- [ ] Forcible reset by Supervisor successfully recovers from a simulated core hang.
