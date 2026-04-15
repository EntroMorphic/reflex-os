# Raw Thoughts: Geometric Spinlocks (Trans-Core Atomicity)

## Stream of Consciousness
HP Core (Mind) and LP Core (Heart) are in a race for the Loom.
If the Heart pulses a heartbeat while the Mind is re-weaving a route, the state might get smudged.
Smudged trits lead to geometric storms (invalid states).

How to lock across the core boundary?
ESP32-C6 has a "Hardware Mutex" but it's usually for the Wi-Fi/BT subsystems.
We need something simpler that the LP RISC-V can access.
Since both cores can see RTC RAM, we use a **Ternary Lock Cell**.

Coordinate `(0, 0, 0)` - `sys_origin`.
What if we reserve `(0, 0, -1)` as `sys_lock`?
`state = -1`: HP Core holds lock.
`state = +1`: LP Core holds lock.
`state =  0`: Available.

HP Core Logic:
1. `atomic_compare_and_set(sys_lock, 0, -1)`
2. Perform Loom update.
3. `sys_lock = 0`

LP Core Logic (RISC-V):
1. `atomic_compare_and_set(sys_lock, 0, 1)`
2. Perform pulse.
3. `sys_lock = 0`

Wait, does the ESP32-C6 LP core support atomic instructions on RTC RAM?
Checking docs... The LP RISC-V usually doesn't have the `A` extension (Atomics).
If no atomics, we need Peterson's Algorithm or Dekker's Algorithm.
Peterson's needs two "Intent" trits and one "Turn" trit.
`HP_Intent`, `LP_Intent`, `Turn`.

Better: **Bit-Banded Atomic Registers**.
The ESP32-C6 has "S32C1I" (Store 32-bit compare and swap) but it might only be for the HP core.
Actually, the C6 has **Atomic Bit Manipulation** (Set/Clear/Toggle) on some peripheral registers.
Maybe we use a spare GPIO or a System Register bit as the physical lock?
GPIO 0-31 have "Set" and "Clear" registers. Writing to `GPIO_OUT_W1TS_REG` is atomic.
But the LP core and HP core can both write to it.
If I use a virtual "Lock Register" in the system control space.

Wait! GOOSE is geometric.
The lock should be a **Route Orientation**.
If the `sys_lock` orientation is `0`, all transitions are inhibited.
If the Mind is working, it inhibits the Heart's routes.

Red-Team thought:
Deadlock. If the HP core crashes while holding the lock, the LP core (The Heart) stops beating.
The system dies.
We need a **Lease** or a **Watchdog** on the lock.
If `sys_lock` is held for >100ms, the Supervisor (Immune System) forcibly clears it.

## Questions Arising
1. Does the LP RISC-V have `lr.w` / `sc.w`?
2. What is the fastest shared memory bit we can flip atomically?
3. How to handle the Supervisor "Healing" a stuck lock without causing more smudging?

## First Instincts
- Use a dedicated `uint32_t` in RTC RAM as a binary spinlock for simplicity, even if it's "Binary age."
- Wrap all `fabric_cells` access in `goose_runtime.c` with this lock.
- Implement the matching lock in the `ulp/lp_pulse.c` RISC-V assembly/C.
