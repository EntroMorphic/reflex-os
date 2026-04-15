# Nodes of Interest: Geometric Spinlocks

## Node 1: The Asymmetric Lock (Peterson's Gap)
The HP core has atomic instructions; the LP core does not.
- **Decision:** Use a **Software Mutex** implemented as three shared trits in RTC RAM: `HP_INTENT`, `LP_INTENT`, and `TURN`.
- This is a classic Peterson's Algorithm adapted for the HP/LP boundary.

## Node 2: Atomic Bit-Banding (The Physical Truth)
While ternary cells are the OS standard, the underlying silicon uses bits.
- **Decision:** Use a single 32-bit word in RTC RAM as the **Loom Authority Word**.
- The HP core uses `S32C1I` or FreeRTOS critical sections.
- The LP core (RISC-V) uses a "Compare-and-Swap" loop in assembly.

## Node 3: Lock-Free Propagation?
Can we avoid locking?
- **Decision:** Only if we use **Double-Buffering**.
- Field A writes to Buffer 1. Field B reads from Buffer 1.
- But the Loom is intended to be a single, shared, persistent fabric. Double buffering doubles the RTC RAM usage (32KB required, only 16KB available).
- Locking is a spatial necessity.

## Node 4: The Immune Watchdog
If a core hangs while holding the Loom authority, the whole OS freezes.
- **Decision:** The Supervisor (Harmonic, 10Hz) can forcibly break a lock if the timestamp on the lock word is stale (>100ms).

---

## Tensions
1. **Latency vs. Safety:** Locking every cell access adds overhead to the 1000Hz `REALTIME` pulse.
2. **Assembly vs. C:** The LP core logic must be implemented in ULP RISC-V assembly to ensure the CAS loop is truly atomic (no interruptions).
3. **Ontological Purity:** Using a "Binary Lock Word" feels like a regression. Can we make the lock a **Geometric Inhibit** route?
