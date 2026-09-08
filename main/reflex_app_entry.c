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
#include "reflex_soc_esp32c6.h" /* REFLEX_INTR_SRC_SYSTIMER_TARGET1 */

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
/* Asking for it on a target that cannot do it is an error, not a no-op.
 *
 * --wrap is applied only on the C6, and this file compiles to nothing
 * elsewhere, so REFLEX_OWN_ENTRY on any other target would silently change
 * nothing at all — a build that looks like it took the entry point and did not.
 * Saying so is cheaper than discovering it from a map file. */
#if defined(REFLEX_OWN_ENTRY) && !CONFIG_IDF_TARGET_ESP32C6
#error                                                                                             \
    "REFLEX_OWN_ENTRY is ESP32-C6 only: --wrap is not applied on this target, so the flag would do nothing"
#endif

#if CONFIG_IDF_TARGET_ESP32C6

/* Refuse the combination at configure time rather than at link time.
 *
 * reflex_trap_install and the reflex_sched_* family are added to the build only
 * by CONFIG_REFLEX_KERNEL_SCHEDULER. Without it, taking the entry point fails
 * as four undefined references roughly two hundred objects into the link,
 * naming symbols rather than the choice that caused them — which is the same
 * failure the Kconfig help already describes for the ESP32 target and the same
 * reason it refuses that one early. */
#if defined(REFLEX_OWN_ENTRY) && !CONFIG_REFLEX_KERNEL_SCHEDULER
#error                                                                                             \
    "REFLEX_OWN_ENTRY needs CONFIG_REFLEX_KERNEL_SCHEDULER: the Reflex scheduler it hands the machine to is not in this build"
#endif

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

    reflex_err_t rc = reflex_sched_init();
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "scheduler init failed rc=0x%x", rc);
        goto fallback;
    }

    /* Prove the tick before committing the machine to a scheduler that needs
     * it, and hand back to FreeRTOS if it is dead.
     *
     * The tick is 5/5 at 1000 Hz on the independence build and 0/5 on the
     * default one, where the routing reads back perfect and nothing arrives.
     * Taking the entry point there would hand the board to a scheduler that can
     * never unblock a task — and unlike `kernel selftest`, there is no shell to
     * abort back to, because the shell has not been started yet. Falling back
     * costs a boot that is not independent and keeps a board that works. */
    rc = reflex_sched_tick_start();
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "tick start failed rc=0x%x", rc);
        goto fallback;
    }
    uint32_t t0 = reflex_sched_get_tick();
    reflex_hal_delay_us(50000);
    if (reflex_sched_get_tick() == t0) {
        /* Say why, not just that.
         *
         * "tick is not running" is a verdict with no evidence attached, and
         * this is the one place where the evidence cannot be gathered
         * afterwards: there is no shell here to ask from, and the fallback
         * hands the machine back to FreeRTOS, which changes every register that
         * would have answered the question. Read them here or not at all. */
        reflex_intr_route_t r;
        reflex_hal_intr_describe(REFLEX_INTR_SRC_SYSTIMER_TARGET1, &r);
        uint32_t ena, raw, st;
        reflex_sched_tick_debug(&ena, &raw, &st);
        /* Both halves, because "routed but not firing" and "firing but not
         * routed" look identical from either side alone. */
        REFLEX_LOGE(TAG, "tick source: systimer int_ena=0x%08x int_raw=0x%08x int_st=0x%08x",
                    (unsigned)ena, (unsigned)raw, (unsigned)st);
        REFLEX_LOGE(TAG,
                    "tick is not running: cpu_int=%u plic_en=%u pri=%u thresh=%u "
                    "level=%u mie=%u mip=%u global_ie=%u live=0x%08x",
                    (unsigned)r.cpu_int, (unsigned)r.plic_enabled, (unsigned)r.plic_priority,
                    (unsigned)r.plic_threshold, (unsigned)r.level_triggered,
                    (unsigned)r.mie_enabled, (unsigned)r.mip_pending, (unsigned)r.global_ie,
                    (unsigned)r.live_line_mask);
        goto fallback;
    }

    /* --- the hand-off ---------------------------------------------------
     *
     * Four steps that reflex_kernel_test performs and this did not. Each one is
     * load-bearing, and the dead tick above was the only reason their absence
     * had never cost anything: the fallback was taken every time, so the code
     * below had never run on hardware.
     *
     * Nothing here needs undoing on the fallback path, because all of it
     * happens after the tick check has already passed. That ordering is
     * deliberate — it keeps the hand-back a hand-back. */
    reflex_intr_route_t tick;
    reflex_hal_intr_describe(REFLEX_INTR_SRC_SYSTIMER_TARGET1, &tick);

    /* Tell the handler which line is the tick. Without this it recognises
     * nothing, acknowledges nothing, and the core stops on the first
     * interrupt it takes — the single most consequential omission here. */
    reflex_trap_set_tick_line((int)tick.cpu_int);

    /* Take back the stack watchpoint, which ESP-IDF armed with the bounds of
     * the FreeRTOS stack this is running on. The first switch to a Reflex
     * stack panics without this. */
    reflex_hal_stack_guard_disable();

    /* Silence every line but the tick. Reflex's handler deliberately does not
     * acknowledge a line it does not recognise, so any ESP-IDF interrupt left
     * enabled asserts, is never cleared, and the machine stops. */
    uint32_t saved_mask = reflex_hal_intr_quiesce_except(1u << tick.cpu_int);
    REFLEX_LOGI(TAG, "quiesced PLIC 0x%08x -> 0x%08x, tick on cpu_int=%u", (unsigned)saved_mask,
                (unsigned)(1u << tick.cpu_int), (unsigned)tick.cpu_int);

    /* Stand down the timer-group watchdogs FreeRTOS was feeding. Without this
     * the chip resets a few seconds in with TG1_WDT_HPSYS, after the scheduler
     * has started perfectly well. */
    reflex_hal_wdt_disable_timg();

    reflex_trap_install();

    rc = reflex_sched_create_task(reflex_main_task, "main", 8192, NULL, 10, NULL);
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "main task creation failed rc=0x%x", rc);
        goto stall;
    }

    rc = reflex_sched_start();
    REFLEX_LOGE(TAG, "scheduler returned rc=0x%x, which it must not", rc);
    goto stall;

fallback:
    /* Give the tick back before handing back.
     *
     * mtvec has not been taken at this point — the install is deliberately
     * after the tick check — so ESP-IDF's world is still intact and this is a
     * genuine hand-back rather than a wish. But reflex_sched_tick_start has
     * already run in the case that matters: routed SYSTIMER TARGET1 to a CPU
     * line, installed Reflex's handler, armed the comparator. Handing back
     * without undoing that leaves Reflex's ISR firing a thousand times a second
     * into a counter nobody reads, on an interrupt line ESP-IDF's allocator can
     * no longer hand out — under a FreeRTOS that has no idea any of it happened.
     *
     * Safe to call unconditionally: it disarms the comparator and clears the
     * peripheral enable whether or not they were ever set, and frees the
     * handle only if there is one. */
    reflex_sched_tick_stop();
    REFLEX_LOGW(TAG, "falling back to the FreeRTOS entry path");
    __real_esp_startup_start_app();
    return;

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
