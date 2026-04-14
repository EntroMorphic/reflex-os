# Remediation Plan

## Purpose

This plan closes the seven audit findings that blocked Reflex OS from being a reliable, reviewable, and hardware-verifiable repository.

Status: completed on the current remediation branch, including rebuild, flash, and on-device red-team validation.

## Scope

1. Make `vm loadhex` and `vm task start` operate on the same loaded program.
2. Initialize the ternary fabric during boot before services and VM messaging depend on it.
3. Ensure background VM tasks run with `REFLEX_NODE_VM` identity.
4. Fix supervisor receive-loop semantics and tighten assembler validation so invalid branch syntax is rejected.
5. Bring the shell `vm load` command into conformance with the v2 image format.
6. Align loader, tooling, and docs around real CRC32 integrity and actual packed-data loading.
7. Accept the implemented syscall surface, including `DELAY`, during binary validation.

## Execution Plan

### Runtime and Boot

- Add explicit fabric initialization to `main/main.c`.
- Fail closed into the shell if core startup primitives cannot initialize.
- Set VM task node identity during task startup.

### Loader and VM Ownership

- Track ownership of decoded programs and unpacked private memory in VM state.
- Free owned allocations only when appropriate.
- Validate decoded v2 binaries after unpacking.
- Unpack packed ternary data segments into `word18` memory and map them into the MMU.
- Replace the ad hoc checksum with CRC32 in both host tooling and device loader.

### Shell and Supervisor Path

- Cache the last loaded binary so the task runner can start the same artifact that foreground inspection uses.
- Make `vm load` emit a valid v2 image with checksum.
- Fix `TRECV` empty-queue behavior to clear the destination register.
- Fix `supervisor.tasm` branch syntax to test the receive register explicitly.

### Tooling and Documentation

- Make `tasm.py` opcode-aware so malformed instructions are rejected during assembly.
- Update docs to describe the actual contract: branch instructions require a source register and v2 images use CRC32.
- Remove generated artifacts from version control via `.gitignore`.

## Verification

- Rebuild with ESP-IDF.
- Reassemble `test.tasm` and `supervisor.tasm`.
- Flash the board.
- Red-team on device by validating:
  - boot success with fabric ready
  - shell `vm loadhex` plus `vm task start` starts the loaded image
  - supervisor button-to-LED path works end to end
  - malformed binaries are rejected
  - empty receive loops remain stable

## Exit Criteria

- Build passes.
- Repo root no longer accumulates generated binary artifacts by default.
- Docs no longer overclaim or contradict the implementation.
- On-device validation confirms the supervisor path and catches regressions in the messaging/runtime path.

## Result

- Local ESP-IDF build: passed.
- Flash to XIAO ESP32C6: passed.
- `vm loadhex` plus `vm task start` using the same loaded image: passed on device.
- Fabric injection to VM node `7` caused the supervisor to toggle the LED through the fabric: passed on device.
- Direct LED fabric injection to node `1`: passed on device.
- Corrupted binary CRC32 rejection: passed on device.
