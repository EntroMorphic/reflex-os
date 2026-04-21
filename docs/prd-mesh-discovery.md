# PRD: Mesh Auto-Discovery

## Problem

Peers must be registered manually on both boards via `mesh peer add <name>
<mac>`. This is error-prone (transcribing 12-hex-digit MACs) and does not scale
beyond two boards.

## Goal

Boards sharing the same Aura key auto-discover each other on boot and maintain
a live peer list without operator intervention.

## Current State

- `goose_atmosphere.c` defines five ARC_OP types: SYNC (0x00), QUERY (0xDE),
  ADVERTISE (0xAD), POSTURE (0xCC), MMIO_SYNC (0x10).
- All arcs are authenticated via HMAC-SHA256 truncated to 32 bits using the
  shared Aura key (goose_aura_key).
- Peers are registered via `reflex_radio_add_peer` which wraps ESP-NOW's peer
  list. The broadcast peer (FF:FF:FF:FF:FF:FF) is added at init.
- `goose_mmio_sync` maintains a named peer table (`goose_mmio_sync_add_peer`).

## Architecture

### New Arc Type

`ARC_OP_DISCOVER 0xD0` -- a broadcast arc carrying the sender's identity.

### Wire Format

Uses the existing `goose_arc_packet_t` (21 bytes):
- `op` = 0xD0
- `coord` field (9 bytes): device name, null-terminated, max 8 chars
- `name_hash`: FNV-1a of the device name (for dedup)
- `state`: unused (0)
- `nonce`, `aura`: standard replay/auth fields

### Transmit Path

1. On `goose_atmosphere_init` completion, broadcast ARC_OP_DISCOVER once.
2. Periodic re-broadcast every 10 seconds (heartbeat), driven by a new divider
   in `goose_supervisor_pulse` or a dedicated timer in atmosphere.

### Receive Path

In `atmosphere_recv_cb`, after Aura validation and replay check:

```c
else if (arc->op == ARC_OP_DISCOVER) {
    mesh_stats.rx_discover++;
    char name[9] = {0};
    memcpy(name, arc->coord.trits, 8);
    name[8] = '\0';
    // Register radio peer (idempotent in ESP-NOW)
    reflex_radio_add_peer(recv_info->src_addr);
    // Register named MMIO sync peer
    goose_mmio_sync_add_peer(name, recv_info->src_addr);
}
```

### Staleness

The existing `goose_mmio_sync_staleness_check` (called on
`REFLEX_SUPERVISOR_STALE_DIV`) already expires peers that stop sending. The
10-second heartbeat keeps discovered peers alive.

## Security

No new trust model. Discovery arcs are HMAC-authenticated with the same Aura
key. Boards without a matching key cannot discover each other. No key exchange
or trust negotiation is introduced.

## Files Changed

| File | Change |
|------|--------|
| `goose_atmosphere.c` | +ARC_OP_DISCOVER define, +discover handler in recv_cb, +broadcast function (~40 LOC) |
| `goose_mmio_sync.c` | +call discover broadcast from init or periodic hook (~5 LOC) |
| `goose.h` | +`rx_discover` field in mesh_stats struct |

## Non-Goals

- Key exchange or pairing negotiation.
- Peer trust levels or capability advertisement.
- Multi-hop discovery relay.

## Validation

1. Flash two boards with the same Aura key (`aura setkey` on both).
2. Power both on. Within 15 seconds, `mesh peer ls` on each board shows the
   other with correct name and MAC.
3. Power-cycle one board. After reboot, the peer re-appears within 15 seconds.
4. Flash a third board with a different Aura key. Verify it does NOT appear in
   the peer list of the first two.

## Effort

~80 LOC across two files. Estimated 2 hours.
