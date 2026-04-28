# Reflex OS User Manual

## 1. Hardware Requirements

### Supported Boards

| Board | Architecture | Role |
|-------|-------------|------|
| Seeed XIAO ESP32-C6 | RISC-V | Primary (full OS + kernel) |
| ESP32 (any variant) | Xtensa | Mesh peer |

### What You Need

- One or more supported boards
- USB-C cable (data-capable, not charge-only)
- Computer with Python 3.8+ (macOS, Linux, or Windows)
- Terminal application (Terminal.app, iTerm, or PuTTY)

---

## 2. Flashing Reflex OS

### Method A: Release Package (Recommended)

No ESP-IDF toolchain required. Download the release zip and run:

```bash
pip install esptool
unzip reflex-os-*.zip
cd reflex-os-*/
./flash.sh
```

The script auto-detects your board's serial port. To specify manually:

```bash
./flash.sh /dev/cu.usbmodem1101
```

### Method B: Build from Source

```bash
git clone https://github.com/EntroMorphic/reflex-os.git
cd reflex-os
source ~/Projects/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash
```

### Updating Firmware

Same as initial flash — re-run `./flash.sh` with the new release zip, or rebuild from source and re-flash. Your purpose and learned state persist in flash (NVS partition) across updates.

### Verifying Success

After flash, connect to the serial console:

```bash
screen /dev/cu.usbmodem1101 115200
```

You should see:

```
[reflex.boot0] Reflex OS bootloader
[reflex.boot0] factory partition at 0x10000
[reflex.boot0] jumping to 0x408002e6
...
reflex>
```

If you see `reflex>`, the OS is running.

### Troubleshooting Flash Failures

| Symptom | Fix |
|---------|-----|
| "No serial data received" | Power-cycle the board (unplug/replug USB) |
| "Port not found" | Check cable is data-capable, try different USB port |
| "Permission denied" | Add yourself to `dialout` group (Linux) or check System Preferences (macOS) |
| Board unresponsive after flash | Hold BOOT button while plugging in, then retry flash |

---

## 3. First Boot

When Reflex OS boots, you see this sequence:

```
[reflex.boot0] Reflex OS bootloader          ← Custom bootloader (Boot0)
[reflex.boot0] factory partition at 0x10000
[reflex.boot0] jumping to 0x408002e6

  ╔══════════════════════════════════════╗
  ║       Reflex OS Kernel Active        ║  ← Kernel supervisor started
  ╚══════════════════════════════════════╝

[reflex.kernel] tick=100Hz supervisor=active
purpose restored from NVS: "liberated"       ← Previous purpose loaded
service_started=led                          ← Services coming online
service_started=temp
service_started=wifi
service_started=system-vm
reflex>                                      ← Shell ready
```

Type `help` to see all available commands:

```
reflex> help
system:  help, reboot, sleep <s>, status, services, config <get|set>
led:     led <on|off|status>
fabric:  goonies <ls|find name>, atlas verify, temp, heartbeat
purpose: purpose <set name|get|clear>
learn:   snapshot <save|load|clear>
mesh:    mesh <mac|emit|query|posture|stat|status|ping|peer add/ls>
vm:      vm <info|run name|stop|list|loadhex hex>
aura:    aura setkey <32 hex chars>
bonsai:  bonsai <exp1|exp2|exp3a|exp4|exp5|runtime> <start|status|...>
```

---

## 4. Key Concepts

Before diving into commands, five terms you'll encounter everywhere:

**Cell** — The atomic unit of the Reflex substrate. Holds a single ternary value: -1 (inhibit), 0 (neutral), or +1 (excite). Every piece of hardware, intent, and sensor reading is a cell.

**Route** — A directional connection between two cells. When the source cell changes, the route propagates the signal to the sink cell, modified by the route's orientation (+1 or -1).

**Purpose** — A string you declare (e.g., "photography", "mesh", "led") that tells the OS what you're trying to do. Purpose biases routing, amplifies learning, and adjusts task priorities.

**Holon** — A named group of fields (pulse-driven route collections) that activate or deactivate together based on purpose. When your purpose matches a holon's domain, that holon comes alive.

**Atmosphere** — The broadcast mesh layer. Multiple Reflex boards communicate by sending "arcs" (authenticated packets) over the air via ESP-NOW or IEEE 802.15.4.

---

## 5. Shell Command Reference

### System

| Command | Description | Example |
|---------|-------------|---------|
| `help` | List all commands | `help` |
| `reboot` | Software reset (preserves NVS) | `reboot` |
| `sleep <seconds>` | Enter deep sleep with LP timer wakeup after duration | `sleep 60` |
| `status` | System status summary | `status` |
| `services` | List registered services and count | `services` |

```
reflex> services
services=5
0: led
1: button
2: temp
3: wifi
4: system-vm
```

### Purpose

| Command | Description |
|---------|-------------|
| `purpose set <name>` | Declare active purpose (persists to NVS) |
| `purpose get` | Show current purpose |
| `purpose clear` | Remove purpose (reverts to neutral behavior) |

```
reflex> purpose set photography
purpose: active, name="photography" (persisted to NVS)

reflex> purpose get
purpose: active, name="photography"

reflex> purpose clear
purpose: cleared
```

### Hardware I/O

| Command | Description |
|---------|-------------|
| `led on` / `led off` | Control the onboard LED |
| `led status` | Show LED state |
| `temp` | Read die temperature (Celsius + ternary state) |

```
reflex> temp
temp=28.5C state=0
```

### GOOSE Fabric

| Command | Description |
|---------|-------------|
| `goonies ls` | List all registered cell names |
| `goonies find <name>` | Look up a cell by name (live, shadow, or phantom) |
| `atlas verify` | Verify shadow atlas integrity (12,738 entries) |

```
reflex> goonies find agency.led.intent
agency.led.intent coord=(0,0,1) state=0 type=5 [live]
```

### Mesh

| Command | Description |
|---------|-------------|
| `mesh status` | Show mesh state (peers, packets sent/received) |
| `mesh stat` | Detailed mesh stats (rx/tx totals, sync counts, per-peer info) |
| `mesh ping` | Broadcast a ping arc to all peers |
| `mesh mac` | Show this board's MAC address |
| `mesh emit [state]` | Broadcast an arc with the given state (-1, 0, or 1) |
| `mesh query <name>` | Query a cell by name across the mesh |
| `mesh posture <state> <weight>` | Broadcast a postural swarm arc (state: -1/0/1, weight: 0-4) |
| `mesh peer add <name> <mac>` | Register a named peer by MAC address (XX:XX:XX:XX:XX:XX) |
| `mesh peer ls` | List all registered mesh peers |

### Learning

| Command | Description |
|---------|-------------|
| `snapshot save` | Persist all learned route orientations to flash |
| `snapshot load` | Restore learned state from flash |
| `snapshot clear` | Erase all saved learning |

### Configuration

| Command | Description |
|---------|-------------|
| `config get <key>` | Read a config value |
| `config set <key> <value>` | Write a config value |
| `aura setkey <hex>` | Set the mesh authentication key (16 bytes hex) |

```
reflex> config set wifi_ssid MyNetwork
reflex> config set wifi_pass secret123
reflex> reboot
```

### Ternary VM

| Command | Description |
|---------|-------------|
| `vm info` | VM status (status, IP, steps, loaded program) |
| `vm run <name>` | Load and execute an embedded program by name |
| `vm stop` | Halt the running VM |
| `vm list` | List all embedded programs |
| `vm loadhex <hex>` | Load a packed binary image from hex string |
| `tasm.py --upload <port>` | Compile and send a .tasm program without reflashing (host tool) |

### Debug

| Command | Description |
|---------|-------------|
| `heartbeat` | LP coprocessor pulse count |
| `telemetry <on\|off>` | Enable/disable real-time substrate telemetry streaming to the host |
| `vitals` | Show vital cell states and metabolic state |
| `vitals override <vital> <state>` | Inject synthetic vital state (-1, 0, +1) for bench testing |
| `vitals clear` | Clear all overrides; resume reading real hardware |
| `bonsai <exp> <start\|status>` | Bonsai hardware experiments (exp1-exp5, runtime) |
| `auth` | Show current session role (observer, agent, operator, admin) |
| `auth role <role>` | Set session capability ceiling. Commands requiring a higher role are denied. Default: admin. |

---

## 6. The Purpose System

### What Purpose Does

When you set a purpose, four things change simultaneously:

1. **Routing is biased** — the autonomous fabrication engine (`weave_sync`) preferentially connects needs to capabilities in your purpose domain. If purpose is "led", a need for `.led` service connects to `agency.led.intent` before falling back to generic matches.

2. **Learning is amplified** — Hebbian reward increments are doubled (2x `purpose_multiplier`). The OS learns faster in the domain you've declared.

3. **Priorities are adjusted** — the kernel supervisor boosts pulse tasks for fields with routes touching the purpose domain (+3 priority). Non-matching fields get +1. This means purpose-relevant processing is more responsive.

4. **Holons activate** — field groups tagged with your purpose domain come alive. Groups tagged with other domains deactivate (drop to base priority).

### Domain Naming

Purpose matching uses **dot-boundary** semantics:

- Purpose `led` matches cell name `agency.led.intent` (`.led.` as a segment)
- Purpose `led` does NOT match `misled` or `ledger` (no dot boundary)
- Purpose `mesh` matches `comm.mesh.broadcast`

Choose purpose names that correspond to segments in your cell naming scheme.

### Persistence

Purpose persists to NVS (flash). After `reboot` or power loss, the OS restores your declared purpose and immediately re-applies all four effects.

### Example Workflow

```
reflex> purpose set led
purpose: active, name="led" (persisted to NVS)
[reflex.kernel] holon agency activated (domain=led)
[reflex.kernel] policy: purpose=led fields=1

reflex> purpose clear
purpose: cleared
[reflex.kernel] holon agency deactivated (domain=led)
[reflex.kernel] policy: purpose=(cleared) fields=1
```

---

## 7. Setting Up Multiple Boards

### Flash All Boards

Flash the same firmware to all boards:

```bash
./flash.sh /dev/cu.usbmodem1101    # Board A
./flash.sh /dev/cu.usbmodem1401    # Board B
```

### Set Different Purposes

Each board can have a different purpose — they specialize:

```
Board A> purpose set sensor
Board B> purpose set display
```

### Provision Shared Aura Key

Boards must share an authentication key to communicate:

```
Board A> aura setkey 0123456789abcdef0123456789abcdef
Board B> aura setkey 0123456789abcdef0123456789abcdef
```

### Verify Mesh

```
Board A> mesh ping
mesh ping: broadcast rc=0x0
Board A> mesh status
mesh: peers=1 rx=54 tx=0 sync=54 mmio_sync_rx=0 mmio_sync_tx=0
  boardb active (last: 0.0s)
```

---

## 8. Mesh Networking

### The Atmosphere

Every Reflex node broadcasts "arcs" — authenticated packets containing cell state. When a remote cell changes state, peers receive the arc and can update their local fabric.

### Radio Backends

| Backend | Range | Blob-free | Selection |
|---------|-------|-----------|-----------|
| ESP-NOW (WiFi) | ~100m | No (WiFi binary blob) | Default |
| IEEE 802.15.4 | ~30m | Yes (open-source) | `CONFIG_REFLEX_RADIO_802154=y` |

### Security

All mesh packets are authenticated with HMAC-SHA256 ("Aura"). Boards with different keys ignore each other's arcs. A 64-slot replay cache prevents packet replay attacks.

### Auto-Discovery

Boards sharing the same Aura key discover each other automatically via `ARC_OP_DISCOVER` heartbeat broadcasts every 10 seconds. Manual `mesh peer add` is no longer required for paired boards. Discovered peers appear in `mesh peer ls` and persist to NVS across reboots.

### Monitoring

```
reflex> mesh status
mesh: peers=1 rx=54 tx=0 sync=54 mmio_sync_rx=0 mmio_sync_tx=0
  charlie active (last: 0.0s)
```

---

## 9. Hebbian Learning

### What the OS Learns

Routes have a `learned_orientation` field that starts at 0 (unlearned). Through repeated co-activation under reward, the route "commits" an orientation (+1 or -1) that permanently changes how signals propagate through it.

### The Learning Cycle

1. You (or another system) sets `sys.ai.reward` cell to +1
2. The supervisor scans all supervised routes
3. For each route where source and sink are co-active (same sign): counter += 1 × purpose_multiplier
4. For each route where they disagree: counter -= 1 × purpose_multiplier
5. When counter reaches ±8 (commit threshold): `learned_orientation` commits
6. The route now permanently applies that sign to all future propagation

### Autonomous Reward

The supervisor automatically evaluates purpose fulfillment at 1Hz. When purpose-relevant routes converge (source and sink states match), `sys.ai.reward` is set to +1. When a hardware output is stuck at the wrong sign for 5+ seconds, `sys.ai.pain` is set to -1. No manual reward signal is needed — the OS learns from its own observations.

### Pain (Unlearning)

Setting `sys.ai.pain` to -1 decays all counters toward zero. This undoes uncommitted learning without erasing committed orientations.

### Snapshots

Learned state lives in RAM. To survive reboots:

```
reflex> snapshot save
snapshot saved: 1 fields, 3 routes

reflex> reboot
...
reflex> snapshot load
snapshot loaded: 3 routes restored
```

---

## 10. Holons

### What Holons Are

A holon is a named group of fields (each field is a collection of routes pulsed together). Holons have a **domain tag** that determines when they're active.

### Built-in Holons

| Holon | Domain | Active when purpose contains... |
|-------|--------|------|
| autonomy | (always) | Always active |
| comm | mesh | "mesh" |
| agency | led | "led" |

### Automatic Lifecycle

```
reflex> purpose set led
[reflex.kernel] holon agency activated (domain=led)

reflex> purpose set mesh
[reflex.kernel] holon agency deactivated (domain=led)
[reflex.kernel] holon comm activated (domain=mesh)
```

When a holon deactivates, its fields drop to base priority — they still run but at reduced responsiveness. When it activates, boosts are restored.

---

## 11. Python SDK

### Installation

```bash
cd reflex-os/sdk/python
pip install -e .
```

Or just copy `reflex.py` and install pyserial:

```bash
pip install pyserial
cp sdk/python/reflex.py ./
```

### Connect to a Node

```python
from reflex import ReflexNode

with ReflexNode("/dev/cu.usbmodem1101") as node:
    print(node.purpose_get())       # "liberated"
    node.purpose_set("sensor")
    print(node.temp())              # 28.5
    node.snapshot_save()
```

### Auto-Discover All Nodes

```python
from reflex import discover

nodes = discover()
for n in nodes:
    print(f"{n['port']}: purpose={n['node'].purpose_get()}")
    n['node'].close()
```

### Multi-Node Coordination

```python
from reflex import discover

nodes = discover()
# Set all boards to the same purpose
for n in nodes:
    n['node'].purpose_set("swarm")
    print(f"{n['port']}: set to swarm")
    n['node'].close()
```

### CLI Mode

```bash
reflex-cli /dev/cu.usbmodem1101
# Connected to /dev/cu.usbmodem1101
#   Purpose: liberated
#   Temp: 28.5
#   Services: services=5...

reflex-cli --discover
# /dev/cu.usbmodem1101: purpose=liberated
# /dev/cu.usbmodem1401: purpose=None
# 2 node(s) found
```

### Error Handling

```python
from reflex import ReflexNode
import serial

try:
    node = ReflexNode("/dev/cu.usbmodem1101")
    node.purpose_set("test")
except serial.SerialException as e:
    print(f"Connection failed: {e}")
except RuntimeError as e:
    print(f"Command failed: {e}")
finally:
    node.close()
```

---

## 12. Configuration

### WiFi Credentials

```
reflex> config set wifi_ssid YourNetwork
reflex> config set wifi_pass YourPassword
reflex> reboot
```

After reboot, the WiFi service connects automatically. Not needed for 802.15.4 mode.

### Aura Key (Mesh Authentication)

```
reflex> aura setkey 0123456789abcdef0123456789abcdef
```

All boards in your mesh must share the same 16-byte key. On first boot, a random key is auto-generated.

### Radio Backend

Set in build configuration:

```
idf.py menuconfig → Reflex OS → Radio backend
  [*] IEEE 802.15.4 (blob-free)
  [ ] ESP-NOW (Wi-Fi)
```

---

## 13. Hardware I/O

### LED

The onboard LED is projected into the substrate as `agency.led.intent`:

```
reflex> led on
led=on
reflex> led off
led=off
reflex> led status
led=on
```

When purpose is "led", the LED responds to substrate state changes — routes connecting to `agency.led.intent` control it automatically.

### Temperature

The die temperature sensor reads every 5 seconds and projects into `perception.temp.reading`:

```
reflex> temp
temp=31.2C state=0
```

States: -1 (cold, <30C), 0 (normal, 30-55C), +1 (warm, >55C)

### Button

GPIO9 (onboard button on XIAO ESP32-C6) is monitored by the button service. Press events propagate through the fabric.

### Deep Sleep

Deep sleep uses `esp_deep_sleep_start()` with LP timer wakeup. Sleep current is ~7µA (HP core fully powered down). NVS and RTC RAM survive. On wake, the full boot sequence re-runs.

---

## 14. Loom Viewer (Substrate Visualization)

The Loom Viewer renders the GOOSE substrate in real-time via [Rerun.io](https://rerun.io). The firmware pushes telemetry as state changes occur — the viewer never polls the board.

### Installation

```bash
pip install rerun-sdk pyserial
```

### Usage

```bash
python tools/loom_viewer.py /dev/cu.usbmodem1101
```

The viewer:
1. Connects to the board and sends `telemetry on`
2. Requests `goonies ls` to seed the initial cell inventory
3. Streams live telemetry events to a Rerun window

### What You See

- **Topology graph** — cells as nodes (color by type), routes as edges (color by coupling). Rebuilt on alloc/evict/weave events.
- **Time series** — Hebbian counters per route, system balance, autonomous reward/pain scores.
- **Event log** — cell lifecycle (alloc, evict, weave), mesh arcs (sync, query, discover), purpose changes.

### Telemetry Event Types

| Prefix | Meaning |
|--------|---------|
| `#T:B` | Supervisor equilibrium (10Hz) |
| `#T:V` | Autonomous evaluation — reward score + pain flag (1Hz) |
| `#T:C` | Cell state change (on delta) |
| `#T:R` | Route sink write (on delta) |
| `#T:H` | Hebbian counter update |
| `#T:W` | New route autonomously woven |
| `#T:A` | Cell allocated |
| `#T:E` | Cell evicted |
| `#T:M` | Mesh arc received |
| `#T:P` | Purpose set or cleared |
| `#T:X` | Metabolic state (metabolic, temp, battery, mesh, heap) |
| `#T:D` | Exploration discovery — the OS found a hot register |

### Manual Telemetry

You can also enable telemetry from the shell without the viewer:

```
reflex> telemetry on
#T:B,1
#T:B,1
#T:V,0,0
...
reflex> telemetry off
```

When disabled (the default), the cost is a single branch-not-taken per hook site (~2 CPU cycles).

---

## 15. What Can You Build?

### Sensor Mesh
Three boards with temperature sensors, purpose "monitor". Each reads its local temp, broadcasts state via atmosphere. A Python script on your laptop discovers all nodes and aggregates readings.

### Purpose-Aware Lighting
Purpose "focus" configures the substrate to dim ambient LEDs. Purpose "relax" warms them. Over time, the Hebbian system learns which routes best serve each purpose — the lighting behavior improves with use.

### Distributed Learning Experiment
Flash N boards with the same firmware. Each runs independently with the same purpose. After a learning period, `snapshot save` on each, then compare: did they converge on the same learned orientations?

### AI Agent Workspace
Connect the Python SDK to an LLM with tool-use. The agent can `purpose_set`, read `temp`, trigger `mesh_ping`, and observe responses — autonomous hardware control via natural language.

### Live Demo
Set purpose → watch kernel log show holon activation → trigger reward signal → observe Hebbian counter increment → wait for commit → see routing behavior change. The full closed loop in 60 seconds.

---

## 16. Troubleshooting

### Board Not Responding on USB

1. Unplug, wait 10 seconds, replug
2. Try a different USB cable (must be data-capable)
3. Try a different USB port
4. Hold BOOT button while plugging in (forces ROM bootloader)

### Boot Loop

The boot counter increments each boot. If it reaches 10, the board enters safe mode. To fix:

```bash
# Erase NVS to reset boot counter:
python3 -m esptool --chip esp32c6 -p /dev/cu.usbmodem1101 erase_region 0x9000 0x6000
# Then re-flash:
./flash.sh
```

### "Device not configured" Error

The USB-JTAG controller is wedged. This happens after flash corruption. Options:

1. Unplug for 60+ seconds (let capacitors drain)
2. Try a different USB port
3. Use a USB-to-UART adapter on the TX/RX header pins

### No Mesh Peers Found

- Both boards must have the **same aura key** (`aura setkey`)
- Both must use the **same radio backend** (both ESP-NOW or both 802.15.4)
- Boards must be within **radio range** (~100m for ESP-NOW, ~30m for 802.15.4)
- After setting aura key, **reboot both boards**

### Purpose Not Affecting Behavior

Domain matching uses **dot boundaries**. Purpose "led" matches cells named `agency.led.intent` but not `misled`. Check your cell names:

```
reflex> goonies ls
```

Look for cells containing your purpose as a dot-delimited segment.

### SDK Timeout Errors

The board takes ~3 seconds to boot. If you connect immediately after flash:

```python
import time
time.sleep(4)  # Wait for boot
node = ReflexNode("/dev/cu.usbmodem1101")
```

---

## 17. Glossary

| Term | Definition |
|------|-----------|
| **Arc** | A single mesh broadcast packet (source MAC + payload + HMAC) |
| **Atmosphere** | The broadcast mesh layer connecting Reflex nodes |
| **Aura** | HMAC-SHA256 authentication on mesh packets |
| **Boot0** | Custom 5.2KB second-stage bootloader |
| **Cell** | Atomic state holder: value is -1, 0, or +1 |
| **Commit** | When Hebbian counter crosses threshold, orientation becomes permanent |
| **Field** | Collection of routes pulsed together at a fixed rhythm |
| **GOOSE** | Geometric Ontologic Operating System Execution |
| **GOONIES** | Naming registry: human-readable names → geometric coordinates |
| **Hebbian counter** | Per-route accumulator tracking co-activation under reward |
| **Holon** | Named group of fields managed as a lifecycle unit |
| **Kernel Supervisor** | Highest-priority task, modulates scheduling via 1Hz policy |
| **Loom** | Active ternary fabric (256 cells in RTC RAM) |
| **Orientation** | Sign (+1 or -1) applied by a route during propagation |
| **Policy Tick** | 1Hz evaluation of purpose, Hebbian maturity, holon state |
| **Purpose** | User-declared intent that reshapes OS behavior |
| **Route** | Directional connection carrying ternary state between cells |
| **Sanctuary Guard** | Substrate-level security (MMIO isolation, authority sentries) |
| **Snapshot** | Persisted learned state (orientations + counters) |
| **Tapestry** | Geometric interpretation of the hardware surface |
| **Trit** | Balanced ternary digit: -1, 0, or +1 |
