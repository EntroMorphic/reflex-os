#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "reflex_boot.h"
#include "reflex_log.h"
#include "reflex_storage.h"
#include "reflex_config.h"
#include "reflex_event.h"
#include "reflex_service.h"
#include "reflex_led_service.h"
#include "reflex_wifi.h"
#include "reflex_shell.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_task.h"
#include "reflex_vm_loader.h"

#define REFLEX_BOOT_LOOP_THRESHOLD 3

void app_main(void)
{
    int32_t boot_count = 0;
    bool safe_mode = false;
    static reflex_vm_task_runtime_t system_vm;

    reflex_boot_print_banner();
    
    // 1. Init Storage
    if (reflex_storage_init() != ESP_OK) {
        REFLEX_LOGE(REFLEX_BOOT_TAG, "storage init failed, entering minimal shell");
        reflex_shell_run();
        return;
    }

    // 2. Check Boot Loop / Safe Mode
    reflex_config_get_safe_mode(&safe_mode);
    reflex_config_get_boot_count(&boot_count);
    
    if (boot_count >= REFLEX_BOOT_LOOP_THRESHOLD) {
        REFLEX_LOGW(REFLEX_BOOT_TAG, "boot loop detected (%ld attempts)", (long)boot_count);
        safe_mode = true;
    }

    if (safe_mode) {
        REFLEX_LOGW(REFLEX_BOOT_TAG, "safe mode active");
        // Reset boot count so we don't stay here forever if fixed manually
        reflex_config_set_boot_count(0);
        reflex_config_set_safe_mode(false);
        reflex_shell_run();
        return;
    }

    // 3. Normal Startup
    reflex_config_set_boot_count(boot_count + 1);
    
    reflex_event_bus_init();
    reflex_service_manager_init();
    
    reflex_led_service_register();
    reflex_wifi_service_register();
    
    reflex_vm_task_runtime_init(&system_vm);
    reflex_vm_task_register_service(&system_vm, "system-vm");
    
    reflex_service_start_all();

    reflex_config_set_boot_count(0);

    reflex_event_publish(REFLEX_EVENT_BOOT_COMPLETE, NULL, 0);

    // Run self-checks (keep these for now to verify logic)
    reflex_ternary_self_check();
    reflex_vm_self_check();
    reflex_vm_loader_self_check();
    reflex_vm_task_self_check();

    reflex_shell_run();
}
