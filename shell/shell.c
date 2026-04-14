#include "reflex_shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"

#include "reflex_log.h"
#include "reflex_config.h"
#include "reflex_event.h"
#include "reflex_service.h"
#include "reflex_wifi.h"
#include "reflex_led.h"
#include "reflex_cache.h"
#include "reflex_fabric.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_loader.h"

#define REFLEX_SHELL_LINE_MAX 256
#define REFLEX_SHELL_ARGV_MAX 8

static const reflex_vm_instruction_t reflex_shell_sample_program[] = {
    {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 0, .imm = 1},
    {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 1, .imm = -1},
    {.opcode = REFLEX_VM_OPCODE_TADD, .dst = 2, .src_a = 0, .src_b = 0},
    {.opcode = REFLEX_VM_OPCODE_TCMP, .dst = 3, .src_a = 2, .src_b = 0},
    {.opcode = REFLEX_VM_OPCODE_TBRPOS, .src_a = 3, .imm = 6},
    {.opcode = REFLEX_VM_OPCODE_THALT},
    {.opcode = REFLEX_VM_OPCODE_TSUB, .dst = 4, .src_a = 2, .src_b = 0},
    {.opcode = REFLEX_VM_OPCODE_THALT},
};

static const reflex_vm_image_t reflex_shell_sample_image = {
    .magic = REFLEX_VM_IMAGE_MAGIC,
    .version = REFLEX_VM_IMAGE_VERSION,
    .entry_ip = 0,
    .instructions = reflex_shell_sample_program,
    .instruction_count = sizeof(reflex_shell_sample_program) / sizeof(reflex_shell_sample_program[0]),
};

static reflex_vm_state_t reflex_shell_vm = {0};
static reflex_vm_task_runtime_t reflex_shell_vm_task = {0};
static bool reflex_shell_vm_loaded = false;
static bool reflex_shell_io_ready = false;
static uint8_t *reflex_shell_vm_binary = NULL;
static size_t reflex_shell_vm_binary_len = 0;

static void reflex_shell_init_io(void)
{
    if (reflex_shell_io_ready) {
        return;
    }

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    }

    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    reflex_shell_io_ready = true;
}

static const char *reflex_shell_vm_status_name(reflex_vm_status_t status)
{
    switch (status) {
    case REFLEX_VM_STATUS_READY:
        return "ready";
    case REFLEX_VM_STATUS_RUNNING:
        return "running";
    case REFLEX_VM_STATUS_HALTED:
        return "halted";
    case REFLEX_VM_STATUS_FAULTED:
        return "faulted";
    default:
        return "unknown";
    }
}

static const char *reflex_shell_vm_fault_name(reflex_vm_fault_t fault)
{
    switch (fault) {
    case REFLEX_VM_FAULT_NONE:
        return "none";
    case REFLEX_VM_FAULT_INVALID_OPCODE:
        return "invalid_opcode";
    case REFLEX_VM_FAULT_INVALID_REGISTER:
        return "invalid_register";
    case REFLEX_VM_FAULT_INVALID_IMMEDIATE:
        return "invalid_immediate";
    case REFLEX_VM_FAULT_PC_OUT_OF_RANGE:
        return "pc_out_of_range";
    case REFLEX_VM_FAULT_ARITHMETIC_OVERFLOW:
        return "arithmetic_overflow";
    case REFLEX_VM_FAULT_INVALID_SYSCALL:
        return "invalid_syscall";
    case REFLEX_VM_FAULT_INVALID_MEMORY_ACCESS:
        return "invalid_memory_access";
    default:
        return "unknown";
    }
}

static void reflex_shell_print_word18(const reflex_word18_t *word)
{
    for (int i = REFLEX_WORD18_TRITS - 1; i >= 0; --i) {
        char digit = '0';
        if (word->trits[i] == REFLEX_TRIT_NEG) {
            digit = '-';
        } else if (word->trits[i] == REFLEX_TRIT_POS) {
            digit = '+';
        }
        putchar(digit);
    }
}

static void reflex_shell_print_prompt(void)
{
    printf("reflex> ");
    fflush(stdout);
}

static int reflex_shell_tokenize(char *line, char *argv[REFLEX_SHELL_ARGV_MAX])
{
    int argc = 0;
    char *token = strtok(line, " \t\r\n");

    while (token != NULL && argc < REFLEX_SHELL_ARGV_MAX) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\r\n");
    }

    return argc;
}

static void reflex_shell_help(void)
{
    printf("commands: help, reboot, version, uptime, heap, led status, config <get|set>, services, event test, wifi <status|connect>, fabric send <to> <op> <value>, vm info, vm load, vm loadhex <HEX>, vm run [steps], vm step, vm regs, vm task <start|stop|status>\n");
}

static void reflex_shell_led_status(void)
{
    printf("led=%s\n", reflex_led_get() ? "on" : "off");
}

static void reflex_shell_fabric_send(const char *to_arg, const char *op_arg, const char *value_arg)
{
    reflex_message_t msg = {0};
    reflex_word18_t payload;
    char *end = NULL;
    unsigned long to = 0;
    unsigned long op = 0;
    long value = 0;

    if (to_arg == NULL || op_arg == NULL || value_arg == NULL) {
        printf("usage: fabric send <to> <op> <value>\n");
        return;
    }

    to = strtoul(to_arg, &end, 10);
    if (end == NULL || *end != '\0' || to > UINT8_MAX) {
        printf("invalid destination\n");
        return;
    }

    op = strtoul(op_arg, &end, 10);
    if (end == NULL || *end != '\0' || op > UINT8_MAX) {
        printf("invalid op\n");
        return;
    }

    value = strtol(value_arg, &end, 10);
    if (end == NULL || *end != '\0') {
        printf("invalid value\n");
        return;
    }

    if (reflex_word18_from_int32((int32_t)value, &payload) != ESP_OK) {
        printf("value out of range\n");
        return;
    }

    msg.to = (uint8_t)to;
    msg.from = REFLEX_NODE_SYSTEM;
    msg.op = (uint8_t)op;
    msg.channel = REFLEX_CHAN_SYSTEM;
    msg.payload = payload;

    if (reflex_fabric_send(&msg) != ESP_OK) {
        printf("fabric send failed\n");
        return;
    }

    printf("fabric sent to=%u op=%u\n", msg.to, msg.op);
}

static esp_err_t reflex_shell_set_vm_binary(const uint8_t *buffer, size_t len)
{
    uint8_t *owned = NULL;

    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, REFLEX_SHELL_TAG, "buffer required");
    ESP_RETURN_ON_FALSE(len > 0, ESP_ERR_INVALID_ARG, REFLEX_SHELL_TAG, "length required");

    owned = malloc(len);
    ESP_RETURN_ON_FALSE(owned != NULL, ESP_ERR_NO_MEM, REFLEX_SHELL_TAG, "vm binary alloc failed");
    memcpy(owned, buffer, len);

    free(reflex_shell_vm_binary);
    reflex_shell_vm_binary = owned;
    reflex_shell_vm_binary_len = len;
    return ESP_OK;
}

static void reflex_shell_vm_loadhex(const char *hex)
{
    if (hex == NULL) {
        printf("usage: vm loadhex <HEX_STRING>\n");
        return;
    }

    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) {
        printf("invalid hex string length\n");
        return;
    }

    size_t bin_len = hex_len / 2;
    uint8_t *bin_buf = malloc(bin_len);
    if (!bin_buf) {
        printf("malloc failed\n");
        return;
    }

    for (size_t i = 0; i < bin_len; i++) {
        char pair[3] = {hex[i*2], hex[i*2+1], '\0'};
        bin_buf[i] = (uint8_t)strtoul(pair, NULL, 16);
    }

    esp_err_t err = reflex_vm_load_binary(&reflex_shell_vm, bin_buf, bin_len);

    if (err != ESP_OK) {
        free(bin_buf);
        printf("vm loadhex failed err=%d\n", err);
        return;
    }

    err = reflex_shell_set_vm_binary(bin_buf, bin_len);
    free(bin_buf);
    if (err != ESP_OK) {
        printf("vm loadhex cached binary failed err=%d\n", err);
        return;
    }

    reflex_shell_vm_loaded = true;
    printf("vm loaded from hex: instructions=%lu\n", (unsigned long)reflex_shell_vm.program_length);
}

static void reflex_shell_wifi_status(void)
{
    char ssid[33] = {0};
    esp_netif_ip_info_t ip_info;
    
    if (reflex_config_get_wifi_ssid(ssid, sizeof(ssid)) == ESP_OK && strlen(ssid) > 0) {
        printf("configured_ssid=%s\n", ssid);
    } else {
        printf("wifi not configured\n");
        return;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        printf("ip=" IPSTR "\n", IP2STR(&ip_info.ip));
        printf("gw=" IPSTR "\n", IP2STR(&ip_info.gw));
        printf("mask=" IPSTR "\n", IP2STR(&ip_info.netmask));
    } else {
        printf("ip=disconnected\n");
    }
}

static void reflex_shell_wifi_connect(const char *ssid, const char *pass)
{
    if (ssid == NULL || pass == NULL) {
        printf("usage: wifi connect <ssid> <password>\n");
        return;
    }

    reflex_config_set_wifi_ssid(ssid);
    reflex_config_set_wifi_password(pass);
    printf("wifi credentials updated, reboot to connect\n");
}

static void reflex_shell_event_handler(const reflex_event_t *event, void *ctx)
{
    printf("\n[event] type=%d timestamp=%lu\n", (int)event->type, (unsigned long)event->timestamp_ms);
    reflex_shell_print_prompt();
}

static void reflex_shell_event_test(void)
{
    static bool subscribed = false;
    if (!subscribed) {
        reflex_event_subscribe(REFLEX_EVENT_CUSTOM, reflex_shell_event_handler, NULL);
        subscribed = true;
    }
    printf("publishing custom event\n");
    reflex_event_publish(REFLEX_EVENT_CUSTOM, NULL, 0);
}

static const char *reflex_shell_service_status_name(reflex_service_status_t status)
{
    switch (status) {
    case REFLEX_SERVICE_STATUS_STOPPED:
        return "stopped";
    case REFLEX_SERVICE_STATUS_STARTED:
        return "started";
    case REFLEX_SERVICE_STATUS_FAULTED:
        return "faulted";
    default:
        return "unknown";
    }
}

static void reflex_shell_services(void)
{
    size_t count = reflex_service_get_count();
    printf("services=%zu\n", count);
    for (size_t i = 0; i < count; ++i) {
        const reflex_service_desc_t *svc = reflex_service_get_by_index(i);
        reflex_service_status_t status = REFLEX_SERVICE_STATUS_STOPPED;
        if (svc->status != NULL) {
            status = svc->status(svc->context);
        }
        printf("%zu: name=%s status=%s\n", i, svc->name, reflex_shell_service_status_name(status));
    }
}

static void reflex_shell_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();

    printf("project=%s version=%s idf=%s\n",
           app_desc->project_name,
           app_desc->version,
           app_desc->idf_ver);
}

static void reflex_shell_uptime(void)
{
    printf("uptime_ms=%lu\n", (unsigned long)(esp_timer_get_time() / 1000));
}

static void reflex_shell_heap(void)
{
    printf("heap_free=%lu\n", (unsigned long)esp_get_free_heap_size());
}

static void reflex_shell_reboot(void)
{
    printf("rebooting\n");
    fflush(stdout);
    esp_restart();
}

static void reflex_shell_vm_info(void)
{
    printf("vm loaded=%s status=%s fault=%s ip=%lu steps=%lu program=%lu\n",
           reflex_shell_vm_loaded ? "yes" : "no",
           reflex_shell_vm_status_name(reflex_shell_vm.status),
           reflex_shell_vm_fault_name(reflex_shell_vm.fault),
           (unsigned long)reflex_shell_vm.ip,
           (unsigned long)reflex_shell_vm.steps_executed,
           (unsigned long)reflex_shell_vm.program_length);
    
    if (reflex_shell_vm.cache) {
        reflex_cache_t *c = (reflex_cache_t*)reflex_shell_vm.cache;
        printf("vm_cache hits=%lu misses=%lu evicts=%lu\n",
               (unsigned long)c->hits, (unsigned long)c->misses, (unsigned long)c->evictions);
    }
}

static void reflex_shell_vm_regs(void)
{
    if (!reflex_shell_vm_loaded) {
        printf("vm not loaded\n");
        return;
    }

    for (size_t i = 0; i < REFLEX_VM_REGISTER_COUNT; ++i) {
        printf("r%u=", (unsigned)i);
        reflex_shell_print_word18(&reflex_shell_vm.registers[i]);
        printf("\n");
    }
}

static void reflex_shell_config_get(const char *key)
{
    if (key == NULL) {
        printf("usage: config get <key>\n");
        return;
    }

    if (strcmp(key, "device_name") == 0) {
        char name[32];
        if (reflex_config_get_device_name(name, sizeof(name)) == ESP_OK) {
            printf("device_name=%s\n", name);
        } else {
            printf("failed to get device_name\n");
        }
    } else if (strcmp(key, "log_level") == 0) {
        int32_t level;
        if (reflex_config_get_log_level(&level) == ESP_OK) {
            printf("log_level=%ld\n", (long)level);
        } else {
            printf("failed to get log_level\n");
        }
    } else if (strcmp(key, "wifi_ssid") == 0) {
        char ssid[33];
        if (reflex_config_get_wifi_ssid(ssid, sizeof(ssid)) == ESP_OK) {
            printf("wifi_ssid=%s\n", ssid);
        } else {
            printf("failed to get wifi_ssid\n");
        }
    } else if (strcmp(key, "wifi_password") == 0) {
        char pass[65];
        if (reflex_config_get_wifi_password(pass, sizeof(pass)) == ESP_OK) {
            printf("wifi_password=***\n");
        } else {
            printf("failed to get wifi_password\n");
        }
    } else if (strcmp(key, "safe_mode") == 0) {
        bool safe;
        if (reflex_config_get_safe_mode(&safe) == ESP_OK) {
            printf("safe_mode=%s\n", safe ? "true" : "false");
        } else {
            printf("failed to get safe_mode\n");
        }
    } else if (strcmp(key, "boot_count") == 0) {
        int32_t count;
        if (reflex_config_get_boot_count(&count) == ESP_OK) {
            printf("boot_count=%ld\n", (long)count);
        } else {
            printf("failed to get boot_count\n");
        }
    } else {
        printf("unknown key: %s\n", key);
    }
}

static void reflex_shell_config_set(const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        printf("usage: config set <key> <value>\n");
        return;
    }

    esp_err_t err = ESP_OK;

    if (strcmp(key, "device_name") == 0) {
        err = reflex_config_set_device_name(value);
    } else if (strcmp(key, "log_level") == 0) {
        int32_t level = atoi(value);
        err = reflex_config_set_log_level(level);
    } else if (strcmp(key, "wifi_ssid") == 0) {
        err = reflex_config_set_wifi_ssid(value);
    } else if (strcmp(key, "wifi_password") == 0) {
        err = reflex_config_set_wifi_password(value);
    } else if (strcmp(key, "safe_mode") == 0) {
        bool safe = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        err = reflex_config_set_safe_mode(safe);
    } else if (strcmp(key, "boot_count") == 0) {
        int32_t count = atoi(value);
        err = reflex_config_set_boot_count(count);
    } else {
        printf("unknown key: %s\n", key);
        return;
    }

    if (err == ESP_OK) {
        printf("config set %s ok\n", key);
    } else {
        printf("config set %s failed err=%d\n", key, err);
    }
}

static void reflex_shell_vm_task_status(void)
{
    printf("vm task running=%s status=%s fault=%s ip=%lu steps=%lu\n",
           reflex_vm_task_is_running(&reflex_shell_vm_task) ? "yes" : "no",
           reflex_shell_vm_status_name(reflex_shell_vm_task.vm.status),
           reflex_shell_vm_fault_name(reflex_shell_vm_task.vm.fault),
           (unsigned long)reflex_shell_vm_task.vm.ip,
           (unsigned long)reflex_shell_vm_task.vm.steps_executed);
}

static void reflex_shell_vm_task_start(void)
{
    esp_err_t err;

    if (reflex_vm_task_is_running(&reflex_shell_vm_task)) {
        printf("vm task already running\n");
        return;
    }

    if (reflex_shell_vm_binary != NULL && reflex_shell_vm_binary_len > 0) {
        err = reflex_vm_task_start_binary(&reflex_shell_vm_task,
                                          reflex_shell_vm_binary,
                                          reflex_shell_vm_binary_len,
                                          NULL);
    } else {
        err = reflex_vm_task_start(&reflex_shell_vm_task, &reflex_shell_sample_image, NULL);
    }
    if (err != ESP_OK) {
        printf("vm task start failed err=%d\n", err);
        return;
    }

    printf("vm task started source=%s\n", reflex_shell_vm_binary != NULL ? "loaded" : "sample");
}

static void reflex_shell_vm_task_stop(void)
{
    esp_err_t err = reflex_vm_task_stop(&reflex_shell_vm_task);

    if (err != ESP_OK) {
        printf("vm task stop failed err=%d\n", err);
        return;
    }

    printf("vm task stopped\n");
}

static void reflex_shell_vm_load(void)
{
    uint8_t image_buf[16 + (8 * 4)];
    uint32_t checksum = 0;
    memset(image_buf, 0, sizeof(image_buf));
    
    uint32_t magic = REFLEX_VM_IMAGE_MAGIC;
    uint16_t ver = 2;
    uint32_t checksum_placeholder = 0;
    uint16_t entry = 0;
    uint16_t instr_count = 8;
    uint16_t data_count = 0;
    
    memcpy(image_buf, &magic, 4);
    memcpy(image_buf + 4, &ver, 2);
    memcpy(image_buf + 6, &checksum_placeholder, 4);
    memcpy(image_buf + 10, &entry, 2);
    memcpy(image_buf + 12, &instr_count, 2);
    memcpy(image_buf + 14, &data_count, 2);
    
    // Sample Program (Same as v1 but packed)
    // TLDI r0, 1
    uint32_t *p = (uint32_t*)(image_buf + 16);
    p[0] = REFLEX_VM_OPCODE_TLDI | (0 << 6) | (1 << 15);
    // TLDI r1, -1
    p[1] = REFLEX_VM_OPCODE_TLDI | (1 << 6) | (0x1FFFF << 15); // -1 signed 17-bit
    // TADD r2, r0, r0
    p[2] = REFLEX_VM_OPCODE_TADD | (2 << 6) | (0 << 9) | (0 << 12);
    // TCMP r3, r2, r0
    p[3] = REFLEX_VM_OPCODE_TCMP | (3 << 6) | (2 << 9) | (0 << 12);
    // TBRPOS r3, 6
    p[4] = REFLEX_VM_OPCODE_TBRPOS | (3 << 9) | (6 << 15);
    // THALT
    p[5] = REFLEX_VM_OPCODE_THALT;
    // TSUB r4, r2, r0
    p[6] = REFLEX_VM_OPCODE_TSUB | (4 << 6) | (2 << 9) | (0 << 12);
    // THALT
    p[7] = REFLEX_VM_OPCODE_THALT;

    if (reflex_vm_crc32(image_buf + 16, sizeof(image_buf) - 16, &checksum) != ESP_OK) {
        printf("vm load v2 failed err=%d\n", ESP_FAIL);
        return;
    }
    memcpy(image_buf + 6, &checksum, 4);

    esp_err_t err = reflex_vm_load_binary(&reflex_shell_vm, image_buf, sizeof(image_buf));

    if (err != ESP_OK) {
        printf("vm load v2 failed err=%d\n", err);
        return;
    }

    err = reflex_shell_set_vm_binary(image_buf, sizeof(image_buf));
    if (err != ESP_OK) {
        printf("vm load cached binary failed err=%d\n", err);
        return;
    }

    reflex_shell_vm_loaded = true;
    printf("vm loaded packed binary (v2) instructions=%lu\n", (unsigned long)reflex_shell_vm.program_length);
}

static void reflex_shell_vm_run(const char *steps_arg)
{
    char *end = NULL;
    uint32_t steps = 16;
    esp_err_t err;

    if (!reflex_shell_vm_loaded) {
        printf("vm not loaded\n");
        return;
    }

    if (steps_arg != NULL) {
        unsigned long parsed = strtoul(steps_arg, &end, 10);
        if (end == NULL || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
            printf("invalid step count\n");
            return;
        }
        steps = (uint32_t)parsed;
    }

    err = reflex_vm_run(&reflex_shell_vm, steps);
    printf("vm run err=%d status=%s fault=%s ip=%lu steps=%lu\n",
           err,
           reflex_shell_vm_status_name(reflex_shell_vm.status),
           reflex_shell_vm_fault_name(reflex_shell_vm.fault),
           (unsigned long)reflex_shell_vm.ip,
           (unsigned long)reflex_shell_vm.steps_executed);
}

static void reflex_shell_vm_step(void)
{
    esp_err_t err;

    if (!reflex_shell_vm_loaded) {
        printf("vm not loaded\n");
        return;
    }

    err = reflex_vm_step(&reflex_shell_vm);
    printf("vm step err=%d status=%s fault=%s ip=%lu steps=%lu\n",
           err,
           reflex_shell_vm_status_name(reflex_shell_vm.status),
           reflex_shell_vm_fault_name(reflex_shell_vm.fault),
           (unsigned long)reflex_shell_vm.ip,
           (unsigned long)reflex_shell_vm.steps_executed);
}

static void reflex_shell_dispatch(int argc, char *argv[REFLEX_SHELL_ARGV_MAX])
{
    if (argc == 0) {
        return;
    }

    if (strcmp(argv[0], "help") == 0) {
        reflex_shell_help();
        return;
    }

    if (strcmp(argv[0], "reboot") == 0) {
        reflex_shell_reboot();
        return;
    }

    if (strcmp(argv[0], "version") == 0) {
        reflex_shell_version();
        return;
    }

    if (strcmp(argv[0], "uptime") == 0) {
        reflex_shell_uptime();
        return;
    }

    if (strcmp(argv[0], "heap") == 0) {
        reflex_shell_heap();
        return;
    }

    if (strcmp(argv[0], "led") == 0) {
        if (argc < 2 || strcmp(argv[1], "status") != 0) {
            printf("usage: led status\n");
            return;
        }

        reflex_shell_led_status();
        return;
    }

    if (strcmp(argv[0], "services") == 0) {
        reflex_shell_services();
        return;
    }

    if (strcmp(argv[0], "event") == 0) {
        if (argc < 2 || strcmp(argv[1], "test") != 0) {
            printf("usage: event test\n");
            return;
        }
        reflex_shell_event_test();
        return;
    }

    if (strcmp(argv[0], "wifi") == 0) {
        if (argc < 2) {
            printf("usage: wifi <status|connect>\n");
            return;
        }

        if (strcmp(argv[1], "status") == 0) {
            reflex_shell_wifi_status();
            return;
        }

        if (strcmp(argv[1], "connect") == 0) {
            if (argc < 4) {
                printf("usage: wifi connect <ssid> <password>\n");
                return;
            }
            reflex_shell_wifi_connect(argv[2], argv[3]);
            return;
        }

        printf("unknown wifi command\n");
        return;
    }

    if (strcmp(argv[0], "fabric") == 0) {
        if (argc < 5 || strcmp(argv[1], "send") != 0) {
            printf("usage: fabric send <to> <op> <value>\n");
            return;
        }

        reflex_shell_fabric_send(argv[2], argv[3], argv[4]);
        return;
    }

    if (strcmp(argv[0], "config") == 0) {
        if (argc < 3) {
            printf("usage: config <get|set> <key> [value]\n");
            return;
        }

        if (strcmp(argv[1], "get") == 0) {
            reflex_shell_config_get(argv[2]);
            return;
        }

        if (strcmp(argv[1], "set") == 0) {
            if (argc < 4) {
                printf("usage: config set <key> <value>\n");
                return;
            }
            reflex_shell_config_set(argv[2], argv[3]);
            return;
        }

        printf("unknown config command\n");
        return;
    }

    if (strcmp(argv[0], "vm") == 0) {
        if (argc < 2) {
            printf("usage: vm <info|load|run|step|regs>\n");
            return;
        }

        if (strcmp(argv[1], "info") == 0) {
            reflex_shell_vm_info();
            return;
        }
        if (strcmp(argv[1], "load") == 0) {
            reflex_shell_vm_load();
            return;
        }
        if (strcmp(argv[1], "loadhex") == 0) {
            if (argc < 3) {
                printf("usage: vm loadhex <HEX>\n");
                return;
            }
            reflex_shell_vm_loadhex(argv[2]);
            return;
        }
        if (strcmp(argv[1], "run") == 0) {
            reflex_shell_vm_run(argc >= 3 ? argv[2] : NULL);
            return;
        }
        if (strcmp(argv[1], "step") == 0) {
            reflex_shell_vm_step();
            return;
        }
        if (strcmp(argv[1], "regs") == 0) {
            reflex_shell_vm_regs();
            return;
        }
        if (strcmp(argv[1], "task") == 0) {
            if (argc < 3) {
                printf("usage: vm task <start|stop|status>\n");
                return;
            }
            if (strcmp(argv[2], "start") == 0) {
                reflex_shell_vm_task_start();
                return;
            }
            if (strcmp(argv[2], "stop") == 0) {
                reflex_shell_vm_task_stop();
                return;
            }
            if (strcmp(argv[2], "status") == 0) {
                reflex_shell_vm_task_status();
                return;
            }
            printf("unknown vm task command\n");
            return;
        }

        printf("unknown vm command\n");
        return;
    }

    printf("unknown command\n");
}

void reflex_shell_run(void)
{
    char line[REFLEX_SHELL_LINE_MAX];
    char *argv[REFLEX_SHELL_ARGV_MAX];
    size_t line_len = 0;

    reflex_shell_init_io();
    reflex_shell_vm.node_id = REFLEX_NODE_VM;
    
    REFLEX_LOGI(REFLEX_SHELL_TAG, "shell ready; type 'help'");
    reflex_shell_print_prompt();

    while (true) {
        uint8_t ch = 0;
        int read = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(50));

        if (read <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            line[line_len] = '\0';
            int argc = reflex_shell_tokenize(line, argv);

            reflex_shell_dispatch(argc, argv);
            line_len = 0;
            reflex_shell_print_prompt();
            continue;
        }

        if (line_len < sizeof(line) - 1) {
            line[line_len++] = (char)ch;
        } else {
            line_len = 0;
            printf("line too long\n");
            reflex_shell_print_prompt();
        }
    }
}
