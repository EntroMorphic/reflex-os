/** @file reflex_cache.h
 * @brief VM cache types and public API.
 */
#ifndef REFLEX_CACHE_H
#define REFLEX_CACHE_H

#include <stdint.h>
#include <stdbool.h>

#include "reflex_ternary.h"
#include "reflex_vm_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_CACHE_SET_COUNT 16

typedef enum {
    REFLEX_CACHE_I = 0, // Invalid
    REFLEX_CACHE_E,     // Exclusive (Clean)
    REFLEX_CACHE_M      // Modified (Dirty)
} reflex_cache_state_t;

typedef struct {
    uint32_t tag;
    reflex_cache_state_t state;
    reflex_word18_t value;
} reflex_cache_line_t;

typedef struct {
    reflex_cache_line_t sets[REFLEX_CACHE_SET_COUNT];
    uint32_t hits;
    uint32_t misses;
    uint32_t evictions;
} reflex_cache_t;

void reflex_cache_init(reflex_cache_t *cache);

/**
 * @brief Load a word from cache or host.
 */
reflex_err_t reflex_cache_load(reflex_vm_state_t *vm, uint32_t addr, reflex_word18_t *out);

/**
 * @brief Store a word to cache (write-back).
 */
reflex_err_t reflex_cache_store(reflex_vm_state_t *vm, uint32_t addr, const reflex_word18_t *in);

/**
 * @brief Flush a specific address (write-back if dirty).
 */
reflex_err_t reflex_cache_flush(reflex_vm_state_t *vm, uint32_t addr);

/**
 * @brief Invalidate a specific address.
 */
reflex_err_t reflex_cache_invalidate(reflex_vm_state_t *vm, uint32_t addr);

/**
 * @brief Flush the entire cache.
 */
reflex_err_t reflex_cache_flush_all(reflex_vm_state_t *vm);

/**
 * @brief Host-side write proxy. 
 * Updates host RAM and invalidates VM cache to maintain coherency.
 */
reflex_err_t reflex_vm_host_write(reflex_vm_state_t *vm, uint32_t addr, const reflex_word18_t *in);

#ifdef __cplusplus
}
#endif

#endif
