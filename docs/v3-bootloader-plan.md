# Reflex OS Bootloader — Hardware Ownership Plan

**Goal:** Reflex OS owns the second-stage bootloader. The boot chain becomes: ROM bootloader (silicon, immutable) → Reflex bootloader (ours) → Reflex OS kernel.

---

## The Boot Chain Today

```
[ROM bootloader]  (mask ROM, Espressif, immutable)
       ↓ loads from flash 0x0
[ESP-IDF bootloader]  (22 KB, bootloader.bin, Espressif code)
       ↓ reads partition table at 0x8000
       ↓ loads app image from 0x10000
[Reflex OS]  (app_main, our code)
```

The ROM bootloader is silicon — it can't change. It looks for a valid image header at flash address 0x0 (magic byte 0xE9, 24-byte header defined by `esp_image_header_t`), loads it into SRAM, and jumps to its entry point (`call_start_cpu0`).

The second-stage bootloader (ESP-IDF's) does:
1. Hardware init: clock configuration, SPI flash speed, cache/MMU init, WDT management, console init
2. Partition table: reads the CSV-formatted table from 0x8000
3. App selection: picks the factory or OTA partition
4. Image loading: loads each segment to the correct memory address
5. Jump: transfers control to the app's entry point

## The Target Boot Chain

```
[ROM bootloader]  (mask ROM, immutable — accepted as silicon)
       ↓ loads from flash 0x0
[Reflex bootloader]  (ours, minimal, <8 KB target)
       ↓ reads Reflex partition map
       ↓ loads Reflex OS kernel image
[Reflex OS kernel]  (our code, full substrate)
```

## What the Reflex Bootloader Must Do

### Minimum viable (Phase 0)

1. **Clock init** — configure the PLL so the CPU runs at 160 MHz (ROM leaves it at a lower clock)
2. **Flash init** — configure SPI flash speed and mode (DIO, 80 MHz) so flash reads are fast
3. **Cache/MMU init** — enable the instruction and data cache so flash-mapped code can execute
4. **Console init** — bring up USB serial JTAG so boot messages are visible
5. **WDT management** — feed or disable the super watchdog so we don't reset during boot
6. **Image load** — read the Reflex OS binary from a known flash offset, load each segment to its target address
7. **Jump** — set the stack pointer and branch to the Reflex OS entry point

### What we can defer

- OTA partition selection (no OTA yet)
- Secure boot signature verification (tracked as a TODO)
- Flash encryption (tracked as a TODO)
- Factory reset via GPIO hold (not needed yet)

### What we accept as given

- The ROM bootloader's image header format (24 bytes, magic 0xE9). We must produce an image the ROM accepts.
- The ROM bootloader's SPI flash configuration. ROM configures basic flash access before we run.
- The RISC-V startup ABI: we get a stack, BSS is not cleared, data is not initialized. We must do that ourselves.

## Implementation Strategy

### Option A: Minimal from-scratch bootloader

Write a standalone RISC-V C program that:
- Links against the ESP32-C6 ROM function table (rom API for flash read, UART, delay)
- Uses register-level access for clock, cache, and MMU configuration
- Has its own linker script targeting the bootloader SRAM region
- Produces a binary that the ROM bootloader can load

The ROM provides helper functions (`esp_rom_spiflash_read`, `esp_rom_printf`, `esp_rom_delay_us`) that are available without ESP-IDF. These are in mask ROM — they're part of the silicon, not the SDK.

### Option B: ESP-IDF bootloader hooks

ESP-IDF supports `bootloader_before_init` and `bootloader_after_init` hooks. We could install our own logic into the existing bootloader without rewriting it. This gives us control over the boot sequence without owning the binary.

**Recommendation:** Option A. The hooks approach means we still ship Espressif's bootloader. If we're going to own the hardware, we own the boot.

## ROM Functions Available to the Bootloader

The ESP32-C6 ROM exports functions we can call without any SDK:

- `esp_rom_spiflash_read(uint32_t src_addr, uint32_t *dest, int32_t len)` — read from flash
- `esp_rom_printf(const char *fmt, ...)` — print to UART0
- `esp_rom_delay_us(uint32_t us)` — busy-wait delay
- `esp_rom_get_reset_reason(int cpu)` — query last reset cause
- `Cache_Enable_ICache(uint32_t autoload)` — enable instruction cache
- `cache_hal_init()` / `mmu_hal_init()` — low-level HAL for cache/MMU (available in ROM)

These are the building blocks. The Reflex bootloader calls ROM functions for flash I/O and basic services, then hands off to the Reflex OS kernel.

## File Layout

```
bootloader/
  reflex_boot0.c       — entry point (call_start_cpu0), hardware init, image loader
  reflex_boot0.ld      — linker script (targets bootloader SRAM region)
  CMakeLists.txt        — standalone build (not part of the ESP-IDF app build)
```

## Success Criteria

- [ ] Reflex bootloader binary < 8 KB
- [ ] Board boots from power-on to Reflex OS shell with only ROM + Reflex bootloader (no ESP-IDF bootloader.bin)
- [ ] Boot time ≤ existing boot time
- [ ] USB serial console output visible from the bootloader
- [ ] All existing functionality works unchanged after the bootloader swap
- [ ] `idf.py flash` can still flash the image (or we provide an equivalent flash tool)

## Risks

1. **Flash configuration sensitivity** — the ROM bootloader does some flash configuration that we might miss. If we get the SPI mode/speed wrong, the board won't boot. Mitigation: start with the same DIO/80 MHz settings the current build uses, and validate on hardware before changing anything.

2. **Cache/MMU initialization** — getting this wrong means flash-mapped code can't execute. The ROM provides `cache_hal_init` and `mmu_hal_init` which we can call directly. Mitigation: use the ROM HAL functions first, then understand and replace them later.

3. **WDT timeout during boot** — the super watchdog timer is running from ROM. If we don't feed or disable it within ~1 second, the board resets. Mitigation: disable it early in our entry point.

4. **Bricking** — a broken bootloader means the board doesn't boot. The ROM bootloader has a recovery mode (hold GPIO during power-on to enter download mode via USB), so the board is always recoverable via `esptool.py`. This is a safety net, not a solution.
