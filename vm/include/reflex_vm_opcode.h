#ifndef REFLEX_VM_OPCODE_H
#define REFLEX_VM_OPCODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REFLEX_VM_REGISTER_COUNT 8
#define REFLEX_VM_COMPARE_TRIT_INDEX 0

typedef enum {
    REFLEX_VM_OPCODE_TNOP = 0,
    REFLEX_VM_OPCODE_TLDI,
    REFLEX_VM_OPCODE_TMOV,
    REFLEX_VM_OPCODE_TLD,
    REFLEX_VM_OPCODE_TST,
    REFLEX_VM_OPCODE_TADD,
    REFLEX_VM_OPCODE_TSUB,
    REFLEX_VM_OPCODE_TCMP,
    REFLEX_VM_OPCODE_TSEL,
    REFLEX_VM_OPCODE_TJMP,
    REFLEX_VM_OPCODE_TBRNEG,
    REFLEX_VM_OPCODE_TBRZERO,
    REFLEX_VM_OPCODE_TBRPOS,
    REFLEX_VM_OPCODE_TSEND,
    REFLEX_VM_OPCODE_TRECV,
    REFLEX_VM_OPCODE_TFLUSH,
    REFLEX_VM_OPCODE_TINV,
    REFLEX_VM_OPCODE_TSYS,
    REFLEX_VM_OPCODE_THALT,
    
    /* GOOSE-Native Geometric Opcodes (Phase 10/11) */
    REFLEX_VM_OPCODE_TROUTE, ///< Establish route. IMM=0: Names, IMM=1: Coordinates.
    REFLEX_VM_OPCODE_TBIAS,  ///< Update orientation. dst=RouteIdx, src_a=BiasTrit.
    REFLEX_VM_OPCODE_TSENSE, ///< Sample cell. dst=Trit, src_a=Name/Coord, IMM=0:Name, 1:Coord.
} reflex_vm_opcode_t;

typedef enum {
    REFLEX_VM_SYSCALL_LOG = 0,
    REFLEX_VM_SYSCALL_UPTIME,
    REFLEX_VM_SYSCALL_CONFIG_GET,
    REFLEX_VM_SYSCALL_DELAY,
} reflex_vm_syscall_t;

typedef enum {
    REFLEX_VM_CONFIG_DEVICE_NAME_LENGTH = 0,
    REFLEX_VM_CONFIG_LOG_LEVEL,
} reflex_vm_config_key_t;

typedef struct {
    reflex_vm_opcode_t opcode;
    uint8_t dst;
    uint8_t src_a;
    uint8_t src_b;
    int32_t imm;
} reflex_vm_instruction_t;

/*
 * Instruction field intent for the first encoding revision:
 * - TLDI uses `imm` as a small signed literal converted into word18.
 * - TCMP writes its ternary result into trit slot 0 of `dst` and clears the rest.
 * - TBRNEG/TBRZERO/TBRPOS inspect trit slot 0 of `src_a`.
 */

#ifdef __cplusplus
}
#endif

#endif
