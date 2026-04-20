/**
 * @file reflex_hal_esp32c6.c
 * @brief Reflex HAL — ESP32-C6 / ESP-IDF backend.
 */

#include "reflex_hal.h"
#include "driver/temperature_sensor.h"

#include <stdarg.h>
#include <stdio.h>
/* SYSTIMER direct read (Gap C — replaces esp_timer_get_time) */
#define SYSTIMER_BASE_ADDR      0x60004000
#define SYSTIMER_UNIT0_OP       (SYSTIMER_BASE_ADDR + 0x04)
#define SYSTIMER_UNIT0_VAL_HI   (SYSTIMER_BASE_ADDR + 0x40)
#define SYSTIMER_UNIT0_VAL_LO   (SYSTIMER_BASE_ADDR + 0x44)
#define REG32_HAL(a) (*(volatile uint32_t *)(a))
#include "esp_rom_sys.h"
#include "esp_log.h"

/* PMU registers for deep sleep */
#define PMU_BASE              0x600B0000
#define PMU_SLP_WAKEUP_CNTL0 (PMU_BASE + 0x120)  /* bit 31 = sleep_req */
#define PMU_SLP_WAKEUP_CNTL1 (PMU_BASE + 0x124)  /* wakeup enable mask */

/* LP timer for timed wake */
#define LP_TIMER_BASE         0x600B0C00
#define LP_TIMER_TAR0_LOW     (LP_TIMER_BASE + 0x00)
#define LP_TIMER_TAR0_HIGH    (LP_TIMER_BASE + 0x04)
#define LP_TIMER_UPDATE       (LP_TIMER_BASE + 0x10)
#define LP_TIMER_MAIN_BUF0_LO (LP_TIMER_BASE + 0x1C)
#define LP_TIMER_MAIN_BUF0_HI (LP_TIMER_BASE + 0x20)

/* LP timer runs at ~32 kHz (RC_SLOW). Use esp_sleep for now because
 * the full PMU sleep configuration (power domain retention, voltage
 * regulator timing, analog state) requires ~200 lines of register
 * writes with chip-revision-specific calibration constants from
 * pmu_param.c. The sleep TRIGGER is one bit (PMU_SLEEP_REQ), but
 * the PREPARATION is what makes it safe.
 *
 * For the sleep command and wakeup cause, we keep esp_sleep.h as
 * the one remaining ESP-IDF dependency in the HAL. It will be
 * replaced when the full PMU configuration is extracted. */
#include "esp_sleep.h"

/* Hardware RNG register */
#define RNG_DATA_REG     0x600B2808

/* eFuse MAC address registers */
#define EFUSE_MAC0_REG   0x600B0844
#define EFUSE_MAC1_REG   0x600B0848

/* GPIO registers (Gap G — replaces driver/gpio.h) */
#define GPIO_BASE_ADDR        0x60091000
#define GPIO_OUT_W1TS         (GPIO_BASE_ADDR + 0x08)
#define GPIO_OUT_W1TC         (GPIO_BASE_ADDR + 0x0C)
#define GPIO_ENABLE_W1TS      (GPIO_BASE_ADDR + 0x24)
#define GPIO_ENABLE_W1TC      (GPIO_BASE_ADDR + 0x28)
#define GPIO_IN_REG_ADDR      (GPIO_BASE_ADDR + 0x3C)
#define GPIO_FUNC_OUT_BASE    (GPIO_BASE_ADDR + 0x554)
/* IO MUX — per-pin config (function select, pull-up/down, input enable) */
#define IO_MUX_BASE_ADDR      0x60090000
#define IO_MUX_PIN_REG(n)     (IO_MUX_BASE_ADDR + 4 + (n) * 4)
#define IO_MUX_FUN_PU_BIT     (1 << 8)
#define IO_MUX_FUN_PD_BIT     (1 << 7)
#define IO_MUX_FUN_IE_BIT     (1 << 9)
#define IO_MUX_MCU_SEL_BITS   (0x7 << 12)

/* ROM function for GPIO matrix signal routing */
extern void esp_rom_gpio_connect_out_signal(uint32_t gpio, uint32_t signal, bool invert, bool oen_inv);

uint64_t reflex_hal_time_us(void) {
    /* Trigger UNIT0 value latch and read. XTAL = 40 MHz, divide by 40.
     * Bounded spin (max 20 iterations) — the latch completes in ~2 cycles. */
    REG32_HAL(SYSTIMER_UNIT0_OP) = (1U << 30);
    for (volatile int i = 0; i < 20; i++) {
        if (REG32_HAL(SYSTIMER_UNIT0_OP) & (1U << 29)) break;
    }
    uint32_t lo = REG32_HAL(SYSTIMER_UNIT0_VAL_LO);
    uint32_t hi = REG32_HAL(SYSTIMER_UNIT0_VAL_HI);
    return (((uint64_t)hi << 32) | lo) / 40;
}

uint32_t reflex_hal_cpu_cycles(void) {
    uint32_t cycles;
    __asm__ volatile ("csrr %0, 0x7E2" : "=r"(cycles)); /* C6 PCCR (machine mode) */
    return cycles;
}

void reflex_hal_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

reflex_err_t reflex_hal_gpio_init_output(uint32_t pin) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    /* IO MUX: GPIO function (MCU_SEL=1), no pulls, no input */
    uint32_t mux = REG32_HAL(IO_MUX_PIN_REG(pin));
    mux &= ~(IO_MUX_MCU_SEL_BITS | IO_MUX_FUN_IE_BIT | IO_MUX_FUN_PU_BIT | IO_MUX_FUN_PD_BIT);
    mux |= (1 << 12);  /* MCU_SEL = 1 → GPIO function */
    REG32_HAL(IO_MUX_PIN_REG(pin)) = mux;
    /* Enable output + set func_out_sel to 0x80 (simple GPIO output) */
    REG32_HAL(GPIO_ENABLE_W1TS) = (1 << pin);
    REG32_HAL(GPIO_FUNC_OUT_BASE + pin * 4) = 0x80;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_init_input(uint32_t pin, bool pullup) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    uint32_t mux = REG32_HAL(IO_MUX_PIN_REG(pin));
    mux &= ~(IO_MUX_MCU_SEL_BITS | IO_MUX_FUN_PU_BIT | IO_MUX_FUN_PD_BIT);
    mux |= (1 << 12) | IO_MUX_FUN_IE_BIT;  /* GPIO func + input enable */
    if (pullup) mux |= IO_MUX_FUN_PU_BIT;
    REG32_HAL(IO_MUX_PIN_REG(pin)) = mux;
    /* Disable output */
    REG32_HAL(GPIO_ENABLE_W1TC) = (1 << pin);
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    if (level) REG32_HAL(GPIO_OUT_W1TS) = (1 << pin);
    else       REG32_HAL(GPIO_OUT_W1TC) = (1 << pin);
    return REFLEX_OK;
}

int reflex_hal_gpio_get_level(uint32_t pin) {
    if (pin >= 31) return 0;
    return (REG32_HAL(GPIO_IN_REG_ADDR) >> pin) & 1;
}

reflex_err_t reflex_hal_gpio_connect_out(uint32_t out_pin, uint32_t signal,
                                         bool invert, bool enable) {
    esp_rom_gpio_connect_out_signal(out_pin, signal, invert, !enable);
    return REFLEX_OK;
}

void reflex_hal_reboot(void) {
    extern void software_reset(void);  /* ROM function at 0x40000090 */
    software_reset();
}

int reflex_hal_sleep_wakeup_cause(void) {
    return (int)esp_sleep_get_wakeup_cause();
}

void reflex_hal_sleep_enter(uint64_t duration_us) {
    esp_sleep_enable_timer_wakeup(duration_us);
    esp_deep_sleep_start();
}

void reflex_hal_random_fill(uint8_t *buf, size_t len) {
    /* XOR multiple RNG reads for better entropy mixing, matching
     * the ESP-IDF approach. Each read returns a 32-bit word from
     * the hardware noise source. */
    for (size_t i = 0; i < len; i += 4) {
        uint32_t rnd = REG32_HAL(RNG_DATA_REG) ^ REG32_HAL(RNG_DATA_REG);
        size_t n = (len - i < 4) ? len - i : 4;
        for (size_t j = 0; j < n; j++) buf[i + j] = (uint8_t)(rnd >> (j * 8));
    }
}

reflex_err_t reflex_hal_mac_read(uint8_t mac[6]) {
    uint32_t w0 = REG32_HAL(EFUSE_MAC0_REG);
    uint32_t w1 = REG32_HAL(EFUSE_MAC1_REG);
    mac[0] = (uint8_t)(w1 >> 8);
    mac[1] = (uint8_t)(w1);
    mac[2] = (uint8_t)(w0 >> 24);
    mac[3] = (uint8_t)(w0 >> 16);
    mac[4] = (uint8_t)(w0 >> 8);
    mac[5] = (uint8_t)(w0);
    return REFLEX_OK;
}

reflex_err_t reflex_hal_temp_init(reflex_temp_handle_t *out) {
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    temperature_sensor_handle_t h = NULL;
    esp_err_t rc = temperature_sensor_install(&cfg, &h);
    if (rc != ESP_OK) return (reflex_err_t)rc;
    rc = temperature_sensor_enable(h);
    if (rc != ESP_OK) return (reflex_err_t)rc;
    *out = (reflex_temp_handle_t)h;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_temp_read(reflex_temp_handle_t h, float *celsius) {
    return (reflex_err_t)temperature_sensor_get_celsius((temperature_sensor_handle_t)h, celsius);
}

/* --- Interrupt allocation via direct PLIC register writes ---
 *
 * The ESP32-C6 uses a custom interrupt controller (not standard PLIC).
 * CPU interrupts 0-31 are available. The interrupt matrix routes
 * peripheral sources (0-63) to CPU interrupt numbers.
 *
 * ROM dependency: intr_handler_set() lives in mask ROM and registers
 * the handler in the ROM's interrupt vector table. The ROM calls the
 * handler with (int cpu_int, void *arg) — our dispatch reads the
 * handler from s_intr_table[cpu_int]. */

#define INTMTX_BASE           0x60010000
#define INTMTX_SOURCE_MAX     63
#define PLIC_MX_BASE          0x20001000
#define PLIC_MXINT_ENABLE     (PLIC_MX_BASE + 0x00)
#define PLIC_MXINT_TYPE       (PLIC_MX_BASE + 0x04)
#define PLIC_MXINT_CLEAR      (PLIC_MX_BASE + 0x08)
#define PLIC_MXINT_PRI(n)     (PLIC_MX_BASE + 0x10 + (n) * 4)
#define PLIC_MXINT_THRESH     (PLIC_MX_BASE + 0x90)

/* CPU interrupt bitmap allocator. Bits 0-9 reserved for ESP-IDF/ROM.
 * CPU interrupt 10 is verified safe on C6 with ESP-IDF 5.5. */
#define REFLEX_INTR_MIN       10
#define REFLEX_INTR_MAX       31
static uint32_t s_intr_alloc_bitmap = 0;

typedef struct {
    reflex_intr_handler_t handler;
    void *arg;
} reflex_intr_entry_t;

static reflex_intr_entry_t s_intr_table[32];

/* The ROM vector table calls registered handlers as fn(void).
 * We use a single dispatch that scans the pending interrupt from
 * the PLIC claim register would be ideal, but the C6's interrupt
 * controller doesn't expose a standard claim. Instead, we use the
 * mcause CSR which holds the interrupt number during an ISR. */
static void __attribute__((section(".iram1")))
reflex_intr_dispatch(void) {
    uint32_t mcause;
    __asm__ volatile ("csrr %0, mcause" : "=r"(mcause));
    int cpu_int = (int)(mcause & 0x1F);
    if (cpu_int < 32) {
        reflex_intr_entry_t *e = &s_intr_table[cpu_int];
        if (e->handler) e->handler(e->arg);
    }
}

reflex_err_t reflex_hal_intr_alloc(int source, int flags,
                                   reflex_intr_handler_t handler, void *arg,
                                   reflex_intr_handle_t *out_handle) {
    (void)flags;

    if (source < 0 || source > INTMTX_SOURCE_MAX)
        return REFLEX_ERR_INVALID_ARG;
    if (!handler)
        return REFLEX_ERR_INVALID_ARG;

    /* Allocate a free CPU interrupt number */
    int cpu_int = -1;
    for (int i = REFLEX_INTR_MIN; i <= REFLEX_INTR_MAX; i++) {
        if (!(s_intr_alloc_bitmap & (1U << i))) {
            s_intr_alloc_bitmap |= (1U << i);
            cpu_int = i;
            break;
        }
    }
    if (cpu_int < 0) return REFLEX_ERR_NO_MEM;

    s_intr_table[cpu_int].handler = handler;
    s_intr_table[cpu_int].arg = arg;

    /* Route peripheral source → CPU interrupt via interrupt matrix */
    REG32_HAL(INTMTX_BASE + 4 * source) = cpu_int;

    /* Set priority (1 = lowest non-zero) */
    REG32_HAL(PLIC_MXINT_PRI(cpu_int)) = 1;

    /* Disable interrupts for atomic RMW of shared registers */
    __asm__ volatile ("csrci mstatus, 0x8");

    /* Level-triggered (clear the type bit) */
    uint32_t type = REG32_HAL(PLIC_MXINT_TYPE);
    type &= ~(1U << cpu_int);
    REG32_HAL(PLIC_MXINT_TYPE) = type;

    /* Enable the CPU interrupt in PLIC */
    REG32_HAL(PLIC_MXINT_ENABLE) |= (1U << cpu_int);

    __asm__ volatile ("csrsi mstatus, 0x8");

    /* Register dispatch with ROM's interrupt vector table */
    extern void intr_handler_set(int n, void (*fn)(void), void *arg);
    intr_handler_set(cpu_int, (void (*)(void))reflex_intr_dispatch, NULL);

    /* Enable in mie CSR (bit 16+n for external interrupts on C6) */
    uint32_t mie;
    __asm__ volatile ("csrr %0, mie" : "=r"(mie));
    mie |= (1U << (16 + cpu_int));
    __asm__ volatile ("csrw mie, %0" : : "r"(mie));

    if (out_handle) *out_handle = (reflex_intr_handle_t)(uintptr_t)(cpu_int + 1);
    return REFLEX_OK;
}

reflex_err_t reflex_hal_intr_free(reflex_intr_handle_t handle) {
    int cpu_int = (int)(uintptr_t)handle - 1;
    if (cpu_int < REFLEX_INTR_MIN || cpu_int > REFLEX_INTR_MAX)
        return REFLEX_ERR_INVALID_ARG;

    /* Disable with interrupts off to prevent RMW race */
    __asm__ volatile ("csrci mstatus, 0x8");
    REG32_HAL(PLIC_MXINT_ENABLE) &= ~(1U << cpu_int);
    __asm__ volatile ("csrsi mstatus, 0x8");

    /* Clear interrupt matrix routing */
    for (int s = 0; s <= INTMTX_SOURCE_MAX; s++) {
        if (REG32_HAL(INTMTX_BASE + 4 * s) == (uint32_t)cpu_int) {
            REG32_HAL(INTMTX_BASE + 4 * s) = 0;
        }
    }

    s_intr_table[cpu_int].handler = NULL;
    s_intr_table[cpu_int].arg = NULL;
    s_intr_alloc_bitmap &= ~(1U << cpu_int);
    return REFLEX_OK;
}

void reflex_hal_log(int level, const char *tag, const char *fmt, ...) {
    /* Use esp_log_writev to route through the VFS layer to the
     * USB-JTAG console. esp_rom_printf goes to UART0 which is
     * not connected on the C6's USB-JTAG interface. */
    esp_log_level_t esp_level;
    switch (level) {
        case REFLEX_LOG_LEVEL_ERROR: esp_level = ESP_LOG_ERROR; break;
        case REFLEX_LOG_LEVEL_WARN:  esp_level = ESP_LOG_WARN;  break;
        case REFLEX_LOG_LEVEL_INFO:  esp_level = ESP_LOG_INFO;  break;
        case REFLEX_LOG_LEVEL_DEBUG: esp_level = ESP_LOG_DEBUG;  break;
        default:                     esp_level = ESP_LOG_INFO;   break;
    }
    va_list args;
    va_start(args, fmt);
    esp_log_writev(esp_level, tag, fmt, args);
    va_end(args);
}
