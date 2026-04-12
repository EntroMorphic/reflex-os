#include "reflex_vm_loader.h"

#include "esp_check.h"

#include "reflex_vm.h"

static bool reflex_vm_loader_opcode_valid(reflex_vm_opcode_t opcode)
{
    return opcode >= REFLEX_VM_OPCODE_TNOP && opcode <= REFLEX_VM_OPCODE_THALT;
}

static bool reflex_vm_loader_register_valid(uint8_t index)
{
    return index < REFLEX_VM_REGISTER_COUNT;
}

static bool reflex_vm_loader_syscall_valid(int16_t imm)
{
    return imm >= REFLEX_VM_SYSCALL_LOG && imm <= REFLEX_VM_SYSCALL_CONFIG_GET;
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
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a) &&
               reflex_vm_loader_register_valid(instruction->src_b);
    case REFLEX_VM_OPCODE_TBRNEG:
    case REFLEX_VM_OPCODE_TBRZERO:
    case REFLEX_VM_OPCODE_TBRPOS:
        return reflex_vm_loader_register_valid(instruction->src_a);
    case REFLEX_VM_OPCODE_TLD:
    case REFLEX_VM_OPCODE_TST:
        return reflex_vm_loader_register_valid(instruction->dst) &&
               reflex_vm_loader_register_valid(instruction->src_a);
    default:
        return false;
    }
}

esp_err_t reflex_vm_validate_image(const reflex_vm_image_t *image)
{
    ESP_RETURN_ON_FALSE(image != NULL, ESP_ERR_INVALID_ARG, "vm_loader", "image is required");
    ESP_RETURN_ON_FALSE(image->magic == REFLEX_VM_IMAGE_MAGIC,
                        ESP_ERR_INVALID_RESPONSE,
                        "vm_loader",
                        "invalid image magic");
    ESP_RETURN_ON_FALSE(image->version == REFLEX_VM_IMAGE_VERSION,
                        ESP_ERR_INVALID_VERSION,
                        "vm_loader",
                        "unsupported image version");
    ESP_RETURN_ON_FALSE(image->instructions != NULL,
                        ESP_ERR_INVALID_ARG,
                        "vm_loader",
                        "instructions are required");
    ESP_RETURN_ON_FALSE(image->instruction_count > 0,
                        ESP_ERR_INVALID_SIZE,
                        "vm_loader",
                        "instruction count is required");
    ESP_RETURN_ON_FALSE(image->entry_ip < image->instruction_count,
                        ESP_ERR_INVALID_ARG,
                        "vm_loader",
                        "entry point out of range");
    ESP_RETURN_ON_FALSE(image->memory != NULL || image->memory_count == 0,
                        ESP_ERR_INVALID_ARG,
                        "vm_loader",
                        "memory pointer missing");

    for (size_t i = 0; i < image->instruction_count; ++i) {
        const reflex_vm_instruction_t *instruction = &image->instructions[i];

        ESP_RETURN_ON_FALSE(reflex_vm_loader_opcode_valid(instruction->opcode),
                            ESP_ERR_INVALID_RESPONSE,
                            "vm_loader",
                            "invalid opcode in image");
        ESP_RETURN_ON_FALSE(reflex_vm_loader_instruction_fields_valid(instruction),
                            ESP_ERR_INVALID_ARG,
                            "vm_loader",
                            "invalid register fields in image");

        if (instruction->opcode == REFLEX_VM_OPCODE_TJMP ||
            instruction->opcode == REFLEX_VM_OPCODE_TBRNEG ||
            instruction->opcode == REFLEX_VM_OPCODE_TBRZERO ||
            instruction->opcode == REFLEX_VM_OPCODE_TBRPOS) {
            ESP_RETURN_ON_FALSE(reflex_vm_loader_target_in_range(instruction->imm, image->instruction_count),
                                ESP_ERR_INVALID_ARG,
                                "vm_loader",
                                "branch target out of range");
        }

        if (instruction->opcode == REFLEX_VM_OPCODE_TSYS) {
            ESP_RETURN_ON_FALSE(reflex_vm_loader_syscall_valid(instruction->imm),
                                ESP_ERR_INVALID_ARG,
                                "vm_loader",
                                "invalid syscall selector in image");
        }
    }

    return ESP_OK;
}

esp_err_t reflex_vm_load_image(reflex_vm_state_t *vm, const reflex_vm_image_t *image)
{
    ESP_RETURN_ON_FALSE(vm != NULL, ESP_ERR_INVALID_ARG, "vm_loader", "vm is required");
    ESP_RETURN_ON_ERROR(reflex_vm_validate_image(image), "vm_loader", "image validation failed");

    vm->program = image->instructions;
    vm->program_length = image->instruction_count;
    vm->memory = image->memory;
    vm->memory_length = image->memory_count;

    reflex_vm_reset(vm);
    vm->ip = image->entry_ip;
    return ESP_OK;
}

esp_err_t reflex_vm_loader_self_check(void)
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
        .version = REFLEX_VM_IMAGE_VERSION,
        .entry_ip = 0,
        .instructions = valid_program,
        .instruction_count = sizeof(valid_program) / sizeof(valid_program[0]),
    };
    reflex_vm_image_t invalid_magic_image = valid_image;
    reflex_vm_image_t invalid_register_image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = REFLEX_VM_IMAGE_VERSION,
        .entry_ip = 0,
        .instructions = invalid_program,
        .instruction_count = sizeof(invalid_program) / sizeof(invalid_program[0]),
    };
    reflex_vm_image_t invalid_branch_image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = REFLEX_VM_IMAGE_VERSION,
        .entry_ip = 0,
        .instructions = invalid_branch_program,
        .instruction_count = sizeof(invalid_branch_program) / sizeof(invalid_branch_program[0]),
    };
    reflex_vm_image_t invalid_syscall_image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = REFLEX_VM_IMAGE_VERSION,
        .entry_ip = 0,
        .instructions = invalid_syscall_program,
        .instruction_count = sizeof(invalid_syscall_program) / sizeof(invalid_syscall_program[0]),
    };
    reflex_vm_state_t vm = {0};

    ESP_RETURN_ON_ERROR(reflex_vm_validate_image(&valid_image), "vm_loader", "valid image rejected");
    ESP_RETURN_ON_ERROR(reflex_vm_load_image(&vm, &valid_image), "vm_loader", "valid image failed to load");
    ESP_RETURN_ON_FALSE(vm.program == valid_program, ESP_FAIL, "vm_loader", "program pointer not loaded");
    ESP_RETURN_ON_FALSE(vm.program_length == 2, ESP_FAIL, "vm_loader", "program length not loaded");
    ESP_RETURN_ON_FALSE(vm.ip == 0, ESP_FAIL, "vm_loader", "entry point not loaded");
    ESP_RETURN_ON_FALSE(vm.status == REFLEX_VM_STATUS_READY, ESP_FAIL, "vm_loader", "vm must be ready after load");

    invalid_magic_image.magic = 0;
    ESP_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_magic_image) == ESP_ERR_INVALID_RESPONSE,
                        ESP_FAIL,
                        "vm_loader",
                        "invalid magic must be rejected");
    ESP_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_register_image) == ESP_ERR_INVALID_ARG,
                        ESP_FAIL,
                        "vm_loader",
                        "invalid register image must be rejected");
    ESP_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_branch_image) == ESP_ERR_INVALID_ARG,
                        ESP_FAIL,
                        "vm_loader",
                        "invalid branch image must be rejected");
    ESP_RETURN_ON_FALSE(reflex_vm_validate_image(&invalid_syscall_image) == ESP_ERR_INVALID_ARG,
                        ESP_FAIL,
                        "vm_loader",
                        "invalid syscall image must be rejected");

    return ESP_OK;
}
