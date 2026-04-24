# Reflex OS Strategy: The Chronicler's Path

## Birds-Eye View (v2.5.1)

Reflex OS has moved from a binary-hosted VM to a hardened, geometric substrate. The ESP32-C6 MMIO surface is reachable by name through a 12,738-node shadow catalog (all 65 SVD peripherals with fully expanded array registers), of which 104 high-priority cells are pre-woven into the active Loom at boot and the remainder are paged on demand via `goose_shadow_resolve`. Cross-board coordination is live via HMAC-SHA256-signed ESP-NOW Arcs with a time-bounded replay cache and a protocol version epoch. The LP RISC-V coprocessor runs a parallel heartbeat alongside HP.

### 1. Accepted Design Decisions (Former Technical Debt)

All four items previously listed as technical debt were audited and assessed as benign. They are now accepted design decisions with known monitoring:

- **Round-Robin Eviction (not LRU):** `goose_fabric_alloc_cell` evicts via `last_eviction_idx` round-robin. Warmth-factor LRU would add writes to the 10Hz hot path and touch the RTC-resident `goose_cell_t` layout. No evidence of thrashing in any on-device testing. If repeated eviction of hot cells appears, telemetry (`#T:E` events) will surface it. **Accepted: profile first, optimize second.**
- **Route-Only Snapshots (not full loom):** `goose_snapshot_save/load` persists `learned_orientation` + `hebbian_counter` for supervised routes — the learned manifold. Full cell state is stateless by design: cells are re-woven from the atlas on boot, and hardware state is re-read from registers. Full loom-wide snapshots would add ~700 bytes per save for state that's reconstructed in <100ms. **Accepted: current scope is correct.**
- **Alloc-Under-Lock Duration:** `goose_fabric_alloc_cell` holds `loom_authority` across `goonies_register` (O(n) linear scan, ~40µs worst case at n=256). The 50K-cycle timeout (~500µs at 100MHz) is conservative. `LOOM_CONTENTION_FAULT` is monitored and has never fired in any on-device session. **Accepted: sub-threshold, instrumented, revisit if contention appears.**
- **C Supervisor (not TASM):** The regulation pass in `goose_supervisor.c` remains in C. TASM lacks string comparison, dynamic route iteration, and function calls — rewriting would require 3-4 new syscalls, loop unrolling, and loss of runtime flexibility for zero performance gain. TASM is the right tool for ternary algebra; C is the right tool for policy. **Accepted: right tool for the job.**

### 2. Money on the Table (Unfair Advantages)

- **Atmospheric Discovery:** global hardware DNS across nodes using ESP-NOW. Validated on three physical boards: name query → ghost solidification → per-peer rx/tx counters.
- **Ontological Safety:** framing substrate rebalancing as a "Functional Safety" product.
- **LoomScript DSL:** a high-level geometric language to replace raw TASM (compiler in `tools/loomc.py`, binary format in `goose_library.c`, examples in `examples/*.loom`).
- **Coherent Heartbeat:** the LP core runs in parallel to HP. With HP mirroring intent into LP-local globals, the LP program can observe deep-sleep cycles the HP misses — useful for independent liveness, failure detection, and sleep-aware telemetry.

### 3. The Starting Line (Next Phases)

- **Phase 26:** LoomScript DSL (Geometric High-level Language). (Completed)
- **Phase 27:** Geometric AI (Ternary Neural Manifolds — `neuron_quorum` + Hebbian plasticity). (Completed)
- **Phase 28:** Autonomous Fabrication. (Completed — `goose_supervisor_weave_sync` wires `GOOSE_CELL_NEED` cells to capability-matched sinks using a generic last-segment resolver.)
- **Phase 29:** Tapestry Snapshots. (Completed — `goose_snapshot_save/load/clear` serialize supervised-route plasticity to NVS; `snapshot save/load/clear` shell commands.)
- **Phase 29.5:** `GOOSE_CELL_PURPOSE` — intent as a first-class cell type. (Completed — `purpose set/get/clear` shell commands; learn_sync doubles Hebbian reward increments when a purpose is active.)
- **Phase 29.7:** Purpose name persistence + purpose-modulated routing. (Completed — purpose name persisted to NVS, restored on boot; `weave_sync` biases routing toward the purpose domain via segment-bounded matching.)
- **Phase 29.6:** First real perception — internal temperature sensor projected as `perception.temp.reading` on the GOOSE fabric. (Completed.)
- **Phase 30:** Substrate Visualization (Loom Viewer). (Completed — push-based telemetry via `goose_telemetry.c` + `tools/loom_viewer.py` + Rerun.io. Shell: `telemetry on/off`. 10 event types, deferred emission outside locks, direct USB JTAG writes.)
- **Phase 31:** Metabolic Regulation. (Completed — two-layer self-governance via `goose_metabolic.c`. Circuit breaker: `sys.metabolic` with instant degradation and hysteretic recovery. Resource governance: per-vital per-sub-pass modulation. Mesh isolation increases discovery. `vitals` + `vitals override` shell commands.)
- **Phase 32:** Collective Unconscious (mesh-shared plasticity deltas).

---

## Lincoln Manifold Method (LMM) Archive

### Cycle 1: Substrate Gaps (v2.3)
- **Problem:** manual Atlas + O(N) resolver latency.
- **Synthesis:** automated SVD scraper (`tools/goose_scraper.py`) + lattice-hashed resolver (`goose_lattice_hash` in `goose_runtime.c`, O(1) average with linear-scan fallback on collision). (Completed)

### Cycle 2: Atmospheric Mesh (v2.4)
- **Problem:** distributed discovery and coordination.
- **Synthesis:** hashed probing + asynchronous holons + postural consensus with per-peer weight cap and inertial hysteresis. (Completed)

### Cycle 3: The Geometric Organism (v2.5)
- **Problem:** declarative logic and self-sculpting topology.
- **Synthesis:** LoomScript DSL + Ternary Neural Manifolds (quorum aggregation via `neuron_quorum`) + reward-gated co-activation Hebbian plasticity in `goose_supervisor_learn_sync`. (Completed)

### Cycle 4: Substrate Hardening (v2.5.1)
- **Problem:** silent data races on the pulse path, unauthenticated mesh arcs, non-reproducible boot.
- **Synthesis:** public `goose_loom_try_lock` around all hot-path mutations, HMAC-SHA256 Aura with replay cache and protocol version byte, NVS-backed key auto-provisioning, RTC magic word for cold-boot determinism, LP RISC-V Coherent Heartbeat. Three-board mesh trial validated cross-board SYNC, QUERY/ADVERTISE, POSTURE, and HMAC rejection. (Completed)
