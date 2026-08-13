# Reflex OS Security Model: The Sanctuary

Reflex OS implements a "Substrate-First" security model designed to protect the integrity of the **Geometric Loom** against malicious hijacking and physical privilege escalation.

## 0. Boot Integrity (Reflex Boot0)

Reflex OS ships its own second-stage bootloader (`bootloader_components/main/reflex_boot0.c`) in place of the ESP-IDF one. That makes boot integrity our responsibility rather than the SDK's, so it is stated here explicitly.

-   **Image verification:** when the image header sets `hash_appended` (which `esptool` does for our builds), boot0 verifies the appended SHA-256 over the entire image body using the ROM SHA engine *before* any segment is copied into RAM. A mismatch refuses the boot outright; it does not attempt a partial load. An image without an appended hash is loaded with a logged warning.
-   **Bounded segment loads:** every segment whose destination lies in a RAM window must fit entirely inside that window. Validating only the start address would let a malformed image write past the end of SRAM or the 16 KB LP window, over boot0's own stack and code. Segments are validated in a pass that writes nothing, so a bad image cannot leave RAM half-populated.
-   **Boot-loop protection:** a magic-tagged counter in `LP_AON_STORE0` is incremented before load and cleared only on a successful jump. After `BOOT_FAIL_MAX` (3) consecutive failures boot0 halts rather than looping.

**Honest limits.** The appended digest is *not signed*. This is corruption detection, not authentication: anyone who can rewrite flash can rewrite the digest alongside it. It closes the "jumped into a half-erased or truncated image" failure mode, which is the one that occurs in practice, but it is not a defence against an attacker with physical write access. Real authentication requires ESP-IDF Secure Boot with an efuse-burned key, which Reflex OS does not currently enable. Boot0 also does not verify the partition table, and rollback protection is not implemented.

## 1. The Sanctuary Guard (MMIO Isolation)
The Sanctuary Guard prevents non-system ternary cells from mapping to critical hardware registers. 

-   **Safe Agency:** Cells can only be mapped to specific peripherals (GPIO, LEDC, RMT).
-   **The Sanctuary:** Access to the PMU (Power Management), EFUSE, MMU, and Interrupt Matrix is restricted to `sys.` zone cells woven by the core OS.
-   **Enforcement:** `goose_fabric_set_agency` rejects any mapping that attempts to bridge a user-level cell to a Sanctuary address.
-   **The shell's `goonies read` is guarded too.** It dereferences a register the caller names, so it consults the same predicate and refuses a sanctuary address rather than sampling it. It is also `operator`, not `observer`: sampling a register is a side effect, and a role whose definition is "read-only" should not be able to clear an interrupt status bit or pop a live FIFO.
-   **`tapestry signal` cannot write the `sys.` zone.** It is the one shell path that sets a cell's state from an operator-supplied number, so it refuses a `sys.*` name outright — the same namespace rule the MMIO sync layer applies to remote writes (§8), applied locally. Supervisor state such as `sys.kernel.disposition` is derived by the kernel policy layer, and a hand-signalled value is indistinguishable from a derived one downstream. The name is checked before resolution, so a refusal does not also confirm whether the cell exists. The state is range-checked to a real trit rather than cast: `reflex_trit_t` is a three-value enum, and storing anything else breaks the invariant every consumer relies on.
-   **Probing is guarded too:** the Guard covers *reads*, not only agency binding. The curiosity prober in `goose_supervisor_explore` samples raw MMIO from the shadow atlas before any cell is bound, so it consults `goose_fabric_addr_is_sanctuary` at both the probe gate and the confirming re-read. An MMIO read is not side-effect-free — read-to-clear interrupt status registers and peripheral RX FIFOs are disturbed by being sampled — so an unguarded prober would perturb live peripherals it never bound to.

### Serial shell information surface

The serial shell (`shell/shell.c`) is a trusted administrative interface — it runs with full host privileges and is trust-equivalent to JTAG. After the atlas coverage work in `6bb1f0a` / `c5ee5c6`, the shell's `goonies find <name>` command falls through from the live registry to `goose_shadow_resolve` and reports the MMIO address, bit mask, and ontological type of any of the 12738 SVD-documented registers. This is an intentional expansion of the shell's information surface in service of developer ergonomics; it does not bypass the Sanctuary Guard (the Guard enforces *agency mapping*, not *name resolution*), and anyone with shell access already has equivalent or stronger access to the same information through JTAG/flash read. Future deployments that expose the shell over a less-trusted transport (remote serial, network relay) should gate `goonies find` — or the entire shell — behind an additional authentication layer.

## 2. Role-Based Access (RBA)

The shell implements capability-based role restriction. Every command has a minimum role level:

| Role | Level | Can do |
|------|-------|--------|
| observer | 0 | Read-only: status, goonies ls/find, temp, telemetry display, vitals display |
| agent | 1 | Observer + purpose set/clear, snapshot save/load |
| operator | 2 | Agent + led, vm run/stop, mesh emit/ping/posture, bonsai, goonies read, tapestry signal (non-`sys.` cells) |
| admin | 3 | Everything: reboot, sleep, aura setkey/clear, config set, vm loadhex, loom load, vitals override, snapshot clear, mesh peer add |

The table above is enforced by `shell_required_role()` in `shell/shell_policy.c`, and every row of it — plus every sub-command escalation — is asserted in `tests/host/test_shell_policy.c`. The lookup fails closed: a command with no policy entry requires `admin` rather than defaulting to `observer`, so a command added to the shell without a matching policy entry is locked down rather than exposed.

Sessions default to **admin** (backward compatible). The `auth role <role>` command restricts the session's capability ceiling voluntarily. The Python SDK accepts `role="agent"` in the constructor; commands exceeding the role raise `AccessDenied`.

This is **operational safety**, not cryptographic security. The serial cable is the trust boundary — physical access bypasses RBA by design. When remote access (WiFi/BLE shell) is added, PIN-based authentication will layer on top of the existing role infrastructure.

Telemetry: `#T:U,<role>` on role transitions.

## 3. The Authority Sentry (Deadlock Observability)
To ensure system durability, the `loom_authority` spinlock is monitored by a cycle-accurate watchdog.

-   **Limit:** 50,000 CPU cycles (~300μs).
-   **Action:** If a core fails to acquire the Loom lock within the limit, the Sentry records `lock_contention_cycles` in field stats, emits a `LOOM_CONTENTION_FAULT` log, and **skips the current pulse**. The in-flight lock holder is not forcibly preempted — breaking another core's lock would permit racing mutation of the fabric.
-   **Purpose:** Makes sustained lock contention observable (stats + log) without introducing data-race corruption. A persistently wedged holder starves pulses and is surfaced for higher-level recovery (e.g., supervisor-triggered reset), rather than being masked by silent unsynchronized execution.

## 4. Atmospheric Aura (Geometric Authentication)
Radio-based state propagation (Arcing) is protected by a keyed message authentication layer.

-   **The key is validated on the way in.** `aura setkey` is the only place in the shell whose bytes have no downstream check — they *are* the HMAC key, so a bad parse cannot be caught later. It previously decoded with `strtoul(pair, NULL, 16)`, which maps any non-hex character to 0 without signalling: `aura setkey <32 non-hex chars>` provisioned an **all-zero key** — a guessable shared secret — and reported "key provisioned". Decoding now goes through `shell_parse_hex` (`shell/shell_parse.c`), which rejects any non-hex character, enforces the length, and leaves the output buffer untouched on failure so a partially-typo'd key can never be written. Covered by `tests/host/test_shell_parse.c` and exercised on hardware by `tests/hardware/validate_shell.py`.
-   **Provisioning and de-provisioning:** a factory-fresh board generates its own random 16-byte key on first boot, so two new boards do not trust each other by default. `aura setkey <32 hex>` pairs boards by placing a shared secret on each; `aura clear` erases it, returning that board to isolation and causing a fresh per-board key to be generated on the next boot. De-provisioning matters because pairing is a disclosure — whoever performed it knows the secret — and previously the only way to undo it was wiping all of NVS, which also destroys purpose, learned plasticity and the peer table.
-   **The Aura:** Every Arc packet carries a **64-bit** Aura computed as `HMAC-SHA256(GOOSE_AURA_KEY, version || op || coord || name_hash || state || nonce)` truncated to the first 64 bits. Widened from 32 bits at protocol epoch `0x03`: truncation caps collision resistance at the birthday bound, so 32 bits meant roughly 2^16 forgery attempts — trivially reachable. 64 bits moves that to about 2^32, at a cost of four bytes on a packet far below the ESP-NOW payload limit. Peers still running epoch `0x02` log `AURA_VERSION_MISMATCH` rather than silently failing the MAC check.
-   **The Ground:** Nodes will "ground" (ignore) any Arc packet whose Aura does not match the locally computed value.
-   **Purpose:** Prevents "Geometric Spoofing" where an unauthorized node attempts to hijack local hardware by broadcasting fake state changes.
-   **Key provisioning:** on first boot (or NVS wipe), a unique 16-byte key is generated via `esp_fill_random()` and persisted to NVS under `goose/aura_key`. Two factory-fresh boards therefore do not accidentally trust each other. Pairing requires an operator to run `aura setkey <hex>` on both peers with a chosen shared key. A compile-time default is retained only as a last-resort fallback if NVS writes fail.
-   **Replay protection:** a 64-slot time-bounded replay cache in the RX path rejects packets whose `(src_mac, nonce)` pair has been seen within a 5-second window. Stale entries outside the window are treated as empty. The slot index blends the nonce low bits with the last two bytes of the sender MAC, so two peers with colliding nonce low bits land in different slots and don't evict each other.
-   **Known limits:** the Aura key is still extractable by anyone with JTAG or flash-read access to the board (unchanged by this provisioning model); `esp_random()` entropy quality at very early boot is bounded by the Wi-Fi/RF subsystem seed. Wire-format Aura is 32 bits (HMAC-SHA256 truncated), capping collision resistance at the birthday bound (~2^16); a future protocol epoch can bump `GOOSE_ARC_VERSION` and expand the Aura field. These limits are tracked in [`docs/implementation-status.md`](docs/implementation-status.md) "Known Gaps".

## 5. G.O.O.N.I.E.S. Zone Protection
Hierarchical naming is protected at the root.

-   **Immutable Zones:** Names starting with `sys.` or `agency.` are immutable once woven.
- **Shadow Protection:** Attempting to re-register a protected name to a new coordinate is rejected.
- **Shadow Hijack Check:** The registry performs a mandatory check against the Flash-native **Shadow Atlas** before any registration. This prevents malicious scripts from "squatting" on protected names (like `sys.pmu.*`) before the system has paged them into RAM.

-   **Purpose:** Ensures that the mapping from `agency.led.intent` to physical silicon cannot be intercepted by user scripts.

## 6. Loom Quota & Eviction (Resource Isolation)
To prevent "Loom Bloat" (Resource Exhaustion attacks), the substrate implements a self-balancing memory policy.

- **Pinned Cells:** Boot-time core nodes (Origins, Primary Agency) are marked as `GOOSE_CELL_PINNED` and can never be evicted.
- **Shadow Recycling:** When the 256-slot RAM Loom reaches capacity, the allocator uses a round-robin policy to find and recycle an unpinned shadow node.
- **Registry Coherency:** G.O.O.N.I.E.S. is automatically updated during eviction to ensure the name-to-coord mapping remains consistent.
- **Purpose:** Protects the system from being paralyzed by an attacker resolving thousands of unique shadow nodes to overfill the RTC RAM Hearth.

## 7. Mesh Integrity (Aura Shield)
Atmospheric discovery and swarming are protected from external interference.
- **Hashed Discovery:** `ARC_OP_QUERY` uses name-hashes to prevent passive observers from mapping the mesh's physical topology.
- **Discovery Throttling:** Nodes rate-limit incoming queries to 10Hz, preventing Mesh Denial-of-Service (DoS) attacks.
- **Inertial Hysteresis:** Swarm consensus requires an accumulated weight (+/- 10) before flipping local posture.
- **Accumulator Saturation:** To prevent consensus hijacking, the swarm accumulator is capped at +/- 100. This ensures the mesh remains responsive to the majority and prevents a single node from "locking" the system state indefinitely.
- **Self-Arc Suppression:** Nodes ignore postural arcs originating from their own MAC address, preventing atmospheric feedback loops and radio saturation.
- **Auto-Discovery Security:** `ARC_OP_DISCOVER` broadcasts are authenticated with the same Aura HMAC. Only boards with a matching key can register as peers. Discovery arcs carry the device name in the coord field (max 8 chars). Unknown-key discover arcs are silently dropped at the Aura validation stage.

## 8. MMIO Sync Layer Security (Distributed Surface)
The MMIO Sync Layer extends the mesh to carry cell state across boards. Security enforcement:
- **Namespace restriction:** Remote writes only to `agency.*` cells. The receive handler rejects any sync arc targeting a `sys.*` cell.
- **Phantom cell isolation:** Remote state lives in phantom cells (peer_id != 0) which are separate from local cells. A stale peer's phantom cells reset to state=0 after 5 seconds without update.
- **Aura protection:** All sync arcs (`ARC_OP_MMIO_SYNC`) carry the same HMAC-SHA256 Aura as other arc types. Unauthenticated sync arcs are dropped.
- **Peer registry:** max 8 peers. Auto-registration from unknown MACs creates an `auto_XXXX` entry for observability but does not bypass Aura checks.

## 9. Application Integrity (LoomScript Quotas)
To protect the system from resource exhaustion at the application layer, LoomScript fragments are strictly managed.
- **Fragment Quotas:** The system limits the number of active LoomScript fragments (max 8) and the routes per fragment (max 32).
- **Allocation Validation:** Fragment manifests are validated before heap allocation to prevent memory exhaustion attacks.
