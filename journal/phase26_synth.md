# Synthesis: Phase 26 (LoomScript DSL)

## Architecture
Moving from "Instructional Code" to "Topological Specification." We will implement a high-level DSL that compiles into native GOOSE manifestations (Routes and Transitions).

## Key Decisions
1. **Loom Fragment Format:** A `.loom` file is a packed binary containing:
   - Header (Magic, Version, Fragment Name Hash).
   - Name Table (Strings or Hashes to resolve via GOONIES).
   - Route Table (Source Index, Sink Index, Orientation, Coupling).
   - Transition Table (Target Index, Rhythm, TASM code).
2. **The `weave` Primitive:** The primary keyword for establishing flow. `weave perception.button -> agency.led` compiles into a Software Route.
3. **The `rhythm` Block:** Defines temporal evolution. `rhythm heartbeat { target: agency.led; every: 500ms; logic: "TADD R1, R1" }`.

## Implementation Spec
### 1. LoomScript Grammar (`tools/loomc.py`)
- **Lexer:** Handles `weave`, `holon`, `rhythm`, `as`, `->`.
- **Semantic Analysis:** Validates names against the Shadow Atlas.
- **Generator:** Produces the `loom_fragment_t` binary.

### 2. OS Loader (`goose_library.c`)
Add `goose_weave_loom(const uint8_t *buffer, size_t size)`:
- Iterates through the `.loom` manifest.
- Resolves each name via `goonies_resolve_cell()`.
- Allocates routes and transitions in the Tapestry.

### 3. Face-Melting "Live" Weaving
Add a `loom load <filename>` command to the shell that reads a `.loom` file from the `storage` partition and weaves it into the active Loom instantly.

## Success Criteria
- [ ] `blink.ls` is human-readable and under 10 lines.
- [ ] Compiling `blink.ls` produces a `.loom` file under 256 bytes.
- [ ] Loading `blink.ls` on the XIAO starts the LED oscillating without a firmware flash.
