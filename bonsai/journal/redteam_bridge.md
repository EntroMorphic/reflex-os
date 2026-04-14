# Red-Team: GOOSE Gateway & MMIO Projection

## Objective
Falsify the "Mothership Bridge." Determine if the ability to project physical MMIO registers into the GOOSE Tapestry via legacy messaging introduces critical system risks or architectural instability.

## Vulnerabilities Identified

### 1. Root-Level Access (Security Breach)
- **Problem:** The Gateway accepts any `correlation_id` as an MMIO address without validation.
- **Risk:** A non-privileged VM task or a shell user could project the MMIO of the Memory Management Unit (MMU), the Interrupt Controller, or the AES key registers.
- **Risk:** By routing a `POS` signal to a projected MMU register, a dev could disable memory protection for the entire chip.

### 2. The Non-Atomic Write (Race Condition)
- **Problem:** `goose_runtime.c` performs register updates using non-atomic OR/AND operations: `*reg |= mask`.
- **Risk:** If two different manifolds (e.g. Supervisor and a High-Speed Task) attempt to update different bits of the same 32-bit register simultaneously, one update will be overwritten.
- **Fix:** Use the ESP32-C6's bit-banded registers or atomic atomic-clear/atomic-set if available, or wrap in a port mux.

### 3. Register Semantic Blindness (Electrical Safety)
- **Problem:** Many registers have strict traits (Read-Only, Write-Only, Clear-on-Read, Set-to-Clear). 
- **Risk:** Projecting a "Set-to-Clear" interrupt register as a `HARDWARE_OUT` cell could cause the OS to enter a permanent interrupt storm.
- **Risk:** Projecting a Read-Only status register as `HARDWARE_OUT` and writing to it might trigger a hardware exception or silent failure.

### 4. Alignment Trap (Kernel Panic)
- **Problem:** The ESP32-C6 requires 32-bit alignment for all MMIO register access. The Gateway does not check if the provided address is a multiple of 4.
- **Risk:** A projection to `0x60000001` will cause a **Store Alignment Exception**, triggering a full system reboot.

### 5. Memory Exhaustion (Denial of Service)
- **Problem:** Projection spams the `fabric_cells` pool. 
- **Risk:** Since the Loom is static (16 cells), a single misbehaving script or test can "lock out" the system by filling the Loom with junk MMIO projections, preventing critical system cells (like `sys_status`) from being updated.

## Falsification Test (Phase 14.5)
**The "Brick" Test:**
1. Send a projection message to an unaligned address (e.g., `0x60000001`).
2. Observe if the system crashes.
- **Expected Result:** Instant crash. This proves the Bridge is "Dull" and unsafe for general use.
