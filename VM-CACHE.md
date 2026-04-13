# Reflex OS VM Soft-Cache Specification

## Purpose

This document defines the MVP Soft-Cache and Coherency model for the Reflex OS ternary VM.

The goal is to provide a hardware-like memory acceleration layer that reduces slow host RAM fetches and establishes a foundation for multi-core coherency.

## Cache Architecture

- **Organization:** Direct-Mapped.
- **Cache Line:** Stores one `word18` and its metadata.
- **Set Count:** 16 sets (configurable).
- **Index:** Address % Set Count.
- **Tag:** Full Address.

## Coherency Model (MESI-lite)

Reflex OS uses a simplified MESI protocol for host-VM memory synchronization:

- **I (Invalid):** Line contains no valid data. Fetch from host required.
- **E (Exclusive):** Data matches host RAM. Clean.
- **M (Modified):** Data has been changed by VM and does not match host RAM. Dirty. Write-back required on eviction.

*Note: 'S' (Shared) is deferred until multi-VM concurrent execution is implemented.*

## Cache Operations

### `TLD` (Load)
1. Calculate cache index.
2. If `tag == addr` and state is `E` or `M`: **Hit**. Return value immediately.
3. If **Miss**:
    - If current line in set is `M`: **Evict** (write back to host RAM).
    - Fetch `word18` from host RAM.
    - Set state to `E`, update tag and value.

### `TST` (Store)
1. Calculate cache index.
2. If `tag == addr`: **Hit**. Update value, set state to `M`.
3. If **Miss**:
    - If current line in set is `M`: **Evict** (write back to host RAM).
    - Update value, set state to `M`, update tag.

## Cache Control Opcodes

To support explicit software-managed coherency, the following opcodes are added:

- `TFLUSH`: Force write-back of a specific address if dirty.
- `TINV`: Mark a specific address as Invalid.

## Design Constraints

- Cache lookups must remain constant-time.
- The cache must not bypass the host's "VMM" (boundary-checked access) during evictions or fetches.
- Initialization must leave all sets in the `I` state.

## MVP Success Condition

The Soft-Cache is real when:
- A `TLD` after a `TST` to the same address returns from cache without a host fetch.
- An eviction of a `M` line correctly updates host RAM.
- A `TINV` opcode forces a re-fetch from host RAM.
