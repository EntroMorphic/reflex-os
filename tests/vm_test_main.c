/**
 * @file vm_test_main.c
 * @brief Host-side test runner for Reflex ternary primitives and loader.
 *
 * Compiles and runs on macOS/Linux without ESP-IDF or hardware.
 * The interpreter and cache self-checks need GOOSE — tested on-device.
 */

#include <stdio.h>
#include "reflex_types.h"
#include "reflex_ternary.h"

extern reflex_err_t reflex_ternary_self_check(void);
extern reflex_err_t reflex_vm_loader_self_check(void);

int main(void) {
    int failures = 0;

    printf("Reflex OS Host Test\n");
    printf("====================\n\n");

    printf("ternary self-check... ");
    if (reflex_ternary_self_check() == REFLEX_OK) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        failures++;
    }

    printf("loader self-check...  ");
    if (reflex_vm_loader_self_check() == REFLEX_OK) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        failures++;
    }

    printf("\nResults: %d failures\n", failures);
    return failures;
}
