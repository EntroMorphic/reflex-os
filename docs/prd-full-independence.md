# PRD: Full Espressif Elimination

**Objective:** Remove every Espressif dependency from Reflex OS. What can't be removed gets documented as a silicon constraint with the same honesty we'd give the mask ROM.

**Principle:** No bullshit. Each item is either replaceable (with a plan) or irreplaceable (with an honest reason). No "future work" hand-waves.

---

## Dependency 1: `bootloader_clock_configure()`

**What it does:** Configures the PLL from 40 MHz XTAL to 160 MHz CPU clock. Involves ~200 lines of register writes to the RTC, BBPLL, and PCR clock domains. Chip-revision-aware (different calibration values for different steppings).

**Where:** `bootloader_components/main/reflex_boot0.c`, called from `hw_clock_init()`.

**Replaceable:** Yes.

**Plan:**
1. Read `$IDF_PATH/components/esp_hw_support/port/esp32c6/rtc_clk_init.c` — this is the actual implementation
2. Extract the register writes for the C6 rev 0.2 (our boards) into a standalone `reflex_clock_init()` function
3. The key sequence: configure BBPLL → set CPU source to PLL → set divider to 1 (160 MHz) → switch MSPI to high-speed divider
4. Use SOC register definitions (just `#define` constants) — no code dependency
5. Test: board boots at 160 MHz, `esp_timer` (now `reflex_hal_time_us`) returns correct microseconds

**Risk:** Wrong PLL calibration values → CPU runs at wrong speed or doesn't boot. Mitigated by: read the working values from the current build's register dump before changing anything.

**Effort:** 1 day. The code exists — we're extracting, not inventing.

---

## Dependency 2: HAL Cache/MMU Wrappers

**What they do:** `cache_hal_init()`, `cache_hal_enable()`, `cache_hal_disable()`, `mmu_hal_init()`, `mmu_hal_map_region()`, `mmu_hal_unmap_all()`. Thin register wrappers that configure the instruction/data cache and the MMU page table for flash-mapped code.

**Where:** `bootloader_components/main/reflex_boot0.c`, image loader.

**Replaceable:** Yes.

**Plan:**
1. Read `$IDF_PATH/components/hal/esp32c6/cache_hal.c` and `mmu_hal.c` — each is ~100 lines of register writes
2. The C6 has a single shared I/D cache with 64 MMU entries of 64 KB pages
3. `cache_hal_init` → write to `EXTMEM_L1_CACHE_CTRL_REG` (enable cache, set line size)
4. `mmu_hal_map_region` → write entries to the MMU table at `DR_REG_MMU_TABLE` (each entry maps a 64 KB virtual page to a physical flash page)
5. `cache_hal_enable` → set the enable bit in `EXTMEM_L1_CACHE_CTRL_REG`
6. Write these as `reflex_mmu_init()`, `reflex_mmu_map()`, `reflex_cache_enable()` in Boot0

**Risk:** Wrong cache line size or MMU entry format → flash-mapped code faults. Mitigated by: verify the working register values via JTAG or ROM `REG_READ` before replacing.

**Effort:** 1 day. The registers are documented in the TRM and the HAL source is readable.

---

## Dependency 3: The Build System

**What it is:** `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` in the root CMakeLists.txt. ESP-IDF's CMake build system provides: the RISC-V cross-compiler toolchain path, component registration macros (`idf_component_register`), linker scripts, partition table generation, ULP compilation, and the `idf.py` CLI (build, flash, monitor).

**Where:** Every `CMakeLists.txt` in the project.

**Replaceable:** Partially.

**What we can own:**
- A standalone CMakeLists.txt that uses the RISC-V GCC toolchain directly (the toolchain is a separate download from Espressif, not part of ESP-IDF — it's standard GCC cross-compiled for RISC-V)
- Our own linker script derived from ESP-IDF's (the memory map is a silicon property, not an SDK property)
- Our own partition table tool (the format is documented and simple — we already parse it in Boot0)
- Our own flash tool or direct use of `esptool.py` (which is a standalone Python package, MIT-licensed, not GPL)

**What we can't replace without massive effort:**
- The FreeRTOS kernel port for RISC-V (interrupt vectors, context switching, tick timer) — this is OS-level work
- The startup assembly (`vectors.S`) that sets up the initial stack and exception handlers before `app_main`

**Plan:**
1. Create `build/` scripts that invoke `riscv32-esp-elf-gcc` directly with our own linker script
2. Write a `reflex_flash.py` tool that calls `esptool.py` (MIT license) with our partition table and image
3. Keep `idf.py build` as the primary build path for now; add a `make` or standalone CMake path as a parallel option
4. Document exactly which ESP-IDF build-system features we use and which are replaceable

**Risk:** Linker script errors → boot failure. Mitigated by: derive from the working ESP-IDF linker script, change nothing initially, diff against the generated output.

**Effort:** 1 week for a parallel build path. Full replacement of the ESP-IDF build system is a month-level project.

---

## Dependency 4: FreeRTOS

**What it is:** The RTOS kernel. Task scheduling, context switching, queues, mutexes, tick timer, interrupt management. The ESP-IDF port includes RISC-V-specific context save/restore, machine timer configuration, and interrupt vector setup.

**Where:** `platform/esp32c6/reflex_task_esp32c6.c` (abstracted), plus 22 direct references in Tier C files.

**Replaceable:** Yes, but this is the single largest remaining effort.

**Options:**

**Option A: Write a Reflex scheduler.** A minimal cooperative or preemptive scheduler for single-core RISC-V. Needs: task context struct, stack initialization, context switch (save/restore 32 GP registers + CSRs), machine timer tick, yield mechanism, priority queue. This is a real RTOS kernel — 1000-2000 lines of C + assembly. Proven designs exist (FreeRTOS itself is ~5000 lines for the core).

**Option B: Use Zephyr's scheduler.** Apache-2.0 licensed, supports RISC-V, designed for exactly this class of MCU. We'd write a Zephyr backend for `reflex_task.h` instead of the FreeRTOS backend. Zephyr is not Espressif — it's a Linux Foundation project.

**Option C: Port a minimal RTOS.** picoRTOS, RIOT, or similar. Small, open-source, RISC-V-capable.

**Recommendation:** Option A for the long term (true ownership), but Option B or C as a bridge. Writing a correct preemptive scheduler with interrupt safety takes time and the bugs are subtle (priority inversion, stack overflow, interrupt latency).

**Plan:**
1. Abstract the remaining 22 direct FreeRTOS calls in Tier C files through `reflex_task.h` (the task abstraction already exists — the Tier C files just haven't been migrated)
2. Write a minimal cooperative scheduler as `platform/reflex_native/reflex_task_native.c` for host testing
3. Write a preemptive RISC-V scheduler as `platform/esp32c6/reflex_scheduler.c` using the machine timer interrupt
4. Replace the FreeRTOS backend with the Reflex scheduler

**Effort:** 2-4 weeks for a working preemptive scheduler. The cooperative version (for host testing) is 1-2 days.

---

## Dependency 5: Platform Backend Implementations

**What they are:** The 5 files in `platform/esp32c6/` that bridge Reflex interfaces to ESP-IDF.

**Replaceable:** Yes — that's their entire purpose.

| File | ESP-IDF calls | Replacement |
|---|---|---|
| `reflex_hal_esp32c6.c` | `esp_timer_get_time`, `esp_cpu_get_cycle_count`, `gpio_set_level`, `esp_sleep_*`, `esp_random`, `esp_log` | Direct register access: machine timer CSR for time, GPIO output register for pins, LP timer for sleep, RNG register for random, UART for log |
| `reflex_task_esp32c6.c` | `xTaskCreate`, `vTaskDelay`, `xQueueCreate`, `portMUX` | Reflex scheduler (Dependency 4) |
| `reflex_kv_esp32c6.c` | `nvs_open/get/set/close` | Reflex filesystem on LittleFS or raw flash sectors |
| `reflex_radio_esp32c6.c` | `esp_now_init/send/register_recv` | See Dependency 8 (Wi-Fi blob) |
| `reflex_crypto_esp32c6.c` | `mbedtls_md_hmac_*` | Software SHA-256 implementation (~200 lines) or hardware SHA accelerator via direct register access (`sys.sha.*` is in our SVD catalog) |

**Plan:** Replace each file one at a time, validating on hardware after each. The crypto backend is the easiest (self-contained SHA-256). The HAL is next (register access). The KV store needs a filesystem. The task backend needs the scheduler. The radio backend is blocked by the Wi-Fi blob.

**Effort:** 1-2 weeks for HAL + crypto + KV. Task backend depends on the scheduler. Radio backend depends on the Wi-Fi blob question.

---

## Dependency 6: Tier C Driver Files

**What they are:** 6 source files that directly use ESP-IDF drivers.

| File | ESP-IDF dependency | Replacement |
|---|---|---|
| `core/boot.c` | `esp_app_desc`, `esp_chip_info`, `esp_flash` | Read chip ID and flash size from registers directly. The app version string can come from a Reflex build-time constant. |
| `drivers/led.c` | `gpio_config`, `gpio_set_level` | Direct GPIO output register write (one register, one bit) |
| `drivers/button.c` | `gpio_config`, `gpio_get_level`, FreeRTOS ISR | Direct GPIO input register read + Reflex interrupt abstraction |
| `services/temp_service.c` | `temperature_sensor_*` | Direct SAR ADC register access (the temp sensor is an internal ADC channel) |
| `net/wifi.c` | Full Wi-Fi stack | See Dependency 8 |
| `shell/shell.c` | USB serial JTAG, `esp_heap_caps`, `esp_app_desc`, `esp_sleep` | USB-JTAG console via direct register access, heap tracking via our own allocator, sleep via HAL |

**Plan:** Replace each with direct register access or Reflex HAL calls. `led.c` and `button.c` are trivial (one GPIO register each). `boot.c` is straightforward (eFuse register for chip ID, SPI register for flash size). `temp_service.c` requires understanding the SAR ADC calibration. `shell.c` is the largest — USB serial JTAG console init requires understanding the USB peripheral registers.

**Effort:** 1 week for GPIO/boot/temp. Shell USB console is 2-3 days. Wi-Fi is blocked.

---

## Dependency 7: Startup Assembly and Linker Script

**What it is:** The RISC-V startup code that runs before `app_main()`: exception vector table, stack pointer initialization, BSS clearing, data section copying from flash to SRAM, C runtime init, FreeRTOS kernel start.

**Where:** `$IDF_PATH/components/esp_system/port/cpu_start.c`, `$IDF_PATH/components/riscv/vectors.S`, and the linker script that defines memory regions.

**Replaceable:** Yes.

**Plan:**
1. Write `reflex_vectors.S` — minimal RISC-V exception vector table (trap handler, interrupt dispatcher)
2. Write `reflex_startup.c` — BSS clear, data copy, stack init, call `app_main`
3. Write `reflex.ld` — linker script defining IRAM, DRAM, flash-mapped regions, stack
4. The memory map is a silicon property (documented in TRM Chapter 3) — we just express it in our linker script

**Risk:** Exception handling wrong → board crashes on any interrupt. Mitigated by: start with the exact same vector layout as ESP-IDF, validate that interrupts (timer tick, GPIO) still work.

**Effort:** 1 week. This is careful, test-heavy work.

---

## Dependency 8: The Wi-Fi/BLE Binary Blob

**What it is:** `libnet80211.a`, `libphy.a`, `libpp.a`, `libcoexist.a` — Espressif's proprietary Wi-Fi/BLE protocol stack. Runs as binary firmware. The 802.11 MAC, PHY calibration, power management, and coexistence logic are all in these blobs. ESP-NOW sits on top of this stack.

**Where:** Linked into the final binary by ESP-IDF. Called indirectly through `esp_wifi_*` APIs.

**Replaceable:** No. Not without reverse-engineering Espressif's radio firmware, which is:
- Legally questionable (proprietary binary)
- Technically massive (the Wi-Fi protocol stack is tens of thousands of lines)
- Unnecessary (every chip vendor ships radio firmware as a blob — Qualcomm, Intel, Broadcom, Nordic all do this)

**Honest assessment:** The Wi-Fi blob is the Espressif equivalent of a NIC firmware. Linux doesn't own Intel's Wi-Fi firmware either. The blob runs on the radio coprocessor, not the application CPU. It's a peripheral driver, not an OS dependency.

**What we CAN do:**
1. Use the 802.15.4 radio directly (the C6 has a Zigbee/Thread radio with documented registers — no blob needed for raw 802.15.4)
2. Write our own mesh protocol on 802.15.4 instead of ESP-NOW
3. Keep ESP-NOW as one radio backend behind `reflex_radio.h` and add a blob-free 802.15.4 backend

**Plan:**
1. Add `reflex_radio_802154.c` as a second radio backend using the documented 802.15.4 MAC registers
2. Modify `goose_atmosphere.c` to work with either backend (it already goes through `reflex_radio.h`)
3. ESP-NOW remains available for Wi-Fi-capable boards; 802.15.4 provides a fully-owned radio path

**Effort:** 2-3 weeks for a raw 802.15.4 mesh protocol. The radio registers are in our SVD catalog (`radio.*`).

---

## Execution Priority

| Priority | Dependency | Effort | Impact |
|---|---|---|---|
| 1 | Clock configure (Dep 1) | 1 day | Boot0 becomes fully self-owned |
| 2 | Cache/MMU wrappers (Dep 2) | 1 day | Boot0 has zero HAL calls |
| 3 | Tier C GPIO drivers (Dep 6 partial) | 2 days | `led.c`, `button.c`, `boot.c` become Reflex-only |
| 4 | Crypto backend (Dep 5 partial) | 1 day | Software SHA-256 replaces mbedtls |
| 5 | Temp sensor (Dep 6 partial) | 2 days | Last ESP-IDF driver in services/ |
| 6 | KV store (Dep 5 partial) | 3 days | LittleFS or raw flash replaces NVS |
| 7 | Shell USB console (Dep 6 partial) | 3 days | Shell becomes platform-independent |
| 8 | 802.15.4 radio (Dep 8) | 2-3 weeks | Blob-free radio path |
| 9 | Scheduler (Dep 4) | 2-4 weeks | FreeRTOS eliminated |
| 10 | Startup assembly (Dep 7) | 1 week | Full hardware ownership |
| 11 | Build system (Dep 3) | 1 month | Standalone build, no `idf.py` |

**Total to full independence (excluding build system):** 8-12 weeks of focused work.

**Total to "no Espressif code executes on the application CPU":** Items 1-7 + 9-10 = 6-8 weeks. The radio blob runs on the radio coprocessor, not the application CPU, so it doesn't count against application-level independence.

---

## Definition of Done

- [ ] `grep -r "esp_\|ESP_\|freertos\|FreeRTOS\|nvs_\|mbedtls" --include="*.c" --include="*.h" | grep -v platform/ | grep -v archive/` returns **zero results**
- [ ] Boot0 contains zero calls to `bootloader_support` or `hal` components
- [ ] The RISC-V toolchain is invoked directly, not through `idf.py` (parallel build path)
- [ ] A `.loom` app deploys and runs through the containerized holon model
- [ ] The atmospheric mesh operates over 802.15.4 without the Wi-Fi blob (alternative path)
- [ ] All existing hardware-validated behaviors pass unchanged
- [ ] The project builds and the VM self-checks pass on a host machine (macOS/Linux) without any ESP-IDF installation
