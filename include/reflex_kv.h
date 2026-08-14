#ifndef REFLEX_KV_H
#define REFLEX_KV_H

/**
 * @file reflex_kv.h
 * @brief Reflex OS portable key-value persistent storage.
 *
 * Replaces direct NVS calls. Platform implementations provide the
 * actual storage backend (NVS on ESP-IDF, LittleFS elsewhere, etc.).
 */

#include "reflex_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *reflex_kv_handle_t;

/**
 * Contract for the accessors below, none of which is inferable from the names:
 *
 * - **Handles**: `open` yields a handle that must be released with `close`,
 *   including on the error paths of whatever you do in between. `readonly`
 *   opens for reading only; a namespace that does not exist is an error rather
 *   than an implicit create.
 * - **`len` is in/out** on `get_str` and `get_blob`. Set it to the capacity of
 *   your buffer before the call; on success it is overwritten with the size of
 *   the value actually read. `get_str` values carry their own terminator
 *   because `set_str` stores `strlen + 1`.
 * - **`commit` is required for portability, and is a no-op on the ESP flash
 *   backend**, whose `set_*` calls already write through. Do not read the
 *   ESP behaviour as a guarantee: another backend may buffer.
 * - **Errors are returned, never asserted.** A missing key is
 *   `REFLEX_ERR_NOT_FOUND`, not a fatal condition.
 */
reflex_err_t reflex_kv_init(void);

reflex_err_t reflex_kv_open(const char *ns, bool readonly,
                            reflex_kv_handle_t *out);
void         reflex_kv_close(reflex_kv_handle_t h);

reflex_err_t reflex_kv_get_str(reflex_kv_handle_t h, const char *key,
                               char *buf, size_t *len);
reflex_err_t reflex_kv_set_str(reflex_kv_handle_t h, const char *key,
                               const char *val);

reflex_err_t reflex_kv_get_blob(reflex_kv_handle_t h, const char *key,
                                void *buf, size_t *len);
reflex_err_t reflex_kv_set_blob(reflex_kv_handle_t h, const char *key,
                                const void *buf, size_t len);

reflex_err_t reflex_kv_get_i32(reflex_kv_handle_t h, const char *key, int32_t *out);
reflex_err_t reflex_kv_set_i32(reflex_kv_handle_t h, const char *key, int32_t val);

reflex_err_t reflex_kv_get_u8(reflex_kv_handle_t h, const char *key, uint8_t *out);
reflex_err_t reflex_kv_set_u8(reflex_kv_handle_t h, const char *key, uint8_t val);

reflex_err_t reflex_kv_erase(reflex_kv_handle_t h, const char *key);
reflex_err_t reflex_kv_commit(reflex_kv_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* REFLEX_KV_H */
