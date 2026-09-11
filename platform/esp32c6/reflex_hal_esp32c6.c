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

/* ---- Low-power watchdog ------------------------------------------------
 *
 * A net under deep sleep. The reason sleep is the one dependency this file has
 * not taken over is that getting the entry wrong does not report an error — the
 * board simply never wakes, and nothing is left running to say why. Recovery
 * means hands on the hardware.
 *
 * This watchdog lives in the always-on domain and, with WDT_PAUSE_IN_SLP clear,
 * keeps counting while the rest of the chip is powered down. Armed before a
 * sleep and disarmed after a successful wake, it turns "never wakes" into
 * "reboots a few seconds later" — which is the difference between an experiment
 * that can be iterated and one that costs a bench visit each time it is wrong.
 *
 * The net has to be proved before it is trusted. That it is armed is not
 * evidence that it fires, and the assumption most likely to be wrong is the one
 * that matters most: whether it really keeps running through the sleep
 * power-down. */

#define LP_WDT_STAGE_OFF 0u
/* 1 raises an interrupt and latches INT_RAW; it resets nothing. */
#define LP_WDT_STAGE_INTERRUPT 1u
/* 3 resets the CPU and peripherals; 4 resets the RTC domain as well.
 *
 * RESET_RTC was the first choice, reasoning that a board asleep with domains
 * powered down needs the most thorough reset available. It is worse. After
 * firing once, the board reset every five to eight seconds indefinitely — with
 * the watchdog itself reading back disarmed, so the loop was not the watchdog
 * re-firing but the state it had left behind. Tearing down the RTC domain
 * loses something the boot path does not re-establish. Only reflashing
 * recovered it, twice.
 *
 * RESET_SYSTEM returns a running board, which is the whole requirement of a
 * recovery net. */
#define LP_WDT_STAGE_RESET_SYSTEM 3u
/* 7 == 3.2us, matching ESP-IDF. 0 is 100ns and does not take. */
#define LP_WDT_RESET_LEN_3_2US 7u

/* Nominal RC_SLOW, and deliberately left nominal.
 *
 * Measured, because an earlier version of this comment claimed the figure was
 * calibrated when nothing had calibrated it. Asked for 5000ms the watchdog
 * fires at about 5300ms — three consecutive trials agreed on 5.30s, so the real
 * oscillator is nearer 128 kHz than the nominal 136 kHz and every timeout runs
 * roughly 6% long. (Two further samples read 1.7s and 9.5s; those were the
 * measuring harness losing the port across the reset, not the watchdog.)
 *
 * Correcting the constant would make the requested interval accurate at this
 * temperature and no other — RC_SLOW drifts. Erring long is the safe direction
 * for a net: firing late still recovers a board that never woke, while firing
 * early resets one that was working. So the bias is kept and named rather than
 * tuned away. */
#define LP_WDT_SLOW_HZ 136000u

static void wdt_unlock(void) {
    REFLEX_REG(REFLEX_LP_WDT_WPROTECT_REG) = REFLEX_LP_WDT_WKEY;
}

static void wdt_lock(void) {
    REFLEX_REG(REFLEX_LP_WDT_WPROTECT_REG) = 0u;
}

/* Arm with the interrupt stage action instead of a reset, and report whether
 * stage 0 has expired.
 *
 * This exists to answer one question without destroying a board to do it: does
 * the LP watchdog count while the chip is in deep sleep? The recorded attempts
 * to find out armed it for a reset and read the answer from what the board did
 * next, and both reset actions cost a board — RESET_RTC left it resetting every
 * five to eight seconds until it was reflashed, RESET_SYSTEM left it
 * unreachable by the shell *and* by esptool until it was power-cycled. Neither
 * is a measurement anyone can afford to repeat.
 *
 * Stage action 1 raises an interrupt and latches LP_WDT.INT_RAW bit 31 instead.
 * Nothing is wired to that interrupt, so nothing happens — the bit is simply
 * there to be read afterwards, which is all the experiment needs. */
reflex_err_t reflex_hal_wdt_arm_observe(uint32_t timeout_ms) {
    if (timeout_ms == 0u) return REFLEX_ERR_INVALID_ARG;
    uint64_t ticks = ((uint64_t)timeout_ms * LP_WDT_SLOW_HZ) / 1000u;
    if (ticks == 0u || ticks > 0xFFFFFFFFULL) return REFLEX_ERR_INVALID_ARG;

    wdt_unlock();
    /* Clear any latched expiry so the bit read afterwards is this run's.
     * Through INT_CLR: writing the bit back to INT_RAW does not clear it, which
     * the first version of this did and which showed up as an arm that
     * reported expired=1 immediately. */
    REFLEX_REG(REFLEX_LP_WDT_INT_CLR_REG) = REFLEX_LP_WDT_INT_RAW_BIT;
    REFLEX_REG(REFLEX_LP_WDT_CONFIG1_REG) = (uint32_t)ticks;
    uint32_t c0 = REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG);
    c0 &= ~(REFLEX_LP_WDT_STG_MASK << REFLEX_LP_WDT_STG0_S);
    c0 |= (LP_WDT_STAGE_INTERRUPT << REFLEX_LP_WDT_STG0_S);
    c0 &= ~REFLEX_LP_WDT_PAUSE_IN_SLP;
    c0 |= REFLEX_LP_WDT_EN;
    REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG) = c0;
    REFLEX_REG(REFLEX_LP_WDT_FEED_REG) = 1u;
    wdt_lock();
    return REFLEX_OK;
}

static uint32_t s_wdt_entry_c0, s_wdt_entry_c1;
static bool s_wdt_entry_expired;

/* Declared here, not in reflex_rom_esp32c6.h, and deliberately.
 *
 * ESP-IDF's rom/rtc.h declares this as returning its own RESET_REASON enum.
 * Boot0 includes both that header and Reflex's, so a second declaration in the
 * shared header is a conflicting-types error at compile time — the same
 * collision reflex_kv_flash.c already documents for the ROM flash routines.
 * This translation unit is the only one that needs the value, so the
 * declaration lives here where it cannot collide. The return is an int-sized
 * enum; the codes are in soc/reset_reasons.h (0x05 deep-sleep wake, 0x10 the
 * low-power watchdog, 0x12 the super watchdog, 0x01 power-on). */
extern int rtc_get_reset_reason(int cpu_no);

static uint32_t s_reset_reason;
static bool s_reset_swd_flag;

/* Same key as the low-power watchdog's, on its own write-protect register;
 * ESP-IDF calls it LP_WDT_SWD_WKEY_VALUE and Boot0 writes the same literal.
 *
 * Arming this is destructive and the shell keeps it behind an experiment
 * flag: measured, the watchdog fires ~2.9 s after the feed stops and the
 * board does not return until reflashed, despite Boot0 re-enabling the feed
 * on every boot. */
#define LP_WDT_SWD_WKEY 0x50D83AA1u

uint32_t reflex_hal_swd_auto_feed(bool enable) {
    REFLEX_REG(REFLEX_LP_WDT_SWD_WPROTECT_REG) = LP_WDT_SWD_WKEY;
    uint32_t c = REFLEX_REG(REFLEX_LP_WDT_SWD_CONFIG_REG);
    if (enable) {
        c |= REFLEX_LP_WDT_SWD_AUTO_FEED_EN;
    } else {
        c &= ~REFLEX_LP_WDT_SWD_AUTO_FEED_EN;
    }
    REFLEX_REG(REFLEX_LP_WDT_SWD_CONFIG_REG) = c;
    uint32_t readback = REFLEX_REG(REFLEX_LP_WDT_SWD_CONFIG_REG);
    REFLEX_REG(REFLEX_LP_WDT_SWD_WPROTECT_REG) = 0u;
    return readback;
}

void reflex_hal_reset_cause(uint32_t *reason, bool *swd_flag) {
    if (reason) *reason = s_reset_reason;
    if (swd_flag) *swd_flag = s_reset_swd_flag;
}

void reflex_hal_wdt_capture_entry(void) {
    /* Captured here for the same reason as the watchdog state beside it: this
     * runs before anything in startup has a chance to clear either, and the
     * console does not exist yet to print them. */
    s_reset_reason = (uint32_t)rtc_get_reset_reason(0);
    s_reset_swd_flag =
        (REFLEX_REG(REFLEX_LP_WDT_SWD_CONFIG_REG) & REFLEX_LP_WDT_SWD_RESET_FLAG) != 0u;

    s_wdt_entry_c0 = REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG);
    s_wdt_entry_c1 = REFLEX_REG(REFLEX_LP_WDT_CONFIG1_REG);
    s_wdt_entry_expired =
        (REFLEX_REG(REFLEX_LP_WDT_INT_RAW_REG) & REFLEX_LP_WDT_INT_RAW_BIT) != 0u;
}

void reflex_hal_wdt_entry_state(uint32_t *config0, uint32_t *config1, bool *expired) {
    if (config0) *config0 = s_wdt_entry_c0;
    if (config1) *config1 = s_wdt_entry_c1;
    if (expired) *expired = s_wdt_entry_expired;
}

bool reflex_hal_wdt_expired(void) {
    return (REFLEX_REG(REFLEX_LP_WDT_INT_RAW_REG) & REFLEX_LP_WDT_INT_RAW_BIT) != 0u;
}

reflex_err_t reflex_hal_wdt_arm(uint32_t timeout_ms) {
    if (timeout_ms == 0u) return REFLEX_ERR_INVALID_ARG;
    uint64_t ticks = ((uint64_t)timeout_ms * LP_WDT_SLOW_HZ) / 1000u;
    if (ticks == 0u || ticks > 0xFFFFFFFFULL) return REFLEX_ERR_INVALID_ARG;

    wdt_unlock();
    REFLEX_REG(REFLEX_LP_WDT_CONFIG1_REG) = (uint32_t)ticks;
    uint32_t c0 = REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG);
    c0 &= ~(REFLEX_LP_WDT_STG_MASK << REFLEX_LP_WDT_STG0_S);
    c0 |= (LP_WDT_STAGE_RESET_SYSTEM << REFLEX_LP_WDT_STG0_S);
    /* Clear, not set: this is the bit that decides whether the net exists at
     * all while the chip is asleep, which is the only time it is needed. */
    c0 &= ~REFLEX_LP_WDT_PAUSE_IN_SLP;
    /* Reset pulse length, which ESP-IDF sets and the first version of this did
     * not. Left at 0 the pulse is 100ns; the watchdog expired on schedule, the
     * configuration read back exactly as intended, and the board carried on
     * regardless. Armed is not the same as working, and the register readback
     * is what showed the difference. 7 is 3.2us, which is what ESP-IDF uses. */
    c0 &= ~((REFLEX_LP_WDT_RESET_LEN_MASK << REFLEX_LP_WDT_SYS_RESET_LEN_S) |
            (REFLEX_LP_WDT_RESET_LEN_MASK << REFLEX_LP_WDT_CPU_RESET_LEN_S));
    c0 |= (LP_WDT_RESET_LEN_3_2US << REFLEX_LP_WDT_SYS_RESET_LEN_S) |
          (LP_WDT_RESET_LEN_3_2US << REFLEX_LP_WDT_CPU_RESET_LEN_S);
    c0 |= REFLEX_LP_WDT_EN;
    REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG) = c0;
    REFLEX_REG(REFLEX_LP_WDT_FEED_REG) = 1u;
    wdt_lock();
    return REFLEX_OK;
}

void reflex_hal_wdt_feed(void) {
    wdt_unlock();
    REFLEX_REG(REFLEX_LP_WDT_FEED_REG) = 1u;
    wdt_lock();
}

void reflex_hal_wdt_disarm(void) {
    wdt_unlock();
    uint32_t c0 = REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG);
    c0 &= ~(REFLEX_LP_WDT_STG_MASK << REFLEX_LP_WDT_STG0_S);
    c0 |= (LP_WDT_STAGE_OFF << REFLEX_LP_WDT_STG0_S);
    c0 &= ~REFLEX_LP_WDT_EN;
    REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG) = c0;
    wdt_lock();
}

/* Read the configuration back. Armed is not the same as working: the first
 * attempt at this net reported armed=1 and never fired, and the only way to
 * tell why is to look at what the register actually holds. */
/* Stand down ESP-IDF's stack-pointer watchpoint.
 *
 * ESP-IDF arms the assist-debug SP watchpoint with the bounds of whichever
 * FreeRTOS task is running. A scheduler that switches to its own task stacks
 * therefore trips it on the first switch, which is exactly what happened the
 * first time the Reflex scheduler was actually started:
 *
 *   Guru Meditation Error: Core 0 panic'ed (Stack protection fault).
 *   Stack pointer: 0x408369b0   Stack bounds: 0x40825c28 - 0x40827e20
 *
 * The switch was not wrong — the SP was inside the Reflex task's own stack.
 * The guard simply did not know that stack existed. Reflex has to be able to
 * take the watchpoint back before it can own scheduling, and this is that.
 *
 * Deliberately narrow: only the two SP spill bits are cleared, leaving the
 * area watchpoints alone. It is one-way for now, because whoever calls it is
 * about to stop returning. */
/* Stand down both timer-group watchdogs.
 *
 * ESP-IDF arms these — the interrupt watchdog on one group, the task watchdog
 * on the other — and both are fed from FreeRTOS. Quiesce FreeRTOS and nothing
 * feeds them, so a few seconds after the mtvec hand-off the chip resets:
 * observed exactly once as `rst:0x8 (TG1_WDT_HPSYS)`, with the scheduler having
 * started cleanly first.
 *
 * Reflex takes them down rather than feeding them, because a scheduler that
 * owns the machine should not be servicing another kernel's liveness checks.
 * That does mean no watchdog covers the hand-off — which is a real gap, and
 * the LP watchdog is not yet a usable answer to it. */
void reflex_hal_wdt_disable_timg(void) {
    REFLEX_REG(REFLEX_TIMG0_WDTWPROTECT_REG) = REFLEX_TIMG_WDT_WKEY;
    REFLEX_REG(REFLEX_TIMG0_WDTCONFIG0_REG) &= ~REFLEX_TIMG_WDT_EN;
    REFLEX_REG(REFLEX_TIMG0_WDTWPROTECT_REG) = 0u;

    REFLEX_REG(REFLEX_TIMG1_WDTWPROTECT_REG) = REFLEX_TIMG_WDT_WKEY;
    REFLEX_REG(REFLEX_TIMG1_WDTCONFIG0_REG) &= ~REFLEX_TIMG_WDT_EN;
    REFLEX_REG(REFLEX_TIMG1_WDTWPROTECT_REG) = 0u;
}

void reflex_hal_stack_guard_disable(void) {
    REFLEX_REG(REFLEX_ASSIST_DEBUG_MONTR_ENA_REG) &=
        ~(REFLEX_ASSIST_DEBUG_SP_SPILL_MIN_ENA | REFLEX_ASSIST_DEBUG_SP_SPILL_MAX_ENA);
}

void reflex_hal_wdt_regs(uint32_t *config0, uint32_t *config1) {
    if (config0) *config0 = REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG);
    if (config1) *config1 = REFLEX_REG(REFLEX_LP_WDT_CONFIG1_REG);
}

bool reflex_hal_wdt_armed(void) {
    return (REFLEX_REG(REFLEX_LP_WDT_CONFIG0_REG) & REFLEX_LP_WDT_EN) != 0u;
}

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
 * Reflex's interrupt table is now the only one on this path.
 *
 * This block used to document a borrowed ESP-IDF interrupt layer:
 * intr_handler_set, intr_handler_get and intr_handler_get_arg, defined in
 * ESP-IDF's riscv component (components/riscv/interrupt.c) and keeping their
 * own table, s_intr_handlers. An earlier version of this comment claimed they
 * lived in mask ROM, and that claim mattered — declared locally rather than
 * included, and believed to be ROM, they read as a hardware fact rather than as
 * the four Tier C dependencies they were.
 *
 * All four are gone from the own-entry build, in two steps, and neither step
 * was a rename. Both tempting shortcuts were refused: wrapping intr_handler_set
 * with --wrap, or calling _global_interrupt_handler directly, leave ESP-IDF's
 * table exactly where it is and only change which symbol names it.
 *
 *   - intr_handler_set, in alloc and free, went by changing *when* the vector
 *     is taken. The entry path used to allocate the tick, prove it under
 *     ESP-IDF's vector, and only then install Reflex's; during that window
 *     ESP-IDF's table was the only thing that could deliver a Reflex interrupt.
 *     It now quiesces, takes mtvec, and then starts the tick, so every line
 *     Reflex allocates is dispatched through s_intr_table from its first
 *     interrupt onward. The safety property was not traded for the count — the
 *     tick is now proved under the vector that will actually service it, and a
 *     dead tick still hands the machine back, because reflex_trap_snapshot made
 *     taking mtvec a reversible step.
 *
 *   - intr_handler_get and intr_handler_get_arg, in dispatch_foreign, went by
 *     moving *who owns the registration*. --wrap=esp_intr_alloc sends a
 *     driver's allocation into reflex_hal_intr_alloc, so its ISR is filed here
 *     and dispatched by reflex_hal_intr_dispatch_line like any other. That is
 *     the opposite of wrapping intr_handler_set: the store moves, rather than
 *     the name of the thing that writes to it. With no reader of ESP-IDF's
 *     table left, dispatch_foreign was deleted rather than fenced. See
 *     reflex_intr_espidf_shim.c.
 *
 * ESP-IDF's own intr_handler_set and intr_handler_get are still *defined* in
 * the image, because __real_esp_intr_alloc is still linked and still used for
 * every allocation made before Reflex takes mtvec. Nothing in Reflex references
 * them. That is the honest statement, and it is the one the object files
 * support: no reflex_*.c.obj in build_own_entry carries an undefined reference
 * to any of the three.
 *
 * tools/check_independence.py counts local externs under EXTERN_TIERS, because
 * a dependency reached by a local extern is as real as one reached by an
 * #include and was invisible to the measure until it did. The three names are
 * kept in that table deliberately: if one ever comes back, it is classified
 * rather than reported as an unrecognised symbol. */

#define INTMTX_BASE           0x60010000
#define INTMTX_SOURCE_MAX     63
#define PLIC_MX_BASE          0x20001000
#define PLIC_MXINT_ENABLE     (PLIC_MX_BASE + 0x00)
#define PLIC_MXINT_TYPE       (PLIC_MX_BASE + 0x04)
#define PLIC_MXINT_CLEAR      (PLIC_MX_BASE + 0x08)
#define PLIC_MXINT_PRI(n)     (PLIC_MX_BASE + 0x10 + (n) * 4)
#define PLIC_MXINT_THRESH     (PLIC_MX_BASE + 0x90)

/* Silence every CPU interrupt line except the ones named, and report what was
 * enabled before.
 *
 * Taking mtvec means Reflex's handler receives every interrupt the PLIC
 * forwards, including the nine ESP-IDF has live — its own tick among them. The
 * handler deliberately does not acknowledge a line it does not recognise,
 * because acknowledging one blind is worse; so a line left enabled asserts,
 * is not cleared, and the core stops making progress. Quiescing first is what
 * makes the hand-off survivable rather than instantaneous deadlock.
 *
 * Returns the previous mask so the caller can put it back, which matters
 * because the shell has to survive an experiment that fails. */
uint32_t reflex_hal_intr_quiesce_except(uint32_t keep_mask) {
    uint32_t saved;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));
    uint32_t prev = REFLEX_REG(PLIC_MXINT_ENABLE);
    REFLEX_REG(PLIC_MXINT_ENABLE) = prev & keep_mask;
    if (saved & 0x8u) {
        __asm__ volatile("csrsi mstatus, 0x8");
    }
    return prev;
}

void reflex_hal_intr_restore(uint32_t mask) {
    uint32_t saved;
    __asm__ volatile("csrrci %0, mstatus, 0x8" : "=r"(saved));
    REFLEX_REG(PLIC_MXINT_ENABLE) = mask;
    if (saved & 0x8u) {
        __asm__ volatile("csrsi mstatus, 0x8");
    }
}

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
    /* What the line held before Reflex took it, so freeing can put it back.
     *
     * alloc sets a PLIC priority, installs a dispatch handler and flips the
     * line's edge/level bit; free used to clear only the enable bit and the
     * matrix routing. A line handed back that way still carries Reflex's
     * handler and a changed priority and type — which is wrong on its own
     * terms, and breaks a board when the line is taken and released before
     * ESP-IDF's interrupt world exists: bisected to exactly that, a build that
     * boots and then fails every hardware check. */
    uint32_t prev_priority;
    uint32_t prev_type_bit;
    uint32_t prev_mie_bit;
    bool taken;
} reflex_intr_entry_t;

/* Routing a source here detaches it. See reflex_hal_intr_free for why this is
 * 6 and not 0, and why it cannot be a bridge-verified constant. */
#define INTMTX_DISABLED_LINE 6u

static reflex_intr_entry_t s_intr_table[32];

/* The ROM vector table calls registered handlers as fn(void).
 * We use a single dispatch that scans the pending interrupt from
 * the PLIC claim register would be ideal, but the C6's interrupt
 * controller doesn't expose a standard claim. Instead, we use the
 * mcause CSR which holds the interrupt number during an ISR. */
/* Dispatch one line from Reflex's own trap handler.
 *
 * The same table, reached a different way. When ESP-IDF owns mtvec its vectored
 * dispatch calls reflex_intr_dispatch below, which recovers the line from
 * mcause. When Reflex owns mtvec, reflex_trap_handler has already decoded the
 * line and passes it in.
 *
 * That second path did not exist, and its absence is what a shell prompt with
 * no keyboard looks like: reflex_trap_handler serviced the scheduler tick and
 * deliberately left every other line alone, so the console receive interrupt —
 * Reflex's own, allocated through Reflex's own allocator — was never delivered
 * to the handler sitting in this table waiting for it. Output worked because
 * output does not need an interrupt.
 *
 * Returns false when nothing is registered for the line, so the caller can keep
 * its existing behaviour of leaving an unknown interrupt alone rather than
 * acknowledging something it does not understand.
 *
 * No PLIC acknowledgement here: Reflex allocates its lines level-triggered, so
 * the line deasserts when the handler clears its peripheral's own status. An
 * edge-triggered line would need PLIC_MXINT_CLEAR, and none is allocated. */
/* Lines masked because nothing anywhere claimed them. Declared here because
 * both the adopter below and the masker further down touch it. */
static volatile uint32_t s_unclaimed_lines;

/* Hand a line Reflex does not own to whoever ESP-IDF registered for it.
 *
 * Reflex's dispatch table only knows about interrupts Reflex allocated. ESP-IDF
 * keeps its own per-line table, and it has a getter, so a line that is nobody's
 * as far as Reflex is concerned may still have a driver waiting on it — the
 * radio being the case that matters. Trying this before masking is the
 * difference between a machine with its peripherals and one without.
 *
 * Returns false when ESP-IDF has nothing registered either, which is the only
 * case where masking is the honest answer. */

/* Lines that fired with nobody registered for them, and were masked.
 *
 * A level-triggered interrupt that no handler acknowledges re-enters the moment
 * the handler returns, forever. The core makes no progress, prints nothing and
 * takes no fault: it is indistinguishable from a hang, and that is exactly what
 * it looked like — a boot that stopped at a different line each time depending
 * on when the first such interrupt happened to arrive.
 *
 * Masking the line at the controller trades a dead machine for a degraded one
 * that can still say what happened. It is the right trade for a trap handler
 * that has just been handed the whole machine: leaving the source asserted was
 * defensible when Reflex owned mtvec only for the length of one experiment. */

uint32_t reflex_hal_intr_unclaimed_lines(void) {
    return s_unclaimed_lines;
}

void __attribute__((section(".iram1"))) reflex_hal_intr_mask_unclaimed(int cpu_int) {
    if (cpu_int < 0 || cpu_int >= 32) return;
    /* No mstatus bracketing around the read-modify-write, unlike every other
     * writer of PLIC_MXINT_ENABLE in this file.
     *
     * Stated rather than left to be noticed, because the difference is not an
     * oversight and not free: this runs only from reflex_trap_handler, and trap
     * entry has already cleared MIE, so the sequence is already atomic against
     * preemption on this single core. Disabling interrupts again would be
     * harmless but would misrepresent where this can be called from. It cannot
     * be called from task context — if that ever changes, this needs the same
     * bracketing as reflex_hal_intr_set_enabled. */
    s_unclaimed_lines |= (1U << cpu_int);
    REFLEX_REG(PLIC_MXINT_ENABLE) &= ~(1U << cpu_int);
}

bool __attribute__((section(".iram1"))) reflex_hal_intr_dispatch_line(int cpu_int) {
    if (cpu_int < 0 || cpu_int >= 32) return false;
    reflex_intr_entry_t *e = &s_intr_table[cpu_int];
    if (!e->handler) return false;
    e->handler(e->arg);
    return true;
}

#ifndef REFLEX_OWN_ENTRY
/* ESP-IDF's table calls handlers with no argument, so the line has to be
 * recovered from mcause. Reflex's own trap handler passes the line in, which is
 * why reflex_hal_intr_dispatch_line above needs none of this — and why this
 * function has no caller once Reflex owns the vector. */
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
#endif /* !REFLEX_OWN_ENTRY */

/* Is Reflex's vector the one the hardware will actually use?
 *
 * Read from the CSR rather than tracked in a flag, because the question is not
 * "did we call reflex_trap_install" but "where will the next interrupt go".
 * mtvec's low two bits are the mode field and the base is masked to 256 bytes
 * by the hardware, so both sides are masked before comparing.
 *
 * Two callers, and they want the same fact for opposite reasons: the allocator
 * below refuses to hand out a line before this is true, and the esp_intr_alloc
 * shim uses it to decide whose interrupt table a foreign driver should land in.
 */
bool reflex_hal_intr_vector_is_reflex(void) {
    extern uint32_t reflex_vector_table[];
    uint32_t mtvec;
    __asm__ volatile("csrr %0, mtvec" : "=r"(mtvec));
    return (mtvec & ~0xFFu) == ((uint32_t)(uintptr_t)reflex_vector_table & ~0xFFu);
}

reflex_err_t reflex_hal_intr_alloc(int source, int flags,
                                   reflex_intr_handler_t handler, void *arg,
                                   reflex_intr_handle_t *out_handle) {
    (void)flags;

    if (source < 0 || source > INTMTX_SOURCE_MAX)
        return REFLEX_ERR_INVALID_ARG;
    if (!handler)
        return REFLEX_ERR_INVALID_ARG;

#ifdef REFLEX_OWN_ENTRY
    /* Refuse to allocate before Reflex's vector is the one on mtvec.
     *
     * This build no longer registers anything in ESP-IDF's interrupt table, so
     * a line allocated while ESP-IDF still owns mtvec is routed, enabled,
     * above threshold — and delivered to a dispatcher that has never heard of
     * it. Nothing faults. The interrupt simply never arrives, which for the
     * tick means a scheduler that parks on wfi and goes quiet, and that is
     * indistinguishable from a hang.
     *
     * The ordering in reflex_app_entry.c is what makes this safe: the vector is
     * taken before the first allocation. That ordering is a property of one
     * file, and the failure it prevents is silent, so it is checked here rather
     * than trusted. A caller added later that allocates too early gets an error
     * it can print instead of a board that stops.
     *
     * Read from the CSR rather than tracked in a flag, because the question is
     * what the hardware will actually do with the interrupt. mtvec's low two
     * bits are the mode field and the base is 256-byte aligned, so both sides
     * are masked before comparing. */
    {
        uint32_t mtvec;
        __asm__ volatile("csrr %0, mtvec" : "=r"(mtvec));
        if (!reflex_hal_intr_vector_is_reflex()) {
            /* esp_rom_printf, not REFLEX_LOGE. This file is where REFLEX_LOG*
             * is implemented, and the log path takes a lock and writes through
             * the console — neither of which is a good idea from inside the
             * interrupt allocator. The ROM routine is the same escape hatch
             * reflex_trap_handler uses, for the same reason. */
            extern uint32_t reflex_vector_table[];
            esp_rom_printf("[reflex.hal] intr_alloc(source=%d) before Reflex took mtvec "
                           "(mtvec=0x%x, expected base 0x%x): it would never be delivered\n",
                           source, (unsigned)mtvec,
                           (unsigned)((uint32_t)(uintptr_t)reflex_vector_table));
            return REFLEX_ERR_INVALID_STATE;
        }
    }
#endif

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
    /* A line masked earlier as unclaimed is claimed now, so drop it from the
     * record. Without this the report is a list of lines that were once
     * unclaimed rather than lines that are currently lost, and the PLIC enable
     * below silently contradicts it. */
    s_unclaimed_lines &= ~(1U << cpu_int);

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
    /* Record what is being displaced before displacing it. */
    s_intr_table[cpu_int].prev_priority = REFLEX_REG(PLIC_MXINT_PRI(cpu_int));
    s_intr_table[cpu_int].prev_type_bit = (REFLEX_REG(PLIC_MXINT_TYPE) >> cpu_int) & 1u;
    s_intr_table[cpu_int].taken = true;
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
#ifndef REFLEX_OWN_ENTRY
    /* Only where ESP-IDF's vector is still the one on mtvec.
     *
     * This registers Reflex's dispatcher in ESP-IDF's per-line table so that
     * ESP-IDF's `_global_interrupt_handler` delivers Reflex's own interrupts.
     * In the default build that is the only way they arrive at all, and it is
     * load-bearing: `kernel selftest` allocates the tick with ESP-IDF holding
     * mtvec throughout.
     *
     * Under REFLEX_OWN_ENTRY it is dead weight, and the reason is an ordering
     * change rather than a trick. The entry path now installs Reflex's vector
     * *before* the first allocation, so every line Reflex allocates is
     * dispatched through s_intr_table by reflex_trap_handler and ESP-IDF's
     * table is never consulted. It used to be the other way round — allocate,
     * prove the tick under ESP-IDF's vector, then install — and that ordering
     * was the sole reason this call had to exist.
     *
     * The safety property that ordering bought is not given up. It is stronger
     * now: the tick is proved under the vector that will actually service it,
     * and a failure still hands the machine back, because reflex_trap_snapshot
     * makes taking mtvec a reversible step. Proving it under ESP-IDF's vector
     * only ever proved the routing, not the dispatcher. */
    extern void intr_handler_set(int n, void (*fn)(void), void *arg);
    intr_handler_set(cpu_int, (void (*)(void))reflex_intr_dispatch, NULL);
#endif

    /* Disable interrupts for atomic RMW of shared registers */
    __asm__ volatile ("csrci mstatus, 0x8");

    /* Level-triggered (clear the type bit) */
    uint32_t type = REFLEX_REG(PLIC_MXINT_TYPE);
    type &= ~(1U << cpu_int);
    REFLEX_REG(PLIC_MXINT_TYPE) = type;

    /* Enable the line in the mie CSR — bit n, not bit 16+n.
     *
     * This said 16+n, which is the ESP32-C3 mapping, and on the C6 it set the
     * bit belonging to a completely different line: for the scheduler tick on
     * line 11 it enabled line 27, which is one of Wi-Fi's. The tick's own bit
     * was never set by Reflex at all.
     *
     * It went unnoticed for as long as it did because it only matters when
     * nobody else has already enabled the line. Measured across two builds
     * that differ in nothing but the radio, with every PLIC register
     * identical — enable, priority, threshold, type — and only one source
     * routed to the line:
     *
     *   802.15.4 build: mie=0x0f000f24, bit 11 = 1  ->  998 Hz
     *   Wi-Fi build:    mie=0x0f000726, bit 11 = 0  ->  0 Hz, source asserting
     *                                                  in 1000 of 1000 samples
     *
     * In the first, ESP-IDF had already claimed line 11 for source 12 and set
     * the bit; Reflex took the line over and inherited it. In the second
     * nothing had claimed line 11, so the bit stayed clear and a correctly
     * routed, PLIC-enabled, above-threshold interrupt was simply never
     * delivered. The console survived the same bug the same way — ESP-IDF had
     * already enabled line 10.
     *
     * The previous value is recorded rather than assumed clear, because the
     * line may well be one ESP-IDF is using, and free restores it. */
    uint32_t mie;
    __asm__ volatile("csrr %0, mie" : "=r"(mie));
    s_intr_table[cpu_int].prev_mie_bit = (mie >> cpu_int) & 1u;
    mie |= (1U << cpu_int);
    __asm__ volatile("csrw mie, %0" : : "r"(mie));

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
/* Dump the whole interrupt controller, not the three fields of one line.
 *
 * Every per-line field read back so far is identical between a build where the
 * tick is delivered and one where it is not — same enable bit, same priority,
 * same type, same threshold, same mstatus. When two configurations differ in
 * behaviour and agree on every register you have looked at, the difference is
 * in one you have not, and narrowing further by argument has already failed
 * twice here. So this reads all of it and lets the diff say where to look. */
/* Which peripheral sources the interrupt matrix currently points at a line.
 *
 * reflex_hal_intr_alloc scans Reflex's own bitmap only and tells ESP-IDF
 * nothing, so ESP-IDF's allocator is free to hand the same CPU line to a
 * driver of its own. Sharing is invisible from the line's own registers —
 * enable, priority and type read the same either way — and it is the remaining
 * explanation for a line that is configured identically to a working one and
 * delivers nothing. This answers it by reading the matrix rather than
 * reasoning about the allocator. */
uint32_t reflex_hal_intr_sources_on_line(int cpu_int) {
    uint32_t n = 0;
    for (int s = 0; s <= INTMTX_SOURCE_MAX; s++) {
        if (REFLEX_REG(INTMTX_BASE + 4 * s) == (uint32_t)cpu_int) n++;
    }
    return n;
}

uint32_t reflex_hal_intr_first_source_on_line(int cpu_int, int after) {
    for (int s = after + 1; s <= INTMTX_SOURCE_MAX; s++) {
        if (REFLEX_REG(INTMTX_BASE + 4 * s) == (uint32_t)cpu_int) return (uint32_t)s;
    }
    return 0xFFFFFFFFu;
}

void reflex_hal_intr_dump(uint32_t *enable, uint32_t *type, uint32_t *thresh, uint8_t *pri32,
                          uint32_t *mie_raw) {
    if (mie_raw) {
        /* The whole mie CSR, not one bit of it.
         *
         * reflex_hal_intr_alloc sets bit `16 + cpu_int`, which is the C3-style
         * mapping; ESP-IDF's PLIC path never touches mie for external
         * interrupts, so that bit has never been shown to be the right one or
         * to matter. If the mapping is instead one-to-one, then a line
         * delivers only when its own bit is set — and the whole word is what
         * distinguishes "Reflex set the wrong bit and got away with it on one
         * line" from "mie is irrelevant here". */
        __asm__ volatile("csrr %0, mie" : "=r"(*mie_raw));
    }
    if (enable) *enable = REFLEX_REG(PLIC_MXINT_ENABLE);
    if (type) *type = REFLEX_REG(PLIC_MXINT_TYPE);
    if (thresh) *thresh = REFLEX_REG(PLIC_MXINT_THRESH);
    if (pri32) {
        for (int i = 0; i < 32; i++) {
            pri32[i] = (uint8_t)(REFLEX_REG(PLIC_MXINT_PRI(i)) & 0xFFu);
        }
    }
}

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

    /* Detach the sources, which is not the same as writing zero.
     *
     * This wrote 0, on the reading that zero means "no route". It does not:
     * zero is CPU interrupt line 0, and this tree already documents that —
     * reflex_sched.c records SYSTIMER asserting onto line 0 precisely because
     * its matrix entry still held the reset value. So freeing a line was
     * re-pointing every source that used it at line 0 rather than detaching
     * them.
     *
     * ESP-IDF disables a source by routing it to interrupt 6, unconditionally
     * and including RISC-V targets ("Disable using int matrix",
     * INT_MUX_DISABLED_INTNO in intr_alloc.c). That constant is private to a .c
     * file, so it cannot go through the SoC bridge like a register; the value
     * and its provenance are stated here instead.
     *
     * Currently latent: callers quiesce the peripheral first and line 0 is not
     * enabled, so nothing has ever asserted through it. Wrong all the same, and
     * exactly the kind of thing that surfaces once something else claims line
     * 0. */
    for (int s = 0; s <= INTMTX_SOURCE_MAX; s++) {
        if (REFLEX_REG(INTMTX_BASE + 4 * s) == (uint32_t)cpu_int) {
            REFLEX_REG(INTMTX_BASE + 4 * s) = INTMTX_DISABLED_LINE;
        }
    }

    /* Put back everything alloc changed, not just the enable bit.
     *
     * The priority, the edge/level bit and the vector entry were all set by
     * alloc and none of them were restored here, so a freed line still carried
     * Reflex's dispatcher at Reflex's priority. Harmless while Reflex is the
     * only allocator; not harmless when the line is taken and given back before
     * ESP-IDF has built its own interrupt state, which is what the entry path
     * does to test the tick. */
    if (s_intr_table[cpu_int].taken) {
        REFLEX_REG(PLIC_MXINT_PRI(cpu_int)) = s_intr_table[cpu_int].prev_priority;
        uint32_t type = REFLEX_REG(PLIC_MXINT_TYPE);
        type &= ~(1U << cpu_int);
        type |= (s_intr_table[cpu_int].prev_type_bit << cpu_int);
        REFLEX_REG(PLIC_MXINT_TYPE) = type;
        /* And put the mie bit back where it was. alloc sets it and free did not
         * clear it — the same acquire-without-release asymmetry this file has
         * now been through three times. Restored rather than cleared, because
         * the line may be one ESP-IDF enabled before Reflex borrowed it, and
         * clearing that would break its interrupt instead of ours. */
        uint32_t mie_now;
        __asm__ volatile("csrr %0, mie" : "=r"(mie_now));
        mie_now &= ~(1U << cpu_int);
        mie_now |= (s_intr_table[cpu_int].prev_mie_bit << cpu_int);
        __asm__ volatile("csrw mie, %0" : : "r"(mie_now));
#ifndef REFLEX_OWN_ENTRY
        /* Paired with the registration in alloc, and fenced for the same
         * reason: under REFLEX_OWN_ENTRY nothing was ever written there. */
        extern void intr_handler_set(int n, void (*fn)(void), void *arg);
        intr_handler_set(cpu_int, NULL, NULL);
#endif
        s_intr_table[cpu_int].taken = false;
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

static volatile uint32_t s_usj_isr_entries;

uint32_t reflex_hal_console_isr_entries(void) {
    return s_usj_isr_entries;
}

static void usj_rx_isr(void *arg) {
    /* Counts entries, not bytes.
     *
     * The console and the scheduler tick are allocated through the same
     * function and differ only in which CPU line they land on, so the console
     * is the control for "does a Reflex interrupt get delivered in this build
     * at all". It is only a valid control if it is genuinely interrupt-driven
     * here rather than falling back to polling, and a shell that answers
     * keystrokes cannot tell those apart. This can. */
    s_usj_isr_entries++;
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

/* ---- LEDC: Reflex's own PWM, replacing driver/ledc.h -------------------- */

/* Tier E. The one consumer is `bonsai exp4`, which drives a single channel off
 * a single timer, so this owns exactly that and says so rather than pretending
 * to be a general LEDC driver.
 *
 * Every register and field below comes from the SoC bridge, which proves each
 * equal to the ESP-IDF macro it replaces. That is not ceremony: the transmit
 * timeout in this same file was written against a hand-assumed CSR that reads
 * as zero here, and reflex_hal_time_us once ran against a hand-typed base
 * address that pointed at I2C. */

#define LEDC_XTAL_HZ 40000000u
#define LEDC_SCLK_SEL_XTAL 3u
/* The duty register carries four fractional bits below the integer duty. */
#define LEDC_DUTY_FRAC_BITS 4u

reflex_err_t reflex_hal_pwm_init(uint32_t freq_hz, uint8_t duty_res_bits, uint32_t duty) {
    if (freq_hz == 0u || duty_res_bits == 0u || duty_res_bits > 20u) {
        return REFLEX_ERR_INVALID_ARG;
    }
    uint32_t max_duty = 1u << duty_res_bits;
    if (duty > max_duty) return REFLEX_ERR_INVALID_ARG;

    /* Output frequency is source / (divider * 2^resolution), with the divider
     * held as Q10.8 — so the integer part is what gets multiplied by 256 here,
     * not the result. Rejecting a divider below 1.0 matters: the field would
     * silently truncate and the pin would run at a frequency nobody asked for,
     * which is the kind of wrong that looks like working. */
    uint64_t div_q8 = ((uint64_t)LEDC_XTAL_HZ << 8) / ((uint64_t)freq_hz * max_duty);
    if (div_q8 < (1u << 8) || div_q8 > REFLEX_LEDC_CLK_DIV_MASK) {
        return REFLEX_ERR_INVALID_ARG;
    }

    /* Ungate the peripheral and let it out of reset before writing anything
     * into it. RST_EN asserted means held in reset, so this clears it. */
    REFLEX_REG(REFLEX_PCR_LEDC_CONF_REG) |= REFLEX_PCR_LEDC_CLK_EN;
    REFLEX_REG(REFLEX_PCR_LEDC_CONF_REG) &= ~REFLEX_PCR_LEDC_RST_EN;

    uint32_t sclk = REFLEX_REG(REFLEX_PCR_LEDC_SCLK_CONF_REG);
    sclk &= ~(REFLEX_PCR_LEDC_SCLK_SEL_MASK << REFLEX_PCR_LEDC_SCLK_SEL_S);
    sclk |= (LEDC_SCLK_SEL_XTAL << REFLEX_PCR_LEDC_SCLK_SEL_S) | REFLEX_PCR_LEDC_SCLK_EN;
    REFLEX_REG(REFLEX_PCR_LEDC_SCLK_CONF_REG) = sclk;

    /* Timer 0. Written whole, which also clears RST — it reads high out of
     * reset, and a timer left in reset produces no edges at all. */
    uint32_t tconf =
        (((uint32_t)duty_res_bits & REFLEX_LEDC_DUTY_RES_MASK) << REFLEX_LEDC_DUTY_RES_S) |
        (((uint32_t)div_q8 & REFLEX_LEDC_CLK_DIV_MASK) << REFLEX_LEDC_CLK_DIV_S);
    REFLEX_REG(REFLEX_LEDC_TIMER0_CONF_REG) = tconf;
    REFLEX_REG(REFLEX_LEDC_TIMER0_CONF_REG) = tconf | REFLEX_LEDC_TIMER_PARA_UP;

    /* Channel 0 on timer 0. Duty is latched by DUTY_START; the channel's own
     * configuration by PARA_UP. Both are needed — setting the duty register
     * alone changes nothing the output can see. */
    REFLEX_REG(REFLEX_LEDC_CH0_HPOINT_REG) = 0u;
    REFLEX_REG(REFLEX_LEDC_CH0_DUTY_REG) = (duty << LEDC_DUTY_FRAC_BITS) & REFLEX_LEDC_DUTY_MASK;
    REFLEX_REG(REFLEX_LEDC_CH0_CONF1_REG) = REFLEX_LEDC_DUTY_START;
    REFLEX_REG(REFLEX_LEDC_CH0_CONF0_REG) =
        ((0u & REFLEX_LEDC_TIMER_SEL_MASK) << REFLEX_LEDC_TIMER_SEL_S) | REFLEX_LEDC_SIG_OUT_EN |
        REFLEX_LEDC_CH_PARA_UP;
    return REFLEX_OK;
}

/* Give LEDC back.
 *
 * pwm_init ungates the peripheral, selects its clock source, configures a timer
 * and a channel and drives a pin — and nothing undid any of it. `bonsai exp4
 * detach` re-routed the pin to plain GPIO and left LEDC clocked and running for
 * the rest of the boot.
 *
 * That is the same asymmetry just fixed twice in reflex_hal_intr_free, and it
 * was found by asking which other acquire in this file has no matching release:
 * PCNT and RMT were given one earlier, PWM was not. Stopping the channel
 * driving before gating the clock matters — gating first freezes whatever level
 * the output happened to be at. */
void reflex_hal_pwm_release(void) {
    uint32_t c0 = REFLEX_REG(REFLEX_LEDC_CH0_CONF0_REG);
    c0 &= ~REFLEX_LEDC_SIG_OUT_EN;
    REFLEX_REG(REFLEX_LEDC_CH0_CONF0_REG) = c0;
    REFLEX_REG(REFLEX_LEDC_CH0_CONF0_REG) = c0 | REFLEX_LEDC_CH_PARA_UP;

    REFLEX_REG(REFLEX_PCR_LEDC_SCLK_CONF_REG) &= ~REFLEX_PCR_LEDC_SCLK_EN;
    REFLEX_REG(REFLEX_PCR_LEDC_CONF_REG) &= ~REFLEX_PCR_LEDC_CLK_EN;
}

void reflex_hal_pwm_snapshot(reflex_pwm_snapshot_t *out) {
    if (!out) return;
    out->timer_conf = REFLEX_REG(REFLEX_LEDC_TIMER0_CONF_REG);
    out->ch_conf0 = REFLEX_REG(REFLEX_LEDC_CH0_CONF0_REG);
    out->ch_duty = REFLEX_REG(REFLEX_LEDC_CH0_DUTY_REG);
    out->pcr_conf = REFLEX_REG(REFLEX_PCR_LEDC_CONF_REG);
    out->pcr_sclk = REFLEX_REG(REFLEX_PCR_LEDC_SCLK_CONF_REG);
}

/* ---- RMT -------------------------------------------------------------- */

/* Channel memory. ESP-IDF places it by linker script — `PROVIDE (RMTMEM =
 * 0x60006400)` in esp32c6.peripherals.ld — so there is no macro for the SoC
 * bridge to compare against and the offset is stated here instead, against a
 * base that *is* bridge-verified. It is not taken on trust: a wrong address
 * means the channel transmits nothing at all, which the equivalence check and
 * exp5's own count both show immediately. */
#define RMT_CH0_MEM (REFLEX_DR_REG_RMT_BASE + 0x400u)

/* One 48-word block per channel; two of them cover the 64 symbols exp5 asks
 * for, which is why ESP-IDF leaves MEM_SIZE at 2. */
#define RMT_MEM_WORDS_PER_BLOCK 48u

/* The clock ESP-IDF selects for this channel, read back from the configuration
 * it left rather than derived: PCR picks source 1 with no division, and the
 * channel's own DIV_CNT of 80 turns it into the 1 MHz exp5 asked for. So the
 * source is 80 MHz. */
#define RMT_SCLK_SEL_80M 1u
#define RMT_SRC_HZ 80000000u

/* Which pin the channel drives, so release can give it back. 0xFF means none,
 * since 0 is a real pin. */
static uint32_t s_rmt_pin = 0xFFu;

uint32_t reflex_hal_rmt_symbol(uint16_t dur0, bool lvl0, uint16_t dur1, bool lvl1) {
    /* A symbol is two runs packed into one word: 15 bits of duration and a
     * level, twice. Durations are in channel ticks, so they mean whatever
     * resolution the channel was configured for. */
    return ((uint32_t)(dur0 & 0x7FFFu)) | ((uint32_t)(lvl0 ? 1u : 0u) << 15) |
           ((uint32_t)(dur1 & 0x7FFFu) << 16) | ((uint32_t)(lvl1 ? 1u : 0u) << 31);
}

reflex_err_t reflex_hal_rmt_tx_init(uint32_t pin, uint32_t resolution_hz) {
    if (pin >= 31u || resolution_hz == 0u) return REFLEX_ERR_INVALID_ARG;
    uint32_t div = RMT_SRC_HZ / resolution_hz;
    if (div == 0u || div > (REFLEX_RMT_DIV_CNT_MASK + 1u)) return REFLEX_ERR_INVALID_ARG;
    /* Refuse a resolution the divider cannot produce exactly rather than
     * silently rounding to one it can. A caller asking for 3 MHz would
     * otherwise get 3.077 with no indication — the same shape of quiet
     * wrongness as a duty cycle computed against the wrong crystal. */
    if ((RMT_SRC_HZ % resolution_hz) != 0u) return REFLEX_ERR_INVALID_ARG;
    /* The field holds 0 for a divide of 256; every other value is itself. */
    uint32_t div_field = (div == 256u) ? 0u : div;

    REFLEX_REG(REFLEX_PCR_RMT_CONF_REG) |= REFLEX_PCR_RMT_CLK_EN;
    REFLEX_REG(REFLEX_PCR_RMT_CONF_REG) &= ~REFLEX_PCR_RMT_RST_EN;

    uint32_t sclk = REFLEX_REG(REFLEX_PCR_RMT_SCLK_CONF_REG);
    sclk &= ~((REFLEX_PCR_RMT_SCLK_DIV_A_MASK << REFLEX_PCR_RMT_SCLK_DIV_A_S) |
              (REFLEX_PCR_RMT_SCLK_DIV_B_MASK << REFLEX_PCR_RMT_SCLK_DIV_B_S) |
              (REFLEX_PCR_RMT_SCLK_DIV_NUM_MASK << REFLEX_PCR_RMT_SCLK_DIV_NUM_S) |
              (REFLEX_PCR_RMT_SCLK_SEL_MASK << REFLEX_PCR_RMT_SCLK_SEL_S));
    sclk |= (1u << REFLEX_PCR_RMT_SCLK_DIV_B_S) | (RMT_SCLK_SEL_80M << REFLEX_PCR_RMT_SCLK_SEL_S) |
            REFLEX_PCR_RMT_SCLK_EN;
    REFLEX_REG(REFLEX_PCR_RMT_SCLK_CONF_REG) = sclk;

    /* APB_FIFO_MASK selects addressing the channel memory directly rather than
     * pushing symbols through the FIFO window, which is what the memory writes
     * below depend on. */
    REFLEX_REG(REFLEX_RMT_SYS_CONF_REG) =
        REFLEX_RMT_APB_FIFO_MASK | (1u << REFLEX_RMT_SCLK_DIV_NUM_S) |
        (RMT_SCLK_SEL_80M << REFLEX_RMT_SCLK_SEL_S) | REFLEX_RMT_SCLK_ACTIVE;

    REFLEX_REG(REFLEX_RMT_CH0_TX_LIM_REG) =
        ((RMT_MEM_WORDS_PER_BLOCK & REFLEX_RMT_TX_LIM_MASK) << REFLEX_RMT_TX_LIM_S) |
        REFLEX_RMT_LOOP_STOP_EN;

    /* CARRIER_EFF_EN and CARRIER_OUT_LV are left set because that is what
     * ESP-IDF leaves, and they are inert while CARRIER_EN is clear. Matching
     * them is not cargo cult: the PCNT driver counted correctly while four bits
     * off ESP-IDF's configuration, and only an exact comparison found it. */
    REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) =
        REFLEX_RMT_MEM_TX_WRAP_EN | REFLEX_RMT_IDLE_OUT_EN |
        ((div_field & REFLEX_RMT_DIV_CNT_MASK) << REFLEX_RMT_DIV_CNT_S) |
        ((2u & REFLEX_RMT_MEM_SIZE_MASK) << REFLEX_RMT_MEM_SIZE_S) | REFLEX_RMT_CARRIER_EFF_EN |
        REFLEX_RMT_CARRIER_OUT_LV;
    REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_CONF_UPDATE;

    /* Drive the pin, and leave its input enabled so a peripheral reading the
     * same pad still sees the signal — the loopback exp5 needs to count its own
     * output. */
    reflex_hal_gpio_init_output(pin);
    REFLEX_REG(IO_MUX_PIN_REG(pin)) |= IO_MUX_FUN_IE_BIT;
    esp_rom_gpio_connect_out_signal(pin, REFLEX_RMT_SIG_OUT0_IDX, false, false);
    s_rmt_pin = pin;
    return REFLEX_OK;
}

reflex_err_t reflex_hal_rmt_tx_symbols(const uint32_t *symbols, uint32_t count,
                                       uint32_t timeout_us) {
    if (!symbols || count == 0u) return REFLEX_ERR_INVALID_ARG;
    /* Two blocks, less one word for the end marker. */
    if (count >= 2u * RMT_MEM_WORDS_PER_BLOCK) return REFLEX_ERR_INVALID_ARG;

    volatile uint32_t *mem = (volatile uint32_t *)RMT_CH0_MEM;
    for (uint32_t i = 0; i < count; i++)
        mem[i] = symbols[i];
    /* A zero-duration word ends the transmission; without it the channel runs
     * on into whatever the memory happens to hold. */
    mem[count] = 0u;

    REFLEX_REG(REFLEX_RMT_INT_CLR_REG) = REFLEX_RMT_CH0_TX_END_INT_RAW;
    REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_MEM_RD_RST | REFLEX_RMT_APB_MEM_RST;
    REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_CONF_UPDATE;
    REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_TX_START;

    uint64_t start = reflex_hal_time_us();
    while (!(REFLEX_REG(REFLEX_RMT_INT_RAW_REG) & REFLEX_RMT_CH0_TX_END_INT_RAW)) {
        if ((reflex_hal_time_us() - start) > timeout_us) {
            REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_TX_STOP;
            REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_CONF_UPDATE;
            return REFLEX_ERR_TIMEOUT;
        }
    }
    REFLEX_REG(REFLEX_RMT_INT_CLR_REG) = REFLEX_RMT_CH0_TX_END_INT_RAW;
    return REFLEX_OK;
}

void reflex_hal_rmt_release(void) {
    /* Stop, hand the pin back, gate the clock.
     *
     * The middle one is what this claimed to do and did not: the pin stayed
     * routed to the transmit signal with its input buffer still forced on, for
     * the rest of the boot. That is the debt the counter had one commit
     * earlier, and a comment asserting it was paid is worse than no comment,
     * because it is what a reader checks instead of the code. */
    REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_TX_STOP;
    REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG) |= REFLEX_RMT_CONF_UPDATE;
    REFLEX_REG(REFLEX_RMT_INT_CLR_REG) = REFLEX_RMT_CH0_TX_END_INT_RAW;
    if (s_rmt_pin < 31u) {
        esp_rom_gpio_connect_out_signal(s_rmt_pin, REFLEX_SIG_GPIO_OUT_IDX, false, false);
        reflex_hal_gpio_init_input(s_rmt_pin, false);
        s_rmt_pin = 0xFFu;
    }
    REFLEX_REG(REFLEX_PCR_RMT_CONF_REG) &= ~REFLEX_PCR_RMT_CLK_EN;
}

void reflex_hal_rmt_snapshot(reflex_rmt_snapshot_t *out) {
    if (!out) return;
    out->tx_conf0 = REFLEX_REG(REFLEX_RMT_CH0_TX_CONF0_REG);
    out->sys_conf = REFLEX_REG(REFLEX_RMT_SYS_CONF_REG);
    out->tx_lim = REFLEX_REG(REFLEX_RMT_CH0_TX_LIM_REG);
    out->pcr = REFLEX_REG(REFLEX_PCR_RMT_CONF_REG);
    out->pcr_sclk = REFLEX_REG(REFLEX_PCR_RMT_SCLK_CONF_REG);
}

/* ---- PCNT ------------------------------------------------------------- */

/* Count up on a rising edge, hold on a falling one.
 *
 * Read off the silicon rather than off a header: ESP-IDF was asked for
 * (INCREASE, HOLD) and left CH0_POS_MODE = 1 with CH0_NEG_MODE = 0, so those
 * are the encodings, confirmed rather than assumed from an enum's declaration
 * order. */
#define PCNT_MODE_HOLD 0u
#define PCNT_MODE_INCREASE 1u

reflex_err_t reflex_hal_pcnt_start(uint32_t edge_pin, uint32_t level_pin, int16_t low_limit,
                                   int16_t high_limit) {
    if (edge_pin >= 31u || level_pin >= 31u) return REFLEX_ERR_INVALID_ARG;
    if (high_limit <= 0 || low_limit >= 0) return REFLEX_ERR_INVALID_ARG;

    /* Ungate and release. RST_EN asserted means held in reset. */
    REFLEX_REG(REFLEX_PCR_PCNT_CONF_REG) |= REFLEX_PCR_PCNT_CLK_EN;
    REFLEX_REG(REFLEX_PCR_PCNT_CONF_REG) &= ~REFLEX_PCR_PCNT_RST_EN;

    /* PCNT counts a signal index, not a pin, so the matrix has to join them. */
    reflex_hal_gpio_init_input(edge_pin, false);
    reflex_hal_gpio_init_input(level_pin, false);
    esp_rom_gpio_connect_in_signal(edge_pin, REFLEX_PCNT_SIG_CH0_IN0_IDX, false);
    esp_rom_gpio_connect_in_signal(level_pin, REFLEX_PCNT_CTRL_CH0_IN0_IDX, false);

    REFLEX_REG(REFLEX_PCNT_U0_CONF2_REG) =
        (((uint32_t)(uint16_t)high_limit & REFLEX_PCNT_LIM_MASK) << REFLEX_PCNT_CNT_H_LIM_S) |
        (((uint32_t)(uint16_t)low_limit & REFLEX_PCNT_LIM_MASK) << REFLEX_PCNT_CNT_L_LIM_S);

    /* Establish the state rather than inherit it.
     *
     * The filter and all four watch-event enables come up *set* out of reset,
     * and a read-modify-write that only touches the mode fields leaves them
     * that way. That version of this function counted correctly — the same
     * value ESP-IDF's driver produced — while leaving the peripheral filtering
     * pulses shorter than the reset threshold and arming events nothing
     * handles. It was caught only by comparing conf0 against what ESP-IDF
     * leaves: 0x00043c10 against 0x00040010, four bits apart, no difference in
     * behaviour on this signal and a real one on a faster edge.
     *
     * The filter threshold itself is left as found, which is what ESP-IDF also
     * does; it is inert with the filter off. */
    uint32_t c0 = REFLEX_REG(REFLEX_PCNT_U0_CONF0_REG);
    c0 &= ~(REFLEX_PCNT_FILTER_EN | REFLEX_PCNT_THR_ZERO_EN | REFLEX_PCNT_THR_H_LIM_EN |
            REFLEX_PCNT_THR_L_LIM_EN | REFLEX_PCNT_THR_THRES0_EN | REFLEX_PCNT_THR_THRES1_EN);
    c0 &= ~((REFLEX_PCNT_MODE_MASK << REFLEX_PCNT_CH0_POS_MODE_S) |
            (REFLEX_PCNT_MODE_MASK << REFLEX_PCNT_CH0_NEG_MODE_S) |
            (REFLEX_PCNT_MODE_MASK << REFLEX_PCNT_CH0_HCTRL_MODE_S) |
            (REFLEX_PCNT_MODE_MASK << REFLEX_PCNT_CH0_LCTRL_MODE_S));
    c0 |= (PCNT_MODE_INCREASE << REFLEX_PCNT_CH0_POS_MODE_S) |
          (PCNT_MODE_HOLD << REFLEX_PCNT_CH0_NEG_MODE_S);
    REFLEX_REG(REFLEX_PCNT_U0_CONF0_REG) = c0;

    /* Zero the count, then run. CTRL carries all four units, so this touches
     * unit 0's bits and leaves the other three exactly as they were — they come
     * up held in reset, and clearing that would quietly start them.
     *
     * The read and the two writes are not atomic against another writer of this
     * register, and deliberately so: unit 0 is the only one Reflex drives, and
     * nothing here runs from an interrupt. A second PCNT user would need this
     * to become a critical section, the way reflex_hal_time_us did once its
     * shared latch acquired one. */
    uint32_t ctrl = REFLEX_REG(REFLEX_PCNT_CTRL_REG);
    REFLEX_REG(REFLEX_PCNT_CTRL_REG) = ctrl | REFLEX_PCNT_CNT_RST_U0;
    REFLEX_REG(REFLEX_PCNT_CTRL_REG) = ctrl & ~(REFLEX_PCNT_CNT_RST_U0 | REFLEX_PCNT_CNT_PAUSE_U0);
    return REFLEX_OK;
}

int reflex_hal_pcnt_read(void) {
    /* The count is 16 bits and signed — the unit counts down as readily as up,
     * and a plain widening read turns -1 into 65535. */
    uint32_t raw = REFLEX_REG(REFLEX_PCNT_U0_CNT_REG) & REFLEX_PCNT_CNT_MASK;
    return (int)(int16_t)(uint16_t)raw;
}

void reflex_hal_pcnt_stop(void) {
    REFLEX_REG(REFLEX_PCNT_CTRL_REG) |= REFLEX_PCNT_CNT_PAUSE_U0;
}

void reflex_hal_pcnt_release(void) {
    /* Give back everything start took.
     *
     * Pausing alone leaves both pins routed into the counter's matrix inputs
     * and the peripheral ungated for the rest of the boot — a side effect the
     * caller did not ask for, and the same thing that was wrong with leaving
     * GPIO 6 driven. Routing the constant-zero source into a signal index is
     * how the matrix disconnects an input; it is what ESP-IDF's own del_unit
     * does. */
    REFLEX_REG(REFLEX_PCNT_CTRL_REG) |= REFLEX_PCNT_CNT_PAUSE_U0 | REFLEX_PCNT_CNT_RST_U0;
    esp_rom_gpio_connect_in_signal(REFLEX_GPIO_MATRIX_CONST_ZERO_INPUT, REFLEX_PCNT_SIG_CH0_IN0_IDX,
                                   false);
    esp_rom_gpio_connect_in_signal(REFLEX_GPIO_MATRIX_CONST_ZERO_INPUT,
                                   REFLEX_PCNT_CTRL_CH0_IN0_IDX, false);
    REFLEX_REG(REFLEX_PCR_PCNT_CONF_REG) &= ~REFLEX_PCR_PCNT_CLK_EN;
}

void reflex_hal_pcnt_snapshot(reflex_pcnt_snapshot_t *out) {
    if (!out) return;
    out->conf0 = REFLEX_REG(REFLEX_PCNT_U0_CONF0_REG);
    out->conf1 = REFLEX_REG(REFLEX_PCNT_U0_CONF1_REG);
    out->conf2 = REFLEX_REG(REFLEX_PCNT_U0_CONF2_REG);
    out->ctrl = REFLEX_REG(REFLEX_PCNT_CTRL_REG);
    out->pcr = REFLEX_REG(REFLEX_PCR_PCNT_CONF_REG);
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
