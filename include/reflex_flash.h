/* Reflex's own flash access window.
 *
 * Three operations against the SPI flash medium, performed with the cache
 * suspended so they reach the chip rather than the cache, and with interrupts
 * masked because a trap taken inside that window would fetch its handler from
 * flash — which is not readable there.
 *
 * The transfer itself is the mask ROM's. That is a deliberate choice and it is
 * measured rather than assumed: on this bench, ROM erase+write+read inside the
 * window round-trips correctly, `esp_flash_read` sees what the ROM wrote, and
 * the ROM sees what `esp_flash_write` wrote. The two drivers agree on the same
 * medium. ROM entry points are bedrock Reflex already stands on; ESP-IDF's
 * spi_flash component is a dependency this replaces.
 */
#ifndef REFLEX_FLASH_H
#define REFLEX_FLASH_H

#include <stddef.h>
#include <stdint.h>
#include "reflex_types.h"

/** Read @p len bytes from flash offset @p addr.
 *  @p addr, @p len and @p dst must be 4-byte aligned: the ROM routines move
 *  32 bits at a time and do not check. */
reflex_err_t reflex_flash_read(uint32_t addr, void *dst, size_t len);

/** Write @p len bytes to flash offset @p addr, which must already be erased.
 *  Same alignment rules. */
reflex_err_t reflex_flash_write(uint32_t addr, const void *src, size_t len);

/** Erase @p len bytes from @p addr. Both must be sector-aligned. */
reflex_err_t reflex_flash_erase(uint32_t addr, size_t len);

#endif /* REFLEX_FLASH_H */
