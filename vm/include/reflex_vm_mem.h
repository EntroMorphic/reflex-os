#ifndef REFLEX_VM_MEM_H
#define REFLEX_VM_MEM_H

#include <stdint.h>
#include <stddef.h>

#include "reflex_types.h"
#include "reflex_ternary.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_MEM_MAX_REGIONS 4

typedef enum {
    REFLEX_MEM_PRIVATE = 0,
    REFLEX_MEM_SHARED
} reflex_mem_type_t;

typedef struct {
    reflex_word18_t *buffer;
    size_t length;
    reflex_mem_type_t type;
    uint32_t base_addr;
} reflex_mem_region_t;

typedef struct {
    reflex_mem_region_t regions[REFLEX_MEM_MAX_REGIONS];
    size_t count;
} reflex_vm_mmu_t;

void reflex_vm_mmu_init(reflex_vm_mmu_t *mmu);
reflex_err_t reflex_vm_mmu_add_region(reflex_vm_mmu_t *mmu, reflex_word18_t *buffer, size_t length, reflex_mem_type_t type, uint32_t base);

/**
 * @brief Map a VM address to a host pointer using the MMU.
 */
reflex_err_t reflex_vm_mmu_translate(const reflex_vm_mmu_t *mmu, uint32_t vm_addr, reflex_word18_t **out_host_ptr);

#ifdef __cplusplus
}
#endif

#endif
