#ifndef REFLEX_VM_OPCODE_H
#define REFLEX_VM_OPCODE_H

/**
 * @file reflex_vm_opcode.h
 * @brief Opcode, syscall, and instruction-encoding definitions for
 * the Reflex ternary VM.
 *
 * The VM is register-based with 8 registers (r0..r7). Every
 * instruction is a reflex_vm_instruction_t containing an opcode and
 * up to three register indices plus a signed 32-bit immediate.
 * Packing to wire format is handled by the loader
 * (see reflex_vm_loader.h and docs/vm/loader-v2.md).
 *
 * Full assembler syntax lives in docs/tasm-spec.md; the semantics
 * of each opcode are summarized on its enum value below.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Number of general-purpose VM registers (r0..r7). */
#define REFLEX_VM_REGISTER_COUNT 8

/** @brief Trit slot index that TCMP writes and branch ops read. */
#define REFLEX_VM_COMPARE_TRIT_INDEX 0

/** @brief VM opcodes. Binary IDs are stable across the packed-image format. */
typedef enum {
    REFLEX_VM_OPCODE_TNOP = 0,   ///< No operation.
    REFLEX_VM_OPCODE_TLDI,       ///< Load immediate into DST (IMM -> word18).
    REFLEX_VM_OPCODE_TMOV,       ///< Copy register SRC_A into DST.
    REFLEX_VM_OPCODE_TLD,        ///< Load from private memory at DST := mem[SRC_A].
    REFLEX_VM_OPCODE_TST,        ///< Store to private memory at mem[SRC_A] := SRC_B.
    REFLEX_VM_OPCODE_TADD,       ///< DST := SRC_A + SRC_B (balanced ternary).
    REFLEX_VM_OPCODE_TSUB,       ///< DST := SRC_A - SRC_B (balanced ternary).
    REFLEX_VM_OPCODE_TCMP,       ///< Compare SRC_A, SRC_B; sign into DST trit slot 0.
    REFLEX_VM_OPCODE_TSEL,       ///< Three-way select on SRC_A's sign.
    REFLEX_VM_OPCODE_TJMP,       ///< Unconditional branch to IMM target.
    REFLEX_VM_OPCODE_TBRNEG,     ///< Branch if SRC_A's trit[0] < 0.
    REFLEX_VM_OPCODE_TBRZERO,    ///< Branch if SRC_A's trit[0] == 0.
    REFLEX_VM_OPCODE_TBRPOS,     ///< Branch if SRC_A's trit[0] > 0.
    REFLEX_VM_OPCODE_TSEND,      ///< Send a fabric message to node DST_REG.
    REFLEX_VM_OPCODE_TRECV,      ///< Receive a fabric message into DST.
    REFLEX_VM_OPCODE_TFLUSH,     ///< Flush the soft cache line for SRC_A.
    REFLEX_VM_OPCODE_TINV,       ///< Invalidate the soft cache line for SRC_A.
    REFLEX_VM_OPCODE_TSYS,       ///< Host syscall (selector in IMM).
    REFLEX_VM_OPCODE_THALT,      ///< Halt the VM cleanly (status -> HALTED).

    /* GOOSE-Native Geometric Opcodes (Phase 10/11) */
    REFLEX_VM_OPCODE_TROUTE,     ///< Establish GOOSE route. IMM=0: names; IMM=1: coordinates.
    REFLEX_VM_OPCODE_TBIAS,      ///< Update route orientation. DST=route index, SRC_A=bias trit.
    REFLEX_VM_OPCODE_TSENSE,     ///< Sample a cell. DST=trit, SRC_A=name/coord, IMM=0 name, 1 coord.
} reflex_vm_opcode_t;

/** @brief Host syscall selectors reachable via TSYS. */
typedef enum {
    REFLEX_VM_SYSCALL_LOG = 0,        ///< Log SRC_A as a signed integer to the host console.
    REFLEX_VM_SYSCALL_UPTIME,         ///< Return uptime in milliseconds.
    REFLEX_VM_SYSCALL_CONFIG_GET,     ///< Return a scalar config value (SRC_A is the key).
    REFLEX_VM_SYSCALL_DELAY,          ///< vTaskDelay(SRC_A ms). Yields; does not busy-wait.
} reflex_vm_syscall_t;

/** @brief Config keys reachable via REFLEX_VM_SYSCALL_CONFIG_GET. */
typedef enum {
    REFLEX_VM_CONFIG_DEVICE_NAME_LENGTH = 0, ///< Length of persisted device name.
    REFLEX_VM_CONFIG_LOG_LEVEL,              ///< Persisted log level token.
} reflex_vm_config_key_t;

/**
 * @brief A single decoded VM instruction.
 *
 * Instruction field intent for the first encoding revision:
 * - TLDI uses `imm` as a small signed literal converted into word18.
 * - TCMP writes its ternary result into trit slot 0 of `dst` and
 *   clears the rest.
 * - TBRNEG/TBRZERO/TBRPOS inspect trit slot 0 of `src_a`.
 */
typedef struct {
    reflex_vm_opcode_t opcode;
    uint8_t dst;
    uint8_t src_a;
    uint8_t src_b;
    int32_t imm;
} reflex_vm_instruction_t;

#ifdef __cplusplus
}
#endif

#endif
