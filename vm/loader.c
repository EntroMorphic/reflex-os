/** @file loader.c
 * @brief VM program image loader and validator.
 */
#include "reflex_vm_loader.h"

#include "reflex_types.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "reflex_vm.h"

static bool reflex_vm_loader_opcode_valid(reflex_vm_opcode_t opcode)
{
    return opcode >= REFLEX_VM_OPCODE_TNOP && opcode <= REFLEX_VM_OPCODE_TSENSE;
}

static bool reflex_vm_loader_register_valid(uint8_t index)
{
    return index < REFLEX_VM_REGISTER_COUNT;
}

static bool reflex_vm_loader_syscall_valid(int16_t imm)
{
    return imm >= REFLEX_VM_SYSCALL_LOG && imm <= REFLEX_VM_SYSCALL_DELAY;
}

static bool reflex_vm_loader_target_in_range(int16_t imm, size_t instruction_count)
{
    return imm >= 0 && (size_t)imm < instruction_count;
}

static bool reflex_vm_loader_instruction_fields_valid(const reflex_vm_instruction_t *instruction)
{
    switch (instruction->opcode) {
    case REFLEX_VM_OPCODE_TNOP:
    case REFLEX_VM_OPCODE_TJMP:
    case REFLEX_VM_OPCODE_THALT:
        return true;
    case REFLEX_VM_OPCODE_TSYS:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a) &&
               reflex_vm_loader_register_valid(instruction->src_b);
    case REFLEX_VM_OPCODE_TLDI:
        return reflex_vm_loader_register_valid(instruction->dst);
    case REFLEX_VM_OPCODE_TMOV:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a);
    case REFLEX_VM_OPCODE_TADD:
    case REFLEX_VM_OPCODE_TSUB:
    case REFLEX_VM_OPCODE_TCMP:
    case REFLEX_VM_OPCODE_TSEL:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a) &&
               reflex_vm_loader_register_valid(instruction->src_b);
    case REFLEX_VM_OPCODE_TBRNEG:
    case REFLEX_VM_OPCODE_TBRZERO:
    case REFLEX_VM_OPCODE_TBRPOS:
    case REFLEX_VM_OPCODE_TFLUSH:
    case REFLEX_VM_OPCODE_TINV:
        return reflex_vm_loader_register_valid(instruction->src_a);
    case REFLEX_VM_OPCODE_TSEND:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a);
    case REFLEX_VM_OPCODE_TRECV:
        return reflex_vm_loader_register_valid(instruction->dst);
    case REFLEX_VM_OPCODE_TLD:
    case REFLEX_VM_OPCODE_TST:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a);
    case REFLEX_VM_OPCODE_TSENSE:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a);
    case REFLEX_VM_OPCODE_TROUTE:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a) &&
               reflex_vm_loader_register_valid(instruction->src_b);
    case REFLEX_VM_OPCODE_TBIAS:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a);
    default:
        return false;
    }
}

reflex_err_t reflex_vm_validate_image(const reflex_vm_image_t *image)
{
    REFLEX_RETURN_ON_FALSE(image != NULL, REFLEX_ERR_INVALID_ARG, "vm_loader", "image is required");
    REFLEX_RETURN_ON_FALSE(image->magic == REFLEX_VM_IMAGE_MAGIC,
                        REFLEX_ERR_INVALID_RESPONSE,
                        "vm_loader",
                        "invalid image magic");
    
    // Support both v1 and v2 for backward compat during v2 migration
    REFLEX_RETURN_ON_FALSE(image->version == 1 || image->version == 2,
                        REFLEX_ERR_INVALID_VERSION,
                        "vm_loader",
                        "unsupported image version");

    REFLEX_RETURN_ON_FALSE(image->instructions != NULL,
                        REFLEX_ERR_INVALID_ARG,
                        "vm_loader",
                        "instructions are required");
    REFLEX_RETURN_ON_FALSE(image->instruction_count > 0,
                        REFLEX_ERR_INVALID_SIZE,
                        "vm_loader",
                        "instruction count is required");
    REFLEX_RETURN_ON_FALSE(image->entry_ip < image->instruction_count,
                        REFLEX_ERR_INVALID_ARG,
                        "vm_loader",
                        "entry point out of range");
    REFLEX_RETURN_ON_FALSE(image->private_memory != NULL || image->private_memory_count == 0,
                        REFLEX_ERR_INVALID_ARG,
                        "vm_loader",
                        "private memory pointer missing");

    for (size_t i = 0; i < image->instruction_count; ++i) {
        const reflex_vm_instruction_t *instruction = &image->instructions[i];

        REFLEX_RETURN_ON_FALSE(reflex_vm_loader_opcode_valid(instruction->opcode),
                            REFLEX_ERR_INVALID_RESPONSE,
                            "vm_loader",
                            "invalid opcode in image");
        REFLEX_RETURN_ON_FALSE(reflex_vm_loader_instruction_fields_valid(instruction),
                            REFLEX_ERR_INVALID_ARG,
                            "vm_loader",
                            "invalid register fields in image");

        if (instruction->opcode == REFLEX_VM_OPCODE_TJMP ||
            instruction->opcode == REFLEX_VM_OPCODE_TBRNEG ||
            instruction->opcode == REFLEX_VM_OPCODE_TBRZERO ||
            instruction->opcode == REFLEX_VM_OPCODE_TBRPOS) {
            REFLEX_RETURN_ON_FALSE(reflex_vm_loader_target_in_range(instruction->imm, image->instruction_count),
                                REFLEX_ERR_INVALID_ARG,
                                "vm_loader",
                                "branch target out of range");
        }

        if (instruction->opcode == REFLEX_VM_OPCODE_TSYS) {
            REFLEX_RETURN_ON_FALSE(reflex_vm_loader_syscall_valid(instruction->imm),
                                REFLEX_ERR_INVALID_ARG,
                                "vm_loader",
                                "invalid syscall selector in image");
        }
    }

    return REFLEX_OK;
}

reflex_err_t reflex_vm_load_image(reflex_vm_state_t *vm, const reflex_vm_image_t *image)
{
    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "vm_loader", "vm is required");
    REFLEX_RETURN_ON_ERROR(reflex_vm_validate_image(image), "vm_loader", "image validation failed");

    reflex_vm_unload(vm);

    vm->program = image->instructions;
    vm->program_length = image->instruction_count;
    vm->owns_program = false;
    vm->owned_private_memory = NULL;
    vm->owned_private_memory_count = 0;
    
    reflex_vm_mmu_init(&vm->mmu);
    if (image->private_memory_count > 0) {
        REFLEX_RETURN_ON_ERROR(reflex_vm_mmu_add_region(&vm->mmu,
                                                     image->private_memory,
                                                     image->private_memory_count,
                                                     REFLEX_MEM_PRIVATE,
                                                     0),
                            "vm_loader",
                            "private memory map failed");
    }

    reflex_vm_reset(vm);
    vm->ip = image->entry_ip;
    return REFLEX_OK;
}

reflex_err_t reflex_vm_crc32(const uint8_t *data, size_t len, uint32_t *out_crc)
{
    uint32_t crc = 0xFFFFFFFFu;

    REFLEX_RETURN_ON_FALSE(data != NULL || len == 0, REFLEX_ERR_INVALID_ARG, "vm_loader", "data required");
    REFLEX_RETURN_ON_FALSE(out_crc != NULL, REFLEX_ERR_INVALID_ARG, "vm_loader", "crc output required");

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    *out_crc = ~crc;
    return REFLEX_OK;
}

reflex_err_t reflex_vm_load_binary(reflex_vm_state_t *vm, const uint8_t *buffer, size_t len)
{
    reflex_vm_instruction_t *instrs = NULL;
    reflex_word18_t *data_words = NULL;
    reflex_vm_image_t image = {0};
    reflex_err_t err = REFLEX_OK;

    REFLEX_RETURN_ON_FALSE(vm != NULL, REFLEX_ERR_INVALID_ARG, "vm_loader", "vm required");
    REFLEX_RETURN_ON_FALSE(buffer != NULL, REFLEX_ERR_INVALID_ARG, "vm_loader", "buffer required");
    REFLEX_RETURN_ON_FALSE(len >= 16, REFLEX_ERR_INVALID_SIZE, "vm_loader", "image header too small");

    // Pre-unload existing program to prevent leaks
    reflex_vm_unload(vm);

    uint32_t magic;
    memcpy(&magic, buffer, 4);
    uint16_t version;
    memcpy(&version, buffer + 4, 2);
    uint32_t checksum;
    memcpy(&checksum, buffer + 6, 4);
    
    REFLEX_RETURN_ON_FALSE(magic == REFLEX_VM_IMAGE_MAGIC, REFLEX_ERR_INVALID_RESPONSE, "vm_loader", "bad magic");
    REFLEX_RETURN_ON_FALSE(version == 2, REFLEX_ERR_INVALID_VERSION, "vm_loader", "unsupported version");

    uint16_t entry_ip;
    memcpy(&entry_ip, buffer + 10, 2);
    uint16_t instr_count;
    memcpy(&instr_count, buffer + 12, 2);
    uint16_t data_count;
    memcpy(&data_count, buffer + 14, 2);

    size_t payload_len = (instr_count * 4) + (data_count * 5);
    size_t required_len = 16 + payload_len;
    REFLEX_RETURN_ON_FALSE(len >= required_len, REFLEX_ERR_INVALID_SIZE, "vm_loader", "image payload too small");

    uint32_t calc_crc = 0;
    REFLEX_RETURN_ON_ERROR(reflex_vm_crc32(buffer + 16, payload_len, &calc_crc), "vm_loader", "crc failed");
    REFLEX_RETURN_ON_FALSE(calc_crc == checksum, REFLEX_ERR_INVALID_CRC, "vm_loader", "image checksum failed");

    instrs = malloc(sizeof(reflex_vm_instruction_t) * instr_count);
    REFLEX_RETURN_ON_FALSE(instrs != NULL, REFLEX_ERR_NO_MEM, "vm_loader", "malloc failed");

    const uint8_t *instr_stream = buffer + 16;
    for (size_t i = 0; i < instr_count; i++) {
        uint32_t packed;
        memcpy(&packed, instr_stream + (i * 4), 4);
        
        instrs[i].opcode = (reflex_vm_opcode_t)(packed & 0x3F);
        instrs[i].dst    = (uint8_t)((packed >> 6) & 0x07);
        instrs[i].src_a  = (uint8_t)((packed >> 9) & 0x07);
        instrs[i].src_b  = (uint8_t)((packed >> 12) & 0x07);
        
        // Handle 17-bit signed immediate
        int32_t imm = (int32_t)(packed >> 15);
        if (imm & 0x10000) { // Sign bit for 17-bit
            imm |= ~0x1FFFF; // Sign extend
        }
        instrs[i].imm = imm;
    }

    if (data_count > 0) {
        const uint8_t *data_stream = instr_stream + (instr_count * 4);

        data_words = calloc(data_count, sizeof(reflex_word18_t));
        REFLEX_RETURN_ON_FALSE(data_words != NULL, REFLEX_ERR_NO_MEM, "vm_loader", "data alloc failed");

        for (size_t i = 0; i < data_count; ++i) {
            err = reflex_word18_unpack(data_stream + (i * REFLEX_PACKED_WORD18_BYTES), &data_words[i]);
            if (err != REFLEX_OK) {
                goto fail;
            }
        }
    }

    image.magic = REFLEX_VM_IMAGE_MAGIC;
    image.version = version;
    image.entry_ip = entry_ip;
    image.instructions = instrs;
    image.instruction_count = instr_count;
    image.private_memory = data_words;
    image.private_memory_count = data_count;
    err = reflex_vm_validate_image(&image);
    if (err != REFLEX_OK) {
        goto fail;
    }

    reflex_vm_mmu_init(&vm->mmu);
    if (data_count > 0) {
        err = reflex_vm_mmu_add_region(&vm->mmu, data_words, data_count, REFLEX_MEM_PRIVATE, 0);
        if (err != REFLEX_OK) {
            goto fail;
        }
    }

    vm->program = instrs;
    vm->program_length = instr_count;
    vm->owns_program = true;
    vm->owned_private_memory = data_words;
    vm->owned_private_memory_count = data_count;

    reflex_vm_reset(vm);
    vm->ip = entry_ip;

    return REFLEX_OK;

fail:
    free(instrs);
    free(data_words);
    reflex_vm_mmu_init(&vm->mmu);
    return err;
}


reflex_err_t reflex_vm_loader_self_check(void)
{
    static const reflex_vm_instruction_t valid_program[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = 1},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    static const reflex_vm_instruction_t invalid_program[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = REFLEX_VM_REGISTER_COUNT, .imm = 1},
    };
    static const reflex_vm_instruction_t invalid_branch_program[] = {
        {.opcode = REFLEX_VM_OPCODE_TBRZERO, .src_a = 0, .imm = 9},
    };
    static const reflex_vm_instruction_t invalid_syscall_program[] = {
        {.opcode = REFLEX_VM_OPCODE_TSYS, .imm = 99},
    };
    reflex_vm_image_t valid_image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = 1,
        .entry_ip = 0,
        .instructions = valid_program,
        .instruction_count = sizeof(valid_program) / sizeof(valid_program[0]),
        .private_memory = NULL,
        .private_memory_count = 0
    };
    reflex_vm_image_t invalid_magic_image = valid_image;
    reflex_vm_image_t invalid_register_image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = 1,
        .entry_ip = 0,
        .instructions = invalid_program,
        .instruction_count = sizeof(invalid_program) / sizeof(invalid_program[0]),
    };
    reflex_vm_image_t invalid_branch_image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = 1,
        .entry_ip = 0,
        .instructions = invalid_branch_program,
        .instruction_count = sizeof(invalid_branch_program) / sizeof(invalid_branch_program[0]),
    };
    reflex_vm_image_t invalid_syscall_image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = 1,
        .entry_ip = 0,
        .instructions = invalid_syscall_program,
        .instruction_count = sizeof(invalid_syscall_program) / sizeof(invalid_syscall_program[0]),
    };
    static reflex_vm_state_t vm;

    REFLEX_RETURN_ON_ERROR(reflex_vm_validate_image(&valid_image), "vm_loader", "valid image rejected");
    REFLEX_RETURN_ON_ERROR(reflex_vm_load_image(&vm, &valid_image), "vm_loader", "valid image failed to load");
    REFLEX_RETURN_ON_FALSE(vm.program == valid_program, REFLEX_FAIL, "vm_loader", "program pointer not loaded");
    REFLEX_RETURN_ON_FALSE(vm.program_length == 2, REFLEX_FAIL, "vm_loader", "program length not loaded");
    REFLEX_RETURN_ON_FALSE(vm.ip == 0, REFLEX_FAIL, "vm_loader", "entry point not loaded");
    REFLEX_RETURN_ON_FALSE(vm.status == REFLEX_VM_STATUS_READY, REFLEX_FAIL, "vm_loader", "vm must be ready after load");

    invalid_magic_image.magic = 0;
    REFLEX_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_magic_image) == REFLEX_ERR_INVALID_RESPONSE,
                        REFLEX_FAIL,
                        "vm_loader",
                        "invalid magic must be rejected");
    REFLEX_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_register_image) == REFLEX_ERR_INVALID_ARG,
                        REFLEX_FAIL,
                        "vm_loader",
                        "invalid register image must be rejected");
    REFLEX_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_branch_image) == REFLEX_ERR_INVALID_ARG,
                        REFLEX_FAIL,
                        "vm_loader",
                        "invalid branch image must be rejected");
    REFLEX_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_syscall_image) == REFLEX_ERR_INVALID_ARG,
                        REFLEX_FAIL,
                        "vm_loader",
                        "invalid syscall image must be rejected");

    return REFLEX_OK;
}
