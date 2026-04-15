/*
 * GOOSE LP Pulse (Coherent Heartbeat)
 *
 * Runs on the ESP32-C6 LP core (a RISC-V coprocessor separate from the
 * HP core). The HP core loads this binary via ulp_lp_core_run in
 * goose_runtime and mirrors the `agency.led.intent` cell state into
 * lp_led_intent on every supervisor pulse.
 *
 * The LP core on C6 can only drive LP I/O pins (GPIO 0-7). The onboard
 * LED is on GPIO 15, which is HP-only, so the LP core cannot drive it
 * directly. Instead, this program acts as a parallel heartbeat counter:
 * it increments lp_pulse_count once per loop and tracks the mirrored
 * intent for observability from the HP side. The HP supervisor reads
 * lp_pulse_count to confirm the LP core is alive in parallel to HP, and
 * the vm info / heartbeat shell commands can surface the value.
 */

#include <stdint.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"

volatile int32_t  lp_led_intent         __attribute__((used)) = 0;
volatile uint32_t lp_pulse_count        __attribute__((used)) = 0;
volatile int32_t  lp_last_intent_seen   __attribute__((used)) = 0;

int main(void)
{
    lp_pulse_count++;
    /* Mirror the HP-written intent into an observable shadow so the HP
     * side can confirm the LP program is reading its control input.
     * This read-and-store also prevents --gc-sections from stripping
     * lp_led_intent even though we cannot drive the LED from here
     * (the onboard LED is on GPIO 15, a non-LP I/O). */
    lp_last_intent_seen = lp_led_intent;
    return 0;
}
