# Red-Team: Phase 10 (GOOSE-Native VM)

## Objective
Falsify the "Geometric VM" implementation. Determine if the new geometric opcodes (`TSENSE`, `TROUTE`, `TBIAS`) introduce memory safety risks, resource leaks, or ontological breaches.

## Vulnerabilities Identified

### 1. Geometric Memory Corruption (The String Trap)
- **Problem:** `TSENSE` and `TROUTE` read cell names from VM memory using a raw loop up to 15 characters.
- **Risk:** If a VM program provides an address at the very edge of its allocated memory, and that memory doesn't contain a null terminator, the interpreter will read past the VM boundary into host memory.
- **Risk:** Buffer overflow on the host stack if the internal `char name[16]` logic is bypassed or miscalculated.

### 2. Singleton Agency (Resource Shadowing)
- **Problem:** `TROUTE` uses a `static goose_route_t vm_route`.
- **Risk:** Every call to `TROUTE` overwrites the previous VM-created route. A program cannot maintain two concurrent routes.
- **Risk:** This makes multi-field geometric composition impossible for VM-based apps.
- **Fix:** Implement a "Route Pool" for the VM or allow the VM to "own" a slice of the Tapestry.

### 3. Namespace Exhaustion / Collision
- **Problem:** The VM can attempt to `TSENSE` any string.
- **Risk:** There is no validation that the requested cell name is "Safe" or "Exported" to the VM. A guest task could sniff internal system health trits or supervisor-only control cells.
- **Fix:** Introduce a "Fabric Access Control List" (ACL) or namespace prefixing.

### 4. Route Persistence Leak
- **Problem:** Routes created via `TROUTE` are applied to the global GOOSE engine but never cleaned up when the VM halts or faults.
- **Risk:** A crashed VM program could leave "Zombie Routes" that permanently redirect hardware signals, requiring a full system reboot to clear.
- **Fix:** The VM should maintain a "Route Manifest" that is automatically de-patched on `THALT` or fault.

### 5. Type-Check Omission
- **Problem:** `TSENSE` writes the `c->state` into a register but doesn't check if the cell exists (it just does nothing if `c` is NULL).
- **Risk:** The VM program receives the *previous* value of the register, leading to "Stale Signal Logic."
- **Fix:** Explicitly set the destination register to `0` (Neutral) or a "Fault" trit if the cell is missing.

## Falsification Test (Phase 10.5)
**The "Zombie Route" Test:**
1. Load a VM program that creates a route from `led_intent` to `physical_led`.
2. Delete/Unload the VM program.
3. Check if the LED still responds to `led_intent`.
- **Expected Result:** The LED still responds because the GOOSE engine has no idea the VM that created the route is gone. This is a "Geometric Leak."
