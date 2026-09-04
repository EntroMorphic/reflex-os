/**
 * @file reflex_regops.h
 * @brief Memory-mapped register access, owned rather than borrowed.
 *
 * Replaces the seven accessor macros Reflex reached into ESP-IDF's `soc/soc.h`
 * for. They are not silicon knowledge and not ESP-IDF logic — each is a
 * volatile load or store, and depending on a framework header for them coupled
 * the bootloader to ESP-IDF for something the compiler already knows how to do.
 *
 * Seven macros collapse to five operations here, and the missing two are worth
 * naming. ESP-IDF's `SET_PERI_REG_MASK` and `CLEAR_PERI_REG_MASK` route their
 * address through `ETS_UNCACHED_ADDR`, which on the ESP32-C6 is defined as
 * `(addr)` — the identity. They are therefore exactly `REG_SET_BIT` and
 * `REG_CLR_BIT`, and carrying both spellings would preserve a distinction the
 * silicon does not make.
 *
 * This also retires two ad-hoc copies of the same idea: `REG32` in
 * `kernel/reflex_sched.c` and `REG32_HAL` in `platform/esp32c6/reflex_hal_esp32c6.c`,
 * which were the same macro under two names in two files.
 *
 * Every operation is a single volatile access on a `uint32_t`-aligned address.
 * Read-modify-write helpers are *not* atomic: the substrate serialises MMIO
 * through the loom, and the bootloader runs before any scheduler exists.
 */
#ifndef REFLEX_REGOPS_H
#define REFLEX_REGOPS_H

#include <stdint.h>

/** Lvalue reference to a memory-mapped register. */
#define REFLEX_REG(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

/** Read a register. */
#define REFLEX_REG_READ(addr) (REFLEX_REG(addr))

/** Write a register. */
#define REFLEX_REG_WRITE(addr, val)                                                                \
    do {                                                                                           \
        REFLEX_REG(addr) = (uint32_t)(val);                                                        \
    } while (0)

/** Set every bit in @p mask, leaving the rest untouched. */
#define REFLEX_REG_SET_BIT(addr, mask)                                                             \
    do {                                                                                           \
        REFLEX_REG(addr) |= (uint32_t)(mask);                                                      \
    } while (0)

/** Clear every bit in @p mask, leaving the rest untouched. */
#define REFLEX_REG_CLR_BIT(addr, mask)                                                             \
    do {                                                                                           \
        REFLEX_REG(addr) &= ~(uint32_t)(mask);                                                     \
    } while (0)

/**
 * @brief Replace a bit field with @p val.
 *
 * Mask and shift are passed explicitly. ESP-IDF's `REG_SET_FIELD` rebuilds
 * them by pasting `_V` and `_S` onto the field name, which fails inside the
 * preprocessor with a diagnostic that names neither the field nor the caller
 * when either companion macro is missing.
 *
 * @param mask  Value mask *before* shifting, e.g. 0x7 for a 3-bit field.
 * @param shift Bit position of the field's low bit.
 */
#define REFLEX_REG_SET_FIELD(addr, mask, shift, val)                                               \
    do {                                                                                           \
        uint32_t _rf_v = REFLEX_REG(addr);                                                         \
        _rf_v &= ~((uint32_t)(mask) << (shift));                                                   \
        _rf_v |= ((uint32_t)(val) & (uint32_t)(mask)) << (shift);                                  \
        REFLEX_REG(addr) = _rf_v;                                                                  \
    } while (0)

#endif /* REFLEX_REGOPS_H */
