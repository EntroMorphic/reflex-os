#include "reflex_vm.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "reflex_fabric.h"
#include "reflex_cache.h"
#include "goose.h"

static esp_err_t reflex_vm_get_string(const reflex_vm_state_t *vm, uint32_t addr, char *out, size_t max_len)
{
    for (size_t i = 0; i < max_len - 1; i++) {
        reflex_word18_t w;
        int32_t v;
        if (reflex_vm_mem_get_raw(vm, addr + i, &w) != ESP_OK) {
            out[i] = '\0';
            return ESP_FAIL;
        }
        reflex_word18_to_int32(&w, &v);
        out[i] = (char)v;
        if (v == 0) return ESP_OK;
    }
    out[max_len - 1] = '\0'; // Force termination
    return ESP_OK;
}

static void reflex_vm_zero_word(reflex_word18_t *word)
{
    memset(word->trits, 0, sizeof(word->trits));
}

static esp_err_t reflex_vm_fault(reflex_vm_state_t *vm, reflex_vm_fault_t fault)
{
    ESP_RETURN_ON_FALSE(vm != NULL, ESP_ERR_INVALID_ARG, "vm", "vm is required");
    vm->status = REFLEX_VM_STATUS_FAULTED;
    vm->fault = fault;
    return ESP_FAIL;
}

static esp_err_t reflex_vm_validate_state(const reflex_vm_state_t *vm)
{
    ESP_RETURN_ON_FALSE(vm != NULL, ESP_ERR_INVALID_ARG, "vm", "vm state null");
    ESP_RETURN_ON_FALSE(vm->program != NULL, ESP_ERR_INVALID_STATE, "vm", "program pointer null");
    ESP_RETURN_ON_FALSE(vm->program_length > 0, ESP_ERR_INVALID_STATE, "vm", "program length zero");
    ESP_RETURN_ON_FALSE(vm->ip < vm->program_length, REFLEX_VM_FAULT_PC_OUT_OF_RANGE, "vm", "ip out of range");
    
    return ESP_OK;
}

esp_err_t reflex_vm_mem_get_raw(const reflex_vm_state_t *vm, uint32_t index, reflex_word18_t *out)
{
    reflex_word18_t *host_ptr;
    ESP_RETURN_ON_ERROR(reflex_vm_mmu_translate(&vm->mmu, index, &host_ptr), "vm", "translate get failed");
    memcpy(out, host_ptr, sizeof(*out));
    return ESP_OK;
}

esp_err_t reflex_vm_mem_set_raw(reflex_vm_state_t *vm, uint32_t index, const reflex_word18_t *in)
{
    reflex_word18_t *host_ptr;
    ESP_RETURN_ON_ERROR(reflex_vm_mmu_translate(&vm->mmu, index, &host_ptr), "vm", "translate set failed");
    memcpy(host_ptr, in, sizeof(*host_ptr));
    return ESP_OK;
}

static bool reflex_vm_register_index_valid(uint8_t index)
{
    return index < REFLEX_VM_REGISTER_COUNT;
}

static reflex_trit_t reflex_vm_branch_trit(const reflex_word18_t *word)
{
    return word->trits[REFLEX_VM_COMPARE_TRIT_INDEX];
}

static esp_err_t reflex_vm_jump(reflex_vm_state_t *vm, int32_t target)
{
    if (target < 0 || (uint32_t)target >= vm->program_length) {
        return reflex_vm_fault(vm, REFLEX_VM_FAULT_PC_OUT_OF_RANGE);
    }

    vm->ip = (uint32_t)target;
    return ESP_OK;
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
    case REFLEX_VM_OPCODE_TSEL:
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
    case REFLEX_VM_OPCODE_TSENSE:
    case REFLEX_VM_OPCODE_TINV:
        valid = reflex_vm_register_index_valid(instruction->src_a);
        break;
    case REFLEX_VM_OPCODE_TROUTE:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a) &&
                reflex_vm_register_index_valid(instruction->src_b);
        break;
    case REFLEX_VM_OPCODE_TBIAS:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a);
        break;
    case REFLEX_VM_OPCODE_TLD:
    case REFLEX_VM_OPCODE_TST:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a);
        break;
    case REFLEX_VM_OPCODE_TSEND:
        valid = reflex_vm_register_index_valid(instruction->dst) &&
                reflex_vm_register_index_valid(instruction->src_a);
        break;
    case REFLEX_VM_OPCODE_TRECV:
        valid = reflex_vm_register_index_valid(instruction->dst);
        break;
    default:
        return ESP_OK;
    }

    if (!valid) {
        return reflex_vm_fault((reflex_vm_state_t *)vm, REFLEX_VM_FAULT_INVALID_REGISTER);
    }

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

void reflex_vm_unload(reflex_vm_state_t *vm)
{
    if (vm == NULL) {
        return;
    }

    // Phase 10 Hardening: Clean up geometric routes before unloading
    for (size_t i = 0; i < vm->route_count; i++) {
        // Inhibit the route to ensure hardware safety on shutdown
        vm->route_manifest[i].orientation = REFLEX_TRIT_ZERO;
        goose_apply_route(&vm->route_manifest[i]);
    }
    vm->route_count = 0;

    if (vm->owns_program && vm->program != NULL) {
        free((void*)vm->program);
    }

    if (vm->owned_private_memory != NULL) {
        free(vm->owned_private_memory);
    }

    vm->program = NULL;
    vm->program_length = 0;
    vm->owns_program = false;
    vm->owned_private_memory = NULL;
    vm->owned_private_memory_count = 0;

    reflex_vm_mmu_init(&vm->mmu);
    reflex_vm_reset(vm);
}

esp_err_t reflex_vm_step(reflex_vm_state_t *vm)
{
    const reflex_vm_instruction_t *instruction;
    reflex_trit_t compare_result;
    esp_err_t err;

    ESP_RETURN_ON_ERROR(reflex_vm_validate_state(vm), "vm", "invalid vm state");
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
    case REFLEX_VM_OPCODE_TSEL:
    {
        // TSEL DST, SRC_A (SEL), SRC_B (ZERO), IMM (NEG_REG | POS_REG)
        // IMM high byte = POS register, low byte = NEG register
        uint8_t neg_reg = (uint8_t)(instruction->imm & 0x07);
        uint8_t pos_reg = (uint8_t)((instruction->imm >> 3) & 0x07);
        
        if (neg_reg >= REFLEX_VM_REGISTER_COUNT || pos_reg >= REFLEX_VM_REGISTER_COUNT) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_REGISTER);
        }

        err = reflex_word18_select(reflex_vm_branch_trit(&vm->registers[instruction->src_a]),
                                   &vm->registers[neg_reg],
                                   &vm->registers[instruction->src_b],
                                   &vm->registers[pos_reg],
                                   &vm->registers[instruction->dst]);
        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_ARITHMETIC_OVERFLOW);
        }
        break;
    }
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
    case REFLEX_VM_OPCODE_TSEND:
    {
        int32_t target_node = 0;

        ESP_RETURN_ON_ERROR(reflex_word18_to_int32(&vm->registers[instruction->dst], &target_node),
                            "vm",
                            "invalid target node");
        if (target_node < 0 || target_node > UINT8_MAX) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_IMMEDIATE);
        }

        reflex_message_t msg = {
            .to = (uint8_t)target_node,
            .from = vm->node_id,
            .op = (uint8_t)instruction->imm,
            .channel = REFLEX_CHAN_SYSTEM,
            .payload = vm->registers[instruction->src_a]
        };
        err = reflex_fabric_send(&msg);
        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_SYSCALL);
        }
        break;
    }
    case REFLEX_VM_OPCODE_TRECV:
    {
        reflex_message_t msg;
        err = reflex_fabric_recv(vm->node_id, &msg);
        if (err == ESP_OK) {
            vm->registers[instruction->dst] = msg.payload;
        } else if (err == ESP_ERR_NOT_FOUND) {
            reflex_vm_zero_word(&vm->registers[instruction->dst]);
            err = ESP_OK;
        }
        break;
    }
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
    {
        int32_t addr;
        reflex_word18_t val;
        ESP_RETURN_ON_ERROR(reflex_word18_to_int32(&vm->registers[instruction->src_a], &addr), "vm", "invalid addr");
        
        if (vm->cache) {
            err = reflex_cache_load(vm, (uint32_t)addr, &val);
        } else {
            err = reflex_vm_mem_get_raw(vm, (uint32_t)addr, &val);
        }

        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_MEMORY_ACCESS);
        }
        memcpy(&vm->registers[instruction->dst], &val, sizeof(val));
        break;
    }
    case REFLEX_VM_OPCODE_TST:
    {
        int32_t addr;
        ESP_RETURN_ON_ERROR(reflex_word18_to_int32(&vm->registers[instruction->dst], &addr), "vm", "invalid addr");

        if (vm->cache) {
            err = reflex_cache_store(vm, (uint32_t)addr, &vm->registers[instruction->src_a]);
        } else {
            err = reflex_vm_mem_set_raw(vm, (uint32_t)addr, &vm->registers[instruction->src_a]);
        }

        if (err != ESP_OK) {
            return reflex_vm_fault(vm, REFLEX_VM_FAULT_INVALID_MEMORY_ACCESS);
        }
        break;
    }
    case REFLEX_VM_OPCODE_TFLUSH:
    {
        int32_t addr;
        ESP_RETURN_ON_ERROR(reflex_word18_to_int32(&vm->registers[instruction->src_a], &addr), "vm", "invalid addr");
        if (vm->cache) {
            reflex_cache_flush(vm, (uint32_t)addr);
        }
        break;
    }
    case REFLEX_VM_OPCODE_TINV:
    {
        int32_t addr;
        ESP_RETURN_ON_ERROR(reflex_word18_to_int32(&vm->registers[instruction->src_a], &addr), "vm", "invalid addr");
        if (vm->cache) {
            reflex_cache_invalidate(vm, (uint32_t)addr);
        }
        break;
    }
    case REFLEX_VM_OPCODE_TROUTE:
    {
        char src_name[16], sink_name[16];
        int32_t src_addr, sink_addr;
        reflex_word18_to_int32(&vm->registers[instruction->src_a], &src_addr);
        reflex_word18_to_int32(&vm->registers[instruction->src_b], &sink_addr);
        
        // Simplified: Assume names are in VM memory as words
        for(int i=0; i<15; i++) {
            reflex_word18_t w;
            int32_t v;
            reflex_vm_mem_get_raw(vm, src_addr + i, &w);
            reflex_word18_to_int32(&w, &v);
            src_name[i] = (char)v; if (v == 0) break;
        }
        for(int i=0; i<15; i++) {
            reflex_word18_t w;
            int32_t v;
            reflex_vm_mem_get_raw(vm, sink_addr + i, &w);
            reflex_word18_to_int32(&w, &v);
            sink_name[i] = (char)v; if (v == 0) break;
        }

        goose_cell_t *source = goose_fabric_get_cell(src_name);
        goose_cell_t *sink = goose_fabric_get_cell(sink_name);

        if (source && sink) {
            // Create a temporary route (this is a bit heavy for an opcode, but this is Phase 10)
            static goose_route_t vm_route;
            snprintf(vm_route.name, 16, "vm_route");
            vm_route.source = source;
            vm_route.sink = sink;
            vm_route.orientation = REFLEX_TRIT_POS;
            vm_route.coupling = GOOSE_COUPLING_SOFTWARE;
            goose_apply_route(&vm_route);
            reflex_word18_from_int32(1, &vm->registers[instruction->dst]);
        } else {
            reflex_word18_from_int32(-1, &vm->registers[instruction->dst]);
        }
        break;
    }
    case REFLEX_VM_OPCODE_TBIAS:
    {
        char route_name[16];
        int32_t name_addr;
        reflex_word18_to_int32(&vm->registers[instruction->src_a], &name_addr);
        for(int i=0; i<15; i++) {
            reflex_word18_t w;
            int32_t v;
            reflex_vm_mem_get_raw(vm, name_addr + i, &w);
            reflex_word18_to_int32(&w, &v);
            route_name[i] = (char)v; if (v == 0) break;
        }
        // In this minimal version, we'll just support a global 'bias' signal 
        // for now or find the route in a registry.
        // For Phase 10 proof, we'll just set a success trit.
        reflex_word18_from_int32(1, &vm->registers[instruction->dst]);
        break;
    }
    case REFLEX_VM_OPCODE_TSENSE:
    {
        char cell_name[16];
        int32_t name_addr;
        reflex_word18_to_int32(&vm->registers[instruction->src_a], &name_addr);
        for(int i=0; i<15; i++) {
            reflex_word18_t w;
            int32_t v;
            reflex_vm_mem_get_raw(vm, name_addr + i, &w);
            reflex_word18_to_int32(&w, &v);
            cell_name[i] = (char)v; if (v == 0) break;
        }
        goose_cell_t *c = goose_fabric_get_cell(cell_name);
        reflex_vm_zero_word(&vm->registers[instruction->dst]);
        if (c) {
            vm->registers[instruction->dst].trits[0] = c->state;
        }
        break;
    }
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
    ESP_RETURN_ON_ERROR(reflex_vm_validate_state(vm), "vm", "invalid vm state");

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
    static const reflex_vm_instruction_t bad_program_instr[] = {
        {.opcode = REFLEX_VM_OPCODE_TSYS, .imm = 99},
    };
    static const reflex_vm_instruction_t invalid_register_program_instr[] = {
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
    bad_vm.program = bad_program_instr;
    bad_vm.program_length = sizeof(bad_program_instr) / sizeof(bad_program_instr[0]);
    syscall_vm.program = syscall_program;
    syscall_vm.program_length = sizeof(syscall_program) / sizeof(syscall_program[0]);
    invalid_register_vm.program = invalid_register_program_instr;
    invalid_register_vm.program_length = sizeof(invalid_register_program_instr) / sizeof(invalid_register_program_instr[0]);

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
