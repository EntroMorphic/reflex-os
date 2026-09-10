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
#include "reflex_kernel.h"
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

/* And the task backend, for the same reason one layer up.
 *
 * CONFIG_REFLEX_KERNEL_SCHEDULER puts the scheduler in the build;
 * CONFIG_REFLEX_TASK_BACKEND_REFLEX is what makes reflex_task_create file a TCB
 * into it instead of calling xTaskCreate. Without the second, everything below
 * runs on the Reflex scheduler while app_main creates its tasks on a FreeRTOS
 * that was never started — which is not a link error and not a crash, it is a
 * boot that stops partway with no explanation. That cost a debugging session
 * before the configuration was even suspected.
 *
 * Refused here rather than documented, because the two options are independent
 * in Kconfig and nothing else pairs them. */
#if defined(REFLEX_OWN_ENTRY) && !CONFIG_REFLEX_TASK_BACKEND_REFLEX
#error                                                                                             \
    "REFLEX_OWN_ENTRY needs CONFIG_REFLEX_TASK_BACKEND_REFLEX: without it app_main creates tasks on a FreeRTOS that was never started"
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

    /* Declared up here, not at the point of use, so that every goto below lands
     * on defined values. The recovery path reads all three. */
    uint32_t saved_mask = 0;
    uint32_t prior_mtvec = 0;
    uint32_t prior_mscratch = 0;

    reflex_err_t rc = reflex_sched_init();
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "scheduler init failed rc=0x%x", rc);
        goto fallback;
    }

    /* --- the hand-off ---------------------------------------------------
     *
     * Order matters here, and it changed. It used to be: start the tick, prove
     * it under ESP-IDF's trap vector, then take mtvec and prove it again. The
     * first of those two checks was the only reason reflex_hal_intr_alloc had
     * to register Reflex's dispatcher in ESP-IDF's interrupt table — the last
     * structural use of ESP-IDF's scheduler-side machinery on this path.
     *
     * It is now: quiesce, take the vector, then start the tick and prove it
     * once, under the vector that will actually service it. That removes the
     * ESP-IDF table from the picture entirely and makes the check stricter
     * rather than weaker. The old first check ran with ESP-IDF's dispatcher
     * doing the delivering, so it proved the interrupt-matrix routing and the
     * PLIC programming — never Reflex's vector table, its trap entry, or its
     * acknowledgement, which are the parts that had actually been broken.
     *
     * The safety property is unchanged, and it is the one that matters: a board
     * whose tick is dead must not be handed to a scheduler that needs it. That
     * is still true, because taking mtvec is reversible. reflex_trap_snapshot
     * writes down the vector being replaced before it is replaced, so every
     * failure below reaches `handback` and gives the machine back to FreeRTOS
     * rather than stalling on it. Nothing here needs a working tick to unwind:
     * reflex_hal_delay_us is a ROM busy-wait and the log path is a direct FIFO
     * write. */

    /* Take back the stack watchpoint, which ESP-IDF armed with the bounds of
     * the FreeRTOS stack this is running on. The first switch to a Reflex
     * stack panics without this. */
    reflex_hal_stack_guard_disable();

    /* Silence everything ESP-IDF left enabled, before Reflex's handler can be
     * asked about any of it.
     *
     * Nothing is kept: the tick has not been allocated yet at this point in the
     * new ordering, so there is no line worth preserving. Reflex's own
     * allocator re-enables each line it takes, one at a time, with a handler
     * already installed for it. */
    saved_mask = reflex_hal_intr_quiesce_except(0u);
    REFLEX_LOGI(TAG, "quiesced PLIC 0x%08x -> 0x00000000", (unsigned)saved_mask);

    /* Stand down the timer-group watchdogs FreeRTOS was feeding. Without this
     * the chip resets a few seconds in with TG1_WDT_HPSYS, after the scheduler
     * has started perfectly well. */
    reflex_hal_wdt_disable_timg();

    /* Write down what is being replaced, so that taking the vector is a step
     * that can be walked back. Everything after this is recoverable only
     * because of these two words. */
    reflex_trap_snapshot(&prior_mtvec, &prior_mscratch);
    reflex_trap_install();

    /* Now start the tick. Its line is allocated, routed and enabled with
     * Reflex's vector already on mtvec, so it is dispatched through Reflex's
     * own table from the very first interrupt it delivers. */
    rc = reflex_sched_tick_start();
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "tick start failed rc=0x%x", rc);
        goto handback;
    }

    reflex_intr_route_t tick;
    reflex_hal_intr_describe(REFLEX_INTR_SRC_SYSTIMER_TARGET1, &tick);

    /* Tell the handler which line is the tick.
     *
     * Measured, not reasoned: setting this to a line the tick never arrives on
     * still ticks, because the handler falls through to the HAL's own dispatch
     * table where the tick's ISR is registered like any other. Kept as a fast
     * path that skips the table lookup for the one interrupt that runs at
     * 1 kHz, and described as what it is rather than what it used to be. */
    reflex_trap_set_tick_line((int)tick.cpu_int);

    /* Prove the tick, once, under the vector that services it.
     *
     * Everything after this point — every delay, every timed queue wait —
     * depends on s_tick_count advancing, and a scheduler whose clock has
     * stopped does not crash: it parks on wfi and goes quiet, which is
     * indistinguishable from a hang. This check exists to tell those apart
     * before there is a shell to ask from.
     *
     * On failure, read the registers here or not at all. The hand-back changes
     * every register that would have answered the question. */
    uint32_t t1 = reflex_sched_get_tick();
    reflex_hal_delay_us(50000);
    uint32_t t2 = reflex_sched_get_tick();
    if (t2 == t1) {
        /* Re-read the routing rather than reporting the copy taken before the
         * wait. The fields that decide this question are the ones that change
         * during it — mip_pending is a sample of what is asserting *now*, and
         * plic_enabled and live_line_mask can be cleared by the trap handler
         * masking an unclaimed line while the 50ms elapses. Printing the
         * pre-wait copy would describe the machine as it was before the failure
         * it is being asked to explain. */
        reflex_hal_intr_describe(REFLEX_INTR_SRC_SYSTIMER_TARGET1, &tick);
        uint32_t ena, raw, st;
        reflex_sched_tick_debug(&ena, &raw, &st);
        /* Both halves, because "routed but not firing" and "firing but not
         * routed" look identical from either side alone. */
        REFLEX_LOGE(TAG, "tick source: systimer int_ena=0x%08x int_raw=0x%08x int_st=0x%08x",
                    (unsigned)ena, (unsigned)raw, (unsigned)st);
        REFLEX_LOGE(TAG,
                    "tick is not running under Reflex's vector: cpu_int=%u plic_en=%u pri=%u "
                    "thresh=%u level=%u mie=%u mip=%u global_ie=%u live=0x%08x tick_line=%d "
                    "unclaimed=0x%08x",
                    (unsigned)tick.cpu_int, (unsigned)tick.plic_enabled,
                    (unsigned)tick.plic_priority, (unsigned)tick.plic_threshold,
                    (unsigned)tick.level_triggered, (unsigned)tick.mie_enabled,
                    (unsigned)tick.mip_pending, (unsigned)tick.global_ie,
                    (unsigned)tick.live_line_mask, reflex_trap_get_tick_line(),
                    (unsigned)reflex_hal_intr_unclaimed_lines());
        goto handback;
    }
    REFLEX_LOGI(TAG, "tick runs under Reflex's vector: %u ticks in 50ms", (unsigned)(t2 - t1));

    /* Start the kernel policy supervisor here, because the thing that normally
     * starts it never runs on this path.
     *
     * It is created by __wrap_xPortStartScheduler, and under REFLEX_OWN_ENTRY
     * FreeRTOS is never started, so that wrap is never called. Nothing failed
     * visibly: reflex_kernel_set_policy has a weak no-op fallback in
     * goose_supervisor.c, so the policy engine simply stopped modulating task
     * priorities on the one configuration this work exists for, silently. */
    reflex_kernel_start_supervisor();

    rc = reflex_sched_create_task(reflex_main_task, "main", 8192, NULL, 10, NULL);
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "main task creation failed rc=0x%x", rc);
        goto handback;
    }

    rc = reflex_sched_start();
    REFLEX_LOGE(TAG, "scheduler returned rc=0x%x, which it must not", rc);
    goto stall;

fallback:
    /* The only failure that can still reach here is scheduler init, which
     * happens before anything has been taken from ESP-IDF.
     *
     * That is a consequence of the reordering above: the tick now starts after
     * mtvec is taken, so every failure from that point on is a hand-back, not a
     * fallback. This block used to call reflex_sched_tick_stop() first, because
     * the tick could be running by the time it was reached; it cannot be now,
     * and calling it would disarm a comparator that was never armed while
     * implying otherwise to anyone reading the path.
     *
     * ESP-IDF's world is entirely intact here — its vector, its interrupt mask,
     * its watchdogs — so this is a genuine hand-back rather than a wish. */
    REFLEX_LOGW(TAG, "falling back to the FreeRTOS entry path");
    __real_esp_startup_start_app();
    return;

handback:
    /* Undo the hand-off and give the machine back, rather than stalling on it.
     *
     * Reached from the failures that happen *after* the vector is taken, which
     * used to stall — a wfi loop that needs a reflash to leave. That is the
     * same shape as the sleep watchdog earlier in this work: a safety mechanism
     * whose own failure mode was the thing it existed to prevent. Every step of
     * the hand-off is reversible, so there is no reason for it.
     *
     * Reverse order of the hand-off. The vector goes back first, because until
     * it does, an interrupt arriving during the rest of this is dispatched by
     * Reflex's handler against a machine being dismantled. Then the interrupt
     * mask, then the tick.
     *
     * Two things are deliberately not restored. The stack watchpoint is
     * re-armed by FreeRTOS on its next context switch, which is what made it
     * necessary to disable it here in the first place. The timer-group
     * watchdogs stay disabled: an un-fed watchdog that resets the board is
     * worse than no watchdog on a boot that is already reporting a fault, and
     * FreeRTOS will re-enable them if it is configured to. */
    reflex_trap_restore(prior_mtvec, prior_mscratch);
    reflex_hal_intr_restore(saved_mask);
    reflex_sched_tick_stop();
    REFLEX_LOGW(TAG, "handing the machine back to FreeRTOS after the vector was taken");
    __real_esp_startup_start_app();
    return;

stall:
    /* Only for a scheduler that returned, and that one really cannot hand back.
     *
     * By this point tasks have run on their own stacks, so `sp` and the saved
     * contexts are no longer anything ESP-IDF's startup would recognise;
     * calling into it from here would be a guess. Everything reachable before
     * the scheduler starts goes to `handback` instead.
     *
     * Stalling rather than returning, because esp_startup_start_app has no
     * caller to return to. */
    for (;;) {
        __asm__ volatile("wfi");
    }
#endif /* REFLEX_OWN_ENTRY */
}

#endif /* CONFIG_IDF_TARGET_ESP32C6 */
