#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

#include "reflex_boot.h"
#include "reflex_storage.h"
#include "reflex_service.h"
#include "reflex_ternary.h"
#include "reflex_shell.h"
#include "reflex_vm.h"
#include "reflex_vm_task.h"
#include "reflex_vm_loader.h"

void app_main(void)
{
    esp_err_t storage_result;
    static reflex_vm_task_runtime_t system_vm;

    reflex_boot_print_banner();
    storage_result = reflex_storage_init();
    
    reflex_service_manager_init();
    reflex_vm_task_runtime_init(&system_vm);
    reflex_vm_task_register_service(&system_vm, "system-vm");
    reflex_service_start_all();

    esp_err_t ternary_result = reflex_ternary_self_check();
    esp_err_t vm_result = reflex_vm_self_check();
    esp_err_t loader_result = reflex_vm_loader_self_check();
    esp_err_t vm_task_result = reflex_vm_task_self_check();
    printf("ternary self-check=%s word18=%d packed=%dB\n",
           ternary_result == ESP_OK ? "ok" : "failed",
           REFLEX_WORD18_TRITS,
           REFLEX_PACKED_WORD18_BYTES);
    printf("vm self-check=%s registers=%d compare_trit=%d\n",
           vm_result == ESP_OK ? "ok" : "failed",
           REFLEX_VM_REGISTER_COUNT,
           REFLEX_VM_COMPARE_TRIT_INDEX);
    printf("vm loader self-check=%s version=%u\n",
           loader_result == ESP_OK ? "ok" : "failed",
           REFLEX_VM_IMAGE_VERSION);
    printf("vm task self-check=%s\n",
           vm_task_result == ESP_OK ? "ok" : "failed");
    printf("storage init=%s\n",
           storage_result == ESP_OK ? "ok" : "failed");

    reflex_shell_run();
}
