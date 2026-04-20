# Adding a Platform Backend

This guide explains how to port Reflex OS to a new chip/board.

## What a Platform Backend Is

A platform backend implements 5 portable interfaces:

| Header | Functions | Purpose |
|--------|-----------|---------|
| `reflex_hal.h` | Time, GPIO, sleep, random, MAC, temp, log, interrupts | Hardware access |
| `reflex_task.h` | Tasks, queues, mutexes, priorities | Scheduling |
| `reflex_kv.h` | Key-value persistent storage | Configuration |
| `reflex_radio.h` | Mesh broadcast send/receive | Networking |
| `reflex_crypto.h` | HMAC-SHA256 | Packet authentication |

## File Structure

```
platform/<target>/
  CMakeLists.txt          # Component registration with target guard
  reflex_hal_<target>.c   # HAL implementation
  reflex_task_<target>.c  # Task backend (usually wraps an RTOS)
  reflex_kv_<target>.c    # KV store (NVS, LittleFS, or raw flash)
  reflex_radio_<target>.c # Radio backend
  reflex_crypto_<target>.c # Crypto (can copy standalone SHA-256)
```

## Step-by-Step

### 1. Create the directory

```bash
mkdir platform/<target>
```

### 2. CMakeLists.txt (with target guard)

```cmake
if(NOT "${IDF_TARGET}" STREQUAL "<target>")
    idf_component_register()
    return()
endif()

set(REFLEX_PLATFORM_SRCS "reflex_hal_<target>.c" "reflex_kv_<target>.c"
                        "reflex_crypto_<target>.c" "reflex_task_<target>.c"
                        "reflex_radio_<target>.c")

idf_component_register(SRCS ${REFLEX_PLATFORM_SRCS}
                       INCLUDE_DIRS "../../include" "../../kernel"
                       REQUIRES <your-deps>)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    "REFLEX_RTC_DATA_ATTR=<your-rtc-attr-or-empty>")
```

### 3. Add to top-level CMakeLists.txt

```cmake
set(EXTRA_COMPONENT_DIRS ${REFLEX_COMMON_COMPONENTS} platform/esp32c6 platform/esp32 platform/<target>)
```

### 4. Implement the HAL

Start with the ESP32 backend as a template (simplest — uses ESP-IDF APIs):
- `platform/esp32/reflex_hal_esp32.c`

For maximum independence, see the C6 backend (direct register writes):
- `platform/esp32c6/reflex_hal_esp32c6.c`

**Mandatory functions** (will fail to link if missing):
- `reflex_hal_time_us`, `reflex_hal_delay_us`, `reflex_hal_cpu_cycles`
- `reflex_hal_gpio_init_output`, `reflex_hal_gpio_set_level`, `reflex_hal_gpio_get_level`
- `reflex_hal_reboot`, `reflex_hal_random_fill`, `reflex_hal_mac_read`
- `reflex_hal_log`
- `reflex_hal_intr_alloc`, `reflex_hal_intr_free`

**Optional** (can return `REFLEX_ERR_NOT_SUPPORTED`):
- `reflex_hal_temp_init`, `reflex_hal_temp_read`
- `reflex_hal_sleep_enter`, `reflex_hal_sleep_wakeup_cause`
- `reflex_hal_gpio_connect_out`

### 5. Implement the task backend

If your chip runs FreeRTOS: copy `platform/esp32/reflex_task_esp32.c` (identical for any FreeRTOS chip).

If bare-metal: use `kernel/reflex_task_kernel.c` (setjmp/longjmp scheduler, zero RTOS dependency).

### 6. Crypto backend

Copy `platform/esp32c6/reflex_crypto_esp32c6.c` — it's pure portable C (standalone SHA-256 + HMAC). No hardware dependencies.

### 7. Create sdkconfig.defaults.<target>

```
# <target>-specific defaults
CONFIG_REFLEX_RADIO_ESPNOW=y
# ... any target-specific options
```

### 8. Test

```bash
idf.py -B build_<target> set-target <target>
SDKCONFIG_DEFAULTS="sdkconfig.defaults.<target>" idf.py -B build_<target> build
```

## Validation Checklist

After first boot, verify:

- [ ] Boot messages appear on serial
- [ ] `help` command responds
- [ ] `services` shows registered services
- [ ] `temp` reads (or returns error if no sensor)
- [ ] `led on` / `led off` works (if LED exists)
- [ ] `purpose set/get/clear` works
- [ ] `reboot` resets cleanly
- [ ] Purpose persists across reboot

## Reference Implementations

- **Full-featured:** `platform/esp32c6/` — direct register access, custom bootloader, kernel scheduler
- **Minimal:** `platform/esp32/` — ESP-IDF API wrappers, no kernel customization
