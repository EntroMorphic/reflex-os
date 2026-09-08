/**
 * @file reflex_rom_esp32c6.h
 * @brief ESP32-C6 mask-ROM entry points, declared by Reflex.
 *
 * These functions live in the chip's mask ROM. They are silicon, not ESP-IDF
 * code — ESP-IDF only supplies a header declaring them and a linker script
 * giving their addresses. Declaring them here removes the header half of that
 * dependency; `platform/esp32c6/reflex_rom_esp32c6.ld` carries the addresses
 * for the day the link no longer comes from ESP-IDF either.
 *
 * The technique is not new to this tree, only consolidated. `reflex_kv_flash.c`
 * already declared the three SPI-flash routines with `extern` and included no
 * ESP header at all, `reflex_hal_reboot` already called `software_reset()`
 * this way, and Boot0 carried its own externs for the cache and console
 * routines. Those were four separate spellings of the same idea in four files,
 * which is how a signature drifts without anything noticing.
 *
 * ESP32-C6 only. The Xtensa ESP32 has entirely different ROM addresses, so the
 * ESP32 backend deliberately keeps ESP-IDF's own header rather than pretending
 * one declaration set covers both.
 *
 * Signatures match ESP-IDF's, and `make rom-check` verifies every address in
 * the linker fragment against the ROM linker scripts ESP-IDF ships. A wrong
 * address here calls into whatever else happens to sit at that offset.
 */
#ifndef REFLEX_ROM_ESP32C6_H
#define REFLEX_ROM_ESP32C6_H

#include <stdbool.h>
#include <stdint.h>

/* ---- Console ---- */

/** printf against the ROM console. Used before any of ours exists. */
extern int esp_rom_printf(const char *fmt, ...);

/**
 * @brief Install a character sink for ROM console output on channel 1.
 *
 * This is the ROM primitive. Boot0 previously called ESP-IDF's
 * esp_rom_install_channel_putc(1, ...), which is *not* a ROM routine — it is C
 * in components/esp_rom/patches, and its channel-1 arm does exactly this call
 * after caching the pointer for an ESP-IDF console shim Boot0 never uses.
 * Calling the ROM directly drops that dependency rather than renaming it.
 */
extern void ets_install_putc1(void (*putc)(char c));

/** Emit one character through the ROM's UART writer. */
extern void esp_rom_output_putc(char c);

/** Block until the given UART has drained. */
extern void esp_rom_output_tx_wait_idle(uint32_t uart_no);

/* ---- Timing ---- */

/** Busy-wait. ROM implementation, calibrated against the boot clock. */
extern void esp_rom_delay_us(uint32_t us);

/* ---- GPIO matrix ---- */

/** Route a peripheral output signal onto a pin through the GPIO matrix. */
extern void esp_rom_gpio_connect_out_signal(uint32_t gpio_num, uint32_t signal_idx, bool out_inv,
                                            bool oen_inv);

/** Route a pin into a peripheral input signal through the GPIO matrix.
 *
 * The counterpart of the above, and needed for the same reason: PCNT counts
 * edges on a signal index, not on a pin, so something has to connect the two.
 * Taken from ROM rather than reimplemented — the GPIO matrix is Tier A, where
 * the entry points are already Reflex's to call. */
extern void esp_rom_gpio_connect_in_signal(uint32_t gpio_num, uint32_t signal_idx, bool inv);

/* ---- SPI flash ----
 *
 * Addresses and lengths are byte counts, but the buffers must be word-aligned:
 * the ROM routines move 32 bits at a time and do not check.
 */

/** Read @p len bytes from flash @p addr into @p dest. Returns 0 on success. */
extern int esp_rom_spiflash_read(uint32_t addr, uint32_t *dest, int len);

/** Write @p len bytes from @p src to flash @p addr. Returns 0 on success. */
extern int esp_rom_spiflash_write(uint32_t addr, const uint32_t *src, int len);

/** Erase one 4 KB sector by index. Returns 0 on success. */
extern int esp_rom_spiflash_erase_sector(uint32_t sector);

/* ---- Instruction cache ---- */

extern int Cache_Enable_ICache(uint32_t autoload);
extern int Cache_Disable_ICache(void);
extern int Cache_Suspend_ICache(void);
extern int Cache_Resume_ICache(uint32_t autoload);

/* ---- Reset ---- */

/** Software reset of the whole SoC. Does not return. */
extern void software_reset(void);

#endif /* REFLEX_ROM_ESP32C6_H */
