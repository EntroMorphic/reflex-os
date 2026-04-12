#include "reflex_vm.h"

#include <string.h>

#include "esp_check.h"

static esp_err_t reflex_vm_fault(reflex_vm_state_t *vm, reflex_vm_fault_t fault);

static void reflex_vm_zero_word(reflex_word18_t *word)
{
    memset(word->trits, 0, sizeof(word->trits));
}

static bool reflex_vm_register_index_valid(uint8_t index)
{
    return index < REFLEX_VM_REGISTER_COUNT;
}

static esp_err_t reflex_vm_validate_registers(const reflex_vm_state_t *vm,
                                              const reflex_vm_instruction_t *instruction)
{
    bool valid = true;

    switch (instruction->opcode) {
    case REFLEX_VM_OPCODE_TNOP:
    case REFLEX_VM_OPCODE_THALT:
        return ESP_OK;
    case REFLEX_VM_OPCODE_TLDI:
        valid = reflex_vm_register_index_valid(instruction->dst);
        break;
    case REFLEX_VM_OPCODE_TMOV:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a);
        break;
    case REFLEX_VM_OPCODE_TADD:
    case REFLEX_VM_OPCODE_TSUB:
    case REFLEX_VM_OPCODE_TCMP:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a) &&
                reflex_vm_register_index_valid(instruction->src_b);
        break;
    case REFLEX_VM_OPCODE_TJMP:
        return ESP_OK;
    case REFLEX_VM_OPCODE_TSYS:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a) &&
                reflex_vm_register_index_valid(instruction->src_b);
        break;
    case REFLEX_VM_OPCODE_TBRNEG:
    case REFLEX_VM_OPCODE_TBRZERO:
    case REFLEX_VM_OPCODE_TBRPOS:
        valid = reflex_vm_register_index_valid(instruction->src_a);
        break;
    case REFLEX_VM_OPCODE_TLD:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a);
        break;
    case REFLEX_VM_OPCODE_TST:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a);
        break;
    default:
        return ESP_OK;
    }

    if (!valid) {
        return reflex_vm_fault((reflex_vm_state_t *)vm, REFLEX_VM_FAULT_INVALID_REGISTER);
    }

    return ESP_OK;
}

static reflex_trit_t reflex_vm_branch_trit(const reflex_word18_t *word)
{
    return word->trits[REFLEX_VM_COMPARE_TRIT_INDEX];
}

static esp_err_t reflex_vm_fault(reflex_vm_state_t *vm, reflex_vm_fault_t fault)
{
    ESP_RETURN_ON_FALSE(vm != NULL, ESP_ERR_INVALID_ARG, "vm", "vm is required");
    vm->status = REFLEX_VM_STATUS_FAULTED;
    vm->fault = fault;
    return ESP_FAIL;
}

static esp_err_t reflex_vm_validate(reflex_vm_state_t *vm)
{
    ESP_RETURN_ON_FALSE(vm != NULL, ESP_ERR_INVALID_ARG, "vm", "vm is required");
    ESP_RETURN_ON_FALSE(vm->program != NULL, ESP_ERR_INVALID_ARG, "vm", "program is required");
    ESP_RETURN_ON_FALSE(vm->program_length > 0, ESP_ERR_INVALID_ARG, "vm", "program length is required");
    return ESP_OK;
}

static esp_err_t reflex_vm_jump(reflex_vm_state_t *vm, int32_t target)
{
    if (target < 0 || (uint32_t)target >= vm->program_length) {
        return reflex_vm_fault(vm, REFLEX_VM_FAULT_PC_OUT_OF_RANGE);
    }

    vm->ip = (uint32_t)target;
    return ESP_OK;
}

void reflex_vm_reset(reflex_vm_state_t *vm)
{
    if (vm == NULL) {
        return;
    }

    for (size_t i = 0; i < REFLEX_VM_REGISTER_COUNT; ++i) {
        reflex_vm_zero_word(&vm->registers[i]);
    }

    vm->status = REFLEX_VM_STATUS_READY;
    vm->fault = REFLEX_VM_FAULT_NONE;
    vm->ip = 0;
    vm->steps_executed = 0;
}

esp_err_t reflex_vm_step(reflex_vm_state_t *vm)
{
    const reflex_vm_instruction_t *instruction;
    reflex_trit_t compare_result;
    esp_err_t err;

    ESP_RETURN_ON_ERROR(reflex_vm_validate(vm), "vm", "invalid vm state");
    if (vm->status == REFLEX_VM_STATUS_HALTED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (vm->status == REFLEX_VM_STATUS_FAULTED) {
        return ESP_FAIL;
    }
    if (vm->ip >= vm->program_length) {
        return reflex_vm_fault(vm, REFLEX_VM_FAULT_PC_OUT_OF_RANGE);
    }

    vm->status = REFLEX_VM_STATUS_RUNNING;
    instruction = &vm->program[vm->ip];

    ESP_RETURN_ON_ERROR(reflex_vm_validate_registers(vm, instruction), "vm", "invalid register access");

    vm->ip += 1;
    vm->steps_executed += 1;

    switch (instruction->opcode) {
    case REFLEX_VM_OPCODE_TNOP:
        break;
    case REFLEX_VM_OPCODE_TLDI:
        err = reflex_word18_from_int32(instruction->imm, &vm->registers[instruction->dst]);
        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_IMMEDIATE);
        }
        break;
    case REFLEX_VM_OPCODE_TMOV:
        memcpy(&vm->registers[instruction->dst],
               &vm->registers[instruction->src_a],
               sizeof(vm->registers[instruction->dst]));
        break;
    case REFLEX_VM_OPCODE_TADD:
        err = reflex_word18_add(&vm->registers[instruction->src_a],
                                &vm->registers[instruction->src_b],
                                &vm->registers[instruction->dst]);
        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_ARITHMETIC_OVERFLOW);
        }
        break;
    case REFLEX_VM_OPCODE_TSUB:
        err = reflex_word18_subtract(&vm->registers[instruction->src_a],
                                     &vm->registers[instruction->src_b],
                                     &vm->registers[instruction->dst]);
        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_ARITHMETIC_OVERFLOW);
        }
        break;
    case REFLEX_VM_OPCODE_TCMP:
        err = reflex_word18_compare(&vm->registers[instruction->src_a],
                                    &vm->registers[instruction->src_b],
                                    &compare_result);
        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_ARITHMETIC_OVERFLOW);
        }
        reflex_vm_zero_word(&vm->registers[instruction->dst]);
        vm->registers[instruction->dst].trits[REFLEX_VM_COMPARE_TRIT_INDEX] = compare_result;
        break;
    case REFLEX_VM_OPCODE_TJMP:
        return reflex_vm_jump(vm, instruction->imm);
    case REFLEX_VM_OPCODE_TBRNEG:
        if (reflex_vm_branch_trit(&vm->registers[instruction->src_a]) == REFLEX_TRIT_NEG) {
            return reflex_vm_jump(vm, instruction->imm);
        }
        break;
    case REFLEX_VM_OPCODE_TBRZERO:
        if (reflex_vm_branch_trit(&vm->registers[instruction->src_a]) == REFLEX_TRIT_ZERO) {
            return reflex_vm_jump(vm, instruction->imm);
        }
        break;
    case REFLEX_VM_OPCODE_TBRPOS:
        if (reflex_vm_branch_trit(&vm->registers[instruction->src_a]) == REFLEX_TRIT_POS) {
            return reflex_vm_jump(vm, instruction->imm);
        }
        break;
    case REFLEX_VM_OPCODE_TSYS:
        if (vm->syscall_handler == NULL) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_SYSCALL);
        }
        err = vm->syscall_handler(vm,
                                  (reflex_vm_syscall_t)instruction->imm,
                                  &vm->registers[instruction->src_a],
                                  &vm->registers[instruction->src_b],
                                  &vm->registers[instruction->dst],
                                  vm->syscall_context);
        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_SYSCALL);
        }
        break;
    case REFLEX_VM_OPCODE_THALT:
        vm->status = REFLEX_VM_STATUS_HALTED;
        break;
    case REFLEX_VM_OPCODE_TLD:
    case REFLEX_VM_OPCODE_TST:
        return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_MEMORY_ACCESS);
    default:
        return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_OPCODE);
    }

    if (vm->status == REFLEX_VM_STATUS_RUNNING) {
        vm->status = REFLEX_VM_STATUS_READY;
    }

    return ESP_OK;
}

esp_err_t reflex_vm_run(reflex_vm_state_t *vm, uint32_t max_steps)
{
    uint32_t steps = 0;

    ESP_RETURN_ON_FALSE(max_steps > 0, ESP_ERR_INVALID_ARG, "vm", "max steps must be positive");
    ESP_RETURN_ON_ERROR(reflex_vm_validate(vm), "vm", "invalid vm state");

    while (vm->status != REFLEX_VM_STATUS_HALTED && steps < max_steps) {
        esp_err_t err = reflex_vm_step(vm);
        if (err != ESP_OK) {
            return err;
        }
        steps += 1;
    }

    if (vm->status == REFLEX_VM_STATUS_HALTED) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t reflex_vm_self_check(void)
{
    static const reflex_vm_instruction_t program[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = 1},
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 1, .imm = -1},
        {.opcode = REFLEX_VM_OPCODE_TADD, .dst = 2, .src_a = 0, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_TCMP, .dst = 3, .src_a = 2, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_TBRPOS, .src_a = 3, .imm = 6},
        {.opcode = REFLEX_VM_OPCODE_THALT},
        {.opcode = REFLEX_VM_OPCODE_TSUB, .dst = 4, .src_a = 2, .src_b = 0},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    static const reflex_vm_instruction_t syscall_program[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = REFLEX_VM_CONFIG_DEVICE_NAME_LENGTH},
        {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 1, .src_a = 0, .imm = REFLEX_VM_SYSCALL_CONFIG_GET},
        {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 2, .imm = REFLEX_VM_SYSCALL_UPTIME},
        {.opcode = REFLEX_VM_OPCODE_TSYS, .dst = 3, .src_a = 1, .imm = REFLEX_VM_SYSCALL_LOG},
        {.opcode = REFLEX_VM_OPCODE_THALT},
    };
    static const reflex_vm_instruction_t bad_program[] = {
        {.opcode = REFLEX_VM_OPCODE_TSYS, .imm = 99},
    };
    static const reflex_vm_instruction_t invalid_register_program[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = REFLEX_VM_REGISTER_COUNT, .imm = 1},
    };
    static reflex_vm_state_t vm;
    static reflex_vm_state_t bad_vm;
    static reflex_vm_state_t syscall_vm;
    static reflex_vm_state_t invalid_register_vm;
    static reflex_word18_t expected_two;
    static reflex_word18_t expected_one;
    static reflex_word18_t expected_name_length;
    int32_t uptime_value = 0;

    vm.program = program;
    vm.program_length = sizeof(program) / sizeof(program[0]);
    bad_vm.program = bad_program;
    bad_vm.program_length = sizeof(bad_program) / sizeof(bad_program[0]);
    syscall_vm.program = syscall_program;
    syscall_vm.program_length = sizeof(syscall_program) / sizeof(syscall_program[0]);
    invalid_register_vm.program = invalid_register_program;
    invalid_register_vm.program_length = sizeof(invalid_register_program) / sizeof(invalid_register_program[0]);

    reflex_vm_reset(&vm);
    ESP_RETURN_ON_ERROR(reflex_word18_from_int32(2, &expected_two), "vm", "failed to create expected value 2");
    ESP_RETURN_ON_ERROR(reflex_word18_from_int32(1, &expected_one), "vm", "failed to create expected value 1");
    ESP_RETURN_ON_ERROR(reflex_vm_run(&vm, 16), "vm", "sample program failed");
    ESP_RETURN_ON_FALSE(vm.status == REFLEX_VM_STATUS_HALTED, ESP_FAIL, "vm", "sample program must halt");
    ESP_RETURN_ON_FALSE(vm.fault == REFLEX_VM_FAULT_NONE, ESP_FAIL, "vm", "sample program must not fault");
    ESP_RETURN_ON_FALSE(reflex_word18_equal(&vm.registers[2], &expected_two), ESP_FAIL, "vm", "r2 must contain 2");
    ESP_RETURN_ON_FALSE(reflex_word18_equal(&vm.registers[4], &expected_one), ESP_FAIL, "vm", "r4 must contain 1");
    ESP_RETURN_ON_FALSE(vm.steps_executed == 7, ESP_FAIL, "vm", "sample program must execute 7 steps");

    reflex_vm_use_default_syscalls(&syscall_vm);
    reflex_vm_reset(&syscall_vm);
    ESP_RETURN_ON_ERROR(reflex_word18_from_int32((int32_t)strlen("reflex-os"), &expected_name_length),
                        "vm",
                        "failed to create expected name length");
    ESP_RETURN_ON_ERROR(reflex_vm_run(&syscall_vm, 8), "vm", "syscall program failed");
    ESP_RETURN_ON_FALSE(syscall_vm.status == REFLEX_VM_STATUS_HALTED, ESP_FAIL, "vm", "syscall program must halt");
    ESP_RETURN_ON_FALSE(reflex_word18_equal(&syscall_vm.registers[1], &expected_name_length),
                        ESP_FAIL,
                        "vm",
                        "config syscall must return device name length");
    ESP_RETURN_ON_ERROR(reflex_word18_to_int32(&syscall_vm.registers[2], &uptime_value), "vm", "uptime result invalid");
    ESP_RETURN_ON_FALSE(uptime_value > 0, ESP_FAIL, "vm", "uptime syscall must return a positive value");

    reflex_vm_reset(&bad_vm);
    ESP_RETURN_ON_FALSE(reflex_vm_run(&bad_vm, 4) == ESP_FAIL, ESP_FAIL, "vm", "bad program must fault");
    ESP_RETURN_ON_FALSE(bad_vm.status == REFLEX_VM_STATUS_FAULTED, ESP_FAIL, "vm", "bad program must fault status");
    ESP_RETURN_ON_FALSE(bad_vm.fault == REFLEX_VM_FAULT_INVALID_SYSCALL,
                        ESP_FAIL,
                        "vm",
                        "bad program must report invalid syscall");

    reflex_vm_reset(&invalid_register_vm);
    ESP_RETURN_ON_FALSE(reflex_vm_run(&invalid_register_vm, 1) == ESP_FAIL,
                        ESP_FAIL,
                        "vm",
                        "invalid register program must fault");
    ESP_RETURN_ON_FALSE(invalid_register_vm.fault == REFLEX_VM_FAULT_INVALID_REGISTER,
                        ESP_FAIL,
                        "vm",
                        "invalid register program must report invalid register");

    reflex_vm_reset(&bad_vm);
    ESP_RETURN_ON_FALSE(reflex_vm_run(&bad_vm, 4) == ESP_FAIL, ESP_FAIL, "vm", "unsupported syscall must fault");

    return ESP_OK;
}
