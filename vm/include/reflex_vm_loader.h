/** @file reflex_vm_loader.h
 * @brief VM image loader types and API.
 */
#ifndef REFLEX_VM_LOADER_H
#define REFLEX_VM_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "reflex_types.h"

#include "reflex_vm_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_VM_IMAGE_MAGIC 0x52465856u
#define REFLEX_VM_IMAGE_VERSION 2u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t checksum;
    uint32_t entry_ip;
    uint32_t instruction_count;
    uint32_t data_count;
    const uint8_t *binary_payload;
} reflex_vm_binary_image_t;

// V1 style image (host-aligned) - Keep for internal self-checks
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint32_t entry_ip;
    const reflex_vm_instruction_t *instructions;
    size_t instruction_count;
    reflex_word18_t *private_memory;
    size_t private_memory_count;
} reflex_vm_image_t;

reflex_err_t reflex_vm_validate_image(const reflex_vm_image_t *image);
reflex_err_t reflex_vm_load_image(reflex_vm_state_t *vm, const reflex_vm_image_t *image);

/**
 * @brief Load a v2 packed binary image.
 * This function handles allocation of the host instruction array.
 */
reflex_err_t reflex_vm_load_binary(reflex_vm_state_t *vm, const uint8_t *buffer, size_t len);
reflex_err_t reflex_vm_crc32(const uint8_t *data, size_t len, uint32_t *out_crc);

reflex_err_t reflex_vm_loader_self_check(void);

#ifdef __cplusplus
}
#endif

#endif
