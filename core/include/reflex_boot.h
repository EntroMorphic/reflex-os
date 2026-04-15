#ifndef REFLEX_BOOT_H
#define REFLEX_BOOT_H

/**
 * @file reflex_boot.h
 * @brief Host-side boot banner and startup helpers.
 *
 * main/main.c calls #reflex_boot_print_banner as the very first
 * line of app_main so the chip/version/target/reset_reason block
 * appears in the serial log before any service initialization.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Emit the multi-line reflex.boot banner to the host log
 * (project, version, chip revision, flash size, reset reason,
 * console driver). Safe to call multiple times; each call writes a
 * fresh block.
 */
void reflex_boot_print_banner(void);

#ifdef __cplusplus
}
#endif

#endif
