#include "reflex_log.h"

static int s_reflex_log_level = REFLEX_LOG_LEVEL_INFO;

void reflex_log_init(void) {}

void reflex_log_set_level(int level) {
    s_reflex_log_level = level;
}

int reflex_log_get_level(void) {
    return s_reflex_log_level;
}
