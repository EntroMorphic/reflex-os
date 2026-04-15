# Reflex OS Security Model: The Sanctuary

Reflex OS v2.2 implements a "Substrate-First" security model designed to protect the integrity of the **Geometric Loom** against malicious hijacking and physical privilege escalation.

## 1. The Sanctuary Guard (MMIO Isolation)
The Sanctuary Guard prevents non-system ternary cells from mapping to critical hardware registers. 

-   **Safe Agency:** Cells can only be mapped to specific peripherals (GPIO, LEDC, RMT).
-   **The Sanctuary:** Access to the PMU (Power Management), EFUSE, MMU, and Interrupt Matrix is restricted to `sys.` zone cells woven by the core OS.
-   **Enforcement:** `goose_fabric_set_agency` rejects any mapping that attempts to bridge a user-level cell to a Sanctuary address.

## 2. The Authority Sentry (Deadlock Prevention)
To ensure system durability, the `loom_authority` spinlock is monitored by a cycle-accurate watchdog.

-   **Limit:** 50,000 CPU cycles (~300μs).
-   **Action:** If a core fails to acquire the Loom lock within the limit, the Sentry force-breaks the lock and logs a `LOOM_CONTENTION_FAULT`.
-   **Purpose:** Prevents a crashed core or hung evolution from deadlocking the entire system.

## 3. Atmospheric Aura (Geometric Authentication)
Radio-based state propagation (Arcing) is protected by a shared-secret verification layer.

-   **The Aura:** Every Arc packet contains a HMAC-like hash derived from the cell coordinate, state, and a shared `GOOSE_AURA_SECRET`.
-   **The Ground:** Nodes will "ground" (ignore) any Arc packet that does not match the local Aura.
-   **Purpose:** Prevents "Geometric Spoofing" where an unauthorized node attempts to hijack local hardware by broadcasting fake state changes.

## 4. G.O.O.N.I.E.S. Zone Protection
Hierarchical naming is protected at the root.

-   **Immutable Zones:** Names starting with `sys.` or `agency.` are immutable once woven.
- **Shadow Protection:** Attempting to re-register a protected name to a new coordinate is rejected.
- **Shadow Hijack Check:** The registry performs a mandatory check against the Flash-native **Shadow Atlas** before any registration. This prevents malicious scripts from "squatting" on protected names (like `sys.pmu.*`) before the system has paged them into RAM.

-   **Purpose:** Ensures that the mapping from `agency.led.intent` to physical silicon cannot be intercepted by user scripts.

## 5. Loom Quota & Eviction (Resource Isolation)
To prevent "Loom Bloat" (Resource Exhaustion attacks), the substrate implements a self-balancing memory policy.

- **Pinned Cells:** Boot-time core nodes (Origins, Primary Agency) are marked as `GOOSE_CELL_PINNED` and can never be evicted.
- **Shadow Recycling:** When the 256-slot RAM Loom reaches capacity, the allocator uses a round-robin policy to find and recycle an unpinned shadow node.
- **Registry Coherency:** G.O.O.N.I.E.S. is automatically updated during eviction to ensure the name-to-coord mapping remains consistent.
- **Purpose:** Protects the system from being paralyzed by an attacker resolving thousands of unique shadow nodes to overfill the RTC RAM Hearth.

## 6. Mesh Integrity (Aura Shield)
Atmospheric discovery and swarming are protected from external interference.
- **Hashed Discovery:** `ARC_OP_QUERY` uses name-hashes to prevent passive observers from mapping the mesh's physical topology.
- **Discovery Throttling:** Nodes rate-limit incoming queries to 10Hz, preventing Mesh Denial-of-Service (DoS) attacks.
- **Inertial Hysteresis:** Swarm consensus requires an accumulated weight (+/- 10) before flipping local posture, preventing "Consensus Flickering" from malicious or failing nodes.
