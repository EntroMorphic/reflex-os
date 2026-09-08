/**
 * @file reflex_startup.c
 * @brief Reflex kernel entry: install the trap vector, start the scheduler.
 *
 * Compiled, never called. It is the hand-off point for C2 of the independence
 * work — the thing that would make reflex_sched_start actually run — and it is
 * listed in the build so it cannot rot before then, not because anything
 * reaches it.
 *
 * ## Calling this from an ESP-IDF build takes over the machine
 *
 * The first thing it does is write `mtvec`, which is the RISC-V trap vector for
 * *every* interrupt and exception on the core. ESP-IDF owns that register: its
 * interrupt allocator, the FreeRTOS tick, the USB-JTAG console and the radio
 * all arrive through the vector it installed. Overwriting it mid-boot does not
 * degrade anything gracefully — it silently redirects every interrupt in the
 * system to `reflex_trap_entry`, which handles the scheduler tick and nothing
 * else.
 *
 * So this is not a function that can be called from `app_main` to "try the
 * scheduler". Reaching it requires either owning startup outright, or a
 * deliberate hand-off that first quiesces the peripherals whose interrupts
 * would otherwise vanish. That is the substance of C2 and it needs hardware to
 * validate: a wrong trap vector does not fail to build, it produces a board
 * that stops responding with nothing left running to say why.
 *
 * ## Sequence
 *
 * 1. Install `reflex_trap_entry` as the trap vector.
 * 2. Initialise the scheduler, which creates the idle task.
 * 3. Create the caller's main task.
 * 4. Start the scheduler.
 *
 * BSS is deliberately not cleared here, though an earlier version of this
 * comment listed it as step 1. Whoever calls this has already done it — the
 * ESP-IDF startup path clears BSS long before `app_main`, and a standalone
 * entry would clear it in the assembly that sets up the stack, before any C
 * runs. Doing it here would zero live state rather than uninitialised state.
 */

#include "reflex_sched.h"
#include "reflex_hal.h"
#include <stdint.h>

#define TAG "reflex.kernel"

/* Assembly entry point from reflex_vectors.S */
extern void reflex_trap_entry(void);

/**
 * @brief Take over the core and run @p main_task under the Reflex scheduler.
 *
 * Does not return on success. A return value therefore always means failure,
 * and the caller is left holding a machine whose trap vector has already been
 * replaced — see the file comment before calling this from anywhere.
 *
 * @return REFLEX_ERR_NO_MEM if the scheduler or the main task could not be
 *         created, REFLEX_FAIL if the scheduler loop returned, which it should
 *         never do.
 */
reflex_err_t reflex_kernel_startup(void (*main_task)(void *), void *arg) {
    if (!main_task) return REFLEX_ERR_INVALID_ARG;

    /* Install through reflex_trap_install, which knows what this chip does.
     *
     * This wrote mtvec directly with the handler's address, on the assumption
     * of direct mode and 4-byte alignment. Both are wrong here: the base is
     * masked to 256 bytes and the mode field comes back as vectored, so the
     * vector landed short of the handler and interrupts dispatched into
     * whatever preceded it. Nothing had ever called this function, so the bug
     * sat here until the same mistake was made in kernel_test and found on
     * hardware. */
    reflex_trap_install();

    REFLEX_LOGI(TAG, "trap vector installed");

    /* Every one of these returned a status that used to be discarded. A failed
     * init or a failed task creation left the scheduler starting with nothing
     * to run but the idle task, which looks exactly like a hang. */
    reflex_err_t rc = reflex_sched_init();
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "scheduler init failed rc=0x%x", rc);
        return rc;
    }

    rc = reflex_sched_create_task(main_task, "main", 8192, arg, 10, NULL);
    if (rc != REFLEX_OK) {
        REFLEX_LOGE(TAG, "main task creation failed rc=0x%x", rc);
        return rc;
    }

    REFLEX_LOGI(TAG, "scheduler starting");

    /* Does not return while the scheduler is healthy. */
    rc = reflex_sched_start();
    REFLEX_LOGE(TAG, "scheduler returned rc=0x%x — no task left to run", rc);
    return (rc == REFLEX_OK) ? REFLEX_FAIL : rc;
}
