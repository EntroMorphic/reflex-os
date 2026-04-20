/** @file programs.c
 * @brief VM built-in program registry.
 */
#include "programs.h"
#include <stdbool.h>
#include <string.h>

extern const uint8_t vm_program_blink[];
extern const size_t vm_program_blink_len;
extern const uint8_t vm_program_count_prog[];
extern const size_t vm_program_count_prog_len;
extern const uint8_t vm_program_respond[];
extern const size_t vm_program_respond_len;

static bool s_init = false;
static vm_program_t s_programs[3];

static void ensure_init(void) {
    if (s_init) return;
    s_programs[0] = (vm_program_t){"blink",   vm_program_blink,      vm_program_blink_len};
    s_programs[1] = (vm_program_t){"count",   vm_program_count_prog, vm_program_count_prog_len};
    s_programs[2] = (vm_program_t){"respond", vm_program_respond,    vm_program_respond_len};
    s_init = true;
}

const vm_program_t vm_programs[1] = {{NULL, NULL, 0}};
const size_t vm_program_count = 3;

const vm_program_t *vm_program_find(const char *name) {
    ensure_init();
    for (size_t i = 0; i < vm_program_count; i++) {
        if (strcmp(s_programs[i].name, name) == 0)
            return &s_programs[i];
    }
    return NULL;
}

const vm_program_t *vm_program_get(size_t idx) {
    ensure_init();
    return (idx < vm_program_count) ? &s_programs[idx] : NULL;
}
