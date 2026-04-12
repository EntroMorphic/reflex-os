#include "reflex_shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"

#include "reflex_log.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_loader.h"

#define REFLEX_SHELL_LINE_MAX 96
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
    printf("commands: help, vm info, vm load, vm run [steps], vm step, vm regs, vm task <start|stop|status>\n");
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

    err = reflex_vm_task_start(&reflex_shell_vm_task, &reflex_shell_sample_image, NULL);
    if (err != ESP_OK) {
        printf("vm task start failed err=%d\n", err);
        return;
    }

    printf("vm task started\n");
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
    esp_err_t err = reflex_vm_load_image(&reflex_shell_vm, &reflex_shell_sample_image);

    if (err != ESP_OK) {
        printf("vm load failed err=%d\n", err);
        return;
    }

    reflex_shell_vm_loaded = true;
    printf("vm loaded sample program instructions=%lu\n", (unsigned long)reflex_shell_vm.program_length);
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
