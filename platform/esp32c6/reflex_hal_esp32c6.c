/**
 * @file reflex_hal_esp32c6.c
 * @brief Reflex HAL — ESP32-C6 / ESP-IDF backend.
 */

#include "reflex_hal.h"
#include <string.h>

#include <stdarg.h>
#include <stdio.h>
#include "esp_sleep.h"
/* SYSTIMER direct read (Gap C — replaces esp_timer_get_time) */
/* SYSTIMER, from the SoC definitions rather than a literal.
 *
 * This was hardcoded as 0x60004000, which is I2C0 on the ESP32-C6 — the
 * shadow atlas names that very address comm.i2c0.scl_low_period. The real
 * base is DR_REG_SYSTIMER_BASE (0x6000A000). Every offset below was correct;
 * only the base was wrong, so reflex_hal_time_us latched and read I2C
 * registers and returned 0 forever. */
#include "reflex_regops.h"
#include "reflex_soc_esp32c6.h"
#define SYSTIMER_BASE_ADDR REFLEX_DR_REG_SYSTIMER_BASE
/* Offsets and bits come from the SoC bridge, which proves them equal to
 * ESP-IDF's, rather than from a hand-typed offset. The base address in this
 * file was once wrong in exactly that way and reflex_hal_time_us returned zero
 * for as long as it took to notice. */
#define SYSTIMER_UNIT0_OP REFLEX_SYSTIMER_UNIT0_OP_REG
#define SYSTIMER_UNIT0_VAL_HI   (SYSTIMER_BASE_ADDR + 0x40)
#define SYSTIMER_UNIT0_VAL_LO REFLEX_SYSTIMER_UNIT0_VALUE_LO_REG
/* XTAL 40 MHz through the C6's fixed 2.5 divider. */
#define SYSTIMER_TICKS_PER_US 16u
#include "reflex_rom_esp32c6.h"

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

/* Sleep uses "timed reboot" approach — stores duration in LP AON
 * register and software-resets. Full PMU deep sleep (true low-power
 * state) requires ~400 lines of chip-revision-specific PMU register
 * writes. The timed-reboot approach is functionally equivalent for
 * state persistence but uses more power during sleep. */

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
    /* Trigger UNIT0 value latch and read.
     *
     * Ticks per microsecond is 16, not 40. The systimer counts XTAL (40 MHz)
     * through a divider the C6 fixes at 2.5 — soc_caps.h states it outright:
     * "SOC_SYSTIMER_FIXED_DIVIDER 1 // Clock source divider is fixed: 2.5".
     * Dividing by 40 made every timestamp run 2.5x slow, which was invisible
     * while the base address was also wrong and the whole function returned 0.
     *
     * Bounded spin (max 20 iterations) — the latch completes in ~2 cycles. */
    /* The latch and the two reads have to be one indivisible operation.
     *
     * There is a single value-latch shared by every caller, and this function
     * has many across several tasks. If one is preempted between reading LO and
     * reading HI, the other's latch replaces the sample underneath it and the
     * two halves come from different instants. That is harmless almost always —
     * the counter is monotonic, so a mixed sample is merely slightly late — but
     * not when LO has just wrapped: HI is then one greater than the LO it is
     * paired with, and the result lands 2^32 ticks in the future. At 16 MHz
     * that is a clock that jumps forward 268 seconds and comes back.
     *
     * This clock backs LOOM_LOCK_TIMEOUT_US, which is 300 microseconds, so the
     * visible failure is a spurious LOOM_CONTENTION_FAULT on a lock that was
     * held for no time at all — and uptime, telemetry ages and peer timeouts
     * all inherit the same jump. The window is two instructions wide and needs
     * a wrap to coincide with it, which is why it has not been seen; the
     * console transmit path now calls this on every byte it waits for, so the
     * odds stopped being negligible.
     *
     * Restores rather than unconditionally re-enables, because callers inside
     * the loom hold already have interrupts off. */
    uint32_t saved;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));
    REFLEX_REG(SYSTIMER_UNIT0_OP) = REFLEX_SYSTIMER_UNIT0_UPDATE;
    for (volatile int i = 0; i < 20; i++) {
        if (REFLEX_REG(SYSTIMER_UNIT0_OP) & REFLEX_SYSTIMER_UNIT0_VALUE_VALID) break;
    }
    uint32_t lo = REFLEX_REG(SYSTIMER_UNIT0_VAL_LO);
    uint32_t hi = REFLEX_REG(SYSTIMER_UNIT0_VAL_HI);
    if (saved & 0x8u) {
        __asm__ volatile("csrsi mstatus, 0x8");
    }
    return (((uint64_t)hi << 32) | lo) / SYSTIMER_TICKS_PER_US;
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
    uint32_t mux = REFLEX_REG(IO_MUX_PIN_REG(pin));
    mux &= ~(IO_MUX_MCU_SEL_BITS | IO_MUX_FUN_IE_BIT | IO_MUX_FUN_PU_BIT | IO_MUX_FUN_PD_BIT);
    mux |= (1 << 12);  /* MCU_SEL = 1 → GPIO function */
    REFLEX_REG(IO_MUX_PIN_REG(pin)) = mux;
    /* Enable output + set func_out_sel to 0x80 (simple GPIO output) */
    REFLEX_REG(GPIO_ENABLE_W1TS) = (1 << pin);
    REFLEX_REG(GPIO_FUNC_OUT_BASE + pin * 4) = 0x80;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_init_input(uint32_t pin, bool pullup) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    uint32_t mux = REFLEX_REG(IO_MUX_PIN_REG(pin));
    mux &= ~(IO_MUX_MCU_SEL_BITS | IO_MUX_FUN_PU_BIT | IO_MUX_FUN_PD_BIT);
    mux |= (1 << 12) | IO_MUX_FUN_IE_BIT;  /* GPIO func + input enable */
    if (pullup) mux |= IO_MUX_FUN_PU_BIT;
    REFLEX_REG(IO_MUX_PIN_REG(pin)) = mux;
    /* Disable output */
    REFLEX_REG(GPIO_ENABLE_W1TC) = (1 << pin);
    return REFLEX_OK;
}

reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level) {
    if (pin >= 31) return REFLEX_ERR_INVALID_ARG;
    if (level)
        REFLEX_REG(GPIO_OUT_W1TS) = (1 << pin);
    else
        REFLEX_REG(GPIO_OUT_W1TC) = (1 << pin);
    return REFLEX_OK;
}

int reflex_hal_gpio_get_level(uint32_t pin) {
    if (pin >= 31) return 0;
    return (REFLEX_REG(GPIO_IN_REG_ADDR) >> pin) & 1;
}

reflex_err_t reflex_hal_gpio_connect_out(uint32_t out_pin, uint32_t signal,
                                         bool invert, bool enable) {
    esp_rom_gpio_connect_out_signal(out_pin, signal, invert, !enable);
    return REFLEX_OK;
}

/* Boot0's boot-attempt counter lives in LP_AON STORE0 and survives a software
 * reset, which is the whole point: it is how one boot tells the next that the
 * last one did not get far. Zeroing it is what "this boot is good" means. */
void reflex_hal_boot_mark_stable(void) {
    REFLEX_REG(REFLEX_LP_AON_STORE0_REG) = 0;
}

void reflex_hal_reboot(void) {
    extern void software_reset(void);  /* ROM function at 0x40000090 */
    software_reset();
}

/* Wakeup cause and the scratch word shared with Boot0, both from the generated
 * SoC header rather than hand-written base-plus-offset arithmetic. This file
 * already carries the scar from getting that arithmetic wrong once: the
 * systimer base was 0x60004000, which is I2C0, and reflex_hal_time_us returned
 * 0 forever. These are the same shape of constant, so they come from the SVD
 * and are proved against ESP-IDF by `make soc-bridge` like the rest. */
#define REFLEX_SLEEP_MAGIC 0x534C5000 /* "SLP\0" + duration in the low bits */

int reflex_hal_sleep_wakeup_cause(void) {
    uint32_t cause = REFLEX_REG(REFLEX_PMU_SLP_WAKEUP_STATUS0_REG);
    if (cause & (1 << 0)) return 4;  /* LP timer → maps to ESP_SLEEP_WAKEUP_TIMER */
    if (cause & (1 << 4)) return 2;  /* GPIO → maps to ESP_SLEEP_WAKEUP_EXT0 */
    if (cause == 0) return 0;         /* Not a sleep wakeup (power-on) */
    return 1;                          /* Unknown wakeup source */
}

/* The last ESP-IDF API call on the C6 HAL path, and it is staying for now.
 *
 * Everything around it is already independent: the wakeup cause is read
 * straight from PMU.SLP_WAKEUP_STATUS0, reboot goes through the ROM's
 * software_reset, the RNG is a direct register read, and the constants above
 * come from the SVD. Only the entry itself is borrowed.
 *
 * It is borrowed deliberately rather than by oversight. Configuring the wakeup
 * source is tractable — LP_TIMER's TAR0_LOW/HIGH and UPDATE at 0x600B0C00 are
 * fully described in the SVD — but the entry sequence is not just a few
 * register writes. It powers down domains, flushes and disables cache, parks
 * the flash, and lands on a wait-for-interrupt with the chip in a state where
 * a mistake does not report an error: the board simply never wakes, and there
 * is nothing left running to say why. ESP-IDF's implementation is long and
 * carries silicon errata this file has no way to rediscover.
 *
 * Porting it is real driver work that has to be validated on hardware, so it
 * is not written blind here. The non-C6 branch below shows the shape of the
 * alternative already in use: record the duration in LP_AON scratch and reset,
 * which Boot0 reads back — a sleep-as-reboot that costs boot time but never
 * strands the board. That is the fallback to reach for if the ESP-IDF entry
 * has to go before a bench is available to test a real one. */
void reflex_hal_sleep_enter(uint64_t duration_us) {
#if CONFIG_IDF_TARGET_ESP32C6
    esp_sleep_enable_timer_wakeup(duration_us);
    esp_deep_sleep_start();
#else
    uint32_t duration_sec = (uint32_t)(duration_us / 1000000);
    if (duration_sec > 0xFFFF) duration_sec = 0xFFFF;
    REFLEX_REG(REFLEX_LP_AON_STORE1_REG) = REFLEX_SLEEP_MAGIC | (duration_sec & 0xFFFF);
    reflex_hal_reboot();
#endif
}

void reflex_hal_random_fill(uint8_t *buf, size_t len) {
    /* XOR multiple RNG reads for better entropy mixing, matching
     * the ESP-IDF approach. Each read returns a 32-bit word from
     * the hardware noise source. */
    for (size_t i = 0; i < len; i += 4) {
        uint32_t rnd = REFLEX_REG(RNG_DATA_REG) ^ REFLEX_REG(RNG_DATA_REG);
        size_t n = (len - i < 4) ? len - i : 4;
        for (size_t j = 0; j < n; j++) buf[i + j] = (uint8_t)(rnd >> (j * 8));
    }
}

reflex_err_t reflex_hal_mac_read(uint8_t mac[6]) {
    uint32_t w0 = REFLEX_REG(EFUSE_MAC0_REG);
    uint32_t w1 = REFLEX_REG(EFUSE_MAC1_REG);
    mac[0] = (uint8_t)(w1 >> 8);
    mac[1] = (uint8_t)(w1);
    mac[2] = (uint8_t)(w0 >> 24);
    mac[3] = (uint8_t)(w0 >> 16);
    mac[4] = (uint8_t)(w0 >> 8);
    mac[5] = (uint8_t)(w0);
    return REFLEX_OK;
}

/* Temperature sensor via APB_SARADC TSENS registers */
#define TSENS_CTRL_REG   (0x6000E000 + 0x58)
#define TSENS_CTRL2_REG  (0x6000E000 + 0x5C)
#define TSENS_PU_BIT     (1U << 22)
#define TSENS_CLK_SEL    (1U << 15)
#define TSENS_OUT_MASK   0xFF

reflex_err_t reflex_hal_temp_init(reflex_temp_handle_t *out) {
    /* Enable TSENS clock and power up */
    REFLEX_REG(TSENS_CTRL2_REG) |= TSENS_CLK_SEL;
    REFLEX_REG(TSENS_CTRL_REG) |= TSENS_PU_BIT;
    reflex_hal_delay_us(300);
    if (out) *out = (reflex_temp_handle_t)1;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_temp_read(reflex_temp_handle_t h, float *celsius) {
    (void)h;
    if (!celsius) return REFLEX_ERR_INVALID_ARG;
    uint32_t raw = REFLEX_REG(TSENS_CTRL_REG) & TSENS_OUT_MASK;
    *celsius = (float)raw * 0.4386f - 27.88f;
    return REFLEX_OK;
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
/* C6 PLIC priorities run 1..7; the controller forwards only above THRESH. */
#define REFLEX_PLIC_PRIO_MAX 7u
/* C6 PLIC priorities run 1..7; 0 means never delivered. */
#define REFLEX_PLIC_PRIO_MAX 7u

/* CPU interrupt bitmap allocator. Bits 0-9 are reserved for ESP-IDF and the
 * ROM. Above that, availability is read from PLIC_MXINT_ENABLE at allocation
 * time rather than assumed — see reflex_hal_intr_alloc. */
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

    /* Allocate a free CPU interrupt number.
     *
     * This scans Reflex's own bitmap only. An attempt was made to also skip
     * lines whose PLIC_MXINT_ENABLE bit is set, on the reasoning that a set bit
     * means somebody else has the line live — and it broke the working case.
     * The tick runs at a clean 1000 Hz on the default build using line 10, and
     * PLIC-aware allocation moves off line 10 and delivers nothing at all. So a
     * set enable bit does not mean what it looks like it means here, and the
     * original note that "CPU interrupt 10 is verified safe on C6 with ESP-IDF
     * 5.5" is empirical and load-bearing rather than an assumption waiting to
     * be improved. Reverted; the finding is recorded in
     * docs/independence-dependency-map.md so it is not re-attempted blind. */
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
    REFLEX_REG(INTMTX_BASE + 4 * source) = cpu_int;

    /* Priority must exceed the live threshold, or the PLIC never forwards.
     *
     * Measured on a failing re-arm: the SYSTIMER source asserting and latched
     * (raw=0x2 st=0x2), routed to the line, PLIC-enabled, mie set,
     * mstatus.MIE set — and mip_pending=0. The core never sees the interrupt,
     * so nothing downstream is masking it; the PLIC is not forwarding. With
     * pri=1 against thresh=1 that is exactly the expected behaviour, because
     * the comparison is strict.
     *
     * The threshold is not a constant. It is whatever the rest of the system
     * is running at, so reading it and sitting one above keeps this correct in
     * any configuration rather than working by luck in one.
     *
     * Correcting a claim made when this landed: it was said that a cold arming
     * "happens to run at thresh=0", which is why the old hardcoded priority of
     * 1 worked. That was never measured, and it is false. With the readback now
     * printing on success as well as failure, a working arming reads pri=2
     * thresh=1 — the same as a failing one. The threshold is 1 in both cases,
     * so this change is correctness rather than the explanation of anything. */
    REFLEX_REG(PLIC_MXINT_PRI(cpu_int)) = reflex_intr_priority_for(REFLEX_REG(PLIC_MXINT_THRESH));

    /* Install the vector before the line can deliver anything.
     *
     * This used to enable the PLIC line and re-enable interrupts *before*
     * calling intr_handler_set, leaving a window in which a source already
     * asserting would be taken with no handler installed for that line. For a
     * level-triggered interrupt that is not a missed event, it is a permanent
     * one: nothing clears the source, so it re-asserts immediately and the
     * core makes no further progress. Everything that can be set up while the
     * line is still masked now is. */
    extern void intr_handler_set(int n, void (*fn)(void), void *arg);
    intr_handler_set(cpu_int, (void (*)(void))reflex_intr_dispatch, NULL);

    /* Disable interrupts for atomic RMW of shared registers */
    __asm__ volatile ("csrci mstatus, 0x8");

    /* Level-triggered (clear the type bit) */
    uint32_t type = REFLEX_REG(PLIC_MXINT_TYPE);
    type &= ~(1U << cpu_int);
    REFLEX_REG(PLIC_MXINT_TYPE) = type;

    /* Enable in mie CSR (bit 16+n for external interrupts on C6) */
    uint32_t mie;
    __asm__ volatile ("csrr %0, mie" : "=r"(mie));
    mie |= (1U << (16 + cpu_int));
    __asm__ volatile ("csrw mie, %0" : : "r"(mie));

    /* Enable the CPU interrupt in PLIC last: this is the step that makes the
     * line deliverable, and everything it needs is now in place. */
    REFLEX_REG(PLIC_MXINT_ENABLE) |= (1U << cpu_int);

    __asm__ volatile("csrsi mstatus, 0x8");

    if (out_handle) *out_handle = (reflex_intr_handle_t)(uintptr_t)(cpu_int + 1);
    return REFLEX_OK;
}

void reflex_hal_intr_describe(int source, reflex_intr_route_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (source < 0 || source > INTMTX_SOURCE_MAX) return;

    uint32_t cpu_int = REFLEX_REG(INTMTX_BASE + 4 * source);
    out->source = (uint32_t)source;
    out->cpu_int = cpu_int;

    if (cpu_int < 32) {
        out->plic_enabled = (REFLEX_REG(PLIC_MXINT_ENABLE) >> cpu_int) & 1u;
        out->plic_priority = REFLEX_REG(PLIC_MXINT_PRI(cpu_int));
        out->level_triggered = !((REFLEX_REG(PLIC_MXINT_TYPE) >> cpu_int) & 1u);
        uint32_t mie;
        __asm__ volatile("csrr %0, mie" : "=r"(mie));
        out->mie_enabled = (mie >> (16 + cpu_int)) & 1u;
        /* mip says whether the core sees the interrupt pending, and it is only
         * meaningful with traps masked.
         *
         * Read with mstatus.MIE set — which is how a shell command runs — a
         * forwarded interrupt is taken before the read can observe it, so mip
         * reads 0 whether the controller is forwarding or not. That makes the
         * reading tautological rather than decisive, which is exactly the
         * mistake the first version of this made. Masking traps first lets a
         * forwarded-but-not-yet-taken interrupt sit in mip long enough to be
         * seen, so 0 now genuinely means "not forwarded". */
        uint32_t mstatus_save;
        __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(mstatus_save));
        uint32_t mip;
        __asm__ volatile("csrr %0, mip" : "=r"(mip));
        if (mstatus_save & 0x8u) {
            __asm__ volatile("csrsi mstatus, 0x8");
        }
        out->mip_pending = (mip >> (16 + cpu_int)) & 1u;
    }
    out->plic_threshold = REFLEX_REG(PLIC_MXINT_THRESH);
    out->live_line_mask = REFLEX_REG(PLIC_MXINT_ENABLE);

    uint32_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
    out->global_ie = (mstatus >> 3) & 1u; /* MIE */
}

/* Enable or mask one allocated interrupt line at the controller.
 *
 * Exists so a driver can throttle its own interrupt without touching the
 * peripheral's INT_ENA register. That register is shared by every interrupt the
 * peripheral has — on USB-serial-JTAG it carries the transmit path ESP-IDF's
 * console still uses — and a read-modify-write of it from an ISR races code
 * that never agreed to coordinate. Observed: the board emitted a single byte
 * and went silent, which is a wedged stdout, not the receive failure it looked
 * like.
 *
 * PLIC_MXINT_ENABLE is different in the one way that matters: every writer is
 * Reflex's, and each brackets its read-modify-write with interrupts disabled,
 * so on this single core the sequence is atomic against preemption.
 *
 * mstatus is saved and restored rather than unconditionally re-enabled: called
 * from an ISR, MIE is already clear, and setting it would re-enable interrupts
 * part-way through a handler. */
reflex_err_t reflex_hal_intr_set_enabled(reflex_intr_handle_t handle, bool enabled) {
    int cpu_int = (int)(uintptr_t)handle - 1;
    if (cpu_int < REFLEX_INTR_MIN || cpu_int > REFLEX_INTR_MAX) {
        return REFLEX_ERR_INVALID_ARG;
    }
    uint32_t saved;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));
    if (enabled) {
        REFLEX_REG(PLIC_MXINT_ENABLE) |= (1U << cpu_int);
    } else {
        REFLEX_REG(PLIC_MXINT_ENABLE) &= ~(1U << cpu_int);
    }
    if (saved & 0x8u) {
        __asm__ volatile("csrsi mstatus, 0x8");
    }
    return REFLEX_OK;
}

reflex_err_t reflex_hal_intr_free(reflex_intr_handle_t handle) {
    int cpu_int = (int)(uintptr_t)handle - 1;
    if (cpu_int < REFLEX_INTR_MIN || cpu_int > REFLEX_INTR_MAX)
        return REFLEX_ERR_INVALID_ARG;

    /* Disable with interrupts off to prevent RMW race, then drop whatever was
     * pending on the line. Handing a line back while it is still asserting
     * leaves the next owner with a pending interrupt it never caused and
     * cannot acknowledge, so this is worth doing on its own merits —
     * PLIC_MXINT_CLEAR was defined in this file and written by nothing.
     *
     * It is not the fix for the re-arm failure. That was the hypothesis, and
     * clearing pending on both the free and alloc paths changed nothing: the
     * first arming after a cold boot still runs at 1000 Hz and every re-arming
     * still delivers one tick or none. The alloc-side clear was removed again
     * as unjustified; this one stays because handing back an asserting line is
     * wrong regardless of what it failed to explain. */
    __asm__ volatile ("csrci mstatus, 0x8");
    REFLEX_REG(PLIC_MXINT_ENABLE) &= ~(1U << cpu_int);
    REFLEX_REG(PLIC_MXINT_CLEAR) = (1U << cpu_int);
    __asm__ volatile ("csrsi mstatus, 0x8");

    /* Clear interrupt matrix routing */
    for (int s = 0; s <= INTMTX_SOURCE_MAX; s++) {
        if (REFLEX_REG(INTMTX_BASE + 4 * s) == (uint32_t)cpu_int) {
            REFLEX_REG(INTMTX_BASE + 4 * s) = 0;
        }
    }

    s_intr_table[cpu_int].handler = NULL;
    s_intr_table[cpu_int].arg = NULL;
    s_intr_alloc_bitmap &= ~(1U << cpu_int);
    return REFLEX_OK;
}

/* --- Logging via USB-JTAG CDC-ACM FIFO (direct register write) --- */

/* Addresses come from the generated SoC header, not from literals here. They
 * were hand-written as USJ_BASE + 0x00 / + 0x04, which is the same register map
 * transcribed a second time — and `make soc-bridge` cannot prove a literal it
 * cannot see. They are now scraped from the SVD (where the peripheral is called
 * USB_DEVICE) and _Static_assert'ed against ESP-IDF's macros. */
#define USJ_EP1_DATA REFLEX_USJ_EP1_REG
#define USJ_EP1_CONF REFLEX_USJ_EP1_CONF_REG
/* EP1_CONF bits, per soc/usb_serial_jtag_reg.h. */
#define USJ_WR_DONE           (1U << 0)   /* W:  flush the IN endpoint      */
#define USJ_IN_EP_DATA_FREE   (1U << 1)   /* RO: space left in the FIFO     */
/* Spin budget per byte while waiting for FIFO space.
 *
 * The wait used to be unbounded. SERIAL_IN_EP_DATA_FREE only clears when the
 * 64-byte IN endpoint is full, and it only frees again when a USB host drains
 * it — so with no host attached, or one that has stopped reading, this looped
 * forever and took the calling task with it. Every REFLEX_LOG* call and the
 * 10Hz telemetry stream go through here, so a board running headless could
 * wedge its supervisor on a log line.
 *
 * Console output is best-effort by nature: if nobody is listening, dropping
 * the line is correct and hanging is not. */
/* Bound the transmit wait in time, not in loop iterations.
 *
 * The original bound was 20000 spins, which sounds generous and is not: the
 * USB host services this endpoint on a frame boundary, once per millisecond at
 * best, and 20000 iterations of a two-instruction poll elapse in well under
 * that. Any host that had not polled recently — the normal state between reads
 * — lost the rest of the line. It showed as a shell whose replies
 * desynchronised from its prompts, echoes cut mid-word ("reflex> au"), because
 * the discarded bytes were echo characters the harness was matching on.
 *
 * The clock is reflex_hal_time_us, already in this file and already reading
 * SYSTIMER unit 0 — the same counter the scheduler tick comes from. It was
 * reached for late: the first attempt hand-rolled a second reader against the
 * RISC-V standard cycle CSR, which on this part reads back as zero, because
 * SOC_CPU_HAS_CSR_PC moves the counter to a vendor CSR (0x7E2, which
 * reflex_hal_cpu_cycles below has been reading correctly all along). A
 * zero clock makes every elapsed-time test false, so the wait never expires —
 * the first write with no reader attached spins forever, the USB device never
 * finishes enumerating, and the board presents to the host as absent rather
 * than as busy. That firmware had to be flashed out blind, and the cost of
 * assuming a standard counter is exactly one such cycle.
 *
 * The budget covers the whole write, not each byte. Per byte it is ruinous:
 * with no reader, a thousand-byte boot log costs fifty seconds. Per call the
 * same budget costs 50 ms once, which nobody notices. */
#define USJ_TX_TIMEOUT_US 50000u /* 50 ms */

/* Hand the packet over, then wait until the endpoint will accept bytes again.
 *
 * The wait is not for us — it is for whoever writes next. ESP-IDF's console
 * still emits the shell's command output, and its write path pushes its first
 * byte without checking IN_EP_DATA_FREE. If our packet handover is still in
 * flight at that moment, that byte lands on the floor: a reply arrives with its
 * first character missing and nothing else wrong. Measured at one occurrence in
 * sixty runs of the same command sequence, surfacing as a usage line reading
 * "apestry signal <cell>". Leaving the endpoint ready is the cost of sharing it.
 *
 * Bounded by the caller's remaining budget, so a host that is not reading still
 * cannot hold the caller indefinitely. */
static void usj_flush(uint64_t start) {
    REFLEX_REG(USJ_EP1_CONF) |= USJ_WR_DONE;
    while (!(REFLEX_REG(USJ_EP1_CONF) & USJ_IN_EP_DATA_FREE)) {
        if ((reflex_hal_time_us() - start) > USJ_TX_TIMEOUT_US) return;
    }
}

static void usj_write_bytes(const char *data, int len) {
    uint64_t start = reflex_hal_time_us();
    for (int i = 0; i < len; i++) {
        while (!(REFLEX_REG(USJ_EP1_CONF) & USJ_IN_EP_DATA_FREE)) {
            if ((reflex_hal_time_us() - start) > USJ_TX_TIMEOUT_US) {
                /* No reader. Flush whatever made it into the FIFO and drop the
                 * rest of this line rather than blocking the caller forever. */
                usj_flush(start);
                return;
            }
        }
        REFLEX_REG(USJ_EP1_DATA) = (uint32_t)(uint8_t)data[i];
    }
    usj_flush(start);
}

/* --- Console RX: Reflex's own, interrupt-driven ---------------------------
 *
 * Tier E. The ESP-IDF USB-serial-JTAG driver owned this: it installed an RX
 * ring and the shell read one byte at a time through usb_serial_jtag_read_bytes.
 * Owning RX means not installing that driver, because it takes the OUT-endpoint
 * FIFO and a direct read would race it for the same bytes.
 *
 * Polling cannot replace it. The shell idles for 50ms when no byte is waiting,
 * and at 115200 baud the 64-byte FIFO fills in about 5.5ms — so a line arriving
 * during an idle window overruns it several times before anyone looks. That is
 * not a prediction: it is what happened before the driver was installed, and
 * why it was installed.
 *
 * An interrupt decouples arrival from polling. The ISR drains the FIFO into
 * this ring whenever the hardware says a packet landed, so the shell's idle
 * delay costs latency and never a byte.
 *
 * Single producer (the ISR), single consumer (the shell task), on a single
 * core: a plain head/tail ring needs no lock, provided each index is written by
 * exactly one side and both are volatile so neither is cached across the
 * boundary. */
#define USJ_RX_RING_SIZE 2048u /* > REFLEX_SHELL_LINE_MAX, power of two */

static volatile uint8_t s_usj_rx_ring[USJ_RX_RING_SIZE];
static volatile uint32_t s_usj_rx_head; /* written by the ISR only  */
static volatile uint32_t s_usj_rx_tail; /* written by the task only */
static volatile uint32_t s_usj_rx_dropped;
/* Set by the ISR when it stopped draining because the ring was full, cleared
 * by the consumer when it re-enables the interrupt. */
static volatile bool s_usj_rx_stalled;

/* Diagnostic only: distinguishes "the handler never runs" from "it runs and
 * finds nothing" from "it buffers and nobody consumes". */
static volatile uint32_t s_usj_isr_count;
static volatile uint32_t s_usj_isr_bytes;
static reflex_intr_handle_t s_usj_intr;

static void usj_rx_isr(void *arg) {
    (void)arg;
    s_usj_isr_count++;

    /* Drain only while the ring has room, and stop otherwise — do not discard.
     *
     * Draining unconditionally and dropping on a full ring loses data under
     * exactly the load that matters, and it does so for a reason worth stating:
     * emptying the FIFO is what ACKs the USB packet. A receiver that always
     * drains always ACKs, so the host never slows down, and a 913-character
     * line arrives faster than a shell echoing byte by byte can consume it.
     * Measured that way: 962 bytes dropped in one validation run.
     *
     * The ESP-IDF driver this replaces was not merely buffering — it was
     * providing flow control. Leaving bytes in the hardware FIFO makes the
     * endpoint stop accepting more, and the host blocks. That is the property
     * to preserve, and a bigger ring would not have provided it. */
    bool stalled = false;
    for (;;) {
        while (REFLEX_REG(REFLEX_USJ_EP1_CONF_REG) & REFLEX_USJ_OUT_EP_DATA_AVAIL) {
            uint32_t head = s_usj_rx_head;
            uint32_t next = (head + 1u) & (USJ_RX_RING_SIZE - 1u);
            if (next == s_usj_rx_tail) {
                /* Full: leave the rest in the FIFO and let the host wait. Mask
                 * at the interrupt controller, not at REFLEX_USJ_INT_ENA_REG —
                 * that register is shared with the transmit path and writing it
                 * from here wedges stdout. */
                reflex_hal_intr_set_enabled(s_usj_intr, false);
                s_usj_rx_stalled = true;
                stalled = true;
                break;
            }
            s_usj_rx_ring[head] = (uint8_t)REFLEX_REG(REFLEX_USJ_EP1_REG);
            s_usj_rx_head = next;
            s_usj_isr_bytes++;
        }

        if (stalled) {
            /* Acknowledge everything *except* our own bit. The bytes we did not
             * take are still in the FIFO, and the packet notification is what
             * will fetch us back to them: clearing it here would strand them
             * with nothing left to announce their arrival. The line cannot
             * storm meanwhile because it is masked at the controller, and
             * reflex_hal_console_read re-enables it once there is room. */
            uint32_t st = REFLEX_REG(REFLEX_USJ_INT_ST_REG);
            REFLEX_REG(REFLEX_USJ_INT_CLR_REG) = st & ~REFLEX_USJ_OUT_RECV_PKT_INT;
            return;
        }

        /* Acknowledge every bit that was asserted, not only ours.
         *
         * Source 48 is the peripheral's single interrupt line, shared by
         * receive and transmit events. Clearing only the receive bit leaves any
         * other asserted bit pending on a level-triggered line, so the handler
         * re-enters immediately and forever and the core never runs anything
         * else — which presents as a console that produces no output at all,
         * because the task that would print never gets scheduled.
         *
         * Isolated by experiment: with this interrupt not enabled the console
         * emits its full boot log; with it enabled and only our bit cleared,
         * nothing. Nothing else differed. */
        REFLEX_REG(REFLEX_USJ_INT_CLR_REG) = REFLEX_REG(REFLEX_USJ_INT_ST_REG);

        /* Then look again before leaving, because the acknowledgement above is
         * unconditional and a packet that arrived during the drain has just
         * been acknowledged along with ours. Its bytes are still in the FIFO
         * and its announcement is now gone, so returning here would leave them
         * with nothing to fetch them and no interrupt would ever arrive again.
         * The console works, then stops, and stays stopped.
         *
         * That is the shape the hardware suite showed: 86 tests pass, then the
         * shell never answers again. The window is narrow enough that light
         * interactive use never finds it and sustained traffic always does. */
        if (!(REFLEX_REG(REFLEX_USJ_EP1_CONF_REG) & REFLEX_USJ_OUT_EP_DATA_AVAIL)) {
            return;
        }
    }
}

reflex_err_t reflex_hal_console_init(void) {
    if (s_usj_intr) return REFLEX_OK;
    s_usj_rx_head = s_usj_rx_tail = s_usj_rx_dropped = 0;

    /* Clear before enabling, so a packet that arrived during boot does not
     * deliver immediately into a handler that has not been routed yet. */
    REFLEX_REG(REFLEX_USJ_INT_CLR_REG) = REFLEX_USJ_OUT_RECV_PKT_INT;

    reflex_err_t rc =
        reflex_hal_intr_alloc(REFLEX_INTR_SRC_USB_SERIAL_JTAG, 0, usj_rx_isr, NULL, &s_usj_intr);
    if (rc != REFLEX_OK) {
        s_usj_intr = NULL;
        return rc;
    }
    /* The one unavoidable write to the shared register: the peripheral will
     * not raise the interrupt at all unless its own enable bit is set. Done
     * once, at init, with interrupts disabled, and never touched again —
     * throttling happens at the controller instead. */
    {
        uint32_t saved;
        __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));
        REFLEX_REG(REFLEX_USJ_INT_ENA_REG) |= REFLEX_USJ_OUT_RECV_PKT_INT;
        if (saved & 0x8u) {
            __asm__ volatile("csrsi mstatus, 0x8");
        }
    }
    return REFLEX_OK;
}

bool reflex_hal_console_read(uint8_t *out) {
    if (!out) return false;
    uint32_t tail = s_usj_rx_tail;
    if (tail == s_usj_rx_head) {
        /* Empty, but the ISR may have masked itself on a full ring and the
         * consumer has since drained it. Re-arm here rather than only on a
         * successful read, or a ring drained exactly to empty would never be
         * refilled and the console would stop for good. */
        if (s_usj_rx_stalled) {
            s_usj_rx_stalled = false;
            reflex_hal_intr_set_enabled(s_usj_intr, true);
        }
        return false;
    }
    *out = s_usj_rx_ring[tail];
    s_usj_rx_tail = (tail + 1u) & (USJ_RX_RING_SIZE - 1u);
    if (s_usj_rx_stalled) {
        s_usj_rx_stalled = false;
        reflex_hal_intr_set_enabled(s_usj_intr, true);
    }
    return true;
}

uint32_t reflex_hal_console_dropped(void) {
    return s_usj_rx_dropped;
}

void reflex_hal_console_debug(reflex_console_debug_t *out) {
    if (!out) return;
    out->isr_count = s_usj_isr_count;
    out->isr_bytes = s_usj_isr_bytes;
    out->head = s_usj_rx_head;
    out->tail = s_usj_rx_tail;
    out->int_ena = REFLEX_REG(REFLEX_USJ_INT_ENA_REG);
    out->int_raw = REFLEX_REG(REFLEX_USJ_INT_RAW_REG);
    out->ep1_conf = REFLEX_REG(REFLEX_USJ_EP1_CONF_REG);
    out->installed = (s_usj_intr != NULL);
}

void reflex_hal_write_raw(const char *data, int len) {
    usj_write_bytes(data, len);
}

void reflex_hal_log(int level, const char *tag, const char *fmt, ...) {
    /* On the stack, not static.
     *
     * A single shared static buffer meant two tasks logging concurrently
     * clobbered each other's message: task A formats, task B formats over the
     * top, task A then writes B's bytes. FreeRTOS preempts, so this is a real
     * race and not a theoretical one.
     *
     * 192 bytes against the smallest task stack in the tree (2048, used by
     * temp-poll and the bonsai experiments) is under 10%, and the longest line
     * this firmware actually emits is around 110 characters — the atlas verify
     * summary. Buying reentrancy for that is a better trade than a lock, which
     * would have to be held across usj_write_bytes and therefore across a
     * spin on a USB FIFO. */
    char log_buf[192];
    const char *prefix;
    switch (level) {
        case REFLEX_LOG_LEVEL_ERROR: prefix = "E"; break;
        case REFLEX_LOG_LEVEL_WARN:  prefix = "W"; break;
        case REFLEX_LOG_LEVEL_INFO:  prefix = "I"; break;
        case REFLEX_LOG_LEVEL_DEBUG: prefix = "D"; break;
        default:                     prefix = "I"; break;
    }
    /* Formatting lives in core/log.c so it is exercised by the host suite.
     * This function is now just prefix selection plus the platform write. */
    va_list args;
    va_start(args, fmt);
    size_t off = reflex_log_format(log_buf, sizeof(log_buf), prefix, tag, fmt, args);
    va_end(args);
    usj_write_bytes(log_buf, (int)off);
}
