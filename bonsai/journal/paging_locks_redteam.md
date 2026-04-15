# Red-Team Analysis: Paging & Locks (Phase 2)

## 1. Unaligned Atomic Smear (High)
`goose_cell_t` is 19 bytes. Sequential cells in the array will not be 4-byte aligned.
- **Attack/Risk:** A 32-bit write to `hardware_addr` or `bit_mask` could be split across two bus transactions. If a core reads during this split, it sees a corrupted address.
- **Fix:** Force 4-byte alignment on `goose_cell_t`.

## 2. Global Lock Jitter (Medium-High)
A single `loom_authority` lock causes high-frequency tasks to wait for low-frequency ones.
- **Risk:** `REALTIME` (1000Hz) pulses experience 1ms jitter due to `vTaskDelay(1)` if the lock is held.
- **Fix:** Move to **Regional Locking** or use a busy-spin with a timeout for high-priority tasks.

## 3. The "Ghost Page" (Low)
If a route's `cached_ptr` is not invalidated during a swap, it points to memory that might now belong to a different cell.
- **Risk:** One manifold accidentally driving the state of another manifold's cell.
- **Fix:** Every swap MUST increment `fabric_version`.

## 4. LP-Core Priority Inversion (Low)
The LP core doesn't know about FreeRTOS priorities. It will spin on the lock regardless of what the HP core is doing.
- **Risk:** Power consumption spike if LP core spins for long periods.
- **Fix:** LP core should sleep/yield if the lock is held by the HP core.
