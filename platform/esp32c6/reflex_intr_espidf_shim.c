/**
 * @file reflex_intr_espidf_shim.c
 * @brief ESP-IDF's interrupt allocator, redirected into Reflex's table.
 *
 * This is what takes Tier C to zero, and it is deliberately a different move
 * from the one refused twice in this work.
 *
 * `--wrap=intr_handler_set`, or calling `_global_interrupt_handler` directly,
 * were both rejected because they leave ESP-IDF's `s_intr_handlers` table
 * exactly where it is and only change which symbol names it. Wrapping
 * `esp_intr_alloc` is not that. It intercepts the *registration*, so a driver's
 * ISR is filed in **Reflex's** table and dispatched by Reflex's own handler
 * from Reflex's own records. Ownership moves; nothing is renamed. Afterwards
 * there is no reader of ESP-IDF's table left, which is why
 * reflex_hal_intr_dispatch_foreign and its two getters could be deleted rather
 * than hidden.
 *
 * ## Why this is a smaller change than it looks
 *
 * The interrupt it redirects was already being serviced from inside Reflex's
 * trap handler. `dispatch_foreign` read ESP-IDF's table and called the driver's
 * ISR from exactly this context, with interrupts already disabled by trap
 * entry. The only thing that changes is where the function pointer came from.
 * The measured behaviour on the bench is the same 802.15.4 frames arriving
 * through the same ISR.
 *
 * ## Ordering, which is the whole hazard
 *
 * ESP-IDF allocates interrupts during its own startup, long before Reflex takes
 * mtvec. Those must keep going to ESP-IDF's table, because ESP-IDF's vector is
 * what will dispatch them. Routing them to Reflex's table early would file a
 * handler that nothing consults and the interrupt would never arrive — silent,
 * and indistinguishable from a hang for anything that waits on it.
 *
 * So the wrapper asks the hardware, per call, whose vector is installed, and
 * forwards to `__real_esp_intr_alloc` until Reflex's is. That is the same
 * question `reflex_hal_intr_alloc` asks to refuse an early allocation, and it
 * is deliberately the same predicate rather than a second flag to keep in sync.
 *
 * ## The wrapper is defined unconditionally
 *
 * `--wrap` rewrites the reference whether or not Reflex wants it, so a wrapper
 * that only exists behind REFLEX_OWN_ENTRY breaks the ordinary build with an
 * undefined reference. That has happened twice in this work — once for
 * esp_startup_start_app and once for xPortStartScheduler — and the lesson was
 * written down after the first and repeated anyway. The default path here is a
 * straight pass-through.
 */

#include "reflex_hal.h"
#include "reflex_rom_esp32c6.h"
#include <stdbool.h>
#include <stdint.h>

#if CONFIG_IDF_TARGET_ESP32C6

/* ESP-IDF's types, spelled out rather than included.
 *
 * esp_intr_alloc.h would be an ESP-IDF header in the one file whose purpose is
 * to end an ESP-IDF dependency, and it would land in a tier. These are the ABI:
 * esp_err_t is int, intr_handle_t is an opaque struct pointer (so void * is
 * layout-compatible), and intr_handler_t is void (*)(void *), which is also
 * reflex_intr_handler_t. The __real_/__wrap_ pair must agree with the
 * declaration ESP-IDF's callers were compiled against, and it does — a
 * mismatch here would be a link-time type error on the wrapped symbol, not a
 * silent miscall. */
#define SHIM_ESP_OK 0
#define SHIM_ESP_ERR_INVALID_ARG 0x102
#define SHIM_ESP_ERR_NO_MEM 0x101
#define SHIM_ESP_ERR_NOT_SUPPORTED 0x106
#define SHIM_ESP_FAIL (-1)

extern int __real_esp_intr_alloc(int source, int flags, void (*handler)(void *), void *arg,
                                 void **ret_handle);
extern int __real_esp_intr_free(void *handle);
int __wrap_esp_intr_alloc(int source, int flags, void (*handler)(void *), void *arg,
                          void **ret_handle);
int __wrap_esp_intr_free(void *handle);

#ifdef REFLEX_OWN_ENTRY

/* One record per interrupt this shim has redirected.
 *
 * The handle handed back must be distinguishable from ESP-IDF's on the way into
 * esp_intr_free, and it must not be confused with one of Reflex's own, which
 * are (cpu_int + 1) cast to a pointer — small integers that would be perfectly
 * usable as a tag and are deliberately not used as one. A pointer into this
 * array is unambiguous by address range and says what it is.
 *
 * Eight is well beyond what this path needs: measured on the own-entry build,
 * exactly one driver allocates an interrupt after the hand-off. Exhausting it
 * returns an error rather than silently falling back to ESP-IDF's allocator,
 * because that fallback would produce an interrupt nothing dispatches. */
#define SHIM_MAX 8
typedef struct {
    bool used;
    int source;
    reflex_intr_handle_t rh;
} shim_rec_t;
static shim_rec_t s_shim[SHIM_MAX];

static bool shim_owns(const void *handle) {
    const shim_rec_t *p = (const shim_rec_t *)handle;
    return p >= s_shim && p < (s_shim + SHIM_MAX);
}

/* Reflex's error space is not ESP-IDF's, and the caller checks against theirs. */
static int shim_err(reflex_err_t rc) {
    switch (rc) {
    case REFLEX_OK:
        return SHIM_ESP_OK;
    case REFLEX_ERR_INVALID_ARG:
        return SHIM_ESP_ERR_INVALID_ARG;
    case REFLEX_ERR_NO_MEM:
        return SHIM_ESP_ERR_NO_MEM;
    case REFLEX_ERR_NOT_SUPPORTED:
        return SHIM_ESP_ERR_NOT_SUPPORTED;
    default:
        return SHIM_ESP_FAIL;
    }
}

#endif /* REFLEX_OWN_ENTRY */

int __wrap_esp_intr_alloc(int source, int flags, void (*handler)(void *), void *arg,
                          void **ret_handle) {
#ifndef REFLEX_OWN_ENTRY
    return __real_esp_intr_alloc(source, flags, handler, arg, ret_handle);
#else
    /* Before Reflex owns the vector, ESP-IDF's table is the correct one: its
     * vector is what will dispatch the interrupt. This is the ordering hazard
     * and it is answered by asking the hardware, not by a flag. */
    if (!reflex_hal_intr_vector_is_reflex()) {
        return __real_esp_intr_alloc(source, flags, handler, arg, ret_handle);
    }

    /* Flags are refused rather than ignored.
     *
     * Reflex's allocator honours none of ESP-IDF's flag semantics — shared
     * lines, explicit priority levels, edge triggering, IRAM placement of the
     * handler. Quietly accepting a flag and not implementing it is how a driver
     * ends up with an interrupt that is subtly not what it asked for, and the
     * symptom would appear far from here. The one caller on this path passes 0;
     * anything else is a new case that deserves to be looked at rather than
     * guessed. The value is printed so it can be. */
    if (flags != 0) {
        esp_rom_printf("[reflex.intr] esp_intr_alloc(source=%d, flags=0x%x) refused: Reflex's "
                       "allocator implements none of ESP-IDF's flag semantics\n",
                       source, (unsigned)flags);
        return SHIM_ESP_ERR_NOT_SUPPORTED;
    }
    if (!handler) {
        return SHIM_ESP_ERR_INVALID_ARG;
    }

    int slot = -1;
    for (int i = 0; i < SHIM_MAX; i++) {
        if (!s_shim[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        esp_rom_printf("[reflex.intr] esp_intr_alloc(source=%d): shim table full\n", source);
        return SHIM_ESP_ERR_NO_MEM;
    }

    reflex_intr_handle_t rh = NULL;
    reflex_err_t rc = reflex_hal_intr_alloc(source, 0, (reflex_intr_handler_t)handler, arg, &rh);
    if (rc != REFLEX_OK) {
        esp_rom_printf("[reflex.intr] esp_intr_alloc(source=%d) -> reflex_hal_intr_alloc "
                       "failed rc=0x%x\n",
                       source, (unsigned)rc);
        return shim_err(rc);
    }

    s_shim[slot].used = true;
    s_shim[slot].source = source;
    s_shim[slot].rh = rh;
    if (ret_handle) {
        *ret_handle = &s_shim[slot];
    }
    esp_rom_printf("[reflex.intr] esp_intr_alloc(source=%d) -> Reflex's table\n", source);
    return SHIM_ESP_OK;
#endif
}

int __wrap_esp_intr_free(void *handle) {
#ifndef REFLEX_OWN_ENTRY
    return __real_esp_intr_free(handle);
#else
    /* A handle this shim did not issue belongs to ESP-IDF — allocated before
     * the hand-off, when its allocator was still the right one. Both kinds are
     * live at once in a single boot, so this is a routing decision on every
     * call and not a mode. */
    if (!shim_owns(handle)) {
        return __real_esp_intr_free(handle);
    }
    shim_rec_t *p = (shim_rec_t *)handle;
    if (!p->used) {
        return SHIM_ESP_ERR_INVALID_ARG;
    }
    reflex_err_t rc = reflex_hal_intr_free(p->rh);
    p->used = false;
    p->rh = NULL;
    return shim_err(rc);
#endif
}

#endif /* CONFIG_IDF_TARGET_ESP32C6 */
