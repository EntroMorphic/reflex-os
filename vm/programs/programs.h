/** @file programs.h
 * @brief VM program descriptor type and table.
 */
#ifndef REFLEX_VM_PROGRAMS_H
#define REFLEX_VM_PROGRAMS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *name;
    const uint8_t *data;
    size_t len;
} vm_program_t;

extern const vm_program_t vm_programs[];
extern const size_t vm_program_count;

const vm_program_t *vm_program_find(const char *name);
const vm_program_t *vm_program_get(size_t idx);

#endif
