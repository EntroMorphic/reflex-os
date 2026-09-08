/** @file shell.c
 * @brief Interactive UART command shell.
 */
#include "reflex_shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fenced on the target, not on a capability macro.
 *
 * SOC_USB_SERIAL_JTAG_SUPPORTED reads correctly only if soc_caps.h has already
 * been included, and this file was getting that transitively through
 * driver/rmt_tx.h. Removing that include as part of owning RMT left the first
 * `#if` here evaluating an *undefined* macro: it took the else branch, pulled
 * in driver/uart.h, and uart.h then defined the macro so every later guard in
 * the file evaluated the other way. The build still worked, by accident of
 * include order, while quietly reacquiring the two UART includes Tier E had
 * already shed — which is how the independence checker's build-agreement test
 * caught it.
 *
 * CONFIG_IDF_TARGET_ESP32C6 comes from sdkconfig.h, which the build force-
 * includes into every source file, so it cannot be undefined at the point of
 * use. */
#if !CONFIG_IDF_TARGET_ESP32C6
#include "driver/ledc.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

#include "reflex_types.h"
#include "reflex_hal.h"
#include "reflex_task.h"
#include "reflex_soc_esp32c6.h" /* LEDC signal index; was soc/gpio_sig_map.h */
#include "reflex_regops.h"
#include "reflex_rom_esp32c6.h" /* esp_rom_gpio_connect_out_signal */

#include "reflex_log.h"
#include "reflex_config.h"
#include "reflex_event.h"
#include "reflex_service.h"
#if !CONFIG_REFLEX_RADIO_802154
#include "reflex_wifi.h"   /* Wi-Fi builds only; 802.15.4 mode omits net/ entirely */
#endif
#include "reflex_button.h"
#include "reflex_led.h"
#include "reflex_cache.h"
#include "reflex_temp_service.h"
#include "reflex_fabric.h"
#include "reflex_ternary.h"
#include "reflex_vm.h"
#include "reflex_vm_loader.h"
#include "shell_policy.h"
#include "shell_parse.h"
#include "shell_outcome.h"

/* Defined with the session state below, forward-declared here because the
 * config helpers near the top of this file report outcomes too. */
static void outcome(shell_reason_t r);
static bool extra_args(int argc, int expected);
#include "goose.h"
#include "esp_system.h"
#include "goose_telemetry.h"
#include "goose_metabolic.h"
#include "reflex_tuning.h"
#include "programs.h"

#define REFLEX_SHELL_LINE_MAX 1024
#define REFLEX_SHELL_ARGV_MAX 8

// --- Types ---

typedef enum {
    REFLEX_BONSAI_EDGE_NEG = -1,
    REFLEX_BONSAI_EDGE_ZERO = 0,
    REFLEX_BONSAI_EDGE_POS = 1,
} reflex_bonsai_edge_t;

typedef struct {
    reflex_task_handle_t handle;
    bool running;
    int last_level;
    int current_level;
    reflex_bonsai_edge_t last_edge;
    uint32_t rising_edges;
    uint32_t falling_edges;
    uint32_t stable_samples;
} reflex_bonsai_exp1_state_t;

typedef struct {
    reflex_task_handle_t handle;
    bool running;
    reflex_bonsai_edge_t phase;
    uint32_t phase_steps;
    uint64_t last_tick_us;
} reflex_bonsai_exp2_state_t;

typedef struct {
    reflex_task_handle_t handle;
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
        if (reflex_config_get_device_name(name, sizeof(name)) == REFLEX_OK) printf("device_name=%s\n", name);
    } else if (strcmp(key, "log_level") == 0) {
        int32_t level;
        if (reflex_config_get_log_level(&level) == REFLEX_OK) printf("log_level=%ld\n", (long)level);
    } else if (strcmp(key, "boot_count") == 0) {
        int32_t count;
        if (reflex_config_get_boot_count(&count) == REFLEX_OK) printf("boot_count=%ld\n", (long)count);
    } else { printf("unknown key: %s\n", key); outcome(SHELL_USAGE); }
}

static void reflex_shell_config_set(const char *key, const char *value)
{
    reflex_err_t err = REFLEX_FAIL;
    long v;
    if (strcmp(key, "device_name") == 0) err = reflex_config_set_device_name(value);
    else if (strcmp(key, "log_level") == 0) {
        if (!shell_parse_int(value, INT32_MIN, INT32_MAX, &v)) {
            printf("config set %s: expected an integer\n", key); outcome(SHELL_INVALID); return;
        }
        err = reflex_config_set_log_level((int32_t)v);
    }
    else if (strcmp(key, "boot_count") == 0) {
        if (!shell_parse_int(value, INT32_MIN, INT32_MAX, &v)) {
            printf("config set %s: expected an integer\n", key); outcome(SHELL_INVALID); return;
        }
        err = reflex_config_set_boot_count((int32_t)v);
    }

    if (err == REFLEX_OK) printf("config set %s ok\n", key);
    else { printf("config set %s failed\n", key); outcome(SHELL_FAILED); }
}

// --- Experiment Logic ---

static void reflex_shell_bonsai_exp1_task(void *arg) {
    reflex_bonsai_exp1_state_t *s = (reflex_bonsai_exp1_state_t *)arg;
    while (s->running) {
        int l = reflex_hal_gpio_get_level(REFLEX_BUTTON_PIN);
        if (l > s->last_level) { s->last_edge = REFLEX_BONSAI_EDGE_POS; s->rising_edges++; reflex_led_set(true); }
        else if (l < s->last_level) { s->last_edge = REFLEX_BONSAI_EDGE_NEG; s->falling_edges++; reflex_led_set(false); }
        else { s->last_edge = REFLEX_BONSAI_EDGE_ZERO; s->stable_samples++; }
        s->last_level = l; s->current_level = l;
        reflex_task_delay_ms(25);
    }
    s->handle = NULL; reflex_task_delete(NULL);
}

static void reflex_shell_bonsai_exp2_task(void *arg) {
    reflex_bonsai_exp2_state_t *s = (reflex_bonsai_exp2_state_t *)arg;
    while (s->running) {
        s->last_tick_us = reflex_hal_time_us();
        s->phase = reflex_shell_bonsai_next_phase(s->phase);
        s->phase_steps++;
        if (s->phase == REFLEX_BONSAI_EDGE_NEG) reflex_led_set(false);
        else if (s->phase == REFLEX_BONSAI_EDGE_POS) reflex_led_set(true);
        reflex_task_delay_ms(400);
    }
    s->handle = NULL; reflex_task_delete(NULL);
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
        reflex_task_delay_ms(250);
    }
    s->handle = NULL; reflex_task_delete(NULL);
}

// --- Start/Status Logic ---

static void reflex_shell_bonsai_exp1_start(void) {
    if (reflex_shell_bonsai_exp1.running) return;
    reflex_shell_bonsai_exp1.running = true;
    reflex_task_create(reflex_shell_bonsai_exp1_task, "bonsai-exp1", 2048, &reflex_shell_bonsai_exp1, 6, &reflex_shell_bonsai_exp1.handle);
}
static void reflex_shell_bonsai_exp1_status(void) {
    reflex_bonsai_exp1_state_t *s = &reflex_shell_bonsai_exp1;
    printf("bonsai exp1 running=%s level=%d edge=%s rises=%lu falls=%lu led=%s\n", s->running?"yes":"no", s->current_level, reflex_shell_bonsai_edge_name(s->last_edge), (unsigned long)s->rising_edges, (unsigned long)s->falling_edges, reflex_led_get()?"on":"off");
}

static void reflex_shell_bonsai_exp2_start(void) {
    if (reflex_shell_bonsai_exp2.running) return;
    reflex_shell_bonsai_exp2.running = true;
    reflex_task_create(reflex_shell_bonsai_exp2_task, "bonsai-exp2", 2048, &reflex_shell_bonsai_exp2, 6, &reflex_shell_bonsai_exp2.handle);
}
static void reflex_shell_bonsai_exp2_status(void) {
    reflex_bonsai_exp2_state_t *s = &reflex_shell_bonsai_exp2;
    printf("bonsai exp2 running=%s phase=%s steps=%lu led=%s\n", s->running?"yes":"no", reflex_shell_bonsai_edge_name(s->phase), (unsigned long)s->phase_steps, reflex_led_get()?"on":"off");
}

static void reflex_shell_bonsai_exp3a_start(void) {
    if (reflex_shell_bonsai_exp3a.running) return;
    reflex_shell_bonsai_exp3a.running = true;
    reflex_task_create(reflex_shell_bonsai_exp3a_task, "bonsai-exp3a", 2048, &reflex_shell_bonsai_exp3a, 6, &reflex_shell_bonsai_exp3a.handle);
}
static void reflex_shell_bonsai_exp3a_status(void) {
    reflex_bonsai_exp3a_state_t *s = &reflex_shell_bonsai_exp3a;
    printf("bonsai exp3a running=%s field_a=%s field_b=%s output=%s led=%s\n", s->running?"yes":"no", reflex_shell_bonsai_edge_name(s->field_a), reflex_shell_bonsai_edge_name(s->field_b), reflex_shell_bonsai_edge_name(s->output), reflex_led_get()?"on":"off");
}

static void reflex_shell_bonsai_exp4_route(int orient) {
    if (!reflex_shell_bonsai_exp4.ledc_initialized) {
#if CONFIG_IDF_TARGET_ESP32C6
        /* Reflex's own LEDC. Tier E: this is what driver/ledc.h was here for.
         *
         * Checked against the driver it replaces rather than assumed
         * equivalent — configured through ESP-IDF, the peripheral latched
         * timer=0x00138808 ch0=0x00000004 duty=0x00000800 pcr=0x00000001
         * sclk=0x00700000, and configured through this it latches the same. The
         * readback below is what made that comparison possible, and it stays. */
        if (reflex_hal_pwm_init(1000, 8, 128) != REFLEX_OK) {
            printf("bonsai exp4: PWM init failed\n");
            outcome(SHELL_FAILED);
            return;
        }
#else
        /* ESP-IDF still owns LEDC on the classic ESP32.
         *
         * The channel is bound to the LED pin rather than to -1. The intent was
         * to configure a channel without attaching a pin and route the signal
         * by hand below, which is a reasonable technique that LEDC rejects
         * outright ("gpio_num argument is invalid") — and nothing checked, so
         * this handler printed an orientation it had not applied and reported
         * #R:+1,ok. It had therefore never once driven the LED. */
        ledc_timer_config_t t = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                 .timer_num = LEDC_TIMER_0,
                                 .duty_resolution = LEDC_TIMER_8_BIT,
                                 .freq_hz = 1000,
                                 .clk_cfg = LEDC_AUTO_CLK};
        if (ledc_timer_config(&t) != REFLEX_OK) {
            printf("bonsai exp4: LEDC timer config failed\n");
            outcome(SHELL_FAILED);
            return;
        }
        ledc_channel_config_t c = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                   .channel = LEDC_CHANNEL_0,
                                   .timer_sel = LEDC_TIMER_0,
                                   .intr_type = LEDC_INTR_DISABLE,
                                   .gpio_num = REFLEX_LED_PIN,
                                   .duty = 128,
                                   .hpoint = 0};
        if (ledc_channel_config(&c) != REFLEX_OK) {
            printf("bonsai exp4: LEDC channel config failed\n");
            outcome(SHELL_FAILED);
            return;
        }
#endif
        reflex_shell_bonsai_exp4.ledc_initialized = true;
    }
    {
        /* Report what the peripheral actually latched.
         *
         * The previous version of this handler printed an orientation it had
         * not applied, because nothing looked. Reading the configuration back
         * is how that stops being possible — and it is what let the Reflex LEDC
         * driver be checked against the ESP-IDF one it replaces, register for
         * register, rather than declared equivalent. */
        reflex_pwm_snapshot_t pwm;
        reflex_hal_pwm_snapshot(&pwm);
        printf("bonsai exp4 ledc timer=0x%08lx ch0=0x%08lx duty=0x%08lx "
               "pcr=0x%08lx sclk=0x%08lx\n",
               (unsigned long)pwm.timer_conf, (unsigned long)pwm.ch_conf0,
               (unsigned long)pwm.ch_duty, (unsigned long)pwm.pcr_conf,
               (unsigned long)pwm.pcr_sclk);
    }
    if (orient == 1) {
        esp_rom_gpio_connect_out_signal(REFLEX_LED_PIN, REFLEX_LEDC_LS_SIG_OUT0_IDX, false, false);
        reflex_shell_bonsai_exp4.route = REFLEX_BONSAI_EDGE_POS;
    } else if (orient == -1) {
        esp_rom_gpio_connect_out_signal(REFLEX_LED_PIN, REFLEX_LEDC_LS_SIG_OUT0_IDX, true, false);
        reflex_shell_bonsai_exp4.route = REFLEX_BONSAI_EDGE_NEG;
    } else {
        esp_rom_gpio_connect_out_signal(REFLEX_LED_PIN, REFLEX_SIG_GPIO_OUT_IDX, false, false);
        reflex_shell_bonsai_exp4.route = REFLEX_BONSAI_EDGE_ZERO;
#if CONFIG_IDF_TARGET_ESP32C6
        /* Detach means give the peripheral back, not just re-route the pin.
         *
         * This left LEDC ungated, clocked and running for the rest of the boot
         * with only the pin pointed elsewhere. PCNT and RMT both got a release;
         * PWM did not, and an audit for that asymmetry is what turned it up. */
        reflex_hal_pwm_release();
        reflex_shell_bonsai_exp4.ledc_initialized = false;
#endif
    }
    printf("bonsai exp4 route orient=%s\n", reflex_shell_bonsai_edge_name(reflex_shell_bonsai_exp4.route));
}

// --- Exp 5: Silicon Loop (GIE) ---

static void reflex_shell_bonsai_exp5_run(void) {
#if !CONFIG_IDF_TARGET_ESP32C6
    /* Refuse rather than reset the board.
     *
     * This experiment hardcodes GPIO 4 and GPIO 6 and depends on RMT's
     * io_loop_back reaching PCNT on the same pad — C6 wiring throughout. On the
     * classic ESP32, GPIO 6 is one of the pins wired to the SPI flash, so
     * configuring it as an output cuts the chip off from the code it is
     * executing and the watchdog resets: `rst:0x8 (TG1WDT_SYS_RESET)`,
     * reproduced on that board with this build and with main's.
     *
     * A shell command must not be able to reboot the board — "no command can
     * wedge the board" is one of the acceptance criteria in
     * docs/adoption-gaps.md — so on any target this was not wired for it says
     * so and stops. */
    printf("bonsai exp5: not wired for this target "
           "(needs GPIO 4/6 and RMT loopback into PCNT; GPIO 6 is a flash pin "
           "on the classic ESP32)\n");
    outcome(SHELL_FAILED);
    return;
#else

    /* One acquisition order, one cleanup path.
     *
     * There were three separate unwind paths before, and the missing
     * pcnt_del_channel was present in two of them: fixing the success path left
     * the rmt_new_tx_channel failure path still calling pcnt_del_unit with the
     * channel attached, which fails and leaks the unit exactly as before. Three
     * hand-written unwinds is why the bug existed; a single one is why it will
     * not come back. */
    bool pcnt_started = false, rmt_enabled = false;
    bool gpio6_taken = false;
    const char *failed_at = NULL;

    /* Reflex's own PCNT. Tier E: this is what driver/pulse_cnt.h was here for.
     *
     * One call replaces new_unit / new_channel / set_edge_action / enable /
     * start, because that is the whole of what this experiment ever used: one
     * unit, one channel, counting rising edges on pin 4 while pin 6 gates. */
    if (reflex_hal_pcnt_start(4, 6, -1000, 1000) != REFLEX_OK) {
        failed_at = "reflex_hal_pcnt_start";
        goto cleanup;
    }
    pcnt_started = true;

    /* Reflex's own RMT. Tier E: the last two includes this file borrowed.
     *
     * Same shape as before — 1 MHz resolution on pin 4, with the pin's input
     * left enabled so the counter reading the same pad sees what is
     * transmitted, which is what io_loop_back was asking for. */
    if (reflex_hal_rmt_tx_init(4, 1000000) != REFLEX_OK) {
        failed_at = "reflex_hal_rmt_tx_init";
        goto cleanup;
    }
    rmt_enabled = true;

    /* Left as OUTPUT deliberately, after testing the alternative.
     *
     * The obvious theory for the short count below is that a pin PCNT reads
     * should have its input buffer enabled, and that reconfiguring GPIO 6
     * output-only after pcnt_new_channel claimed it as a level input turns that
     * buffer off. Both halves of that were tried on hardware: INPUT_OUTPUT on
     * GPIO 6 changed the count not at all (2 of 10, four runs), and adding it
     * on GPIO 4 made it worse (0 of 10, four runs) by disturbing the routing
     * RMT's io_loop_back sets up. Neither is kept — a change that cannot show a
     * result is not a fix, and this file has reverted speculative changes for
     * that reason before. */
    /* Reflex's own GPIO, which this file already had available and was
     * reaching past to driver/gpio.h for — a header it was only receiving
     * transitively through driver/ledc.h in the first place. */
    reflex_hal_gpio_init_output(6);
    reflex_hal_gpio_set_level(6, 1); // High to enable (keeping it simple)
    gpio6_taken = true;

    uint32_t pulses[10];
    for (int i = 0; i < 10; i++) {
        pulses[i] = reflex_hal_rmt_symbol(1000, true, 1000, false);
    }

    /* Transmit and wait for the channel to say it is done, rather than for an
     * interval that might or might not cover it. */
    /* Time the transmission, because nothing else here can check the clock.
     *
     * The register comparison pins DIV_CNT at 80 and the pulse count proves ten
     * edges arrived, and both would still pass if the source clock were not the
     * 80 MHz this driver assumes — the pulses would simply be the wrong width.
     * Ten symbols of 1000 + 1000 ticks at the 1 MHz asked for is 20 ms, and
     * that is a number the host can check. */
    uint64_t tx_start = reflex_hal_time_us();
    if (reflex_hal_rmt_tx_symbols(pulses, 10, 1000000) != REFLEX_OK) {
        failed_at = "reflex_hal_rmt_tx_symbols";
        goto cleanup;
    }
    uint32_t tx_us = (uint32_t)(reflex_hal_time_us() - tx_start);

    int count = reflex_hal_pcnt_read();
    printf("bonsai exp5 intersect overlap=%d (expected 10) tx_us=%lu (expected ~20000)\n", count,
           (unsigned long)tx_us);
    {
        /* Same readback the PWM path carries, and for the same reason: it is
         * what lets a Reflex driver be checked against the one it replaces
         * register for register rather than declared equivalent. */
        reflex_rmt_snapshot_t rm;
        reflex_hal_rmt_snapshot(&rm);
        printf("bonsai exp5 rmt conf0=0x%08lx sys=0x%08lx lim=0x%08lx "
               "pcr=0x%08lx sclk=0x%08lx\n",
               (unsigned long)rm.tx_conf0, (unsigned long)rm.sys_conf, (unsigned long)rm.tx_lim,
               (unsigned long)rm.pcr, (unsigned long)rm.pcr_sclk);
        reflex_pcnt_snapshot_t pc;
        reflex_hal_pcnt_snapshot(&pc);
        printf("bonsai exp5 pcnt conf0=0x%08lx conf1=0x%08lx conf2=0x%08lx "
               "ctrl=0x%08lx pcr=0x%08lx\n",
               (unsigned long)pc.conf0, (unsigned long)pc.conf1, (unsigned long)pc.conf2,
               (unsigned long)pc.ctrl, (unsigned long)pc.pcr);
    }
    if (count != 10) {
        /* An experiment that misses its own stated expectation has not
         * succeeded, and saying #R:+1,ok when it does is how a broken
         * experiment survives being run. */
        outcome(SHELL_FAILED);
    }

cleanup:
    if (failed_at) {
        printf("bonsai exp5: %s failed\n", failed_at);
        outcome(SHELL_FAILED);
    }
    if (rmt_enabled) reflex_hal_rmt_release();
    /* No unit or channel to hand back any more. The leak that made this
     * cleanup path matter was ESP-IDF's allocator refusing to free a unit whose
     * channel was still attached; there is no allocator now, and pausing is the
     * whole of it. */
    if (pcnt_started) reflex_hal_pcnt_release();
    /* Hand GPIO 6 back. Leaving a pin driven is a side effect the caller did
     * not ask for, and `make hw-test` runs this five times. */
    /* Back to an input, which is how the pin was found. */
    if (gpio6_taken) reflex_hal_gpio_init_input(6, false);
#endif
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
    /* Deliberately does NOT call goose_fabric_init().
     *
     * It used to. On a running system that is a wipe: init takes its cold-boot
     * branch (the wakeup cause is UNDEFINED on a normal boot), sets
     * fabric_cell_count to 0 and clears the registry, so the whole Loom goes.
     * Measured on hardware: 117 cells down to 4, the atlas weave gone, and
     * sys.kernel.disposition reading "(unpublished)" because the supervisor's
     * cell no longer existed.
     *
     * The damage outlives the count, too. Every module that caches a
     * goose_cell_t* — the four metabolic vitals, the supervisor's disposition
     * cell, the atmosphere posture cell, led_service's route endpoints — was
     * left pointing into slots that had been zeroed and were free to be
     * reallocated to unrelated cells. Writes through those pointers land on
     * whatever moved in.
     *
     * This test needs no fresh fabric. It allocates at (10,0,*), which
     * collides with nothing. */

    // 1. Coordinates
    reflex_tryte9_t c_src = goose_make_coord(10, 0, 1);
    reflex_tryte9_t c_snk = goose_make_coord(10, 0, 2);

    // 2. Define Cells
    goose_cell_t *source = goose_fabric_alloc_cell("oscillator", c_src, false);
    goose_cell_t *sink = goose_fabric_alloc_cell("led_proxy", c_snk, false);
    if (!source || !sink) return;
    source->state = REFLEX_TRIT_NEG;
    sink->hardware_addr = REFLEX_LED_PIN;

    // 3. Define Transition (Evolution)
    static goose_transition_t trans;
    memset(&trans, 0, sizeof(trans));
    snprintf(trans.name, 16, "heartbeat");
    trans.target_coord = c_src;
    trans.evolution_fn = reflex_shell_bonsai_rhythm_evolve;
    trans.interval_ms = 500;

    // 4. Define Route
    static goose_route_t route;
    memset(&route, 0, sizeof(route));
    snprintf(route.name, 16, "agency_patch");
    route.source_coord = c_src;
    route.sink_coord = c_snk;
    route.orientation = REFLEX_TRIT_POS;
    route.coupling = GOOSE_COUPLING_SOFTWARE;

    // 5. Define Field
    static goose_field_t field;
    memset(&field, 0, sizeof(field));
    snprintf(field.name, 16, "rhythm_field");
    field.routes = &route;
    field.route_count = 1;
    field.transitions = &trans;
    field.transition_count = 1;

    printf("Field [%s] created.\n", field.name);
    for(int i=0; i<50; i++) {
        goose_process_transitions(&field);
        reflex_task_delay_ms(100);
    }
    printf("Runtime test complete.\n");
}

/* `reflex_trit_t` is a three-value enum, and this is the one shell path that
 * writes a cell's state from an operator-supplied number, so it validates the
 * range like `vitals override` does rather than casting whatever arrived.
 *
 * `sys.*` is refused for the same reason the MMIO sync layer refuses it
 * (`goose_mmio_sync.c` — "reject writes to sys.* namespace"): supervisor state
 * belongs to the OS, and a hand-signalled disposition is indistinguishable
 * downstream from one the kernel policy layer actually derived. The check runs
 * before resolution so a refusal does not also confirm whether the cell exists. */
static void reflex_shell_tapestry_signal(const char *name, const char *state_str) {
    int8_t state;
    if (!shell_parse_trit(state_str, &state)) {
        printf("tapestry: state must be -1, 0 or 1\n");
        outcome(SHELL_INVALID);
        return;
    }
    if (strncmp(name, GOOSE_NS_SYS, GOOSE_NS_SYS_LEN) == 0) {
        printf("tapestry: refusing to signal '%s' — sys.* belongs to the supervisor\n", name);
        outcome(SHELL_GUARD);
        return;
    }
    goose_cell_t *c = goose_fabric_get_cell(name);
    if (!c) {
        printf("Error: Cell '%s' not found in Tapestry.\n", name);
        outcome(SHELL_NOTFOUND);
        return;
    }
    c->state = (reflex_trit_t)state;
    printf("Signal sent to Tapestry: %s = %d\n", name, (int)state);
}

static void reflex_shell_bonsai_heal_test(void) {
    printf("GOOSE Phase 9: Harmonic Supervisor Proof-of-Life...\n");
    reflex_tryte9_t c_src = goose_make_coord(11, 0, 1);
    reflex_tryte9_t c_snk = goose_make_coord(11, 0, 2);

    goose_cell_t *source = goose_fabric_alloc_cell("heal_src", c_src, false);
    goose_cell_t *sink = goose_fabric_alloc_cell("heal_snk", c_snk, false);
    if (!source || !sink) return;
    source->state = REFLEX_TRIT_POS;
    source->type = GOOSE_CELL_INTENT;
    sink->state = REFLEX_TRIT_NEG;
    sink->type = GOOSE_CELL_HARDWARE_OUT;
    sink->hardware_addr = REFLEX_LED_PIN;
    
    static goose_route_t route;
    memset(&route, 0, sizeof(route));
    snprintf(route.name, 16, "unbalanced");
    route.source_coord = c_src;
    route.sink_coord = c_snk;
    route.orientation = REFLEX_TRIT_ZERO;
    route.coupling = GOOSE_COUPLING_SOFTWARE;
    
    static goose_field_t field;
    memset(&field, 0, sizeof(field));
    snprintf(field.name, 16, "healing_field");
    field.routes = &route;
    field.route_count = 1;

    printf("Created field [%s]. Signal=POS, Route=INHIBIT, Sink=NEG.\n", field.name);
    
    if (goose_supervisor_pulse() == REFLEX_OK) { // Simplified for shell test
        printf("Pulse complete. Check logs for rebalance action.\n");
    }
}

static void reflex_shell_bonsai_gvm_test(void) {
    printf("GOOSE Phase 12: Geometric VM Coordinate Test...\n");
    
    static reflex_vm_state_t gvm;
    
    // Program:
    // 0: TLDI R1, 0    (We'll manually set R1 to the coordinate [0,0,1])
    // 1: TSENSE R2, R1, IMM=1 (Sense via coordinate in R1)
    // 2: THALT
    
    static const reflex_vm_instruction_t prog[] = {
        {.opcode = REFLEX_VM_OPCODE_TLDI, .dst = 1, .imm = 0}, // Placeholder
        {.opcode = REFLEX_VM_OPCODE_TSENSE, .dst = 2, .src_a = 1, .imm = 1},
        {.opcode = REFLEX_VM_OPCODE_THALT}
    };
    
    reflex_vm_image_t image = {
        .magic = REFLEX_VM_IMAGE_MAGIC,
        .version = 1,
        .entry_ip = 0,
        .instructions = prog,
        .instruction_count = 3,
        .private_memory = NULL,
        .private_memory_count = 0
    };

    if (reflex_vm_load_image(&gvm, &image) != REFLEX_OK) {
        printf("Error: Failed to load GVM image.\n");
        outcome(SHELL_FAILED);
        return;
    }

    // Manually set R1 to the coordinate of 'led_intent' (0, 0, 1)
    // Based on goose_make_coord: trit[0]=0, trit[3]=0, trit[6]=1
    memset(&gvm.registers[1], 0, sizeof(reflex_word18_t));
    gvm.registers[1].trits[6] = REFLEX_TRIT_POS;

    printf("Running Geometric VM (sensing coordinate [0,0,1])...\n");
    reflex_vm_run(&gvm, 10);
    
    int32_t result;
    reflex_word18_to_int32(&gvm.registers[2], &result);
    printf("VM Coordinate Sense Result: %ld (Ternary State)\n", (long)result);
}

static void reflex_shell_bonsai_deep_sleep(void) {
    printf("GOOSE Phase 12: Entering Deep Reflection (Light Sleep)...\n");
    printf("The HP Mind will sleep. The LP Heart will maintain the LED heartbeat.\n");
    printf("Wake-up via JTAG/Serial in 10 seconds.\n");
    
    reflex_task_delay_ms(1000);
    
    // Configure wake up timer
    reflex_hal_sleep_enter(10 * 1000000);
    
    // Enter light sleep
    
    printf("HP Mind has returned to the manifold.\n");
}

static void reflex_shell_bonsai_weave_test(void) {
    printf("GOOSE Phase 13.5: Hardened Fragment Weaver Test...\n");
    
    reflex_tryte9_t base1 = goose_make_coord(0, 1, 0); 
    reflex_tryte9_t base2 = goose_make_coord(0, 1, 1); 
    
    goose_fragment_handle_t h1, h2;

    // Test 1: Multiple independent weavings
    if (goose_weave_fragment(GOOSE_FRAGMENT_HEARTBEAT, "heart1", base1, &h1) == REFLEX_OK &&
        goose_weave_fragment(GOOSE_FRAGMENT_HEARTBEAT, "heart2", base2, &h2) == REFLEX_OK) {
        printf("Success: Wove two independent Heartbeats.\n");
    }

    // Test 2: Collision Detection
    if (goose_weave_fragment(GOOSE_FRAGMENT_GATE, "clash", base1, NULL) != REFLEX_OK) {
        printf("Success: Weaver prevented coordinate collision.\n");
    } else {
        printf("Error: Weaver failed to detect collision!\n"); outcome(SHELL_FAILED);
    }
}

/* Format a coordinate including its namespace and full index.
 *
 * The old display printed trits[0], [3] and [6] only, which was lossy twice
 * over. It hid the namespace marker in trits[8], so a seeded cell and a
 * shadow-paged cell at the same (f,r,c) rendered identically — after the
 * namespaces were introduced, `peer.alpha.led` and an atlas cell both showed
 * as (5,0,0) while being distinct cells. It also dropped the high byte of the
 * index in trits[7], so two shadow entries 256 apart printed the same triple.
 *
 * Namespace 0 keeps a signed cell index (seeds legitimately use negatives,
 * e.g. perception.heap.pressure at (-1,4,-1)); the paged namespaces compose
 * their unsigned 16-bit index from trits[6] and trits[7]. */
static void shell_format_coord(reflex_tryte9_t c, char *out, size_t len) {
    if (c.trits[8] == 1 || c.trits[8] == -1) {
        int idx = (int)((uint8_t)c.trits[6] | ((uint8_t)c.trits[7] << 8));
        snprintf(out, len, "(%d,%d,%d)%s", (int)c.trits[0], (int)c.trits[3], idx,
                 (c.trits[8] == 1) ? "@shadow" : "@peer");
    } else {
        snprintf(out, len, "(%d,%d,%d)", (int)c.trits[0], (int)c.trits[3], (int)c.trits[6]);
    }
}

static void reflex_shell_loom_list(void) {
    printf("--- GOOSE Manifold: The Loom ---\n");
    printf("%-28s | %-17s | %-5s | %-8s\n", "Name", "Coordinate", "State", "Type");
    printf("--------------------------------------------------------------\n");
    
    uint32_t count = goonies_get_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = goonies_get_name_by_idx(i);
        reflex_tryte9_t coord = goonies_get_coord_by_idx(i);
        goose_cell_t *c = goose_fabric_get_cell_by_coord(coord);
        if (c) {
            char cbuf[32];
            shell_format_coord(coord, cbuf, sizeof(cbuf));
            /* Precision, not just width: registry names are char[40] and a
             * bare %-20s let long ones overflow and break every following
             * column. `goonies find` still reports the full name. */
            printf("%-28.28s | %-17s | %5d | %d\n",
                   name, cbuf, c->state, c->type);
        }
    }
}

static void reflex_shell_goonies_find(const char *name) {
    /* Use the full resolver — it checks live registry, peer allocation,
     * and shadow catalog in that order. */
    goose_cell_t *cell = goonies_resolve_cell(name);
    if (cell) {
        char cbuf[32];
        shell_format_coord(cell->coord, cbuf, sizeof(cbuf));
        if (cell->peer_id != 0) {
            printf("%s coord=%s state=%d peer_id=%u [phantom]\n", name, cbuf, cell->state, cell->peer_id);
        } else if (cell->hardware_addr >= 0x60000000) {
            printf("%s coord=%s addr=0x%08lx state=%d [shadow]\n", name, cbuf, (unsigned long)cell->hardware_addr, cell->state);
        } else {
            printf("%s coord=%s state=%d type=%d [live]\n", name, cbuf, cell->state, cell->type);
        }
        return;
    }
    /* Read-only shadow lookup (doesn't allocate into the Loom). */
    reflex_tryte9_t coord;
    uint32_t addr, mask;
    goose_cell_type_t type;
    if (goose_shadow_resolve(name, &addr, &mask, &coord, &type) == REFLEX_OK) {
        const char *type_str =
            (type == GOOSE_CELL_HARDWARE_IN)  ? "HARDWARE_IN" :
            (type == GOOSE_CELL_HARDWARE_OUT) ? "HARDWARE_OUT" :
            (type == GOOSE_CELL_SYSTEM_ONLY)  ? "SYSTEM_ONLY" :
            (type == GOOSE_CELL_VIRTUAL)      ? "VIRTUAL" : "?";
        printf("GOONIES: '%s' addr=0x%08lx mask=0x%08lx type=%s [shadow-only]\n",
               name, (unsigned long)addr, (unsigned long)mask, type_str);
        return;
    }
    printf("GOONIES: Failed to resolve '%s'\n", name); outcome(SHELL_NOTFOUND);
}

/* `atlas verify`: walk every entry in the shadow catalog, call
 * goose_shadow_resolve on its name, and report any failures. This is
 * the full-surface coverage check — proves every MMIO node in the
 * 12738-entry shadow catalog is reachable by name. */
static void reflex_shell_atlas_verify(void) {
    uint32_t total = (uint32_t)shadow_map_count;
    uint32_t ok = 0;
    uint32_t failed = 0;
    uint32_t duplicates = 0;
    uint32_t first_failure_idx = UINT32_MAX;
    uint32_t first_dup_idx = UINT32_MAX;

    /* Duplicate-name sweep. The catalog is sorted by name, so any
     * duplicate is adjacent. Binary search would silently consolidate
     * dupes to one entry; this sweep surfaces them independently. */
    for (uint32_t i = 1; i < total; i++) {
        if (strcmp(shadow_map[i - 1].name, shadow_map[i].name) == 0) {
            duplicates++;
            if (first_dup_idx == UINT32_MAX) first_dup_idx = i;
        }
    }

    /* Full round-trip resolve: every field must match, not just
     * addr+mask. A scraper bug that emitted wrong type or coord
     * would otherwise slip past the verification.
     *
     * Every 1000 entries we emit a progress dot and yield so
     * higher-priority tasks (supervisor pulse, atmosphere RX,
     * button ISR service) can run between chunks. */
    for (uint32_t i = 0; i < total; i++) {
        if ((i % 1000) == 0) {
            putchar('.');
            fflush(stdout);
            reflex_task_yield();
        }
        uint32_t addr, mask;
        reflex_tryte9_t coord;
        goose_cell_type_t type;
        reflex_tryte9_t expected_coord = goose_make_shadow_coord(
            shadow_map[i].f, shadow_map[i].r, shadow_map[i].c);
        if (goose_shadow_resolve(shadow_map[i].name, &addr, &mask, &coord, &type) == REFLEX_OK
            && addr == shadow_map[i].addr
            && mask == shadow_map[i].bit_mask
            && type == shadow_map[i].type
            && goose_coord_equal(coord, expected_coord)) {
            ok++;
        } else {
            failed++;
            if (first_failure_idx == UINT32_MAX) first_failure_idx = i;
        }
    }
    putchar('\n');
    fflush(stdout);

    printf("ATLAS VERIFY: ok=%lu/%lu (100%% of SVD-documented MMIO catalog); duplicates=%lu, failures=%lu\n",
           (unsigned long)ok, (unsigned long)total,
           (unsigned long)duplicates, (unsigned long)failed);
    if (duplicates > 0 && first_dup_idx != UINT32_MAX) {
        printf("  first_duplicate: idx=%lu name='%s'\n",
               (unsigned long)first_dup_idx,
               shadow_map[first_dup_idx].name);
    }
    if (failed > 0 && first_failure_idx != UINT32_MAX) {
        printf("  first_failure: idx=%lu name='%s'\n",
               (unsigned long)first_failure_idx,
               shadow_map[first_failure_idx].name);
    }
    /* A verify that found duplicates or failures did not do the thing it was
     * asked to confirm, whatever it printed. */
    if (failed > 0 || duplicates > 0) outcome(SHELL_FAILED);
}

static void reflex_shell_loom_bloat_test(void) {
    /* Pressure the Loom by paging in far more shadow nodes than it can hold,
     * to exercise the round-robin eviction path.
     *
     * The names come from the atlas itself. An earlier version looped 300
     * times doing nothing, declared an unused table of guessed names, and then
     * resolved "agency.rmt.ch%d_conf0" for i%8 — eight distinct names, not the
     * 300 unique nodes it reported. Striding the real catalog spreads the
     * sample across peripherals and guarantees the names exist. */
    const int target_count = 300;
    int attempted = 0, resolved = 0;
    uint32_t evictions_before = goose_fabric_get_eviction_count();

    size_t stride = (shadow_map_count > (size_t)target_count)
                  ? (shadow_map_count / (size_t)target_count) : 1;

    printf("RED-TEAM: Loom Bloat — paging up to %d of %u shadow nodes (stride %u)...\n",
           target_count, (unsigned)shadow_map_count, (unsigned)stride);

    for (size_t i = 0; i < shadow_map_count && attempted < target_count; i += stride) {
        attempted++;
        if (goonies_resolve_cell(shadow_map[i].name) != NULL) resolved++;
    }

    printf("Bloat Test: attempted=%d resolved=%d evictions=%lu (this run: %lu)\n",
           attempted, resolved,
           (unsigned long)goose_fabric_get_eviction_count(),
           (unsigned long)(goose_fabric_get_eviction_count() - evictions_before));
}

// --- Command Handlers ---

typedef void (*shell_handler_t)(int argc, char *argv[]);

/* Role-Based Access (RBA): capability levels for shell commands.
 * Sessions default to admin (backward compatible). The `auth` command
 * allows voluntary capability restriction — the caller declares what
 * it is, the OS enforces the ceiling.
 *
 * The role *table* and the escalation rules live in `shell_policy.c` so the
 * host suite can test them; this file holds only the session state and the
 * handlers. See `shell_policy.h`. */
static uint8_t s_session_role = ROLE_ADMIN;

/* The outcome of the command currently being dispatched. Defaults to
 * SHELL_OK so a handler that simply does its job needs no change; anything
 * that refuses, rejects, or does nothing calls `outcome()` on the way out.
 * Reset per dispatch, reported as `#R:<trit>,<reason>` afterwards. */
static shell_reason_t s_outcome = SHELL_OK;
static void outcome(shell_reason_t r) { s_outcome = r; }

/* Map a driver return code onto the outcome trit.
 *
 * A handler that prints `rc=0x%x` and returns has told a human what happened
 * and told the SDK nothing: s_outcome is still SHELL_OK, so a failed NVS
 * commit or a radio send that never left the antenna both came back as
 * `#R:+1,ok`. These handlers predate the outcome marker and were not revisited
 * when it became the SDK's contract. */
static void outcome_rc(reflex_err_t rc) { if (rc != REFLEX_OK) outcome(SHELL_FAILED); }

/* The tokenizer splits on spaces and there is no quoting, so a command handed
 * more arguments than it consumes has lost operator intent rather than gained
 * anything: `purpose set my purpose` stored the name "my", discarded the rest,
 * and reported success. Refusing is the only honest answer until arguments can
 * contain spaces. @p expected counts argv[0]. */
static bool extra_args(int argc, int expected) {
    if (argc <= expected) return false;
    printf("too many arguments: expected %d, got %d "
           "(arguments cannot contain spaces)\n", expected - 1, argc - 1);
    outcome(SHELL_INVALID);
    return true;
}

typedef struct {
    const char *name;
    shell_handler_t handler;
} shell_cmd_t;

static void shell_cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("system:  help, reboot, sleep <s>, status, services, config <get|set>\n");
    printf("led:     led <on|off|status>\n");
    printf("fabric:  goonies <ls|find|read name>, atlas verify, temp, heartbeat\n");
    printf("purpose: purpose <set name|get|clear>, kernel (ternary stance)\n");
    printf("learn:   snapshot <save|load|clear>\n");
    printf("loom:    loom <list|fragments|evictions|load <hex>>\n");
    printf("signal:  tapestry signal <cell> <-1|0|1>  (non-sys cells only)\n");
    printf("mesh:    mesh <mac|emit|query|posture|stat|status|ping|peer add/ls>\n");
    printf("vm:      vm <info|run name|stop|list|loadhex hex>\n");
    printf("aura:    aura <setkey <32 hex chars>|clear>\n");
    printf("bonsai:  bonsai <exp1|exp2|exp3a|exp4|exp5|runtime> <start|status|...>\n");
    printf("telem:   telemetry <on|off>  (stream substrate state to host)\n");
    printf("vitals:  vitals [override <temp|battery|mesh|heap|pain|reward> <state>|clear]\n");
    printf("auth:    auth [role <observer|agent|operator|admin>]\n");
}

static void shell_cmd_status(int argc, char *argv[]) {
    (void)argc; (void)argv;
    const char *purpose = goose_purpose_get_name();
    uint8_t mac[6]; reflex_hal_mac_read(mac);
    uint64_t up = reflex_hal_time_us();
    printf("reflex-os uptime=%lu.%lus purpose=%s led=%s mac=%02x:%02x:%02x:%02x:%02x:%02x peers=%u\n",
           (unsigned long)(up / 1000000), (unsigned long)((up / 100000) % 10),
           (purpose && purpose[0]) ? purpose : "(none)",
           reflex_led_get() ? "on" : "off",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
           (unsigned)goose_mmio_sync_peer_count());
    uint32_t hold_count = goose_loom_hold_count();
    uint32_t avg_hold = hold_count > 0 ? (uint32_t)(goose_loom_hold_total_us() / hold_count) : 0;
    /* The peak carries its site and the uptime at which it happened. Reading
     * `max_us=1062` alone, nobody can tell a one-off boot-time bulk operation
     * from a recurring steady state — and two identical C6s on identical
     * firmware were measured at 331us and 1062us with no way to explain the
     * difference. `max_us=1062@0.4s(alloc)` answers it on sight. */
    printf("loom lock_holds=%lu max_us=%lu@%lu.%03lus(%s) avg_us=%lu evictions=%lu cells=%lu\n",
           (unsigned long)hold_count, (unsigned long)goose_loom_hold_max_us(),
           (unsigned long)(goose_loom_hold_max_at_us() / 1000000ULL),
           (unsigned long)((goose_loom_hold_max_at_us() / 1000ULL) % 1000ULL),
           goose_loom_hold_max_site(), (unsigned long)avg_hold,
           (unsigned long)goose_fabric_get_eviction_count(), (unsigned long)goonies_get_count());
    /* Free heap in bytes. perception.heap.pressure only carries a trit, which
     * is enough for the circuit breaker but useless for spotting a slow leak —
     * the thresholds sit at 8K/16K against ~300K free, so a leak is invisible
     * until it is already critical. Soak runs need the raw number. */
#if CONFIG_IDF_TARGET_ESP32C6
    /* Console input loss, printed only when it has happened.
     *
     * Reflex owns console RX now, so a full receive ring is Reflex's own
     * dropped byte rather than a driver's. Silent when healthy — a counter
     * that is always on screen at zero teaches people to stop reading it —
     * and impossible to miss when it is not. */
    {
        uint32_t dropped = reflex_hal_console_dropped();
        if (dropped) {
            printf("console: %lu byte(s) DROPPED — receive ring overran\n", (unsigned long)dropped);
        }
    }
#endif
    printf("heap free=%lu min_free=%lu\n",
           (unsigned long)esp_get_free_heap_size(),
           (unsigned long)esp_get_minimum_free_heap_size());
    uint16_t explore_discovered = goose_explore_active();
    uint16_t explore_pain = goose_explore_pain_ticks();
    const char *p = goose_purpose_get_name();
    bool exploring = (p && p[0] && goose_metabolic_get_state() >= 0);
    if (exploring) {
        printf("explore: %s cursor=%u/%u discovered=%u\n",
               explore_pain >= REFLEX_EXPLORE_PAIN_THRESHOLD ? "urgent" : "curious",
               (unsigned)goose_explore_cursor(),
               (unsigned)shadow_map_count, (unsigned)explore_discovered);
    } else {
        printf("explore: idle\n");
    }
}

static void shell_cmd_reboot(int argc, char *argv[]) {
    (void)argc; (void)argv;
    reflex_hal_reboot();
}

static void shell_cmd_sleep(int argc, char *argv[]) {
    int secs = (argc >= 2) ? atoi(argv[1]) : 3;
    if (secs < 1) secs = 1;
    printf("entering deep sleep for %d seconds\n", secs);
    fflush(stdout);
    reflex_hal_sleep_enter((uint64_t)secs * 1000000ULL);
}

static void shell_cmd_goonies_read(const char *name) {
    /* Try live cell first (may page in from shadow). */
    /* The Sanctuary Guard covers reads, not just agency binding — an MMIO read
     * is not free. Read-to-clear interrupt status registers and peripheral RX
     * FIFOs change state simply by being sampled, which is why
     * goose_supervisor_explore consults the same predicate before probing.
     * This command bypassed it entirely and would happily dereference any of
     * the 12738 catalog addresses, PMU and EFUSE included.
     *
     * Checked here, before goonies_resolve_cell, because that call is not a
     * lookup — it pages the entry into the Loom and binds the address. Doing it
     * the other way round (as the first version of this guard did) refused the
     * read but still left a live cell bound to the sanctuary register, which is
     * the more consequential half: a bound cell is reachable by routes and by
     * the supervisor pulse, not just by this command. goose_shadow_resolve is a
     * pure catalog lookup and allocates nothing. */
    {
        uint32_t s_addr, s_mask;
        reflex_tryte9_t s_coord;
        goose_cell_type_t s_type;
        if (goose_shadow_resolve(name, &s_addr, &s_mask, &s_coord, &s_type) == REFLEX_OK &&
            s_addr >= 0x60000000 && goose_fabric_addr_is_sanctuary(s_addr)) {
            printf("%s addr=0x%08lx [sanctuary — refused: reading it can clear "
                   "status bits or pop a live FIFO]\n",
                   name, (unsigned long)s_addr);
            outcome(SHELL_GUARD);
            return;
        }
    }

    goose_cell_t *cell = goonies_resolve_cell(name);
    /* A cell already resident from the boot weave can still hold a sanctuary
     * address, so the resident path is checked too. */
    if (cell && cell->hardware_addr >= 0x60000000 &&
        goose_fabric_addr_is_sanctuary(cell->hardware_addr)) {
        printf("%s addr=0x%08lx [sanctuary — refused]\n",
               name, (unsigned long)cell->hardware_addr);
        outcome(SHELL_GUARD);
        return;
    }
    if (cell && cell->hardware_addr >= 0x60000000) {
        volatile uint32_t *reg = (volatile uint32_t *)cell->hardware_addr;
        uint32_t mask = cell->bit_mask ? cell->bit_mask : 0xFFFFFFFF;
        uint32_t raw = *reg;
        uint32_t masked = raw & mask;
        printf("%s addr=0x%08lx raw=0x%08lx masked=0x%08lx ternary=%d\n",
               name, (unsigned long)cell->hardware_addr,
               (unsigned long)raw, (unsigned long)masked,
               masked ? 1 : -1);
        return;
    }
    if (cell && cell->hardware_addr > 0 && cell->hardware_addr < 32) {
        int level = reflex_hal_gpio_get_level(cell->hardware_addr);
        printf("%s gpio=%lu level=%d ternary=%d\n",
               name, (unsigned long)cell->hardware_addr, level, level ? 1 : -1);
        return;
    }
    /* Try shadow-only (no cell allocation). */
    uint32_t addr, mask;
    reflex_tryte9_t coord;
    goose_cell_type_t type;
    if (goose_shadow_resolve(name, &addr, &mask, &coord, &type) == REFLEX_OK && addr >= 0x60000000) {
        if (goose_fabric_addr_is_sanctuary(addr)) {
            printf("%s addr=0x%08lx [sanctuary — refused] [shadow]\n",
                   name, (unsigned long)addr);
            outcome(SHELL_GUARD);
            return;
        }
        volatile uint32_t *reg = (volatile uint32_t *)addr;
        uint32_t raw = *reg;
        uint32_t masked = raw & mask;
        printf("%s addr=0x%08lx raw=0x%08lx masked=0x%08lx ternary=%d [shadow]\n",
               name, (unsigned long)addr, (unsigned long)raw, (unsigned long)masked,
               masked ? 1 : -1);
        return;
    }
    printf("cannot read: %s (not a hardware register)\n", name); outcome(SHELL_NOTFOUND);
}

static void shell_cmd_goonies(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "ls") == 0) reflex_shell_loom_list();
    else if (argc >= 3 && strcmp(argv[1], "find") == 0) { if (extra_args(argc, 3)) return; reflex_shell_goonies_find(argv[2]); }
    else if (argc >= 3 && strcmp(argv[1], "read") == 0) { if (extra_args(argc, 3)) return; shell_cmd_goonies_read(argv[2]); }
}

static void shell_cmd_atlas(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "verify") == 0) reflex_shell_atlas_verify();
    else { printf("atlas <verify>\n"); outcome(SHELL_USAGE); }
}

static void shell_cmd_led(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "status") == 0) printf("led=%s\n", reflex_led_get()?"on":"off");
    else if (argc >= 2 && strcmp(argv[1], "on") == 0) { reflex_led_set(true); printf("led=on\n"); }
    else if (argc >= 2 && strcmp(argv[1], "off") == 0) { reflex_led_set(false); printf("led=off\n"); }
    else { printf("led <on|off|status>\n"); outcome(SHELL_USAGE); }
}

#if CONFIG_REFLEX_KERNEL_SCHEDULER
#include "reflex_sched.h"

/* Prove the Reflex-routed scheduler tick on hardware, without starting the
 * scheduler.
 *
 * The tick is the piece C2 was missing: reflex_sched_start could never wake a
 * blocked task because SYSTIMER TARGET1 was enabled at the peripheral and
 * routed nowhere. reflex_sched_tick_start routes it through Reflex's own
 * interrupt plumbing — not esp_intr_alloc — so what this measures is Reflex
 * delivering its own interrupt.
 *
 * Bounded on purpose: it starts the tick, samples the counter across a known
 * wall-clock interval, and stops it again. Nothing is handed to the scheduler,
 * so a tick that misfires costs a wrong number rather than a board that stops
 * answering. */
static void shell_cmd_kernel_tick(void) {
    uint32_t before = reflex_sched_get_tick();
    reflex_err_t rc = reflex_sched_tick_start();
    if (rc != REFLEX_OK) {
        printf("kernel tick: routing failed rc=0x%x\n", rc);
        outcome(SHELL_FAILED);
        return;
    }

    uint64_t t0 = reflex_hal_time_us();
    reflex_task_delay_ms(500);
    uint64_t elapsed_us = reflex_hal_time_us() - t0;

    uint32_t after = reflex_sched_get_tick();

    /* Capture the routing while it is still live. reflex_sched_tick_stop()
     * clears the interrupt-matrix entry and the peripheral enable, so a
     * readback taken after it describes the teardown rather than the failure —
     * which is exactly the mistake this readback was added to stop someone
     * making from the outside. */
    reflex_intr_route_t route;
    reflex_hal_intr_describe(REFLEX_INTR_SRC_SYSTIMER_TARGET1, &route);
    uint32_t st_ena = 0, st_raw = 0, st_st = 0;
    reflex_sched_tick_debug(&st_ena, &st_raw, &st_st);

    reflex_sched_tick_stop();

    uint32_t ticks = after - before;
    /* Report the measured rate rather than a pass/fail: a tick at the wrong
     * frequency is a different bug from a tick that never fires, and the
     * number says which. */
    unsigned hz = elapsed_us ? (unsigned)((uint64_t)ticks * 1000000u / elapsed_us) : 0;
    printf("kernel tick: %lu ticks in %lu us -> %u Hz (target %u)\n", (unsigned long)ticks,
           (unsigned long)elapsed_us, hz, (unsigned)REFLEX_SCHED_TICK_HZ);
    /* Report the routing rather than the conclusion, and report it whether or
     * not the tick fired.
     *
     * "Routed but not firing" names three or four possible causes and
     * distinguishes none of them; the registers distinguish all of them, and
     * they are three reads away. Every value is read back from hardware, not
     * remembered from setup, because the interesting failure is the one where
     * something else changed it.
     *
     * Printing only on failure was its own mistake: the working case was never
     * observed, so a claim about what the registers look like when it works
     * went into the record unmeasured. A diagnostic visible only when things
     * are broken cannot say what "not broken" looks like. */
    {
        printf("kernel tick: routing readback (taken live):\n");
        {
            /* Which CPU line the console's receive interrupt landed on.
             *
             * Reflex allocates an interrupt at boot for the console that it did
             * not when the tick was first measured, and the interrupt matrix
             * maps many sources onto few CPU lines. If two shared a line the
             * later allocation would replace the earlier one's vector, and the
             * tick would read as routed, enabled and asserting while being
             * delivered somewhere else — which is the signature a dead tick
             * shows. Checked, and they do not share: console on 10, tick on 11.
             * Kept because that question will be asked again every time a new
             * interrupt is allocated, and it costs one line to answer. */
            reflex_intr_route_t con;
            reflex_hal_intr_describe(REFLEX_INTR_SRC_USB_SERIAL_JTAG, &con);
            printf("  console: src=%lu -> cpu_int=%lu%s\n", (unsigned long)con.source,
                   (unsigned long)con.cpu_int,
                   (con.cpu_int == route.cpu_int) ? "  <-- SAME LINE AS THE TICK" : "");
        }
        printf("  intmtx: src=%lu -> cpu_int=%lu\n", (unsigned long)route.source,
               (unsigned long)route.cpu_int);
        printf("  plic:   enabled=%d pri=%lu thresh=%lu level=%d\n", (int)route.plic_enabled,
               (unsigned long)route.plic_priority, (unsigned long)route.plic_threshold,
               (int)route.level_triggered);
        printf("  csr:    mie_bit=%d mip_pending=%d mstatus.MIE=%d\n", (int)route.mie_enabled,
               (int)route.mip_pending, (int)route.global_ie);
        {
            unsigned n = 0;
            for (unsigned b = 0; b < 32; b++) {
                if (route.live_line_mask & (1u << b)) n++;
            }
            printf("  plic live mask=0x%08lx (%u lines, incl. this one)\n",
                   (unsigned long)route.live_line_mask, n);
        }
        printf("  systimer: ena=0x%08lx raw=0x%08lx st=0x%08lx (TARGET1=bit1)\n",
               (unsigned long)st_ena, (unsigned long)st_raw, (unsigned long)st_st);
    }
    if (ticks == 0) {
        printf("kernel tick: no ticks delivered\n");
        outcome(SHELL_FAILED);
    }
}
#endif

static void shell_cmd_kernel(int argc, char *argv[]) {
#if CONFIG_REFLEX_KERNEL_SCHEDULER
    if (argc >= 2 && strcmp(argv[1], "tick") == 0) {
        if (extra_args(argc, 2)) return;
        shell_cmd_kernel_tick();
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "selftest") == 0) {
        /* Hand this task to the Reflex scheduler and see whether it runs.
         *
         * reflex_sched_start does not return — it *is* the scheduler loop — so
         * this shell task becomes that loop and the shell is gone until the
         * board is reset. That is the point: it is the smallest thing that
         * answers the question C3 actually turns on, which is whether the
         * scheduler can run its own tasks at all. Two tasks print through ROM
         * output and delay on the scheduler's tick, so the answer arrives on
         * the console either way.
         *
         * Recoverable: FreeRTOS keeps running the rest of the system, so USB
         * stays up and a reflash resets the board. Admin-gated, because losing
         * the shell is at least as drastic as rebooting. */
        extern void reflex_kernel_test(void);
        printf("kernel selftest: handing this task to the Reflex scheduler\n");
        printf("  the shell does not come back — reset the board when done\n");
        fflush(stdout);
        reflex_kernel_test();
        printf("kernel selftest: scheduler returned, which it should not\n");
        outcome(SHELL_FAILED);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "wdt") == 0) {
        /* Arm or disarm the low-power watchdog, and read back whether it took.
         *
         * This exists to prove the recovery net for deep sleep before anything
         * depends on it. Arming and then not feeding it reboots the board on
         * purpose, which is why it is admin-only and why the hardware suite
         * does not touch it — that suite is documented as never rebooting.
         *
         * Deliberately observable rather than clever: `kernel wdt 5000` arms,
         * the shell keeps answering, and either the board comes back with its
         * uptime reset a few seconds later or the net does not work. Both
         * outcomes are safe, which is the point of testing it awake first. */
        if (argc == 3 && strcmp(argv[2], "off") == 0) {
            reflex_hal_wdt_disarm();
            printf("kernel wdt: disarmed (armed=%d)\n", (int)reflex_hal_wdt_armed());
            return;
        }
        if (argc == 3) {
            long ms = strtol(argv[2], NULL, 10);
            /* The floor is the point of this range, not the ceiling.
             *
             * The watchdog survives the reset it causes, and the disarm that
             * breaks the loop runs when the shell starts — about two seconds
             * into boot. A timeout shorter than that fires again before the
             * disarm is ever reached, and the board is then in a reset loop no
             * command can interrupt, because no command can be typed. 3000ms
             * leaves margin over the boot this was measured on. */
            if (ms < 3000 || ms > 60000) {
                printf("kernel wdt <3000..60000 ms|off>\n");
                printf("  below 3000ms the reset arrives before the boot-time "
                       "disarm and the loop cannot be broken\n");
                outcome(SHELL_USAGE);
                return;
            }
#if !defined(REFLEX_WDT_EXPERIMENT)
            /* Arming is disabled by default, and the evidence for that is
             * hard-won.
             *
             * The watchdog fires exactly as configured. What it does not do is
             * return a usable board. With stage action RESET_RTC the board then
             * reset every five to eight seconds indefinitely — the watchdog
             * itself reading back disarmed, so it was the state left behind and
             * not the watchdog re-firing — and only reflashing recovered it.
             * With RESET_SYSTEM it did not come back at all: no shell, and
             * esptool could not connect either ("Failed to connect to
             * ESP32-C6: No serial data received"), so the board needed a
             * physical power cycle.
             *
             * That is the opposite of a recovery net, and a command that
             * reliably bricks the board it exists to rescue has no business
             * being one keystroke away. Arming now requires a deliberate build:
             *   idf.py -DCMAKE_C_FLAGS=-DREFLEX_WDT_EXPERIMENT=1 build
             * Reading and disarming stay available, and the boot-time disarm in
             * main.c is what protects a board armed by such a build. */
            (void)ms;
            printf("kernel wdt: arming is disabled in this build\n");
            printf("  it fires, but does not return a usable board — RESET_RTC "
                   "loops it, RESET_SYSTEM strands it past esptool\n");
            printf("  rebuild with -DREFLEX_WDT_EXPERIMENT=1 to investigate\n");
            outcome(SHELL_FAILED);
            return;
#else
            reflex_err_t wrc = reflex_hal_wdt_arm((uint32_t)ms);
            if (wrc != REFLEX_OK) {
                printf("kernel wdt: arm failed rc=0x%x\n", wrc);
                outcome(SHELL_FAILED);
                return;
            }
            uint32_t c0 = 0, c1 = 0;
            reflex_hal_wdt_regs(&c0, &c1);
            printf("kernel wdt: armed %ldms, armed=%d conf0=0x%08lx conf1=%lu\n", ms,
                   (int)reflex_hal_wdt_armed(), (unsigned long)c0, (unsigned long)c1);
            return;
#endif
        }
        printf("kernel wdt <3000..60000 ms|off>\n");
        outcome(SHELL_USAGE);
        return;
    }
#endif
    (void)argc; (void)argv;
    goose_cell_t *agg = goonies_resolve_cell("sys.kernel.disposition");
    const char *pname = goose_purpose_get_name();
    printf("kernel disposition: %s (purpose=%s)\n",
           agg ? goose_kernel_disposition_name((reflex_trit_t)agg->state) : "(unpublished)",
           (pname && pname[0]) ? pname : "none");

    size_t n = goose_kernel_field_count();
    if (n == 0) { printf("  (no supervised fields)\n"); outcome(SHELL_NONE); return; }
    printf("%-18s | %-9s | %s\n", "Field", "Stance", "Trit");
    printf("-------------------+-----------+-----\n");
    for (size_t i = 0; i < n; i++) {
        reflex_trit_t d = goose_kernel_field_disposition(i);
        const char *fname = goose_kernel_field_name(i);
        printf("%-18s | %-9s | %2d\n",
               fname ? fname : "?", goose_kernel_disposition_name(d), (int)d);
    }
    printf("engaged=+1 serves the declared purpose; latent=0 undecided; "
           "withheld=-1 held back\n");
}

/* Say what the verb accepts instead of accepting nothing quietly.
 *
 * Every unmatched subcommand — a wrong verb, or a verb whose second word did
 * not match — fell off the end of the chain and returned #R:+1,ok having done
 * nothing. `bonsai nosuchthing` reported success. So did `bonsai exp4 bogus`,
 * and so did `bonsai exp5` without `run`, which is the easy mistake to make
 * because the other experiments take `start`. */
static void bonsai_usage(const char *what) {
    if (what) printf("bonsai: unknown subcommand '%s'\n", what);
    printf("bonsai <exp1|exp2|exp3a> <start|status>\n");
    printf("bonsai exp4 <connect|invert|detach>\n");
    printf("bonsai exp5 run\n");
    printf("bonsai <runtime|heal|gvm|sleep|weave|bloat>\n");
    outcome(SHELL_USAGE);
}

static void shell_cmd_bonsai(int argc, char *argv[]) {
    /* Only the exp* subcommands take a second word. Requiring argc >= 3 here
     * silently disabled every single-word subcommand — sleep, runtime, heal,
     * gvm, weave and bloat all returned without doing anything. `sub_arg`
     * defaults to "" so the two-word branches stay safe to compare. */
    if (argc < 2) {
        bonsai_usage(NULL);
        return;
    }
    const char *sub_arg = (argc >= 3) ? argv[2] : "";
    if (strcmp(argv[1], "exp1") == 0) {
        if (strcmp(sub_arg, "start") == 0)
            reflex_shell_bonsai_exp1_start();
        else if (strcmp(sub_arg, "status") == 0)
            reflex_shell_bonsai_exp1_status();
        else
            bonsai_usage(NULL);
    } else if (strcmp(argv[1], "exp2") == 0) {
        if (strcmp(sub_arg, "start") == 0)
            reflex_shell_bonsai_exp2_start();
        else if (strcmp(sub_arg, "status") == 0)
            reflex_shell_bonsai_exp2_status();
        else
            bonsai_usage(NULL);
    } else if (strcmp(argv[1], "exp3a") == 0) {
        if (strcmp(sub_arg, "start") == 0)
            reflex_shell_bonsai_exp3a_start();
        else if (strcmp(sub_arg, "status") == 0)
            reflex_shell_bonsai_exp3a_status();
        else
            bonsai_usage(NULL);
    } else if (strcmp(argv[1], "exp4") == 0) {
        if (strcmp(sub_arg, "connect") == 0)
            reflex_shell_bonsai_exp4_route(1);
        else if (strcmp(sub_arg, "invert") == 0)
            reflex_shell_bonsai_exp4_route(-1);
        else if (strcmp(sub_arg, "detach") == 0)
            reflex_shell_bonsai_exp4_route(0);
        else
            bonsai_usage(NULL);
    } else if (strcmp(argv[1], "exp5") == 0) {
        if (strcmp(sub_arg, "run") == 0)
            reflex_shell_bonsai_exp5_run();
        else
            bonsai_usage(NULL);
    } else if (strcmp(argv[1], "runtime") == 0) {
        reflex_shell_bonsai_runtime_test();
    } else if (strcmp(argv[1], "heal") == 0) {
        reflex_shell_bonsai_heal_test();
    } else if (strcmp(argv[1], "gvm") == 0) {
        reflex_shell_bonsai_gvm_test();
    } else if (strcmp(argv[1], "sleep") == 0) {
        reflex_shell_bonsai_deep_sleep();
    } else if (strcmp(argv[1], "weave") == 0) {
        reflex_shell_bonsai_weave_test();
    } else if (strcmp(argv[1], "bloat") == 0) {
        reflex_shell_loom_bloat_test();
    } else {
        bonsai_usage(argv[1]);
    }
}

/* Upload a compiled LoomScript fragment as hex and weave it.
 *
 * This is the path docs/prd-tasm-upload.md describes, and until now
 * goose_weave_loom had no caller at all — the linker discarded it, so its
 * parser was hardened by inspection rather than by ever running. Wiring it up
 * makes the validation reachable, which is the only way it can be trusted:
 * that function takes wholly untrusted input, and its bounds, index and NULL
 * checks are the thing standing between a malformed fragment and the fabric. */
static void shell_cmd_loom_load(const char *hex) {
    size_t hlen = strlen(hex);
    if (hlen < 2 || (hlen & 1)) { printf("loom load: even-length hex required\n"); outcome(SHELL_INVALID); return; }
    size_t blen = hlen / 2;
    uint8_t *buf = malloc(blen);
    if (!buf) { printf("loom load: alloc failed\n"); outcome(SHELL_FAILED); return; }
    size_t got = 0;
    if (!shell_parse_hex(hex, buf, blen, &got) || got != blen) {
        printf("loom load: invalid hex\n"); outcome(SHELL_INVALID); free(buf); return;
    }
    reflex_err_t rc = goose_weave_loom(buf, blen);
    free(buf);
    if (rc == REFLEX_OK) {
        printf("loom load: woven (%u fragment(s) active)\n",
               (unsigned)goose_loom_fragment_count());
    } else {
        printf("loom load: rejected rc=0x%x\n", rc); outcome(SHELL_FAILED);
    }
}

/* Reader for the eviction ring.
 *
 * The ring was written on every eviction since it was introduced and had no
 * reader anywhere in the tree, so the substrate paid an snprintf under
 * loom_authority to fill a buffer nothing consumed. This is that reader.
 *
 * It reports what the substrate is currently discarding — useful for confirming
 * that only shadow-paged and peer cells are ever chosen, and that seeds never
 * are. It deliberately does *not* claim to detect thrashing: eviction is
 * round-robin, so a re-paged cell lands behind the cursor and cannot be chosen
 * again for a full lap of the table. See the note in goose_runtime.c.
 *
 * The distinctness check is an invariant, not a load signal. Round-robin
 * guarantees the window holds distinct victims, so a repeat means the cursor
 * stopped advancing or the evictable set collapsed. It should never fire.
 *
 * The scan runs here, on read, so nothing is added to the path under the lock. */
static void shell_cmd_loom_evictions(void) {
    char ring[GOOSE_EVICTION_RING_SIZE][GOOSE_NAME_MAX];
    size_t count = 0;
    goose_fabric_get_eviction_ring(ring, &count);

    uint32_t total = goose_fabric_get_eviction_count();
    if (count == 0) {
        printf("loom evictions: total=%lu (none yet)\n", (unsigned long)total);
        outcome(SHELL_NONE);
        return;
    }

    size_t distinct = 0;
    for (size_t i = 0; i < count; i++) {
        bool seen = false;
        for (size_t j = 0; j < i; j++) {
            if (strncmp(ring[i], ring[j], GOOSE_NAME_MAX) == 0) { seen = true; break; }
        }
        if (!seen) distinct++;
    }

    printf("loom evictions: total=%lu recent=%u distinct=%u%s\n",
           (unsigned long)total, (unsigned)count, (unsigned)distinct,
           distinct < count
               ? "  ANOMALY: round-robin repeated a victim (eviction cursor or evictable set)"
               : "");
    for (size_t i = 0; i < count; i++) {
        printf("  %u: %s\n", (unsigned)(i + 1), ring[i]);
    }
}

static void shell_cmd_loom(int argc, char *argv[]) {
    if (argc >= 3 && strcmp(argv[1], "load") == 0) {
        if (extra_args(argc, 3)) return;
        shell_cmd_loom_load(argv[2]);
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "evictions") == 0) {
        shell_cmd_loom_evictions();
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "fragments") == 0) {
        printf("loom fragments: %u active\n", (unsigned)goose_loom_fragment_count());
        return;
    }
    if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        reflex_shell_loom_list();
        return;
    }
    printf("loom <list|fragments|evictions|load <hex>>\n");
    outcome(SHELL_USAGE);
}

static void shell_cmd_tapestry(int argc, char *argv[]) {
    if (argc >= 4 && strcmp(argv[1], "signal") == 0) {
        if (extra_args(argc, 4)) return;
        reflex_shell_tapestry_signal(argv[2], argv[3]);
    } else {
        printf("tapestry signal <cell> <-1|0|1>\n"); outcome(SHELL_USAGE);
    }
}

static void shell_cmd_services(int argc, char *argv[]) {
    (void)argc; (void)argv;
    size_t c = reflex_service_get_count(); printf("services=%zu\n", c);
    for(size_t i=0; i<c; i++) { const reflex_service_desc_t *s = reflex_service_get_by_index(i); printf("%zu: %s\n", i, s->name); }
}

static void shell_cmd_config(int argc, char *argv[]) {
    if (argc >= 3 && strcmp(argv[1], "get") == 0) { if (extra_args(argc, 3)) return; reflex_shell_config_get(argv[2]); }
    else if (argc >= 4 && strcmp(argv[1], "set") == 0) { if (extra_args(argc, 4)) return; reflex_shell_config_set(argv[2], argv[3]); }
    else { printf("config <get <key>|set <key> <value>>\n"); outcome(SHELL_USAGE); }
}

static void shell_cmd_temp(int argc, char *argv[]) {
    (void)argc; (void)argv;
    float c = reflex_temp_get_celsius();
    goose_cell_t *cell = goonies_resolve_cell("perception.temp.reading");
    printf("temp=%.1fC state=%d\n", c, cell ? cell->state : 0);
}

static void shell_cmd_snapshot(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "save") == 0) {
        reflex_err_t rc = goose_snapshot_save();
        printf("snapshot save: rc=0x%x\n", rc); outcome_rc(rc);
    } else if (argc >= 2 && strcmp(argv[1], "load") == 0) {
        reflex_err_t rc = goose_snapshot_load();
        printf("snapshot load: rc=0x%x\n", rc); outcome_rc(rc);
    } else if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        reflex_err_t rc = goose_snapshot_clear();
        printf("snapshot clear: rc=0x%x\n", rc); outcome_rc(rc);
    } else {
        printf("snapshot <save|load|clear>\n"); outcome(SHELL_USAGE);
    }
}

static void shell_cmd_purpose(int argc, char *argv[]) {
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        if (extra_args(argc, 3)) return;
        goose_cell_t *p = goonies_resolve_cell("sys.purpose");
        if (!p) {
            reflex_tryte9_t coord = goose_make_coord(0, 0, 2);
            p = goose_fabric_alloc_cell("sys.purpose", coord, true);
        }
        if (p) {
            p->type = GOOSE_CELL_PURPOSE;
            p->state = 1;
            goose_purpose_set_name(argv[2]);
            TELEM_IF(goose_telem_purpose(argv[2]));
            printf("purpose: active, name=\"%s\" (persisted to NVS)\n", goose_purpose_get_name());
        } else {
            printf("purpose set: failed to allocate cell\n"); outcome(SHELL_FAILED);
        }
    } else if (argc >= 2 && strcmp(argv[1], "get") == 0) {
        goose_cell_t *p = goonies_resolve_cell("sys.purpose");
        const char *name = goose_purpose_get_name();
        if (p && p->state != 0) {
            printf("purpose: active, name=\"%s\"\n", name[0] ? name : "(unnamed)");
        } else {
            printf("purpose: inactive\n");
        }
    } else if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        goose_cell_t *p = goonies_resolve_cell("sys.purpose");
        if (p) p->state = 0;
        goose_purpose_clear();
        TELEM_IF(goose_telem_purpose(NULL));
        printf("purpose: cleared\n");
    } else {
        printf("purpose <set name|get|clear>\n"); outcome(SHELL_USAGE);
    }
}

static void shell_cmd_heartbeat(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("lp_pulse_count=%lu\n", (unsigned long)goose_lp_heartbeat_count());
}

static void shell_cmd_mesh(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "mac") == 0) {
        uint8_t mac[6];
        reflex_hal_mac_read(mac);
        printf("mac=%02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else if (argc >= 3 && strcmp(argv[1], "emit") == 0) {
        if (extra_args(argc, 3)) return;
        goose_cell_t *c = goonies_resolve_cell("agency.led.intent");
        if (!c) { printf("mesh emit: agency.led.intent not resolved\n"); outcome(SHELL_NOTFOUND); return; }
        int8_t req;
        if (!shell_parse_trit(argv[2], &req)) {
            printf("mesh emit: state must be -1, 0, or 1\n");
            outcome(SHELL_INVALID);
            return;
        }
        /* Broadcast from a stack-local copy so we don't mutate the
         * real cell as a side effect (that would toggle the physical
         * LED whenever someone ran `mesh emit` for a transmission
         * test). The copy carries the requested state and the
         * original coord, which is all goose_atmosphere_emit_arc
         * reads from it. */
        goose_cell_t tx = *c;
        tx.state = (int8_t)req;
        reflex_err_t rc = goose_atmosphere_emit_arc(&tx);
        printf("mesh emit: state=%d rc=0x%x\n", (int)tx.state, rc); outcome_rc(rc);
    } else if (argc >= 3 && strcmp(argv[1], "query") == 0) {
        if (extra_args(argc, 3)) return;
        reflex_err_t rc = goose_atmosphere_query(argv[2]);
        if (rc == REFLEX_ERR_TIMEOUT) {
            /* Suppressed by the 10 Hz egress gate, not a radio failure. Say so,
             * because an opaque rc here reads as a broken link — and report it
             * as a guard refusal rather than a failure, because the command was
             * declined by a protective limit and never reached the radio. */
            printf("mesh query: name=%s throttled (10Hz egress limit)\n", argv[2]);
            outcome(SHELL_GUARD);
        } else {
            printf("mesh query: name=%s rc=0x%x\n", argv[2], rc);
            outcome_rc(rc);
        }
    } else if (argc >= 4 && strcmp(argv[1], "posture") == 0) {
        /* The state goes onto the radio and is multiplied by the weight into
         * every peer's swarm accumulator. Unvalidated, `mesh posture 99 4`
         * moved a peer by 396 against a threshold of 10 — a one-packet
         * consensus flip, which is precisely what the inertial hysteresis in
         * SECURITY.md 7 exists to prevent. */
        if (extra_args(argc, 4)) return;
        int8_t state;
        long weight_in;
        if (!shell_parse_trit(argv[2], &state)) {
            printf("mesh posture: state must be -1, 0 or 1\n"); outcome(SHELL_INVALID); return;
        }
        if (!shell_parse_int(argv[3], 0, 255, &weight_in)) {
            printf("mesh posture: weight must be 0..255 (clamped to %d)\n",
                   REFLEX_SWARM_WEIGHT_MAX); outcome(SHELL_INVALID); return;
        }
        uint8_t weight = (uint8_t)weight_in;
        reflex_err_t rc = goose_atmosphere_emit_posture(state, weight);
        printf("mesh posture: state=%d weight=%u rc=0x%x\n", state, weight, rc); outcome_rc(rc);
    } else if (argc >= 2 && strcmp(argv[1], "stat") == 0) {
        /* Print every counter the struct carries. DISCOVER and MMIO_SYNC were
         * omitted here, and they are precisely the ops the supervisor emits on
         * its own — so a healthy two-board mesh (peers registered, aura_fail
         * zero, mesh vital recovered) still reported all-zeros, which reads as
         * a dead mesh. */
        goose_mesh_stats_t s = goose_atmosphere_get_stats();
        printf("rx_sync=%lu rx_query=%lu rx_advertise=%lu rx_posture=%lu\n",
               (unsigned long)s.rx_sync, (unsigned long)s.rx_query,
               (unsigned long)s.rx_advertise, (unsigned long)s.rx_posture);
        printf("rx_discover=%lu tx_discover=%lu rx_mmio_sync=%lu tx_mmio_sync=%lu\n",
               (unsigned long)s.rx_discover, (unsigned long)s.tx_discover,
               (unsigned long)s.rx_mmio_sync, (unsigned long)s.tx_mmio_sync);
        printf("tx_sync=%lu tx_query=%lu tx_advertise=%lu tx_posture=%lu\n",
               (unsigned long)s.tx_sync, (unsigned long)s.tx_query,
               (unsigned long)s.tx_advertise, (unsigned long)s.tx_posture);
        printf("version_mismatch=%lu aura_fail=%lu replay_drop=%lu self_drop=%lu\n",
               (unsigned long)s.rx_version_mismatch, (unsigned long)s.rx_aura_fail,
               (unsigned long)s.rx_replay_drop, (unsigned long)s.rx_self_drop);
        printf("malformed=%lu query_throttled=%lu\n", (unsigned long)s.rx_malformed,
               (unsigned long)s.tx_query_throttled);
    } else if (argc >= 4 && strcmp(argv[1], "peer") == 0 && strcmp(argv[2], "add") == 0) {
        if (argc < 5) { printf("mesh peer add <name> <mac_hex>\n"); outcome(SHELL_USAGE); return; }
        if (extra_args(argc, 5)) return;
        const char *pname = argv[3];
        const char *hex = argv[4];
        uint8_t mac[6];
        if (strlen(hex) != 17) { printf("mesh peer add: mac format XX:XX:XX:XX:XX:XX\n"); outcome(SHELL_INVALID); return; }
        bool mac_ok = true;
        for (int i = 0; i < 5; i++) { if (hex[i*3+2] != ':') mac_ok = false; }
        if (!mac_ok) { printf("mesh peer add: mac format XX:XX:XX:XX:XX:XX\n"); outcome(SHELL_INVALID); return; }
        /* Format-checked above, but the pairs themselves were decoded with
         * strtoul, so `zz:zz:zz:zz:zz:zz` passed and registered a peer at
         * 00:00:00:00:00:00. */
        for (int i = 0; i < 6; i++) {
            char p[3] = {hex[i*3], hex[i*3+1], 0};
            size_t n = 0;
            if (!shell_parse_hex(p, &mac[i], 1, &n) || n != 1) {
                printf("mesh peer add: mac format XX:XX:XX:XX:XX:XX\n"); outcome(SHELL_INVALID); return;
            }
        }
        reflex_err_t rc = goose_mmio_sync_add_peer(pname, mac);
        if (rc == REFLEX_ERR_INVALID_SIZE) {
            /* Name the bound rather than printing a bare rc: the registry
             * refuses an oversized name now instead of storing a truncated
             * one, and the operator needs to know what will fit. */
            printf("mesh peer add: name too long (max %u chars)\n",
                   (unsigned)(sizeof(((reflex_peer_t *)0)->name) - 1));
            outcome(SHELL_INVALID); return;
        }
        /* Echo the stored name, not argv. These were the same string only for
         * as long as nothing rejected or altered it. */
        const reflex_peer_t *added = NULL;
        for (size_t i = 0; i < goose_mmio_sync_peer_count(); i++) {
            const reflex_peer_t *q = goose_mmio_sync_get_peer(i);
            if (q && memcmp(q->mac, mac, 6) == 0) { added = q; break; }
        }
        printf("mesh peer add: %s %02x:%02x:%02x:%02x:%02x:%02x rc=0x%x\n",
               added ? added->name : pname,
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rc);
        outcome_rc(rc);
    } else if (argc >= 3 && strcmp(argv[1], "peer") == 0 && strcmp(argv[2], "ls") == 0) {
        size_t count = goose_mmio_sync_peer_count();
        if (count == 0) { printf("no peers\n"); outcome(SHELL_NONE); return; }
        for (size_t i = 0; i < count; i++) {
            const reflex_peer_t *p = goose_mmio_sync_get_peer(i);
            if (!p) continue;
            uint64_t age_us = reflex_hal_time_us() - p->last_seen_us;
            printf("  %u: %-11s %02x:%02x:%02x:%02x:%02x:%02x  %s (last: %lu.%lus ago)\n",
                   (unsigned)(i+1), p->name,
                   p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5],
                   p->active ? "active" : "stale",
                   (unsigned long)(age_us / 1000000), (unsigned long)((age_us / 100000) % 10));
        }
    } else if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        goose_mesh_stats_t s = goose_atmosphere_get_stats();
        size_t peers = goose_mmio_sync_peer_count();
        /* Both totals omitted DISCOVER, which is the op the supervisor emits
         * on its own — the same omission `mesh stat` was fixed for, left
         * behind here. tx_total counted MMIO_SYNC alone, so a board that had
         * been broadcasting discovery beacons since boot reported tx=0. The
         * emit paths now each carry a counter, so `tx` means every arc this
         * board put on the air rather than one op's worth. */
        uint32_t rx_total = s.rx_sync + s.rx_query + s.rx_advertise + s.rx_posture +
                            s.rx_mmio_sync + s.rx_discover;
        uint32_t tx_total = s.tx_sync + s.tx_query + s.tx_advertise + s.tx_posture +
                            s.tx_mmio_sync + s.tx_discover;
        printf("mesh: peers=%u rx=%lu tx=%lu sync=%lu mmio_sync_rx=%lu mmio_sync_tx=%lu\n",
               (unsigned)peers, (unsigned long)rx_total, (unsigned long)tx_total,
               (unsigned long)s.rx_sync, (unsigned long)s.rx_mmio_sync, (unsigned long)s.tx_mmio_sync);
        for (size_t i = 0; i < peers; i++) {
            const reflex_peer_t *p = goose_mmio_sync_get_peer(i);
            if (!p) continue;
            uint64_t age_us = reflex_hal_time_us() - p->last_seen_us;
            printf("  %s %s (last: %lu.%lus)\n", p->name, p->active ? "active" : "stale",
                   (unsigned long)(age_us / 1000000), (unsigned long)((age_us / 100000) % 10));
        }
    } else if (argc >= 2 && strcmp(argv[1], "ping") == 0) {
        goose_cell_t *c = goonies_resolve_cell("agency.led.intent");
        if (!c) { printf("mesh ping: no agency cell\n"); outcome(SHELL_NOTFOUND); return; }
        goose_cell_t tx = *c;
        tx.state = 1;
        reflex_err_t rc = goose_atmosphere_emit_arc(&tx);
        printf("mesh ping: broadcast rc=0x%x\n", rc); outcome_rc(rc);
    } else {
        printf("mesh <mac|emit|query|posture|stat|status|ping|peer add/ls>\n"); outcome(SHELL_USAGE);
    }
}

static void shell_cmd_aura(int argc, char *argv[]) {
    if (argc >= 3 && strcmp(argv[1], "setkey") == 0) {
        if (extra_args(argc, 3)) return;
        const char *hex = argv[2];
        if (strlen(hex) != 32) { printf("aura: expect 32 hex chars (16 bytes)\n"); outcome(SHELL_INVALID); return; }
        /* Nothing downstream validates these 16 bytes — they *are* the mesh
         * HMAC key. The previous strtoul() loop mapped any non-hex character
         * to 0 without signalling, so a typo silently provisioned a key with
         * zero bytes in those positions (all-zero in the worst case, a
         * guessable shared secret) and still reported success. */
        uint8_t key[16]; size_t klen = 0;
        if (!shell_parse_hex(hex, key, sizeof(key), &klen) || klen != sizeof(key)) {
            printf("aura: invalid hex — key not provisioned\n"); outcome(SHELL_INVALID); return;
        }
        if (goose_atmosphere_set_key(key) == REFLEX_OK) printf("aura: key provisioned\n");
        else { printf("aura: provisioning failed\n"); outcome(SHELL_FAILED); }
    } else if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        if (goose_atmosphere_clear_key() == REFLEX_OK)
            printf("aura: key cleared — a fresh per-board key is generated on next boot\n");
        else
            { printf("aura: clear failed\n"); outcome(SHELL_FAILED); }
    } else {
        printf("aura <setkey <32 hex chars>|clear>\n"); outcome(SHELL_USAGE);
    }
}

static void shell_cmd_vm(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "info") == 0) {
        printf("vm status=%s ip=%lu steps=%lu program=%s\n",
               reflex_shell_vm_status_name(reflex_shell_vm.status),
               (unsigned long)reflex_shell_vm.ip,
               (unsigned long)reflex_shell_vm.steps_executed,
               reflex_shell_vm_loaded ? "loaded" : "none");
    } else if (argc >= 3 && strcmp(argv[1], "run") == 0) {
        if (extra_args(argc, 3)) return;
        const vm_program_t *prog = vm_program_find(argv[2]);
        if (!prog) { printf("vm run: program '%s' not found\n", argv[2]); outcome(SHELL_NOTFOUND); return; }
        reflex_err_t rc = reflex_vm_load_binary(&reflex_shell_vm, prog->data, prog->len);
        if (rc != REFLEX_OK) { printf("vm run: load failed rc=0x%x\n", rc); outcome(SHELL_FAILED); return; }
        reflex_shell_vm_loaded = true;
        reflex_vm_use_default_syscalls(&reflex_shell_vm);
        rc = reflex_vm_run(&reflex_shell_vm, 100000);
        printf("vm run: %s status=%s steps=%lu\n", argv[2],
               reflex_shell_vm_status_name(reflex_shell_vm.status),
               (unsigned long)reflex_shell_vm.steps_executed);
    } else if (argc >= 2 && strcmp(argv[1], "stop") == 0) {
        reflex_shell_vm.status = REFLEX_VM_STATUS_HALTED;
        printf("vm stopped\n");
    } else if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        if (vm_program_registry_len == 0) { printf("no embedded programs\n"); outcome(SHELL_NONE); return; }
        for (size_t i = 0; i < vm_program_registry_len; i++) {
            const vm_program_t *p = vm_program_get(i);
            if (p) printf("  %s (%u bytes)\n", p->name, (unsigned)p->len);
        }
    } else if (argc >= 3 && strcmp(argv[1], "loadhex") == 0) {
        if (extra_args(argc, 3)) return;
        size_t hlen = strlen(argv[2]);
        if (hlen < 2 || (hlen & 1)) { printf("vm loadhex: even hex string required\n"); outcome(SHELL_INVALID); return; }
        size_t blen = hlen / 2;
        uint8_t *b = malloc(blen);
        if (!b) { printf("vm loadhex: alloc failed\n"); outcome(SHELL_FAILED); return; }
        size_t got = 0;
        if (!shell_parse_hex(argv[2], b, blen, &got) || got != blen) {
            printf("vm loadhex: invalid hex\n"); outcome(SHELL_INVALID); free(b); return;
        }
        if(reflex_vm_load_binary(&reflex_shell_vm, b, blen)==REFLEX_OK) { reflex_shell_vm_loaded=true; printf("vm loaded\n"); } else { printf("vm load failed\n"); outcome(SHELL_FAILED); }
        free(b);
    } else {
        printf("vm <info|run name|stop|list|loadhex hex>\n");
    }
}

static void shell_cmd_vitals(int argc, char *argv[]) {
    if (argc >= 4 && strcmp(argv[1], "override") == 0) {
        if (extra_args(argc, 4)) return;
        int8_t state;
        if (!shell_parse_trit(argv[3], &state)) { printf("state must be -1, 0 or 1\n"); outcome(SHELL_INVALID); return; }

        /* sys.ai.pain and sys.ai.reward are not metabolic vitals — they are the
         * autonomous evaluation signals. They are injectable here anyway because
         * nothing else could set them: goose_supervisor_evaluate raises pain only
         * after a HARDWARE_OUT route stays stuck for REFLEX_AUTO_PAIN_STUCK_TICKS
         * under an active purpose, which is not something a bench operator can
         * arrange on demand. That left the pain-derived WITHHELD disposition the
         * one scheduling stance never exercised on hardware. */
        if (strcmp(argv[2], "pain") == 0 || strcmp(argv[2], "reward") == 0) {
            const char *cell_name = (argv[2][0] == 'p') ? "sys.ai.pain" : "sys.ai.reward";
            goose_cell_t *c = goonies_resolve_cell(cell_name);
            if (!c) { printf("override: %s not resolvable\n", cell_name); outcome(SHELL_NOTFOUND); return; }
            c->state = state;
            printf("override: %s=%d\n", cell_name, (int)state);
            return;
        }

        if (goose_metabolic_override(argv[2], state) == REFLEX_OK) {
            printf("override: %s=%d\n", argv[2], (int)state);
        } else {
            printf("unknown vital: %s (use temp, battery, mesh, heap, pain, reward)\n", argv[2]); outcome(SHELL_INVALID);
        }
    } else if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        goose_metabolic_clear_overrides();
        printf("overrides cleared\n");
    } else {
        char buf[128];
        goose_metabolic_format_vitals(buf, sizeof(buf));
        printf("%s\n", buf);
    }
}

static void shell_cmd_telemetry(int argc, char *argv[]) {
    if (argc >= 2 && strcmp(argv[1], "on") == 0) {
        goose_telemetry_enabled = true;
        printf("telemetry: enabled\n");
    } else if (argc >= 2 && strcmp(argv[1], "off") == 0) {
        goose_telemetry_enabled = false;
        printf("telemetry: disabled\n");
    } else {
        printf("telemetry: %s\n", goose_telemetry_enabled ? "on" : "off");
    }
}

static void shell_cmd_auth(int argc, char *argv[]) {
    if (argc >= 3 && strcmp(argv[1], "role") == 0) {
        if (extra_args(argc, 3)) return;
        for (int i = 0; i <= ROLE_ADMIN; i++) {
            if (strcmp(argv[2], shell_role_names[i]) == 0) {
                s_session_role = (uint8_t)i;
                printf("role: %s\n", shell_role_names[i]);
                TELEM_IF(goose_telem_auth(shell_role_names[i]));
                return;
            }
        }
        printf("unknown role: %s (observer|agent|operator|admin)\n", argv[2]);
    } else {
        printf("role: %s\n", shell_role_names[s_session_role]);
    }
}

// --- Dispatch Table ---

/* Name to handler only. The role each command requires lives in
 * `shell_policy.c` — a command missing from that table fails closed to admin
 * rather than defaulting to observer. */
static const shell_cmd_t s_commands[] = {
    {"help",      shell_cmd_help},
    {"status",    shell_cmd_status},
    {"reboot",    shell_cmd_reboot},
    {"sleep",     shell_cmd_sleep},
    {"goonies",   shell_cmd_goonies},
    {"atlas",     shell_cmd_atlas},
    {"led",       shell_cmd_led},
    {"bonsai",    shell_cmd_bonsai},
    {"kernel",    shell_cmd_kernel},
    {"loom",      shell_cmd_loom},
    {"tapestry",  shell_cmd_tapestry},
    {"services",  shell_cmd_services},
    {"config",    shell_cmd_config},
    {"temp",      shell_cmd_temp},
    {"snapshot",  shell_cmd_snapshot},
    {"purpose",   shell_cmd_purpose},
    {"heartbeat", shell_cmd_heartbeat},
    {"mesh",      shell_cmd_mesh},
    {"aura",      shell_cmd_aura},
    {"vm",        shell_cmd_vm},
    {"telemetry", shell_cmd_telemetry},
    {"vitals",    shell_cmd_vitals},
    {"auth",      shell_cmd_auth},
    {NULL, NULL}
};

static void reflex_shell_dispatch(int argc, char *argv[]) {
    if (argc == 0) return;
    outcome(SHELL_OK);
    for (const shell_cmd_t *cmd = s_commands; cmd->name; cmd++) {
        if (strcmp(argv[0], cmd->name) == 0) {
            uint8_t required = shell_required_role(cmd->name, argc, argv);
            if (s_session_role < required) {
                printf("denied: requires %s\n", shell_role_names[required]);
                outcome(SHELL_DENIED);
                return;
            }
            cmd->handler(argc, argv);
            return;
        }
    }
    printf("unknown command: %s\n", argv[0]);
    outcome(SHELL_NOTFOUND);
}

/* Echo one character on the console.
 *
 * Not putchar+fflush. That routes through the stdio VFS, whose write blocks
 * until the host drains the IN endpoint — and a host driving this shell writes
 * a whole line before it reads anything. With Reflex applying receive
 * backpressure, that closes a loop: the device stops ACKing OUT because its
 * ring is full, the host blocks mid-write and so never reads, the IN endpoint
 * fills, and the shell blocks here and never calls the read that would re-arm
 * the receiver. Both ends wait for the other.
 *
 * reflex_hal_write_raw spins a bounded number of times and then drops the byte,
 * because console output is best-effort by nature: if nobody is listening,
 * dropping is correct and hanging is not. Bounded is the property that breaks
 * the deadlock — the shell always returns to its loop, always drains the ring,
 * and always re-arms receive. */
/* Write straight at the endpoint while holding the lock everything else uses.
 *
 * Bypassing stdio is what makes the prompt leave the chip, but it also leaves
 * the shell's own output outside the mutual exclusion that had been keeping it
 * apart from the supervisor task's logging. Both write the same FIFO. The
 * result is a log line landing inside a command's response — measured, not
 * feared: with these writes unlocked the hardware suite failed roughly three
 * runs in ten, once with "I (GOOSE_SUPERVISOR) snapshot saved" spliced into
 * the middle of a reply, where main failed none in five.
 *
 * flockfile is the same lock printf takes, so holding it across the flush and
 * the direct write orders the two paths against every other writer without
 * inventing a second lock for them to disagree with.
 *
 * Deliberately not a critical section: reflex_hal_write_raw waits up to 50 ms
 * for a host that is not reading, and no interrupt should be held off for
 * that. */
static void console_write_locked(const char *text, int len) {
    flockfile(stdout);
    fflush(stdout);
    reflex_hal_write_raw(text, len);
    funlockfile(stdout);
}

/* Echo through a small buffer rather than a write per character.
 *
 * reflex_hal_write_raw ends every call by setting WR_DONE, which hands the
 * bytes to the USB endpoint as a packet. One character per packet means one
 * host frame per character, so a thirteen-character command takes thirteen
 * milliseconds to appear — and a host that reads on a timeout sees the echo
 * arrive split across reads, with a line's first character landing in one and
 * the rest in the next. Against the hardware suite that reads as garbled
 * responses ("AccessDenied: urpose set x"), which is not corruption: every
 * byte is present and in order, just spread across more reads than the caller
 * expected.
 *
 * Buffering collapses a typed line into a packet or two. It is flushed on
 * end-of-line, when full, and whenever the input ring runs dry, so a partly
 * typed line still appears immediately to a human at a terminal. */
static char s_echo_buf[64];
static int s_echo_len;

static void shell_echo_flush(void) {
    if (s_echo_len > 0) {
        /* Drain stdio first. Command output goes through buffered printf and
         * echo goes straight at the endpoint register, so without this the two
         * paths interleave by whoever flushes first — and the direct one wins.
         * The visible effect is a reply arriving after the echo of the command
         * that came *after* it, which reads from the host as every response
         * being one behind: the suite sends a command, reads, and is handed the
         * previous command's text followed by its own echo. Ordering, not loss;
         * every byte was there, in the wrong sequence. */
        console_write_locked(s_echo_buf, s_echo_len);
        s_echo_len = 0;
    }
}

/* Emit the prompt ourselves rather than leaving it to printf.
 *
 * ESP-IDF's console, with no driver installed, hands bytes to the endpoint but
 * only marks the packet complete when it sees a newline. "reflex> " has none,
 * so an fflush is not enough: the prompt reaches the hardware FIFO and stays
 * there, unsent, until some later write marks a packet done — which is the
 * echo of the *next* command. From the host every reply then looks one behind,
 * because the prompt that ends an exchange arrives at the head of the one
 * after it. Measured directly: `auth role admin` returned its full reply and
 * its outcome marker, then nothing for eight seconds, and the missing prompt
 * turned up prefixed to the following command.
 *
 * reflex_hal_write_raw completes the packet unconditionally, so the prompt
 * leaves when it is written. stdout is drained first to keep the two paths in
 * order. */
static void shell_prompt(const char *text, int len) {
    console_write_locked(text, len);
}

static void shell_echo(char c) {
    if (c == '\n') {
#if !CONFIG_IDF_TARGET_ESP32C6
        /* Where ESP-IDF still owns the console it translates outgoing newlines
         * itself, so adding the carriage return here produces "\r\r\n" and the
         * suite's wire-format check catches it. Only the target whose console
         * Reflex owns has to supply its own. */
        if (s_echo_len == (int)sizeof s_echo_buf) shell_echo_flush();
        s_echo_buf[s_echo_len++] = '\n';
        shell_echo_flush();
        return;
#endif
        /* CRLF, because the rest of the console emits CRLF.
         *
         * The removed ESP-IDF driver was translating outgoing newlines, so
         * echoing a bare LF here puts the command and the first line of its
         * response on one line for any terminal that needs the carriage
         * return — and the suite checks for exactly that, since a response
         * sharing a line with its echo is unreadable and unparseable alike. */
        if (s_echo_len + 2 > (int)sizeof s_echo_buf) shell_echo_flush();
        s_echo_buf[s_echo_len++] = '\r';
        s_echo_buf[s_echo_len++] = '\n';
        shell_echo_flush();
        return;
    }
    if (s_echo_len == (int)sizeof s_echo_buf) shell_echo_flush();
    s_echo_buf[s_echo_len++] = c;
}

void reflex_shell_run(void) {
    char line[REFLEX_SHELL_LINE_MAX]; size_t len = 0; bool overflowed = false;
#if CONFIG_IDF_TARGET_ESP32C6
    bool s_last_was_cr = false;
#endif
#if CONFIG_IDF_TARGET_ESP32C6
    /* Reflex owns console receive on this target.
     *
     * This installed ESP-IDF's USB-serial-JTAG driver and read through it. The
     * driver was needed because the shell idles 50ms between polls while the
     * 64-byte FIFO fills in about 5.5ms at 115200 baud, so a line arriving in
     * an idle window was truncated before the shell ever saw it — silently,
     * because the overflow guard cannot trip on characters that never arrived.
     *
     * An interrupt solves that without the driver: the ISR drains the FIFO into
     * Reflex's own ring as packets land, so the idle delay costs latency and
     * never a byte. Installing the driver alongside would race it for the same
     * FIFO, so it is not installed at all.
     *
     * Transmit is untouched. printf still reaches the wire through the console
     * VFS that ESP-IDF's startup registers, which is not this driver, and
     * REFLEX_LOG* and telemetry already bypass stdio entirely through
     * reflex_hal_write_raw's direct register writes. */
    if (reflex_hal_console_init() != REFLEX_OK) {
        printf("console: RX interrupt unavailable; input will not work\n");
    }
#else
    /* UART console. Without a driver-managed ring buffer, getchar() reads
     * straight out of the 128-byte hardware FIFO, and a line longer than the
     * FIFO loses characters *before* the shell can see them — so the overflow
     * guard below never trips and a truncated line dispatches as though
     * complete. Same silent truncation the USB-JTAG RX sizing fixes on the
     * C6, one layer lower. */
    if (!uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM)) {
        uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, REFLEX_SHELL_LINE_MAX * 2, 0, 0, NULL, 0);
        uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    }
#endif
    shell_prompt("reflex> ", 8);
    while (1) {
#if CONFIG_IDF_TARGET_ESP32C6
        uint8_t ch;
        int r = reflex_hal_console_read(&ch) ? 1 : 0;
#else
        int ch = getchar(); int r = (ch != EOF) ? 1 : 0;
#endif

        if (r <= 0) {
            shell_echo_flush();
#if CONFIG_IDF_TARGET_ESP32C6 && defined(REFLEX_CONSOLE_RX_DIAG)
            /* Off by default; build with -DREFLEX_CONSOLE_RX_DIAG=1 to enable.
             *
             * Kept rather than deleted because it is the only way to
             * interrogate a board that has stopped answering commands:
             * transmit and receive share one interrupt line, so the failures
             * that break input tend to break the shell's ability to report
             * them. This reports from the idle path, over transmit, whether
             * the handler ran, how many bytes it buffered, and whether anyone
             * consumed them — which separates three faults that otherwise look
             * identical from the host. It found the last one. */
            static uint32_t idle_ticks;
            if ((idle_ticks++ % 40u) == 0u) {
                reflex_console_debug_t d;
                reflex_hal_console_debug(&d);
                char line[160];
                int n = snprintf(line, sizeof line,
                                 "[rxdiag] inst=%d isr=%lu bytes=%lu head=%lu tail=%lu "
                                 "ena=0x%08lx raw=0x%08lx conf=0x%08lx\n",
                                 (int)d.installed, (unsigned long)d.isr_count,
                                 (unsigned long)d.isr_bytes, (unsigned long)d.head,
                                 (unsigned long)d.tail, (unsigned long)d.int_ena,
                                 (unsigned long)d.int_raw, (unsigned long)d.ep1_conf);
                if (n > 0)
                    reflex_hal_write_raw(line, n < (int)sizeof line ? n : (int)sizeof line - 1);
            }
#endif
            reflex_task_delay_ms(50);
            continue;
        }
#if CONFIG_IDF_TARGET_ESP32C6
        /* Raw bytes now, so line endings are ours to normalise.
         *
         * The removed usb_serial_jtag_vfs_set_rx_line_endings(CRLF) was doing
         * this: with the driver gone the ISR delivers exactly what the host
         * sent, and a host sending CRLF would otherwise leave a bare CR at the
         * end of every line — appended to the buffer, dispatched as part of
         * the command, and matching no verb. Treat CR as end-of-line and
         * swallow a following LF. */
        bool this_was_cr = (ch == '\r');
        if (this_was_cr) {
            ch = '\n';
        } else if (ch == '\n' && s_last_was_cr) {
            /* The LF half of a CRLF pair the CR already ended the line for. */
            s_last_was_cr = false;
            continue;
        }
        /* Track whether the *raw byte* was CR, not whether the result was LF.
         * Setting this from the converted value marked every line ending as a
         * pending CR, so the next command's bare LF was swallowed as the second
         * half of a CRLF pair that never existed — every second command lost
         * its newline. Measured as 86 passed / 84 failed, an almost exact half,
         * which is what a one-in-two failure looks like. */
        s_last_was_cr = this_was_cr;
#endif
        if (ch == '\n') {
            /* Echo the newline before dispatching. Without it the command's
             * echo and its response share a line on the wire
             * ("led ondenied: requires operator"), and any consumer that
             * strips the echo line-wise sees neither. That is why the Python
             * SDK never raised `AccessDenied` despite SECURITY.md §2
             * promising it: `result.startswith("denied:")` was never true,
             * so a role-restricted caller got a string back and carried on as
             * though the command had succeeded. */
            shell_echo('\n');
            line[len] = 0;
            bool dispatched = false;
            if (overflowed) {
                printf("input too long (max %d chars); line discarded\n",
                       REFLEX_SHELL_LINE_MAX - 1);
                outcome(SHELL_OVERFLOW);
                dispatched = true;
            } else {
                char *argv[8]; int argc = 0;
                char *t = strtok(line, " "); while(t && argc < 8) { argv[argc++] = t; t = strtok(NULL, " "); }
                if (argc > 0) { reflex_shell_dispatch(argc, argv); dispatched = true; }
            }
            /* The machine-readable outcome, on its own line after the human
             * output. Additive: no existing message changes, so the Loom
             * Viewer and any prose-matching consumer keep working while the
             * SDK moves onto the marker. Suppressed for a bare newline, which
             * is not a command and should stay silent for someone at a
             * terminal. */
            if (dispatched) {
                char rbuf[32];
                shell_outcome_format(rbuf, sizeof(rbuf), s_outcome);
                printf("%s\n", rbuf);
            }
            len = 0;
            overflowed = false;
            shell_prompt("\nreflex> ", 9);
        } else if (ch == 0x08 || ch == 0x7F) {
            /* Backspace / DEL. Without this the byte was appended to the line
             * and echoed, so `statuX<DEL>s` dispatched as `statuX\x7fs` and a
             * typo cost the whole line — on the primary interface to this OS,
             * including while typing a 32-character Aura key by hand. */
            if (len > 0) { len--; printf("\b \b"); fflush(stdout); }
        } else if (ch != '\r') {
            /* Bound is the buffer, not an arbitrary 255. That constant capped
             * `loom load` and `vm loadhex` — the only two ways to extend a
             * running board — at 122 bytes of payload, against memory already
             * allocated.
             *
             * Past the bound the excess used to be dropped on the floor and
             * the truncated line dispatched as though complete — silent
             * truncation feeding parsers that had just been hardened against
             * exactly that. Now the line is marked and refused. */
            if (len < REFLEX_SHELL_LINE_MAX - 1) {
                line[len++] = ch;
                shell_echo((char)ch);
            } else {
                overflowed = true;
            }
        }
    }
}
