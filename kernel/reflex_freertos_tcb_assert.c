/**
 * @file reflex_freertos_tcb_assert.c
 * @brief Compile-time check that reflex_portasm.S's TCB offsets still match
 *        FreeRTOS's layout.
 *
 * Its own translation unit for one reason: it is the only place left that needs
 * a FreeRTOS header, and a `#ifndef` around the include is not enough to remove
 * the dependency. tools/check_independence.py reads include lines out of the
 * source of every file the configuration compiles — it does not run the
 * preprocessor, and it should not, because a guarded include is still a file
 * the build must be able to find. Splitting is what actually removes it.
 *
 * Compiled only where FreeRTOS is the task backend. reflex_portasm.S wraps
 * rtos_int_enter and rtos_int_exit to re-arm the stack watchpoint across a
 * FreeRTOS context switch; where Reflex owns scheduling those are never called,
 * the offsets describe a structure nothing indexes, and asserting on them
 * measures nothing.
 *
 * This was the last Tier C dependency of the configuration that actually
 * achieves independence — one include, in a file whose entry point never ran
 * there.
 */

#include "freertos/portmacro.h"

_Static_assert(PORT_OFFSET_PX_STACK == 0x30,
               "TCB pxStack offset changed — update TCB_PX_STACK in reflex_portasm.S");
_Static_assert(PORT_OFFSET_PX_END_OF_STACK == 0x44,
               "TCB pxEndOfStack offset changed — update TCB_PX_END_OF_STACK in reflex_portasm.S");
