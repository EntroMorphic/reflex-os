# ESP32-C6 MMIO Map

This document captures the authoritative MMIO base-region map for the ESP32-C6 as exposed by the local ESP-IDF source tree.

This is the correct starting point for Bonsai.

The machine is not first encountered here as services or opcodes, but as a structured field of addressable state regions.

## Source of Truth

The map below was derived from local ESP-IDF headers:

- `components/soc/esp32c6/register/soc/reg_base.h`
- `components/soc/esp32c6/include/modem/reg_base.h`
- per-peripheral register headers under `components/soc/esp32c6/register/soc/`

This approach is preferred over blind runtime probing.

## Important Note

The C6 is not organized as one-address-per-device.

Each hardware domain is typically represented by a register block or region:

- a base address
- a set of register offsets inside that region
- shared couplings into other regions such as clock, reset, interrupt, IO mux, or power control

So the useful view is a field of state regions, not a flat list of isolated addresses.

## Base Regions

| Base Address | Symbol | Notes |
| :--- | :--- | :--- |
| `0x20001000` | `DR_REG_PLIC_MX_BASE` | Platform-level interrupt controller, machine context |
| `0x20001400` | `DR_REG_PLIC_UX_BASE` | Platform-level interrupt controller, user context |
| `0x20001800` | `DR_REG_CLINT_M_BASE` | Core local interrupt/timer, machine context |
| `0x20001C00` | `DR_REG_CLINT_U_BASE` | Core local interrupt/timer, user context |
| `0x60000000` | `DR_REG_UART_BASE` | UART0 |
| `0x60001000` | `DR_REG_UART1_BASE` | UART1 |
| `0x60002000` | `DR_REG_SPI0_BASE` | SPI0 / memory-facing SPI block |
| `0x60003000` | `DR_REG_SPI1_BASE` | SPI1 / memory-facing SPI block |
| `0x60004000` | `DR_REG_I2C_EXT_BASE` | I2C ext |
| `0x60005000` | `DR_REG_UHCI0_BASE` | UHCI0 |
| `0x60006000` | `DR_REG_RMT_BASE` | Remote control peripheral |
| `0x60007000` | `DR_REG_LEDC_BASE` | LED PWM controller |
| `0x60008000` | `DR_REG_TIMERGROUP0_BASE` | Timer Group 0 |
| `0x60009000` | `DR_REG_TIMERGROUP1_BASE` | Timer Group 1 |
| `0x6000A000` | `DR_REG_SYSTIMER_BASE` | System timer |
| `0x6000B000` | `DR_REG_TWAI0_BASE` | TWAI/CAN controller 0 |
| `0x6000C000` | `DR_REG_I2S_BASE` | I2S |
| `0x6000D000` | `DR_REG_TWAI1_BASE` | TWAI/CAN controller 1 |
| `0x6000E000` | `DR_REG_APB_SARADC_BASE` | ADC/SAR block |
| `0x6000F000` | `DR_REG_USB_SERIAL_JTAG_BASE` | USB Serial/JTAG |
| `0x60010000` | `DR_REG_INTMTX_BASE` | Interrupt matrix |
| `0x60011000` | `DR_REG_ATOMIC_BASE` | Hardware lock / atomic block |
| `0x60012000` | `DR_REG_PCNT_BASE` | Pulse counter |
| `0x60013000` | `DR_REG_SOC_ETM_BASE` | Event/task matrix |
| `0x60014000` | `DR_REG_MCPWM_BASE` | Motor control PWM |
| `0x60015000` | `DR_REG_PARL_IO_BASE` | Parallel IO |
| `0x60016000` | `DR_REG_HINF_BASE` | SDIO HINF |
| `0x60017000` | `DR_REG_SLC_BASE` | SDIO SLC |
| `0x60018000` | `DR_REG_SLCHOST_BASE` | SDIO SLC host |
| `0x60019000` | `DR_REG_PVT_MONITOR_BASE` | Process/voltage/temperature monitor |
| `0x60080000` | `DR_REG_GDMA_BASE` | General DMA |
| `0x60081000` | `DR_REG_SPI2_BASE` | General-purpose SPI2 |
| `0x60088000` | `DR_REG_AES_BASE` | AES accelerator |
| `0x60089000` | `DR_REG_SHA_BASE` | SHA accelerator |
| `0x6008A000` | `DR_REG_RSA_BASE` | RSA accelerator |
| `0x6008B000` | `DR_REG_ECC_MULT_BASE` | ECC multiply block |
| `0x6008C000` | `DR_REG_DS_BASE` | Digital signature block |
| `0x6008D000` | `DR_REG_HMAC_BASE` | HMAC block |
| `0x60090000` | `DR_REG_IO_MUX_BASE` | IO mux |
| `0x60091000` | `DR_REG_GPIO_BASE` | GPIO |
| `0x60091F00` | `DR_REG_GPIO_EXT_BASE` | GPIO ext |
| `0x60092000` | `DR_REG_MEM_MONITOR_BASE` | Memory monitor |
| `0x60093000` | `DR_REG_PAU_BASE` | Permission attribution unit |
| `0x60095000` | `DR_REG_HP_SYSTEM_BASE` | High-performance system control |
| `0x60096000` | `DR_REG_PCR_BASE` | Peripheral clock/reset control |
| `0x60098000` | `DR_REG_TEE_BASE` | Trusted execution environment block |
| `0x60099000` | `DR_REG_HP_APM_BASE` | High-performance APM |
| `0x60099800` | `DR_REG_LP_APM0_BASE` | Low-power APM0 |
| `0x6009F000` | `DR_REG_MISC_BASE` | Misc system region |
| `0x600A3000` | `IEEE802154_REG_BASE` | 802.15.4 radio block |
| `0x600A9800` | `DR_REG_MODEM_SYSCON_BASE` | Modem system control |
| `0x600AF000` | `DR_REG_MODEM_LPCON_BASE` | Modem low-power control |
| `0x600AF800` | `DR_REG_I2C_ANA_MST_BASE` | Analog I2C master |
| `0x600B0000` | `DR_REG_PMU_BASE` | Power management unit |
| `0x600B0400` | `DR_REG_LP_CLKRST_BASE` | Low-power clock/reset |
| `0x600B0800` | `DR_REG_EFUSE_BASE` | eFuse |
| `0x600B0C00` | `DR_REG_LP_TIMER_BASE` | Low-power timer |
| `0x600B1000` | `DR_REG_LP_AON_BASE` | Low-power always-on |
| `0x600B1400` | `DR_REG_LP_UART_BASE` | Low-power UART |
| `0x600B1800` | `DR_REG_LP_I2C_BASE` | Low-power I2C |
| `0x600B1C00` | `DR_REG_LP_WDT_BASE` | Low-power watchdog |
| `0x600B2000` | `DR_REG_LP_IO_BASE` | Low-power IO |
| `0x600B2400` | `DR_REG_LP_I2C_ANA_MST_BASE` | Low-power analog I2C master |
| `0x600B2800` | `DR_REG_LPPERI_BASE` | Low-power peripheral control |
| `0x600B2C00` | `DR_REG_LP_ANALOG_PERI_BASE` | Low-power analog peripheral control |
| `0x600B3400` | `DR_REG_LP_TEE_BASE` | Low-power TEE |
| `0x600B3800` | `DR_REG_LP_APM_BASE` | Low-power APM |
| `0x600B3C00` | `DR_REG_OPT_DEBUG_BASE` | OTP/debug region |
| `0x600C0000` | `DR_REG_TRACE_BASE` | Trace block |
| `0x600C2000` | `DR_REG_ASSIST_DEBUG_BASE` | Assist debug |
| `0x600C2000` | `DR_REG_CPU_BUS_MONITOR_BASE` | CPU bus monitor alias |
| `0x600C5000` | `DR_REG_INTPRI_BASE` | Interrupt priority control |
| `0x600C8000` | `DR_REG_EXTMEM_BASE` | External memory control |

## Register Header Coverage

The local ESP-IDF tree also exposes per-register definitions for these blocks through `*_reg.h` files.

Examples:

- `gpio_reg.h`: `197` register macros, max observed offset `0x6FC`
- `pcr_reg.h`: `83` register macros, max observed offset `0xFFC`
- `gdma_reg.h`: `105` register macros, max observed offset `0x280`
- `ieee802154_reg.h`: `92` register macros, max observed offset `0x184`
- `efuse_reg.h`: `116` register macros, max observed offset `0x1FC`
- `soc_etm_reg.h`: `108` register macros, max observed offset `0x1AC`
- `pmu_reg.h`: `106` register macros, max observed offset `0x3FC`

There are many more. The full source set is under:

- `components/soc/esp32c6/register/soc/`

To generate a full per-register table locally, use:

```bash
python3 bonsai/tools/extract_mmio_map.py > bonsai/mmio-registers.tsv
```

The generated TSV includes:

- header filename
- register macro name
- matched base symbol
- observed offset
- original macro expression

## What This Means for Bonsai

This map suggests a useful reinterpretation:

- MMIO is not just a list of numbers.
- MMIO is a field of stateful regions.
- Each region exposes observable and writable state.
- Access is numerically expressed but operationally routed.

So the binary host sees:

- base address
- offset
- register
- bitfield

But Bonsai may choose to see:

- region
- field
- cell
- transition surface
- routed effect

## Limits of Blind Probing

We should not blindly scan all MMIO addresses on-device.

Reasons:

- some reads may have side effects
- some regions are not safe to touch casually
- writes to unknown registers are dangerous
- some behavior is coupled to clocks, resets, interrupts, and power domains

The safe approach is:

1. derive the map from the ESP-IDF headers and TRM
2. classify regions by role
3. selectively probe known-safe read-only or status registers if needed

## Next Bonsai Step

The next useful reduction is not "what is the next register address?"

It is:

- what is a region?
- what is a field?
- what is a cell?
- what is a transition?
- what is a route through that field?

That is where a geometric ontology of the machine can begin.
