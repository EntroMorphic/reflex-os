/** @file mmu.c
 * @brief VM memory management unit.
 */
#include "reflex_vm_mem.h"

#include <string.h>
#include "reflex_types.h"

void reflex_vm_mmu_init(reflex_vm_mmu_t *mmu)
{
    if (!mmu) return;
    memset(mmu, 0, sizeof(*mmu));
}

reflex_err_t reflex_vm_mmu_add_region(reflex_vm_mmu_t *mmu, reflex_word18_t *buffer, size_t length, reflex_mem_type_t type, uint32_t base)
{
    REFLEX_RETURN_ON_FALSE(mmu != NULL, REFLEX_ERR_INVALID_ARG, "mmu", "mmu required");
    REFLEX_RETURN_ON_FALSE(buffer != NULL, REFLEX_ERR_INVALID_ARG, "mmu", "buffer required");
    REFLEX_RETURN_ON_FALSE(mmu->count < REFLEX_MEM_MAX_REGIONS, REFLEX_ERR_NO_MEM, "mmu", "max regions reached");

    // Overflow and Overlap Check
    REFLEX_RETURN_ON_FALSE(UINT32_MAX - base >= length, REFLEX_ERR_INVALID_SIZE, "mmu", "region wraps around");

    uint32_t new_start = base;
    uint32_t new_end = base + length;

    for (size_t i = 0; i < mmu->count; i++) {
        uint32_t existing_start = mmu->regions[i].base_addr;
        uint32_t existing_end = existing_start + mmu->regions[i].length;

        if ((new_start < existing_end) && (new_end > existing_start)) {
            return REFLEX_ERR_INVALID_STATE; // Region overlap
        }
    }

    mmu->regions[mmu->count].buffer = buffer;
    mmu->regions[mmu->count].length = length;
    mmu->regions[mmu->count].type = type;
    mmu->regions[mmu->count].base_addr = base;
    mmu->count++;

    return REFLEX_OK;
}

reflex_err_t reflex_vm_mmu_translate(const reflex_vm_mmu_t *mmu, uint32_t vm_addr, reflex_word18_t **out_host_ptr)
{
    if (!mmu || !out_host_ptr) return REFLEX_ERR_INVALID_ARG;

    for (size_t i = 0; i < mmu->count; i++) {
        const reflex_mem_region_t *r = &mmu->regions[i];
        if (vm_addr >= r->base_addr && vm_addr < (r->base_addr + r->length)) {
            uint32_t offset = vm_addr - r->base_addr;
            *out_host_ptr = &r->buffer[offset];
            return REFLEX_OK;
        }
    }

    return REFLEX_ERR_NOT_FOUND;
}
