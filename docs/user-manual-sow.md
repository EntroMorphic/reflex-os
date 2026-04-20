# Reflex OS User Manual — Statement of Work

## Purpose

Create a comprehensive user manual that enables a person with zero prior Reflex OS knowledge to flash, configure, operate, and build on a Reflex OS node using only the serial shell and/or Python SDK. The manual assumes basic terminal literacy but no embedded systems experience.

## Audience

- Makers with an ESP32-C6 board who want to explore a ternary OS
- Developers evaluating Reflex OS for mesh networking projects
- Researchers studying purpose-aware or neuromorphic scheduling
- AI agents that need to operate Reflex nodes programmatically

## Deliverable

A single Markdown document (`docs/USER_MANUAL.md`) structured as progressive disclosure: start simple, add complexity. Each section is self-contained — a reader can skip to any section and get value without reading prior sections.

## Scope

### In Scope

1. Hardware requirements and setup
2. Flashing the OS (release package + from-source paths + updating firmware)
3. First boot experience and what to expect
4. Key concepts (5 essential terms before diving into commands)
5. Shell command reference (every command, with examples and expected output)
6. Purpose system (set, clear, effects on behavior, persistence)
7. Setting up multiple boards (same firmware, different purpose/keys)
8. Mesh networking (joining the atmosphere, sending arcs, peer discovery)
9. Hebbian learning (reward, pain, snapshots, what "learning" means)
10. Holons (what they are, how purpose activates/deactivates them)
11. Python SDK quickstart (install from repo, connect, discover, common patterns)
12. Configuration (WiFi credentials, aura keys, boot behavior)
13. Temperature sensor and LED (hardware I/O from userspace)
14. What can you build? (3-5 project ideas with Reflex OS)
15. Troubleshooting (common failures, recovery procedures)
16. Glossary (every Reflex-specific term defined in one place)

### Out of Scope

- Kernel internals (port assembly, TCB offsets, interrupt allocation)
- Contributing to the codebase (covered in CONTRIBUTING.md)
- Writing TASM programs (covered in docs/tasm-spec.md when compiler ships)
- Architecture deep-dive (covered in docs/architecture.md)
- Security model internals (covered in SECURITY.md)

### Design Decisions

- **Progressive structure with back-references** — sections build on prior sections but include brief pointers ("see Section X for details") so readers who skip ahead can orient quickly.
- **Ground truth from live hardware** — shell command reference derived from `help` output captured on a running board, not from source code inspection alone.
- **SDK install path is explicit** — not on PyPI; documented as `pip install -e sdk/python/` or direct file copy.

## Quality Criteria

- Every shell command shown with exact input AND expected output
- Every SDK example is copy-pasteable and produces the shown result
- No unexplained jargon — first use of any term links to the glossary
- Troubleshooting covers the top 5 failure modes observed in development
- Total length: 1500-2500 lines (comprehensive but not bloated)

---

# Work Breakdown Structure

## Phase 1: Foundation (Sections 1-4)

### 1.1 Hardware Requirements
- Supported boards (XIAO ESP32-C6, ESP32 original)
- USB cable requirements
- Host OS support (macOS, Linux, Windows note)
- What comes in the box vs what you need

### 1.2 Flashing
- Release package method (flash.sh)
- From-source method (idf.py)
- Updating firmware (re-flash with new release zip)
- Verifying successful flash (expected serial output)
- Troubleshooting flash failures (port not found, permission denied, board not responding)

### 1.3 First Boot
- What the boot sequence looks like (annotated serial output from live board)
- The shell prompt
- First command to try (`help`)
- Understanding the boot messages (Boot0, kernel, services, purpose)

### 1.4 Key Concepts (brief orientation before commands)
- **Cell** — a state holder (-1, 0, +1)
- **Route** — a directional connection that propagates state
- **Purpose** — user-declared intent that reshapes OS behavior
- **Holon** — a named group of fields managed as a lifecycle unit
- **Atmosphere** — the broadcast mesh connecting multiple boards

**Effort:** ~250 lines

---

## Phase 2: Core Operations (Sections 5-6)

### 2.1 Shell Command Reference
- Ground truth: captured from `help` on live hardware
- Table of all commands with one-line descriptions
- Detailed subsections for each command group:
  - System: help, reboot, sleep, status, services
  - Purpose: purpose set/get/clear
  - Fabric: goonies ls/find, loom, tapestry
  - Mesh: mesh status/ping
  - Hardware: led on/off/status, temp
  - Learning: snapshot save/load/clear
  - Config: config get/set
  - Debug: atlas verify, vm info, heartbeat, bonsai
- Each command: syntax, example input, example output (captured from hardware)

### 2.2 Purpose System
- What purpose IS (back-reference to Key Concepts, Section 4)
- How to set/get/clear
- What happens when purpose is active:
  - Routing biased toward purpose domain
  - Learning amplified (2x Hebbian)
  - Task priorities adjusted (+3 domain match, +1 general)
  - Holons activated/deactivated
- Persistence across reboots
- Choosing a purpose name (domain naming: "led" matches "agency.led.intent")
- Example workflow: set → observe → reboot → verify

**Effort:** ~500 lines

---

## Phase 3: Advanced Features (Sections 7-10)

### 3.0 Setting Up Multiple Boards
- Flash same firmware to all boards
- Set different purposes on each (purpose determines behavior)
- Provision shared aura key (same key = same mesh trust domain)
- Verify mesh connectivity (mesh ping)
- Observing cross-node arcs

### 3.1 Mesh Networking
- What the Atmosphere is (broadcast mesh over ESP-NOW or 802.15.4)
- How nodes discover each other
- Sending and receiving arcs
- Aura security (HMAC-SHA256, key provisioning via `aura setkey`)
- Monitoring mesh health (mesh status, mesh ping)
- Radio backend differences (ESP-NOW vs 802.15.4)

### 3.2 Hebbian Learning
- What the OS "learns" (route orientations — how signals propagate)
- Reward and pain signals (sys.ai.reward, sys.ai.pain)
- The learning cycle (co-activation → counter → commit → orientation change)
- Purpose amplification (2x learning rate when purpose is active)
- Snapshots (persist learned state, restore after reboot)
- Observing learning: snapshot save, reboot, snapshot load, compare

### 3.3 Holons
- What a holon is (back-reference to Key Concepts)
- How domain tags work ("led" matches purpose "led")
- Automatic activation/deactivation via purpose
- Current built-in holons (autonomy, comm/mesh, agency/led)
- Observing holon state changes in kernel log

**Effort:** ~450 lines

---

## Phase 4: SDK & Integration (Section 11)

### 4.1 Python SDK Quickstart
- Installation: `cd sdk/python && pip install -e .` (not on PyPI)
- Or: copy `reflex.py` + `pip install pyserial`
- Connecting to a node (`ReflexNode("/dev/cu.usbmodem1101")`)
- Auto-discovery (`discover()` → list of connected nodes)
- Purpose management from Python
- Reading sensors (temp, heartbeat)
- Mesh operations (status, ping)
- Error handling patterns (timeouts, disconnections)
- Multi-node scripting example (discover all, set coordinated purposes)
- CLI mode: `reflex-cli /dev/cu.usbmodem1101`

**Effort:** ~250 lines

---

## Phase 5: Reference & Recovery (Sections 12-16)

### 5.1 Configuration
- WiFi credentials (config set wifi_ssid/wifi_pass)
- Aura key provisioning (aura setkey <hex>)
- Boot behavior (boot counter, safe mode threshold)
- Radio backend selection (menuconfig or sdkconfig.defaults)

### 5.2 Hardware I/O
- LED control (on/off, fabric projection as agency.led.intent)
- Temperature reading (raw celsius, ternary state projection)
- Button input (GPIO9, edge detection)

### 5.3 What Can You Build?
- **Sensor mesh** — 3 boards with temp sensors, purpose "monitor", mesh aggregates readings
- **Purpose-aware lighting** — purpose "focus" dims ambient, "relax" warms, learned over time
- **Distributed learning experiment** — same task on N boards, each learns independently, compare snapshots
- **AI agent workspace** — Python SDK + LLM tool-use for autonomous hardware control
- **Live demo** — closed-loop: set purpose → watch routing change → trigger reward → observe learning commit

### 5.4 Troubleshooting
- Board not responding on USB (power cycle, BOOT button, different cable)
- Boot loop (NVS corruption, safe mode, raised threshold to 10)
- "Device not configured" error (USB-JTAG wedge — needs UART adapter or long unplug)
- No mesh peers found (same aura key? same radio backend? within range?)
- Purpose not affecting behavior (domain naming: "led" must appear as dot-delimited segment)
- SDK timeout errors (board busy with WiFi init — increase timeout or wait after boot)

### 5.5 Glossary
- **Arc** — a single mesh broadcast packet (source MAC + payload + HMAC)
- **Atmosphere** — the broadcast mesh layer connecting Reflex nodes
- **Aura** — HMAC-SHA256 authentication on mesh packets
- **Boot0** — custom 5.2KB second-stage bootloader (replaces ESP-IDF default)
- **Cell** — atomic state holder with a ternary value (-1, 0, +1) and a geometric coordinate
- **Commit** — when a Hebbian counter crosses threshold, the learned orientation becomes permanent
- **Field** — a collection of routes pulsed together at a fixed rhythm
- **GOOSE** — Geometric Ontologic Operating System Execution (the substrate)
- **GOONIES** — the naming registry mapping human-readable names to geometric coordinates
- **Hebbian counter** — per-route accumulator tracking co-activation under reward
- **Holon** — a named group of fields managed as a lifecycle unit
- **Kernel Supervisor** — highest-priority task that modulates scheduling via policy
- **Loom** — the active ternary fabric (256 cells in RTC RAM)
- **Orientation** — the sign (+1 or -1) that a route applies to propagated state
- **Policy Tick** — 1Hz evaluation of purpose, Hebbian maturity, and holon state
- **Purpose** — user-declared intent that biases routing, amplifies learning, and adjusts priorities
- **Route** — directional connection between cells carrying ternary state
- **Sanctuary Guard** — substrate-level security (MMIO isolation, authority sentries)
- **Snapshot** — persisted learned_orientation + hebbian_counter from all supervised routes
- **Tapestry** — the geometric interpretation of the hardware surface
- **Trit** — a balanced ternary digit: -1, 0, or +1

**Effort:** ~500 lines

---

## Total Estimated Effort

| Phase | Sections | Lines | Dependencies |
|-------|----------|-------|-------------|
| 1: Foundation | 1-4 | ~250 | None |
| 2: Core Operations | 5-6 | ~500 | Phase 1 |
| 3: Advanced | 7-10 | ~450 | Phase 2 |
| 4: SDK | 11 | ~250 | Phase 1 |
| 5: Reference | 12-16 | ~500 | Phases 2-3 |
| **Total** | **16** | **~1950** | — |

## Delivery Order

Phases 1 and 2 first (a user can operate the system). Then Phase 4 (programmability). Then Phases 3 and 5 (depth and reference).
