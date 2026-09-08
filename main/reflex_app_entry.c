/**
 * @file reflex_app_entry.c
 * @brief Reflex takes the application entry point from FreeRTOS.
 *
 * This is the load-bearing move for independence, and it is a link-time trick
 * rather than a code change.
 *
 * ESP-IDF's startup calls `esp_startup_start_app()`, which lives in
 * `libfreertos.a(app_startup.c.obj)` and creates the main task, starts the
 * FreeRTOS scheduler, and only then calls `app_main`. Every Reflex line runs
 * after that, which is why ten FreeRTOS objects are in the image no matter what
 * Tier C's include count says: the scheduler is not something Reflex uses, it
 * is the thing that started Reflex.
 *
 * An archive member is pulled in only to resolve an *undefined* symbol. Define
 * `esp_startup_start_app` here — in an object linked directly rather than from
 * an archive — and the linker resolves it from this file and never extracts
 * FreeRTOS's. That is the whole mechanism.
 *
 * Behind REFLEX_OWN_ENTRY, and off by default, because what it does after
 * taking the entry point is not yet survivable: ESP-IDF's console VFS,
 * esp_timer and newlib's reentrancy all expect a running FreeRTOS. The point of
 * having it now is that the *linking* question can be answered by building it
 * and reading the map, without risking a board — and that question is the one
 * that decides whether liberation is reachable at all.
 *
 *   idf.py -DCMAKE_C_FLAGS=-DREFLEX_OWN_ENTRY=1 build
 */

#include "reflex_sched.h"
#include "reflex_hal.h"

#define TAG "reflex.entry"

/* Compiled only where --wrap is applied.
 *
 * The wrapper references __real_esp_startup_start_app, which the linker creates
 * only when --wrap=esp_startup_start_app is on the link line — and that is
 * gated on the C6. On any other target that reference is undefined, and the
 * build survived purely because --gc-sections discarded the unreferenced
 * wrapper and took the dangling reference with it. That is luck, not design:
 * anything that referenced the wrapper, or a build without section GC, would
 * fail to link. The condition is stated here instead.
 *
 * No #include for sdkconfig.h: the build force-includes it into every source
 * file, and adding it explicitly grew Tier F from 1 to 2 — which the
 * independence ratchet refused, correctly. */
#if CONFIG_IDF_TARGET_ESP32C6

/* The application's own entry, still named app_main so nothing else moves. */
extern void app_main(void);

static void reflex_main_task(void *arg) {
    (void)arg;
    app_main();
}

/* --wrap, not a competing definition.
 *
 * Defining esp_startup_start_app outright collides: libfreertos's
 * app_startup.c.obj is pulled into the link anyway and the two definitions
 * fight ("multiple definition of esp_startup_start_app"). --wrap redirects the
 * *call* instead — esp_system's startup.c calls the symbol, the linker sends
 * that reference here, and FreeRTOS's definition is simply never called. */
void __wrap_esp_startup_start_app(void);

/* The wrapper is defined unconditionally and forwards by default.
 *
 * --wrap rewrites the reference whether or not Reflex wants it, so a wrapper
 * that only exists behind the flag breaks the ordinary build with an undefined
 * reference — which is exactly what happened. Forwarding to __real_ when the
 * flag is off makes the default path a pass-through and keeps the switch in one
 * place. */
extern void __real_esp_startup_start_app(void);

void __wrap_esp_startup_start_app(void) {
#ifndef REFLEX_OWN_ENTRY
    /* Not taking the entry point: hand straight back to FreeRTOS. */
    __real_esp_startup_start_app();
    return;
#else
    REFLEX_LOGI(TAG, "Reflex owns the entry point; FreeRTOS was not started");

    reflex_trap_install();

    reflex_err_t rc = reflex_sched_init();
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "scheduler init failed rc=0x%x", rc);
        goto stall;
    }
    rc = reflex_sched_create_task(reflex_main_task, "main", 8192, NULL, 10, NULL);
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "main task creation failed rc=0x%x", rc);
        goto stall;
    }

    rc = reflex_sched_start();
    REFLEX_LOGE(TAG, "scheduler returned rc=0x%x, which it must not", rc);

stall:
    /* Nothing above this point can hand back to ESP-IDF: its startup expects
     * this function never to return. Stalling is honest; returning would run
     * off the end of a call that has no caller. */
    for (;;) {
        __asm__ volatile("wfi");
    }
#endif /* REFLEX_OWN_ENTRY */
}

#endif /* CONFIG_IDF_TARGET_ESP32C6 */
