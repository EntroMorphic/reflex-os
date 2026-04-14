# Red-Team: Phase 9 (Global Harmonic Regulation)

## Objective
Falsify the **Global** integration of the Harmonic Supervisor. Determine if the system remains stable and coherent when multiple fields are woven into the Tapestry under high-load or edge-case conditions.

## Vulnerabilities Identified

### 1. The "Static Ceiling" (Scalability)
- **Problem:** `MAX_SUPERVISED_FIELDS` is hardcoded to 8.
- **Risk:** As the OS grows (WiFi, Net, Storage, App-Fields), the 9th field will fail to register. Since registration failure is often handled silently in service startup, a critical part of the OS could be left unregulated without the user knowing.
- **Fix:** Move to a dynamic linked list or a larger, configurable state pool.

### 2. The "Pulse Latency" (Temporal Slop)
- **Problem:** The regulation pulse is fixed at 1000ms.
- **Risk:** High-frequency agency (like a PWM-driven LED or a motor controller) could experience a full second of "Disequilibrium" before the Supervisor corrects it. 
- **Fix:** Allow fields to register a "Required Rhythm" (Priority) so the Supervisor can pulse them at different rates.

### 3. Priority Starvation
- **Problem:** The Supervisor runs at Priority 10.
- **Risk:** If a rogue VM task or a heavy network stack saturates the CPU, the "Immune System" (Supervisor) will be starved, allowing disequilibrium to persist and potentially escalate.
- **Fix:** The Supervisor should be the highest-priority non-interrupt task, or utilize a hardware-timer interrupt for its perception pass.

### 4. The "Partial Healing" Problem
- **Problem:** The rebalance logic only heals `INTENT -> HARDWARE_OUT` routes.
- **Risk:** If a complex geometric composition (e.g., `Field_A * Field_B -> Tapestry_Cell -> LED_Service`) breaks at the `Tapestry_Cell` level, the Supervisor will ignore it because it doesn't see a direct "Intent" to "Hardware" path.
- **Fix:** Recursive equilibrium checking (Backtracking the Tapestry).

### 5. Thread Safety (Concurrency)
- **Problem:** The `supervised_fields` array and the states of the `cells` are accessed by the Supervisor task and the Service tasks simultaneously without locks.
- **Risk:** A "Smudged State" where the Supervisor reads a half-updated trit, leading to a false "Healing" event.
- **Fix:** Use atomic trit types or a critical section during the Perception pass.

## Falsification Test (Phase 9.6)
**The "Resource Exhaustion" Test:**
1. Manually register 8 dummy fields.
2. Start a 9th critical service (LED).
3. "Break" the LED route.
- **Expected Result:** The Supervisor will NOT heal the LED because it hit the static ceiling. The system remains broken despite having a "Supervisor."
