/* Minimal stubs for VM functions needed by loader but defined in interpreter.c */
#include "reflex_types.h"
#include "reflex_ternary.h"

/* From vm/include/reflex_vm_state.h — but we can't include the full
 * header in standalone mode because it needs goose.h. So we forward-
 * declare the struct and provide stub implementations. */

typedef struct reflex_vm_state reflex_vm_state_t;

void reflex_vm_reset(reflex_vm_state_t *vm) { (void)vm; }
void reflex_vm_unload(reflex_vm_state_t *vm) { (void)vm; }
