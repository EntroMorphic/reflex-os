#ifndef REFLEX_VM_STATE_H
#define REFLEX_VM_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "reflex_ternary.h"
#include "reflex_vm_opcode.h"
#include "reflex_vm_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFLEX_VM_STATUS_READY = 0,
    REFLEX_VM_STATUS_RUNNING,
    REFLEX_VM_STATUS_HALTED,
    REFLEX_VM_STATUS_FAULTED,
} reflex_vm_status_t;

typedef enum {
    REFLEX_VM_FAULT_NONE = 0,
    REFLEX_VM_FAULT_INVALID_OPCODE,
    REFLEX_VM_FAULT_INVALID_REGISTER,
    REFLEX_VM_FAULT_INVALID_IMMEDIATE,
    REFLEX_VM_FAULT_PC_OUT_OF_RANGE,
    REFLEX_VM_FAULT_ARITHMETIC_OVERFLOW,
    REFLEX_VM_FAULT_INVALID_SYSCALL,
    REFLEX_VM_FAULT_INVALID_MEMORY_ACCESS,
} reflex_vm_fault_t;

// Forward declarations
struct reflex_vm_state;
struct reflex_cache;

typedef esp_err_t (*reflex_vm_syscall_handler_t)(struct reflex_vm_state *vm,
                                                 reflex_vm_syscall_t syscall_id,
                                                 const reflex_word18_t *src_a,
                                                 const reflex_word18_t *src_b,
                                                 reflex_word18_t *out,
                                                 void *context);

typedef struct reflex_vm_state {
    reflex_vm_status_t status;
    reflex_vm_fault_t fault;
    uint32_t ip;
    uint32_t steps_executed;
    reflex_word18_t registers[REFLEX_VM_REGISTER_COUNT];
    const reflex_vm_instruction_t *program;
    size_t program_length;
    bool owns_program;
    
    // Memory Management
    reflex_vm_mmu_t mmu;
    reflex_word18_t *owned_private_memory;
    size_t owned_private_memory_count;

    uint8_t node_id;
    reflex_vm_syscall_handler_t syscall_handler;
    void *syscall_context;
    struct reflex_cache *cache;
} reflex_vm_state_t;

#ifdef __cplusplus
}
#endif

#endif
