# Reflex OS

Reflex OS is a binary-hosted ternary operating environment for the Seeed Studio XIAO ESP32C6, built on ESP-IDF. It extends binary silicon with a software-defined ternary machine that manages physical hardware.

The project provides a complete "Soft Silicon" stack:

- **Ternary Core:** Balanced ternary semantics, 32-bit packed bytecode (v2), and an MMU-backed shared memory model.
- **Acceleration:** A software-defined L1 Soft-Cache with MESI-lite coherency for high-speed ternary execution.
- **Coordination:** A high-speed Ternary Message Fabric for 1-to-1 task and service communication.
- **Supervisor:** A background ternary assembly program that manages physical hardware (LED, Button) via the fabric.

## Hardware

- **Board:** Seeed Studio XIAO ESP32C6
- **Flash:** 4MB (OTA-ready layout)
- **Console:** USB Serial/JTAG (Native)
- **I/O:** Onboard LED and User Button (GPIO 9) integrated into the Ternary Fabric.

## Developer Tools

- **TASM:** The Reflex Ternary Assembler (`tasm.py`). Compiles `.tasm` assembly into `.rfxv` binary images with CRC32 integrity and opcode-aware validation.
- **Hex Loader:** Copy-paste assembled binaries directly into the board via the shell.

## Docs

- `OS-PRD.md`: product requirements
- `OS-BACKLOG.md`: implementation backlog and status
- `ARCHITECTURE.md`: project module layout and boundaries
- `TERNARY-ARCHITECTURE.md`: ternary model decisions
- `TASM-SPEC.md`: assembler syntax and opcode mapping
- `VM-LOADER-V2.md`: packed bytecode specification
- `VM-CACHE.md`: soft-cache and coherency specification
- `VM-SYSCALLS.md`: host syscall bridge contract
- `IMPLEMENTATION-STATUS.md`: current hardware-validated state
- `docs/REMEDIATION-PLAN.md`: audit findings and remediation execution plan
- `bonsai/`: incubation space for the GOOSE paradigm (Geometric Ontologic Operating System Execution)

## Bonsai (GOOSE Paradigm)

Bonsai is the research space where the deeper geometric execution model is being proven on the C6 silicon.

- **Ontology:** Reinterpreting hardware as a "State Ecology" (`bonsai/GOOSE.md`).
- **Proof Ladder:** 5 phases of hardware-validated proofs, from atomic cells to native silicon loop intersections (`bonsai/ROADMAP.md`).
- **Notation:** First-class routing and orientation rules for self-choreographing hardware (`bonsai/NOTATION.md`).

## Shell Commands

Current shell commands:

- `help`
- `reboot`
- `led status`: Query the physical LED state.
- `bonsai exp1 <start|stop|status>`: Edge-attention cell proof (physical button).
- `bonsai exp2 <start|stop|status>`: Autonomous rhythm field proof (blinking).
- `bonsai exp3a <start|stop|status>`: Multi-field composition ($A \times B$ multiplication).
- `bonsai exp4 <connect|invert|detach>`: Regional geometry (GPIO Matrix patch).
- `bonsai exp5 run`: Silicon Loop intersection (Native Ternary Hardware Compute).
- `version`, `uptime`, `heap`
- `config <get|set>`
- `wifi <status|connect>`
- `vm loadhex <HEX>`
- `fabric send <to> <op> <value>`
- `vm task <start|stop|status>`
- `vm info`

## Build & Flash

```bash
# Export ESP-IDF
source /Users/aaronjosserand-austin/Projects/esp-idf/export.sh

# Build
idf.py build

# Flash (Forcing download mode)
python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem1101 --before usb_reset write_flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/reflex_os.bin
```

## Validation Status

The current remediation pass has been validated on the XIAO ESP32C6 for:

- Ternary Supervisor background execution
- Fabric-native Button and LED coordination
- Soft-Cache performance and coherency
- Multi-VM Shared Memory (MMU)
- NVS storage and config persistence
- Asynchronous Event Bus stability
- 32-bit Packed Loader with CRC32 verification

Red-team checks executed on device:

- truncated binary rejection in `vm loadhex`
- corrupted CRC32 rejection in `vm loadhex`
- loaded-image background task start path
- supervisor-controlled LED toggle via injected fabric message to VM node `7`
- direct LED toggle via injected fabric message to node `1`
