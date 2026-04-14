# GOOSE Bridge: Projecting the MMIO

## Thesis
The Bridge allows developers to "unfold" the ESP32-C6's physical MMIO register space into the GOOSE Tapestry. It uses the legacy messaging system to establish these projections.

## The API (Legacy Messaging)
To expose a device, send a message to `REFLEX_NODE_GATEWAY`.

### Op: `OP_PROJECT_CELL` (0x10)
Projects a single 32-bit MMIO register into a GOOSE Cell.
- **SrcA:** Field/Region/Cell Coordinate (Encoded as 9-trit word).
- **SrcB:** MMIO Address Offset.
- **Payload:** Bitmask.

## .goose Notation Implementation
When a dev writes `.goose` notation, the compiler (or shell) translates it into these bridge messages.

```goose
// Dev writes:
region UART {
    cell tx_fifo @ (2, 1, 0) maps mmio(0x60000000);
}

// System executes:
// SendMsg(GATEWAY, OP_PROJECT_CELL, coord=(2,1,0), addr=0x60000000, mask=0xFF);
```

## Safety and Regulation
1. **MMIO Validation:** The Gateway checks a "Safety Map" to ensure the dev isn't projecting sensitive registers (e.g. Encryption keys).
2. **Postural Alignment:** The Supervisor treats projected MMIO cells as `HARDWARE_OUT` or `HARDWARE_IN`. If a register value deviates from the Tapestry intent, the Supervisor "re-levels" it by re-writing the register.
