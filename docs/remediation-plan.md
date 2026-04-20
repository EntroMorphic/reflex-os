# Remediation Plan — Remaining Gaps

## Scope

Four highest-leverage gaps, executed in dependency order. Each deliverable is self-contained — builds, tests, and validates independently before the next begins.

## Priority Order

```
R1: KV Sector Compaction (~50 LOC)
    ↓ (storage must work reliably for all subsequent features)
R2: Integration Test (~100 LOC)
    ↓ (validates the closed loop before we extend it)
R3: MMIO Sync Layer (~210 LOC)
    ↓ (extends the substrate across the mesh)
R4: TASM Compiler (~500 LOC)
    (unlocks user-programmability)
```

---

## R1: KV Sector Compaction

### Problem

`reflex_kv_flash.c` writes entries to a single 4KB sector until it's full, then returns `REFLEX_ERR_NO_MEM`. With typical usage (purpose, config, snapshots), this fills in ~50-100 write cycles. No garbage collection.

### Solution

When the active sector is full, compact live entries (latest value per key) to the next sector, erase the old one. Round-robin across 6 sectors (24KB total at 0x9000).

### Implementation

In `reflex_kv_flash.c`, add:

```c
static reflex_err_t kv_compact(void) {
    // 1. Pick next sector: (s_active_sector + 1) % KV_NUM_SECTORS
    // 2. Erase it
    // 3. Write new header with incremented sequence
    // 4. For each unique key in current sector (last occurrence wins):
    //    Copy entry to new sector
    // 5. Update s_active_sector, s_active_seq, s_write_offset
}
```

Call site: in `kv_write_entry`, when `s_write_offset + entry_size >= KV_SECTOR_SIZE`, call `kv_compact()` then retry the write.

### Deduplication logic

Scan current sector FORWARD (variable-length entries can't be scanned backwards). Track last-seen offset per key (namespace hash + key string). After full scan, copy only the entries at their last-seen offsets to the new sector. This ensures only the LATEST value per key survives compaction.

### Test

Add to `tests/host/test_main.c`:
- Write 100 entries to fill a sector
- Verify compaction triggers
- Verify latest values survive, old values don't

### Risk

- Power-loss during compaction loses uncommitted data (acceptable — same as NVS)
- Flash wear: each compaction erases one sector. 24KB across 6 sectors = 100K erases per sector lifetime. At 1 compaction/day = 274 years. No concern.

### Effort

~50 LOC (compact function) + ~30 LOC (test) = ~80 LOC. 2 hours.

---

## R2: Integration Test

### Problem

Host tests cover individual modules (ternary, crypto, tasks, queues, scheduler) but nothing tests the **closed loop**: purpose → routing bias → Hebbian learning → priority adjustment. A regression in the interaction between these modules would go undetected.

### Solution

A host-side integration test that:
1. Initializes the GOOSE fabric (cells, routes, fields)
2. Sets a purpose
3. Runs a simulated pulse cycle
4. Triggers reward
5. Verifies Hebbian counter increments
6. Verifies learned_orientation commits at threshold
7. Verifies priority adjustment via the policy function

### Implementation

`tests/host/test_integration.c` (~100 LOC):

```c
void test_closed_loop(void) {
    // Init fabric with known cells and routes
    // Set purpose matching a route's domain
    // Run learn_sync with reward active
    // Check hebbian_counter incremented by purpose_multiplier=2
    // Repeat until commit threshold
    // Verify learned_orientation committed
    // Call policy tick
    // Verify priority boosted for matching field
}
```

Requires: compile `goose_runtime.c` + `goose_supervisor.c` on host. These use `reflex_task.h` and `reflex_hal.h` (already mocked). The `goose_atmosphere.c` (radio) can be stubbed.

### Dependencies

- R1 must be done first (KV store must not fill during test)
- Mock for `goose_atmosphere.c` (radio send/recv stubs)
- **Pre-step:** compile-check `goose_runtime.c` on host to discover transitive stub requirements (atmosphere, DMA, ETM, atlas may pull in unexpected deps)

### Risk

- GOOSE runtime has many internal dependencies (shadow atlas, atmosphere, DMA, ETM). Likely needs 150-200 LOC of stubs, not 50.
- Pragmatic approach: test only the core path (runtime + supervisor), stub everything else.
- R2 and R3 can proceed in parallel after R1 (R3 doesn't depend on R2).

### Effort

~100 LOC test + ~150 LOC stubs = ~250 LOC. 3 hours.

---

## R3: MMIO Sync Layer

### Problem

Each board's hardware is isolated. Board A can't read Board B's temperature or control Board B's LED without manual coordination.

### Solution

As designed in `docs/design-mmio-sync.md`:
- New arc type `ARC_OP_MMIO_SYNC`
- Route propagation to remote cells emits sync arcs
- Receiver updates phantom cells or applies writes to local hardware
- Staleness timeout (5s → state resets to 0)

### Implementation

| File | Change | LOC |
|------|--------|-----|
| `components/goose/goose_mmio_sync.c` (NEW) | Sync arc encode/decode, broadcast, receive handler | ~120 |
| `components/goose/goose_mmio_sync.h` (NEW) | API: init, write_remote | ~20 |
| `components/goose/goose_runtime.c` | Remote-sink check in route propagation + peer registry | ~20 |
| `components/goose/goose_atmosphere.c` | Register ARC_OP_MMIO_SYNC handler | ~10 |
| `components/goose/goose_supervisor.c` | Staleness timeout in pulse | ~15 |
| `components/goose/include/goose.h` | Phantom cell fields, new arc op, peer registry API | ~15 |
| `shell/shell.c` | `mesh sync` + `mesh peer add <name> <mac>` commands | ~25 |

### Validation

1. Two boards with same aura key
2. Board A: `purpose set monitor`
3. Board B: `temp` (reads locally)
4. Board A: `goonies find peer.<mac>.perception.temp.reading` → shows Board B's temp state
5. Board A: write to `peer.<mac>.agency.led.intent` → Board B's LED turns on

### Dependencies

- R1 (KV compaction) — sync state might persist
- R2 (integration test) — validates the substrate before extension

### Risk

- Bandwidth: hot-tier broadcast at 10Hz × 13-byte arc = 130 bytes/sec. Well within ESP-NOW's capacity.
- Security: Sanctuary Guard must reject remote writes to `sys.*` and `perception.*` — add namespace check in receive handler.
- Staleness: if Board B goes offline, its phantom cells on Board A reset to 0 after 5 seconds.

### Effort

~200 LOC. Half day.

---

## R4: TASM Compiler

### Problem

The ternary VM exists (interpreter, loader, memory, cache) but there's no way to CREATE programs for it. The spec (`docs/tasm-spec.md`) defines the assembly language but no toolchain compiles it.

### Solution

A minimal assembler (Python script) that:
1. Reads `.tasm` source files
2. Parses instructions into opcodes
3. Resolves labels
4. Emits binary in the VM's image format
5. Output can be loaded via `app load` shell command

### Implementation

`tools/tasm/tasm.py` (~400 LOC):

```python
# tasm.py — Ternary Assembly Compiler
# Usage: python tasm.py program.tasm -o program.bin

class Assembler:
    def parse_line(self, line) → Instruction
    def resolve_labels(self)
    def emit_binary(self) → bytes
    def write_image(self, path)
```

Plus `vm run <name>` shell command (~50 LOC in `shell/shell.c`):
- References a compile-time-embedded program array
- Loads via `reflex_vm_load_image`
- Starts execution

Plus 3 example programs in `examples/tasm/`:
- `blink.tasm` — toggle LED cell every 500ms
- `count.tasm` — count to 9 in ternary, print via log syscall
- `respond.tasm` — read a cell, write its inverse to another

The compiler outputs a `.c` file with `const uint8_t program[] = {...}` for compile-time embedding. Runtime upload (serial/mesh) is v2.

### Validation

```bash
python tools/tasm/tasm.py examples/tasm/blink.tasm -o blink.c
# Rebuild firmware with embedded program
idf.py build && idf.py flash
reflex> vm run blink
reflex> vm info
# Shows: running, pc=0x004, ticks=...
```

### Dependencies

- None (VM already exists and passes self-checks)
- R2 integration test validates the substrate is stable before adding programs

### Risk

- The VM image format must match what `reflex_vm_validate_image` expects. The loader self-check already validates format, so we have a known-good format spec.
- Serial binary transfer is fragile (no framing). Could use base64 encoding for v1.

### Effort

~400 LOC compiler + ~50 LOC shell command + ~50 LOC examples = ~500 LOC. 1 day.

---

## Total

| Item | LOC | Time | Validated by |
|------|-----|------|-------------|
| R1: KV Compaction | ~80 | 2 hours | Host test + on-device write cycle |
| R2: Integration Test | ~250 | 3 hours | `make test` includes closed-loop |
| R3: MMIO Sync | ~225 | 0.5 day | Two-board mesh demo |
| R4: TASM Compiler | ~500 | 1 day | Compile + embed + run on device |
| **Total** | **~1055** | **~2.5 days** | — |

## Execution Order

```
R1 → R2 → R3 → R4
 ↓      ↓      ↓      ↓
test   test   2-board  compile+run
              demo     on device
```

Each step is committed independently. Red-team after R1+R2 (foundation). Red-team after R3 (distributed). Red-team after R4 (programmability).
