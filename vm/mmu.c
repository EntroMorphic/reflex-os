#include "reflex_vm_mem.h"

#include <string.h>
#include "esp_check.h"

void reflex_vm_mmu_init(reflex_vm_mmu_t *mmu)
{
    if (!mmu) return;
    memset(mmu, 0, sizeof(*mmu));
}

esp_err_t reflex_vm_mmu_add_region(reflex_vm_mmu_t *mmu, reflex_word18_t *buffer, size_t length, reflex_mem_type_t type, uint32_t base)
{
    ESP_RETURN_ON_FALSE(mmu != NULL, ESP_ERR_INVALID_ARG, "mmu", "mmu required");
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, "mmu", "buffer required");
    ESP_RETURN_ON_FALSE(mmu->count < REFLEX_MEM_MAX_REGIONS, ESP_ERR_NO_MEM, "mmu", "max regions reached");

    // Overflow and Overlap Check
    ESP_RETURN_ON_FALSE(UINT32_MAX - base >= length, ESP_ERR_INVALID_SIZE, "mmu", "region wraps around");

    uint32_t new_start = base;
    uint32_t new_end = base + length;

    for (size_t i = 0; i < mmu->count; i++) {
        uint32_t existing_start = mmu->regions[i].base_addr;
        uint32_t existing_end = existing_start + mmu->regions[i].length;

        if ((new_start < existing_end) && (new_end > existing_start)) {
            return ESP_ERR_INVALID_STATE; // Region overlap
        }
    }

    mmu->regions[mmu->count].buffer = buffer;
    mmu->regions[mmu->count].length = length;
    mmu->regions[mmu->count].type = type;
    mmu->regions[mmu->count].base_addr = base;
    mmu->count++;

    return ESP_OK;
}

esp_err_t reflex_vm_mmu_translate(const reflex_vm_mmu_t *mmu, uint32_t vm_addr, reflex_word18_t **out_host_ptr)
{
    if (!mmu || !out_host_ptr) return ESP_ERR_INVALID_ARG;

    for (size_t i = 0; i < mmu->count; i++) {
        const reflex_mem_region_t *r = &mmu->regions[i];
        if (vm_addr >= r->base_addr && vm_addr < (r->base_addr + r->length)) {
            uint32_t offset = vm_addr - r->base_addr;
            *out_host_ptr = &r->buffer[offset];
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}
