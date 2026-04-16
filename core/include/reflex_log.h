#ifndef REFLEX_LOG_H
#define REFLEX_LOG_H

/**
 * @file reflex_log.h
 * @brief Logging facade routed through reflex_hal_log.
 *
 * All logging goes through the HAL so the platform backend controls
 * the output destination. The REFLEX_LOGI/W/E macros are defined in
 * reflex_hal.h; this header re-exports them and adds the project's
 * standard tag constants.
 */

#include "reflex_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void reflex_log_init(void);
void reflex_log_set_level(int level);
int  reflex_log_get_level(void);

#define REFLEX_BOOT_TAG "reflex.boot"
#define REFLEX_SHELL_TAG "reflex.shell"

#ifdef __cplusplus
}
#endif

#endif
