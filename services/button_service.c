#include "reflex_hal.h"
#include "reflex_task.h"
#include "reflex_button_service.h"



#include "reflex_button.h"
#include "reflex_service.h"

static reflex_err_t reflex_button_service_init(void *ctx)
{
    (void)ctx;
    return reflex_button_init();
}

static reflex_service_status_t reflex_button_service_status(void *ctx)
{
    (void)ctx;
    return REFLEX_SERVICE_STATUS_STARTED;
}

reflex_err_t reflex_button_service_register(void)
{
    static reflex_service_desc_t desc = {
        .name = "button",
        .init = reflex_button_service_init,
        .status = reflex_button_service_status,
    };
    return reflex_service_register(&desc);
}
