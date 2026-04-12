# Reflex OS VM Syscall Bridge

## Purpose

This document defines the MVP syscall bridge from ternary VM programs into the binary host runtime.

For MVP, syscalls remain intentionally small, synchronous, and host-controlled.

## Syscall Contract

- `TSYS` selects the syscall by instruction `IMM`
- `SRC_A` and `SRC_B` provide input words
- `DST` receives the returned `word18`
- syscall failures trap the VM with `INVALID_SYSCALL`

## Initial Syscalls

### `REFLEX_VM_SYSCALL_LOG`
- Input: `SRC_A` interpreted as a signed integer value
- Effect: prints the integer value to the host log/console
- Output: `0` on success

### `REFLEX_VM_SYSCALL_UPTIME`
- Input: none required
- Effect: none
- Output: uptime in milliseconds as a `word18`

### `REFLEX_VM_SYSCALL_CONFIG_GET`
- Input: `SRC_A` interpreted as a scalar config key
- Effect: none
- Output: scalar config value as a `word18` read from the real config store

## Initial Config Keys

The first config bridge only exposes scalar values. String values remain deferred.

- `REFLEX_VM_CONFIG_DEVICE_NAME_LENGTH = 0` -> returns the persisted device name length
- `REFLEX_VM_CONFIG_LOG_LEVEL = 1` -> returns the persisted log level token

## Design Rules

- VM code never talks directly to ESP-IDF drivers
- syscalls must validate and normalize all returned words
- unsupported syscall IDs or config keys must fail cleanly
