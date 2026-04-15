#ifndef REFLEX_SHELL_H
#define REFLEX_SHELL_H

/**
 * @file reflex_shell.h
 * @brief Serial shell entry point.
 *
 * #reflex_shell_run takes over the USB serial JTAG console after
 * the host's boot sequence, reads one line at a time, parses it
 * into argv, and dispatches to a static command table. The
 * command surface covers help/reboot/sleep, GOOSE (`goonies`,
 * `mesh`, `aura`), VM (`vm loadhex`, `vm info`), heartbeat, bonsai
 * experiments, and persistence (`config get/set`). See shell.c.
 *
 * This function blocks and is expected to be called from a
 * long-lived context (the main task after service bring-up).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Run the serial shell read-dispatch loop. Never returns. */
void reflex_shell_run(void);

#ifdef __cplusplus
}
#endif

#endif
