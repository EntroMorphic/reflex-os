#include <stdint.h>
#include <stdio.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"

#include "reflex_ternary.h"
#include "reflex_shell.h"
#include "reflex_vm.h"
#include "reflex_vm_loader.h"

void app_main(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    esp_flash_get_size(NULL, &flash_size);

    printf("reflex-os booted\n");
    printf("target=%s cores=%d revision=%d.%d flash=%luMB\n",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           chip_info.revision / 100,
           chip_info.revision % 100,
           (unsigned long)(flash_size / (1024 * 1024)));

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

    reflex_shell_run();
}
