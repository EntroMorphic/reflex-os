#ifndef REFLEX_VM_STATE_H
#define REFLEX_VM_STATE_H

/**
 * @file reflex_vm_state.h
 * @brief The Reflex VM instance struct plus its fault and status
 * taxonomies.
 *
 * See docs/vm/state.md for the full narrative. This header only
 * documents what each field means in the struct layout.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "reflex_ternary.h"
#include "reflex_vm_opcode.h"
#include "reflex_vm_mem.h"
#include "goose.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum runtime-established GOOSE routes per VM instance. */
#define REFLEX_VM_MAX_ROUTES 4

/** @brief Lifecycle states of a VM instance. */
typedef enum {
    REFLEX_VM_STATUS_READY = 0,   ///< Initialized, ready to step.
    REFLEX_VM_STATUS_RUNNING,     ///< Currently executing.
    REFLEX_VM_STATUS_HALTED,      ///< Stopped cleanly by THALT.
    REFLEX_VM_STATUS_FAULTED,     ///< Stopped by an execution error.
} reflex_vm_status_t;

/** @brief Coarse fault classifications recorded when status == FAULTED. */
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

/**
 * @brief Signature for a host-side syscall handler installed on a
 * VM instance. Invoked from the interpreter on TSYS.
 *
 * @param vm           VM instance issuing the call
 * @param syscall_id   selector from the TSYS immediate
 * @param src_a,src_b  input word18 values
 * @param out          output word18 value (handler writes)
 * @param context      opaque context passed at install time
 */
typedef esp_err_t (*reflex_vm_syscall_handler_t)(struct reflex_vm_state *vm,
                                                 reflex_vm_syscall_t syscall_id,
                                                 const reflex_word18_t *src_a,
                                                 const reflex_word18_t *src_b,
                                                 reflex_word18_t *out,
                                                 void *context);

/**
 * @brief A complete VM instance. Owned by the caller; zero-init is
 * sufficient for first use, followed by a loader call.
 */
typedef struct reflex_vm_state {
    reflex_vm_status_t status;
    reflex_vm_fault_t fault;
    uint32_t ip;                                    ///< Instruction pointer (host index into program[]).
    uint32_t steps_executed;                        ///< Monotonic step counter for diagnostics.
    reflex_word18_t registers[REFLEX_VM_REGISTER_COUNT]; ///< r0..r7.
    const reflex_vm_instruction_t *program;         ///< Loaded instruction array.
    size_t program_length;                          ///< Count of valid instructions.
    bool owns_program;                              ///< If true, unload path frees program.

    // Memory Management
    reflex_vm_mmu_t mmu;                            ///< Region-protected memory descriptor.
    reflex_word18_t *owned_private_memory;          ///< Private data segment from packed loader.
    size_t owned_private_memory_count;

    uint8_t node_id;                                ///< Fabric node identity (stamped on TSEND).
    reflex_vm_syscall_handler_t syscall_handler;    ///< Host bridge for TSYS.
    void *syscall_context;
    struct reflex_cache *cache;                     ///< Optional soft cache (NULL = direct MMU access).

    // GOOSE Manifest (Phase 10): runtime-established routes via TROUTE.
    goose_route_t route_manifest[REFLEX_VM_MAX_ROUTES];
    size_t route_count;
} reflex_vm_state_t;

#ifdef __cplusplus
}
#endif

#endif
