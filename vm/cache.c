#include "reflex_cache.h"

#include <string.h>

#include "reflex_types.h"

#include "reflex_vm.h"

extern reflex_err_t reflex_vm_mem_get_raw(const reflex_vm_state_t *vm, uint32_t index, reflex_word18_t *out);
extern reflex_err_t reflex_vm_mem_set_raw(reflex_vm_state_t *vm, uint32_t index, const reflex_word18_t *in);

static uint32_t reflex_cache_get_index(uint32_t addr)
{
    return addr % REFLEX_CACHE_SET_COUNT;
}

void reflex_cache_init(reflex_cache_t *cache)
{
    if (cache == NULL) {
        return;
    }

    memset(cache, 0, sizeof(*cache));
    for (int i = 0; i < REFLEX_CACHE_SET_COUNT; ++i) {
        cache->sets[i].state = REFLEX_CACHE_I;
    }
}

reflex_err_t reflex_cache_load(reflex_vm_state_t *vm, uint32_t addr, reflex_word18_t *out)
{
    reflex_cache_t *cache;
    reflex_cache_line_t *line;

    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "cache", "vm required");
    REFLEX_RETURN_ON_FALSE(out != NULL, REFLEX_ERR_INVALID_ARG, "cache", "output required");
    REFLEX_RETURN_ON_FALSE(vm->cache != NULL, REFLEX_ERR_INVALID_ARG, "cache", "cache required");

    cache = (reflex_cache_t *)vm->cache;
    line = &cache->sets[reflex_cache_get_index(addr)];

    if (line->state != REFLEX_CACHE_I && line->tag == addr) {
        cache->hits++;
        memcpy(out, &line->value, sizeof(*out));
        return REFLEX_OK;
    }

    cache->misses++;
    if (line->state == REFLEX_CACHE_M) {
        cache->evictions++;
        REFLEX_RETURN_ON_ERROR(reflex_vm_mem_set_raw(vm, line->tag, &line->value), "cache", "evict failed");
    }

    REFLEX_RETURN_ON_ERROR(reflex_vm_mem_get_raw(vm, addr, &line->value), "cache", "fetch failed");
    line->tag = addr;
    line->state = REFLEX_CACHE_E;
    memcpy(out, &line->value, sizeof(*out));
    return REFLEX_OK;
}

reflex_err_t reflex_cache_store(reflex_vm_state_t *vm, uint32_t addr, const reflex_word18_t *in)
{
    reflex_cache_t *cache;
    reflex_cache_line_t *line;

    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "cache", "vm required");
    REFLEX_RETURN_ON_FALSE(in != NULL, REFLEX_ERR_INVALID_ARG, "cache", "input required");
    REFLEX_RETURN_ON_FALSE(vm->cache != NULL, REFLEX_ERR_INVALID_ARG, "cache", "cache required");

    cache = (reflex_cache_t *)vm->cache;
    line = &cache->sets[reflex_cache_get_index(addr)];

    if (line->state == REFLEX_CACHE_M && line->tag != addr) {
        cache->evictions++;
        REFLEX_RETURN_ON_ERROR(reflex_vm_mem_set_raw(vm, line->tag, &line->value), "cache", "evict failed");
    }

    line->tag = addr;
    line->state = REFLEX_CACHE_M;
    memcpy(&line->value, in, sizeof(line->value));
    return REFLEX_OK;
}

reflex_err_t reflex_cache_flush(reflex_vm_state_t *vm, uint32_t addr)
{
    reflex_cache_t *cache;
    reflex_cache_line_t *line;

    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "cache", "vm required");
    REFLEX_RETURN_ON_FALSE(vm->cache != NULL, REFLEX_ERR_INVALID_ARG, "cache", "cache required");

    cache = (reflex_cache_t *)vm->cache;
    line = &cache->sets[reflex_cache_get_index(addr)];

    if (line->state == REFLEX_CACHE_M && line->tag == addr) {
        REFLEX_RETURN_ON_ERROR(reflex_vm_mem_set_raw(vm, line->tag, &line->value), "cache", "flush failed");
        line->state = REFLEX_CACHE_E;
    }
    return REFLEX_OK;
}

reflex_err_t reflex_cache_invalidate(reflex_vm_state_t *vm, uint32_t addr)
{
    reflex_cache_t *cache;
    reflex_cache_line_t *line;

    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "cache", "vm required");
    REFLEX_RETURN_ON_FALSE(vm->cache != NULL, REFLEX_ERR_INVALID_ARG, "cache", "cache required");

    cache = (reflex_cache_t *)vm->cache;
    line = &cache->sets[reflex_cache_get_index(addr)];
    if (line->tag == addr) {
        line->state = REFLEX_CACHE_I;
    }
    return REFLEX_OK;
}

reflex_err_t reflex_cache_flush_all(reflex_vm_state_t *vm)
{
    reflex_cache_t *cache;

    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "cache", "vm required");
    REFLEX_RETURN_ON_FALSE(vm->cache != NULL, REFLEX_ERR_INVALID_ARG, "cache", "cache required");

    cache = (reflex_cache_t *)vm->cache;
    for (int i = 0; i < REFLEX_CACHE_SET_COUNT; ++i) {
        reflex_cache_line_t *line = &cache->sets[i];
        if (line->state == REFLEX_CACHE_M) {
            REFLEX_RETURN_ON_ERROR(reflex_vm_mem_set_raw(vm, line->tag, &line->value), "cache", "flush all failed");
            line->state = REFLEX_CACHE_E;
        }
    }

    return REFLEX_OK;
}

reflex_err_t reflex_vm_host_write(reflex_vm_state_t *vm, uint32_t addr, const reflex_word18_t *in)
{
    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "cache.host", "vm required");
    REFLEX_RETURN_ON_FALSE(in != NULL, REFLEX_ERR_INVALID_ARG, "cache.host", "input required");

    REFLEX_RETURN_ON_ERROR(reflex_vm_mem_set_raw(vm, addr, in), "cache.host", "host mem update failed");
    if (vm->cache != NULL) {
        REFLEX_RETURN_ON_ERROR(reflex_cache_invalidate(vm, addr), "cache.host", "invalidate failed");
    }
    return REFLEX_OK;
}
