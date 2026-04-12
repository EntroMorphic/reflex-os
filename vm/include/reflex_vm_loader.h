#ifndef REFLEX_VM_LOADER_H
#define REFLEX_VM_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "reflex_vm_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_VM_IMAGE_MAGIC 0x52465856u
#define REFLEX_VM_IMAGE_VERSION 1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t entry_ip;
    const reflex_vm_instruction_t *instructions;
    size_t instruction_count;
    reflex_word18_t *memory;
    size_t memory_count;
} reflex_vm_image_t;

esp_err_t reflex_vm_validate_image(const reflex_vm_image_t *image);
esp_err_t reflex_vm_load_image(reflex_vm_state_t *vm, const reflex_vm_image_t *image);
esp_err_t reflex_vm_loader_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
