# LMM: Reflex OS 2.0 Integration (Phase 14)

## Phase 1: RAW (The First Chop)
We are at the threshold of the "Big Rewrite." The current Reflex OS service manager is a legacy of the binary age—it thinks in terms of function pointers, task handles, and message queues. We've proven GOOSE works, but it's currently a "sidecar" to the OS. To move to 2.0, we have to flip the hierarchy. The OS must not "have" a GOOSE runtime; it must **be** the GOOSE runtime. 

The biggest risk is "Abstraction Paralysis." If we turn a WiFi driver into a geometric route, does it still work at 802.11 speeds? Probably not yet. We need a way to wrap legacy high-speed agency in geometric intent. 

I worry about the "Pulse Gap." 10Hz is fine for an LED, but useless for a serial buffer. We need a tiered rhythm system.

## Phase 2: NODES (Identify the Grain)
1. **The Task Habit:** Services currently demand their own FreeRTOS tasks. In GOOSE, a service should just be a "Regional Pattern."
2. **The Pulse Gap:** The 100ms Supervisor pulse is too slow for reactive agency.
3. **The High-Res Boundary:** Ternary is great for logic, but "Legacy Binary" (WiFi packets, file buffers) still exists and must be bridged.
4. **Lifecycle Equivalence:** Init/Start/Stop must map to Weave/Orient/Unweave.
5. **The Weaving Sequence:** Booting is currently a C sequence. It should be a Tapestry manifestation.
6. **Tension:** Abstraction (Geometric Purity) vs. Performance (Hardware Reality).
7. **Tension:** Dynamic Re-patching vs. System Predictability.

## Phase 3: REFLECT (Sharpen the Axe)
Why do we still have a Service Manager? Because we need a way to group cells/routes into "Identities" and give them a "Rhythm." 
**Core Insight:** **Reflex OS 2.0 is a Pattern Host.** 
The Service Manager doesn't "run code"; it **"manages rhythms."** A service is simply a Regional Pattern with a specific "Pulse Frequency." 

Resolution: We will not delete the task system. We will **Geometric-ize** it. A "Service Task" becomes a **"Regional Pulse Task"** that does nothing but call `goose_process_transitions` on a specific sub-manifold at high frequency.

## Phase 4: SYNTHESIZE (The Clean Cut)
The plan for **Reflex OS 2.0** is the **Rhythmic Re-integration.**

### 1. The Regional Controller (`goose_service.h`)
Define a new service structure where `Init` returns a `goose_fragment_handle_t`. The "Service" is the pattern itself.

### 2. Tiered Rhythms
- **Autonomic (LP Core):** 1Hz - Persistent/Sleep-safe regulation.
- **Harmonic (Supervisor):** 10Hz - General system posture.
- **Reactive (Regional Pulse):** 100Hz+ - For high-speed IO (e.g., Serial/Buttons).

### 3. The Shadow Bridge
Legacy services (WiFi, Storage) will be "Wrapped." They will have an **Intent Cell** in the Tapestry. When the Tapestry "tilts" (state change), the legacy C code is triggered. The geometry drives the legacy, not vice-versa.

### 4. Manifest Boot
Modify `main.c` so `app_main` is a "Master Weaver." It weaves the System Fragment, the Net Fragment, and the Agency Fragment. The system's "Startup" is the transition of the `sys_status` cell from `0` to `+1`.

**The wood cuts itself:** We don't rewrite the drivers; we rewrite the **Authority** that drives them.
