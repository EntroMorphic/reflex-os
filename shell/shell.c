#include "reflex_shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
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
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

#include "reflex_log.h"
#include "reflex_config.h"
#include "reflex_event.h"
#include "reflex_service.h"
#include "reflex_wifi.h"
#include "reflex_button.h"
#include "reflex_led.h"
#include "reflex_cache.h"
#include "reflex_fabric.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_loader.h"
#include "goose.h"

#define REFLEX_SHELL_LINE_MAX 256
#define REFLEX_SHELL_ARGV_MAX 8

// --- Types ---

typedef enum {
    REFLEX_BONSAI_EDGE_NEG = -1,
    REFLEX_BONSAI_EDGE_ZERO = 0,
    REFLEX_BONSAI_EDGE_POS = 1,
} reflex_bonsai_edge_t;

typedef struct {
    TaskHandle_t handle;
    bool running;
    int last_level;
    int current_level;
    reflex_bonsai_edge_t last_edge;
    uint32_t rising_edges;
    uint32_t falling_edges;
    uint32_t stable_samples;
} reflex_bonsai_exp1_state_t;

typedef struct {
    TaskHandle_t handle;
    bool running;
    reflex_bonsai_edge_t phase;
    uint32_t phase_steps;
    uint64_t last_tick_us;
} reflex_bonsai_exp2_state_t;

typedef struct {
    TaskHandle_t handle;
    bool running;
    reflex_bonsai_edge_t field_a;
    reflex_bonsai_edge_t field_b;
    reflex_bonsai_edge_t output;
    uint32_t steps_a;
    uint32_t steps_b;
    uint32_t comp_steps;
} reflex_bonsai_exp3a_state_t;

typedef struct {
    reflex_bonsai_edge_t route;
    bool ledc_initialized;
} reflex_bonsai_exp4_state_t;

// --- State ---

static reflex_vm_state_t reflex_shell_vm = {0};
static bool reflex_shell_vm_loaded = false;

static reflex_bonsai_exp1_state_t reflex_shell_bonsai_exp1 = {
    .last_level = 1, .current_level = 1, .last_edge = REFLEX_BONSAI_EDGE_ZERO
};
static reflex_bonsai_exp2_state_t reflex_shell_bonsai_exp2 = { .phase = REFLEX_BONSAI_EDGE_NEG };
static reflex_bonsai_exp3a_state_t reflex_shell_bonsai_exp3a = {0};
static reflex_bonsai_exp4_state_t reflex_shell_bonsai_exp4 = { .route = REFLEX_BONSAI_EDGE_ZERO };

// --- Helpers ---

static const char *reflex_shell_bonsai_edge_name(reflex_bonsai_edge_t edge)
{
    if (edge == REFLEX_BONSAI_EDGE_NEG) return "falling";
    if (edge == REFLEX_BONSAI_EDGE_POS) return "rising";
    return "unchanged";
}

static reflex_bonsai_edge_t reflex_shell_bonsai_next_phase(reflex_bonsai_edge_t phase)
{
    if (phase == REFLEX_BONSAI_EDGE_NEG) return REFLEX_BONSAI_EDGE_ZERO;
    if (phase == REFLEX_BONSAI_EDGE_ZERO) return REFLEX_BONSAI_EDGE_POS;
    return REFLEX_BONSAI_EDGE_NEG;
}

static const char *reflex_shell_vm_status_name(reflex_vm_status_t status)
{
    switch (status) {
    case REFLEX_VM_STATUS_READY: return "ready";
    case REFLEX_VM_STATUS_RUNNING: return "running";
    case REFLEX_VM_STATUS_HALTED: return "halted";
    case REFLEX_VM_STATUS_FAULTED: return "faulted";
    default: return "unknown";
    }
}

static void reflex_shell_config_get(const char *key)
{
    if (strcmp(key, "device_name") == 0) {
        char name[32];
        if (reflex_config_get_device_name(name, sizeof(name)) == ESP_OK) printf("device_name=%s\n", name);
    } else if (strcmp(key, "log_level") == 0) {
        int32_t level;
        if (reflex_config_get_log_level(&level) == ESP_OK) printf("log_level=%ld\n", (long)level);
    } else if (strcmp(key, "boot_count") == 0) {
        int32_t count;
        if (reflex_config_get_boot_count(&count) == ESP_OK) printf("boot_count=%ld\n", (long)count);
    } else printf("unknown key: %s\n", key);
}

static void reflex_shell_config_set(const char *key, const char *value)
{
    esp_err_t err = ESP_FAIL;
    if (strcmp(key, "device_name") == 0) err = reflex_config_set_device_name(value);
    else if (strcmp(key, "log_level") == 0) err = reflex_config_set_log_level(atoi(value));
    else if (strcmp(key, "boot_count") == 0) err = reflex_config_set_boot_count(atoi(value));
    
    if (err == ESP_OK) printf("config set %s ok\n", key);
    else printf("config set %s failed\n", key);
}

// --- Experiment Logic ---

static void reflex_shell_bonsai_exp1_task(void *arg) {
    reflex_bonsai_exp1_state_t *s = (reflex_bonsai_exp1_state_t *)arg;
    while (s->running) {
        int l = gpio_get_level(REFLEX_BUTTON_PIN);
        if (l > s->last_level) { s->last_edge = REFLEX_BONSAI_EDGE_POS; s->rising_edges++; reflex_led_set(true); }
        else if (l < s->last_level) { s->last_edge = REFLEX_BONSAI_EDGE_NEG; s->falling_edges++; reflex_led_set(false); }
        else { s->last_edge = REFLEX_BONSAI_EDGE_ZERO; s->stable_samples++; }
        s->last_level = l; s->current_level = l;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    s->handle = NULL; vTaskDelete(NULL);
}

static void reflex_shell_bonsai_exp2_task(void *arg) {
    reflex_bonsai_exp2_state_t *s = (reflex_bonsai_exp2_state_t *)arg;
    while (s->running) {
        s->last_tick_us = esp_timer_get_time();
        s->phase = reflex_shell_bonsai_next_phase(s->phase);
        s->phase_steps++;
        if (s->phase == REFLEX_BONSAI_EDGE_NEG) reflex_led_set(false);
        else if (s->phase == REFLEX_BONSAI_EDGE_POS) reflex_led_set(true);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    s->handle = NULL; vTaskDelete(NULL);
}

static void reflex_shell_bonsai_exp3a_task(void *arg) {
    reflex_bonsai_exp3a_state_t *s = (reflex_bonsai_exp3a_state_t *)arg;
    uint32_t t = 0;
    while (s->running) {
        t++;
        if (t % 2 == 0) { s->field_b = reflex_shell_bonsai_next_phase(s->field_b); s->steps_b++; }
        if (t % 8 == 0) { s->field_a = reflex_shell_bonsai_next_phase(s->field_a); s->steps_a++; }
        s->output = (reflex_bonsai_edge_t)((int)s->field_a * (int)s->field_b);
        s->comp_steps++;
        if (s->output == REFLEX_BONSAI_EDGE_POS) reflex_led_set(true);
        else if (s->output == REFLEX_BONSAI_EDGE_NEG) { static bool b; b = !b; reflex_led_set(b); }
        else reflex_led_set(false);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    s->handle = NULL; vTaskDelete(NULL);
}

// --- Start/Status Logic ---

static void reflex_shell_bonsai_exp1_start(void) {
    if (reflex_shell_bonsai_exp1.running) return;
    reflex_shell_bonsai_exp1.running = true;
    xTaskCreate(reflex_shell_bonsai_exp1_task, "bonsai-exp1", 2048, &reflex_shell_bonsai_exp1, 6, &reflex_shell_bonsai_exp1.handle);
}
static void reflex_shell_bonsai_exp1_status(void) {
    reflex_bonsai_exp1_state_t *s = &reflex_shell_bonsai_exp1;
    printf("bonsai exp1 running=%s level=%d edge=%s rises=%lu falls=%lu led=%s\n", s->running?"yes":"no", s->current_level, reflex_shell_bonsai_edge_name(s->last_edge), (unsigned long)s->rising_edges, (unsigned long)s->falling_edges, reflex_led_get()?"on":"off");
}

static void reflex_shell_bonsai_exp2_start(void) {
    if (reflex_shell_bonsai_exp2.running) return;
    reflex_shell_bonsai_exp2.running = true;
    xTaskCreate(reflex_shell_bonsai_exp2_task, "bonsai-exp2", 2048, &reflex_shell_bonsai_exp2, 6, &reflex_shell_bonsai_exp2.handle);
}
static void reflex_shell_bonsai_exp2_status(void) {
    reflex_bonsai_exp2_state_t *s = &reflex_shell_bonsai_exp2;
    printf("bonsai exp2 running=%s phase=%s steps=%lu led=%s\n", s->running?"yes":"no", reflex_shell_bonsai_edge_name(s->phase), (unsigned long)s->phase_steps, reflex_led_get()?"on":"off");
}

static void reflex_shell_bonsai_exp3a_start(void) {
    if (reflex_shell_bonsai_exp3a.running) return;
    reflex_shell_bonsai_exp3a.running = true;
    xTaskCreate(reflex_shell_bonsai_exp3a_task, "bonsai-exp3a", 2048, &reflex_shell_bonsai_exp3a, 6, &reflex_shell_bonsai_exp3a.handle);
}
static void reflex_shell_bonsai_exp3a_status(void) {
    reflex_bonsai_exp3a_state_t *s = &reflex_shell_bonsai_exp3a;
    printf("bonsai exp3a running=%s field_a=%s field_b=%s output=%s led=%s\n", s->running?"yes":"no", reflex_shell_bonsai_edge_name(s->field_a), reflex_shell_bonsai_edge_name(s->field_b), reflex_shell_bonsai_edge_name(s->output), reflex_led_get()?"on":"off");
}

static void reflex_shell_bonsai_exp4_route(int orient) {
    if (!reflex_shell_bonsai_exp4.ledc_initialized) {
        ledc_timer_config_t t = { .speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=LEDC_TIMER_0, .duty_resolution=LEDC_TIMER_8_BIT, .freq_hz=1000, .clk_cfg=LEDC_AUTO_CLK };
        ledc_timer_config(&t);
        ledc_channel_config_t c = { .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_0, .timer_sel=LEDC_TIMER_0, .intr_type=LEDC_INTR_DISABLE, .gpio_num=-1, .duty=128, .hpoint=0 };
        ledc_channel_config(&c);
        reflex_shell_bonsai_exp4.ledc_initialized = true;
    }
    if (orient == 1) { esp_rom_gpio_connect_out_signal(REFLEX_LED_PIN, LEDC_LS_SIG_OUT0_IDX, false, false); reflex_shell_bonsai_exp4.route = REFLEX_BONSAI_EDGE_POS; }
    else if (orient == -1) { esp_rom_gpio_connect_out_signal(REFLEX_LED_PIN, LEDC_LS_SIG_OUT0_IDX, true, false); reflex_shell_bonsai_exp4.route = REFLEX_BONSAI_EDGE_NEG; }
    else { esp_rom_gpio_connect_out_signal(REFLEX_LED_PIN, 128, false, false); reflex_shell_bonsai_exp4.route = REFLEX_BONSAI_EDGE_ZERO; }
    printf("bonsai exp4 route orient=%s\n", reflex_shell_bonsai_edge_name(reflex_shell_bonsai_exp4.route));
}

// --- Exp 5: Silicon Loop (GIE) ---

static void reflex_shell_bonsai_exp5_run(void) {
    pcnt_unit_config_t ucfg = { .low_limit = -1000, .high_limit = 1000 };
    pcnt_unit_handle_t pcnt = NULL;
    if (pcnt_new_unit(&ucfg, &pcnt) != ESP_OK) return;
    
    pcnt_chan_config_t ch = { .edge_gpio_num = 4, .level_gpio_num = 6 };
    pcnt_channel_handle_t p_ch;
    if (pcnt_new_channel(pcnt, &ch, &p_ch) != ESP_OK) { pcnt_del_unit(pcnt); return; }
    pcnt_channel_set_edge_action(p_ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_unit_enable(pcnt);
    pcnt_unit_start(pcnt);

    rmt_tx_channel_config_t rcfg = {
        .gpio_num = 4,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1MHz
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.io_loop_back = 1,
    };
    rmt_channel_handle_t rmt_ch = NULL;
    if (rmt_new_tx_channel(&rcfg, &rmt_ch) != ESP_OK) { pcnt_unit_stop(pcnt); pcnt_unit_disable(pcnt); pcnt_del_unit(pcnt); return; }
    rmt_enable(rmt_ch);

    gpio_config_t io = { .pin_bit_mask = (1ULL << 6), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(6, 1); // High to enable (keeping it simple)

    rmt_symbol_word_t pulses[10];
    for(int i=0; i<10; i++) {
        pulses[i].duration0 = 1000;
        pulses[i].level0 = 1;
        pulses[i].duration1 = 1000;
        pulses[i].level1 = 0;
    }
    rmt_transmit_config_t tcfg = { .loop_count = 0 };
    rmt_copy_encoder_config_t ecfg = {};
    rmt_encoder_handle_t encoder = NULL;
    rmt_new_copy_encoder(&ecfg, &encoder);
    rmt_transmit(rmt_ch, encoder, pulses, 10, &tcfg);

    vTaskDelay(pdMS_TO_TICKS(100));

    int count = 0;
    pcnt_unit_get_count(pcnt, &count);
    printf("bonsai exp5 intersect overlap=%d (expected 10)\n", count);

    rmt_disable(rmt_ch);
    rmt_del_channel(rmt_ch);
    pcnt_unit_stop(pcnt);
    pcnt_unit_disable(pcnt);
    pcnt_del_unit(pcnt);
}

// --- Main Shell ---

static reflex_trit_t reflex_shell_bonsai_rhythm_evolve(void *ctx) {
    static reflex_trit_t s = REFLEX_TRIT_NEG;
    if (s == REFLEX_TRIT_NEG) s = REFLEX_TRIT_ZERO;
    else if (s == REFLEX_TRIT_ZERO) s = REFLEX_TRIT_POS;
    else s = REFLEX_TRIT_NEG;
    return s;
}

static void reflex_shell_bonsai_runtime_test(void) {
    printf("GOOSE Phase 7 (Hardened): Initializing Runtime Test...\n");
    goose_fabric_init();

    // 1. Define Cells
    static goose_cell_t source = { .name = "oscillator", .state = REFLEX_TRIT_NEG };
    static goose_cell_t sink = { .name = "led_agency", .state = REFLEX_TRIT_ZERO, .hardware_addr = REFLEX_LED_PIN };

    // 2. Define Transition (Evolution)
    static goose_transition_t trans = {
        .name = "heartbeat",
        .target = &source,
        .evolution_fn = reflex_shell_bonsai_rhythm_evolve,
        .interval_ms = 500
    };

    // 3. Define Route
    static goose_route_t route = {
        .name = "agency_patch",
        .source = &source,
        .sink = &sink,
        .orientation = REFLEX_TRIT_POS,
        .coupling = GOOSE_COUPLING_SOFTWARE
    };

    // 4. Define Field
    static goose_field_t field = {
        .name = "rhythm_field",
        .routes = &route,
        .route_count = 1,
        .transitions = &trans,
        .transition_count = 1
    };

    printf("Field [%s] created with 1 route and 1 transition.\n", field.name);
    
    // Run for 5 seconds
    printf("Running autonomous field for 5s...\n");
    for(int i=0; i<50; i++) {
        goose_process_transitions(&field);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("Runtime test complete.\n");
}

static void reflex_shell_tapestry_signal(const char *name, int state) {
    goose_cell_t *c = goose_fabric_get_cell(name);
    if (!c) {
        printf("Error: Cell '%s' not found in Tapestry.\n", name);
        return;
    }
    c->state = (reflex_trit_t)state;
    printf("Signal sent to Tapestry: %s = %d\n", name, state);
}

static void reflex_shell_bonsai_heal_test(void) {
    printf("GOOSE Phase 9: Harmonic Supervisor Proof-of-Life...\n");
    
    // 1. Setup a "Broken" Field
    static goose_cell_t source = { .name = "signal", .state = REFLEX_TRIT_POS, .type = GOOSE_CELL_INTENT };
    static goose_cell_t sink = { .name = "agency", .state = REFLEX_TRIT_NEG, .type = GOOSE_CELL_HARDWARE_OUT, .hardware_addr = REFLEX_LED_PIN };
    
    // Route is intentionally INHIBITED (0)
    static goose_route_t route = {
        .name = "unbalanced_path",
        .source = &source,
        .sink = &sink,
        .orientation = REFLEX_TRIT_ZERO,
        .coupling = GOOSE_COUPLING_SOFTWARE
    };
    
    static goose_field_t field = {
        .name = "healing_field",
        .routes = &route,
        .route_count = 1
    };

    printf("Created field [%s]. Signal=POS, Route=INHIBIT, Sink=NEG. (Disequilibrium)\n", field.name);
    
    // 2. Perception: Supervisor checks equilibrium
    if (goose_supervisor_check_equilibrium(&field) != ESP_OK) {
        printf("Supervisor detected Disequilibrium! Initiating Rebalance...\n");
        
        // 3. Agency: Supervisor re-levels the manifold
        goose_supervisor_rebalance(&field);
        
        printf("Rebalance complete. Route orientation is now: %d\n", route.orientation);
        printf("Sink state is now: %d (LED should be ON)\n", sink.state);
    } else {
        printf("Error: Supervisor failed to detect disequilibrium.\n");
    }
}

static void reflex_shell_dispatch(int argc, char *argv[]) {
    if (argc == 0) return;
    if (strcmp(argv[0], "help") == 0) printf("commands: help, reboot, led status, bonsai <exp1..5|runtime|heal>, tapestry <signal name state>, services, config <get|set>, vm info\n");
    else if (strcmp(argv[0], "reboot") == 0) esp_restart();
    else if (strcmp(argv[0], "led") == 0) { if (argc >= 2 && strcmp(argv[1], "status") == 0) printf("led=%s\n", reflex_led_get()?"on":"off"); }
    else if (strcmp(argv[0], "bonsai") == 0) {
        if (argc < 3) return;
        if (strcmp(argv[1], "exp1") == 0) { if (strcmp(argv[2], "start") == 0) reflex_shell_bonsai_exp1_start(); else if (strcmp(argv[2], "status") == 0) reflex_shell_bonsai_exp1_status(); }
        else if (strcmp(argv[1], "exp2") == 0) { if (strcmp(argv[2], "start") == 0) reflex_shell_bonsai_exp2_start(); else if (strcmp(argv[2], "status") == 0) reflex_shell_bonsai_exp2_status(); }
        else if (strcmp(argv[1], "exp3a") == 0) { if (strcmp(argv[2], "start") == 0) reflex_shell_bonsai_exp3a_start(); else if (strcmp(argv[2], "status") == 0) reflex_shell_bonsai_exp3a_status(); }
        else if (strcmp(argv[1], "exp4") == 0) { if (argc >= 3 && strcmp(argv[2], "connect") == 0) reflex_shell_bonsai_exp4_route(1); else if (argc >= 3 && strcmp(argv[2], "invert") == 0) reflex_shell_bonsai_exp4_route(-1); else if (argc >= 3 && strcmp(argv[2], "detach") == 0) reflex_shell_bonsai_exp4_route(0); }
        else if (strcmp(argv[1], "exp5") == 0) { if (argc >= 3 && strcmp(argv[2], "run") == 0) reflex_shell_bonsai_exp5_run(); }
        else if (strcmp(argv[1], "runtime") == 0) { reflex_shell_bonsai_runtime_test(); }
        else if (strcmp(argv[1], "heal") == 0) { reflex_shell_bonsai_heal_test(); }
    } else if (strcmp(argv[0], "tapestry") == 0) {
        if (argc >= 4 && strcmp(argv[1], "signal") == 0) {
            reflex_shell_tapestry_signal(argv[2], atoi(argv[3]));
        }
    } else if (strcmp(argv[0], "services") == 0) {
        size_t c = reflex_service_get_count(); printf("services=%zu\n", c);
        for(size_t i=0; i<c; i++) { const reflex_service_desc_t *s = reflex_service_get_by_index(i); printf("%zu: %s\n", i, s->name); }
    } else if (strcmp(argv[0], "config") == 0) {
        if (argc >= 3 && strcmp(argv[1], "get") == 0) reflex_shell_config_get(argv[2]);
        else if (argc >= 4 && strcmp(argv[1], "set") == 0) reflex_shell_config_set(argv[2], argv[3]);
    } else if (strcmp(argv[0], "vm") == 0) {
        if (argc >= 2 && strcmp(argv[1], "info") == 0) printf("vm status=%s fault=%s ip=%lu steps=%lu\n", reflex_shell_vm_status_name(reflex_shell_vm.status), "none", (unsigned long)0, (unsigned long)0);
        else if (argc >= 3 && strcmp(argv[1], "loadhex") == 0) {
            size_t blen = strlen(argv[2]) / 2; uint8_t *b = malloc(blen);
            for(size_t i=0; i<blen; i++) { char p[3]={argv[2][i*2], argv[2][i*2+1], 0}; b[i]=(uint8_t)strtoul(p,NULL,16); }
            if(reflex_vm_load_binary(&reflex_shell_vm, b, blen)==ESP_OK) { reflex_shell_vm_loaded=true; printf("vm loaded\n"); } else printf("vm load failed\n");
            free(b);
        }
    }
}

void reflex_shell_run(void) {
    char line[REFLEX_SHELL_LINE_MAX]; size_t len = 0;
    if (!usb_serial_jtag_is_driver_installed()) { usb_serial_jtag_driver_config_t c = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT(); usb_serial_jtag_driver_install(&c); }
    usb_serial_jtag_vfs_use_driver(); usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF); usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    printf("reflex> "); fflush(stdout);
    while (1) {
        uint8_t ch; int r = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(50));
        if (r <= 0) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        if (ch == '\n') {
            line[len] = 0; char *argv[8]; int argc = 0;
            char *t = strtok(line, " "); while(t && argc < 8) { argv[argc++] = t; t = strtok(NULL, " "); }
            reflex_shell_dispatch(argc, argv);
            len = 0; printf("\nreflex> "); fflush(stdout);
        } else if (ch != '\r' && len < 255) { line[len++] = ch; putchar(ch); fflush(stdout); }
    }
}
