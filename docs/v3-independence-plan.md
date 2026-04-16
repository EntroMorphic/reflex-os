# Reflex OS v3.0 — Platform Independence Plan

**Goal:** Reflex OS includes zero ESP-IDF headers. The GOOSE substrate, ternary VM, and OS surface are fully portable. Platform-specific code lives behind a Reflex-owned HAL in `platform/<target>/`. ESP-IDF becomes one possible backend, not a dependency.

**Principle:** The substrate is an ocean of potential. It surfaces the shapes, forms, and functions the user needs. It is an OS while simultaneously being itself. Applications are containerized manifolds running on the ternary VM — nothing runs on bare metal except the substrate itself.

---

## Part I — The Dependency Map

### Current state (v2.6.0+, commit 6398f38)

1,299 ESP-IDF touch points across the codebase. The hard platform dependencies (excluding error types and return macros) break down as:

| Subsystem | Hard deps | What they call |
|---|---|---|
| GOOSE Runtime | ~88 | FreeRTOS mutexes, tasks, timers, CPU cycles, sleep, NVS, GPIO, ROM GPIO |
| GOOSE Atmosphere | ~69 | ESP-NOW, Wi-Fi, mbedtls HMAC, NVS, MAC read, timers, random |
| GOOSE Supervisor | ~61 | FreeRTOS tasks, NVS, timers, random |
| GOOSE Atlas | ~7 | ESP log |
| GOOSE Shadow Atlas | ~0 | Pure data (generated C array) |
| GOOSE Library | ~19 | ESP log, malloc |
| GOOSE DMA | ~11 | ESP GDMA (scaffold) |
| GOOSE ETM | ~23 | ESP ETM, GPIO ETM (scaffold) |
| GOOSE Gateway | ~12 | FreeRTOS tasks, ESP log |
| Shell | ~66 | FreeRTOS delays, printf, ESP log, NVS (via GOOSE APIs) |
| Services (LED, Button, Temp) | ~30 | GPIO drivers, temperature sensor driver, FreeRTOS tasks |
| Core (boot, fabric, events, services) | ~64 | FreeRTOS queues/tasks, NVS, ESP system, ESP log |
| Storage | ~16 | NVS |
| Net | ~30 | ESP Wi-Fi |
| Drivers | ~19 | GPIO, LED (LEDC) |
| VM layer | ~15 | FreeRTOS tasks (task_runtime), ESP log (syscall), ESP check macros |
| Main | ~14 | ESP system, all of the above |

### What's already portable

- `vm/ternary.c` — balanced ternary arithmetic (only needs `esp_err_t` → `int`)
- `vm/interpreter.c` — instruction dispatch (only needs `esp_err_t`)
- `vm/cache.c` — three-state cache (only needs `esp_err_t`)
- `vm/mmu.c` — region protection (only needs `esp_err_t`)
- `vm/loader.c` — CRC32 + image validation (only needs `esp_err_t`, `malloc`)
- `goose_shadow_atlas.c` — generated data array (no platform calls at all)
- `tools/tasm.py`, `tools/loomc.py`, `tools/goose_scraper.py` — host-only Python

---

## Part II — The Reflex Abstraction Layers

### Layer 0: Reflex Types (`reflex_types.h`)

Replace `esp_err_t`, `ESP_OK`, `ESP_FAIL`, `ESP_ERR_*`, `ESP_RETURN_ON_FALSE`, `ESP_RETURN_ON_ERROR` with Reflex-owned equivalents.

```c
typedef int reflex_err_t;
#define REFLEX_OK               0
#define REFLEX_FAIL            -1
#define REFLEX_ERR_NO_MEM      0x101
#define REFLEX_ERR_INVALID_ARG 0x102
#define REFLEX_ERR_NOT_FOUND   0x105
#define REFLEX_ERR_TIMEOUT     0x107
// ... (map 1:1 from current esp_err_t values used)
```

Plus `REFLEX_RETURN_ON_FALSE`, `REFLEX_RETURN_ON_ERROR` macros.

**Why first:** This is the most pervasive dependency (~800+ sites). It's also the most mechanical — a project-wide find-and-replace with a compatibility shim during migration. Every subsequent layer change is easier once this is done.

### Layer 1: Reflex HAL (`reflex_hal.h`)

Hardware abstraction for everything the substrate touches directly.

```c
// --- Time ---
uint64_t reflex_hal_time_us(void);
uint32_t reflex_hal_cpu_cycles(void);
void     reflex_hal_delay_us(uint32_t us);

// --- GPIO ---
reflex_err_t reflex_hal_gpio_set_level(uint32_t pin, int level);
reflex_err_t reflex_hal_gpio_connect_out(uint32_t out_pin, uint32_t signal, bool invert);

// --- System ---
void     reflex_hal_reboot(void);
int      reflex_hal_sleep_wakeup_cause(void);
void     reflex_hal_sleep_enter(uint64_t duration_us);
void     reflex_hal_random_fill(uint8_t *buf, size_t len);
reflex_err_t reflex_hal_mac_read(uint8_t mac[6]);

// --- Log ---
void reflex_hal_log(int level, const char *tag, const char *fmt, ...);
#define REFLEX_LOG_ERROR 1
#define REFLEX_LOG_WARN  2
#define REFLEX_LOG_INFO  3
```

**ESP-IDF implementation** (`platform/esp32c6/hal_esp32c6.c`): thin wrappers around `esp_timer_get_time`, `esp_cpu_get_cycle_count`, `gpio_set_level`, `esp_rom_gpio_connect_out_signal`, `esp_restart`, `esp_sleep_get_wakeup_cause`, `esp_deep_sleep_start`, `esp_fill_random`, `esp_read_mac`, `ESP_LOGx`.

### Layer 2: Reflex KV (`reflex_kv.h`)

Key-value persistent storage.

```c
typedef void *reflex_kv_handle_t;

reflex_err_t reflex_kv_open(const char *ns, bool readonly, reflex_kv_handle_t *out);
void         reflex_kv_close(reflex_kv_handle_t h);
reflex_err_t reflex_kv_get_str(reflex_kv_handle_t h, const char *key, char *buf, size_t *len);
reflex_err_t reflex_kv_set_str(reflex_kv_handle_t h, const char *key, const char *val);
reflex_err_t reflex_kv_get_blob(reflex_kv_handle_t h, const char *key, void *buf, size_t *len);
reflex_err_t reflex_kv_set_blob(reflex_kv_handle_t h, const char *key, const void *buf, size_t len);
reflex_err_t reflex_kv_erase(reflex_kv_handle_t h, const char *key);
reflex_err_t reflex_kv_commit(reflex_kv_handle_t h);
```

**ESP-IDF implementation**: direct mapping to `nvs_open/close/get_str/set_str/get_blob/set_blob/erase_key/commit`.

**Future implementation**: LittleFS-backed flat-file store, or raw flash sector ring buffer.

### Layer 3: Reflex Task (`reflex_task.h`)

Scheduling, synchronization, and concurrency primitives.

```c
typedef void *reflex_task_handle_t;
typedef void *reflex_queue_handle_t;
typedef void *reflex_mutex_t;

reflex_err_t reflex_task_create(void (*fn)(void*), const char *name,
                                uint32_t stack, void *arg, int prio,
                                reflex_task_handle_t *out);
void         reflex_task_delete(reflex_task_handle_t h);
void         reflex_task_delay_ms(uint32_t ms);
void         reflex_task_yield(void);

reflex_queue_handle_t reflex_queue_create(uint32_t len, uint32_t item_size);
reflex_err_t reflex_queue_send(reflex_queue_handle_t q, const void *item, uint32_t timeout_ms);
reflex_err_t reflex_queue_recv(reflex_queue_handle_t q, void *item, uint32_t timeout_ms);

void reflex_critical_enter(reflex_mutex_t *m);
void reflex_critical_exit(reflex_mutex_t *m);
reflex_mutex_t reflex_critical_init(void);
```

**ESP-IDF implementation**: wrappers around `xTaskCreate`, `vTaskDelete`, `vTaskDelay`, `xQueueCreate/Send/Receive`, `taskENTER/EXIT_CRITICAL`, `portMUX_INITIALIZER_UNLOCKED`.

**Future implementation**: bare-metal cooperative scheduler on RISC-V, or preemptive scheduler using the C6's machine timer interrupt.

### Layer 4: Reflex Radio (`reflex_radio.h`)

Broadcast mesh transport.

```c
typedef void (*reflex_radio_recv_cb_t)(const uint8_t *src_mac,
                                       const uint8_t *data, int len);

reflex_err_t reflex_radio_init(void);
reflex_err_t reflex_radio_send(const uint8_t *dest_mac, const uint8_t *data, size_t len);
reflex_err_t reflex_radio_register_recv(reflex_radio_recv_cb_t cb);
reflex_err_t reflex_radio_add_peer(const uint8_t mac[6]);
```

**ESP-IDF implementation**: `esp_wifi_start` + `esp_now_init` + `esp_now_send` + `esp_now_register_recv_cb` + `esp_now_add_peer`.

**Future implementation**: raw 802.15.4 on the C6's radio, or BLE advertising, or a software-defined radio protocol.

### Layer 5: Reflex Crypto (`reflex_crypto.h`)

Authentication primitives.

```c
reflex_err_t reflex_hmac_sha256(const uint8_t *key, size_t key_len,
                                const uint8_t *data, size_t data_len,
                                uint8_t out[32]);
```

**ESP-IDF implementation**: the existing `mbedtls_md_*` chain, wrapped in a single call.

**Future implementation**: hardware SHA accelerator on the C6 (documented in the SVD under `sys.sha.*`), or a minimal software SHA-256 for portability.

### Layer 6: Reflex Drivers (`reflex_drivers.h`)

Peripheral abstractions for services.

```c
// Temperature
typedef void *reflex_temp_handle_t;
reflex_err_t reflex_temp_init(reflex_temp_handle_t *out);
reflex_err_t reflex_temp_read_celsius(reflex_temp_handle_t h, float *out);

// LED (PWM)
reflex_err_t reflex_led_init(uint32_t pin);
reflex_err_t reflex_led_set(uint32_t pin, bool on);

// Button (input with interrupt or poll)
reflex_err_t reflex_button_init(uint32_t pin);
bool         reflex_button_is_pressed(uint32_t pin);

// I2C (future)
reflex_err_t reflex_i2c_init(uint32_t port, uint32_t sda, uint32_t scl, uint32_t freq);
reflex_err_t reflex_i2c_read(uint32_t port, uint8_t addr, uint8_t *buf, size_t len);
reflex_err_t reflex_i2c_write(uint32_t port, uint8_t addr, const uint8_t *buf, size_t len);
```

**ESP-IDF implementation**: wrappers around `temperature_sensor_*`, `gpio_set_level`, LEDC, GPIO input, I2C driver.

### Layer 7: Reflex LP (`reflex_lp.h`)

Low-power coprocessor abstraction.

```c
reflex_err_t reflex_lp_load_binary(const uint8_t *bin, size_t len);
reflex_err_t reflex_lp_run(uint64_t wakeup_interval_us);
reflex_err_t reflex_lp_write_global(const char *name, int32_t value);
reflex_err_t reflex_lp_read_global(const char *name, int32_t *value);
```

**ESP-IDF implementation**: `ulp_lp_core_load_binary`, `ulp_lp_core_run`, `ulp_*` symbol access.

**Future implementation**: platform-specific or absent (not all targets have an LP core).

---

## Part III — Execution Plan

### Phase 1: Foundation (Layer 0 + Layer 1)

1. Create `include/reflex_types.h` with all error codes and return macros
2. Create `include/reflex_hal.h` with the HAL interface
3. Create `platform/esp32c6/` directory
4. Implement `platform/esp32c6/reflex_types_esp.h` (maps Reflex types to ESP types or vice versa)
5. Implement `platform/esp32c6/reflex_hal_esp32c6.c`
6. Migrate `vm/` to use `reflex_types.h` and `reflex_hal.h` — **the VM becomes platform-independent first** because it has the fewest deps and is the execution layer for user code
7. Build, flash, validate: all existing tests pass

### Phase 2: Storage + Tasks (Layer 2 + Layer 3)

1. Create `include/reflex_kv.h` and `include/reflex_task.h`
2. Implement ESP-IDF backends
3. Migrate `components/goose/goose_runtime.c` — purpose NVS, fabric init
4. Migrate `components/goose/goose_supervisor.c` — snapshot NVS, task delays
5. Migrate `core/fabric.c` — queues
6. Migrate `core/event_bus.c` — queues, tasks
7. Migrate `core/service_manager.c` — task lifecycle
8. Migrate `storage/` — the NVS wrapper becomes a Reflex KV wrapper
9. Build, flash, validate

### Phase 3: Radio + Crypto (Layer 4 + Layer 5)

1. Create `include/reflex_radio.h` and `include/reflex_crypto.h`
2. Implement ESP-IDF backends
3. Migrate `components/goose/goose_atmosphere.c` — the densest file: ESP-NOW, mbedtls, MAC, random, timers
4. Migrate `net/wifi.c` — radio init
5. Build, flash, validate: three-board mesh trial re-run

### Phase 4: Drivers + LP (Layer 6 + Layer 7)

1. Create `include/reflex_drivers.h` and `include/reflex_lp.h`
2. Implement ESP-IDF backends
3. Migrate `services/temp_service.c`, `services/led_service.c`, `services/button_service.c`
4. Migrate `drivers/led.c`, `drivers/button.c`
5. Migrate LP heartbeat code in `goose_runtime.c`
6. Build, flash, validate

### Phase 5: Shell + Main (Cleanup)

1. Migrate `shell/shell.c` — replace any remaining ESP-IDF calls
2. Migrate `main/main.c` — the boot sequence uses only Reflex APIs
3. Remove all direct `#include` of ESP-IDF headers from non-platform code
4. Build with a compile-time check: any ESP-IDF header included outside `platform/` fails the build
5. Full validation: cold boot, purpose set/get/reboot, atlas verify, mesh trial, heartbeat, temp sensor

### Phase 6: Scaffold Cleanup

1. Remove or clearly stub `goose_dma.c`, `goose_etm.c` (scaffolds that claim functionality they don't have)
2. Fix `goose_library.c` NULL dereference in `goose_weave_loom`
3. Evaluate `goose_gateway.c` — remove if still dead code, or wire it into the new task model if the concept is worth keeping
4. Move `bonsai/`, `journal/`, `lmm_repo/` to `archive/` or a dedicated branch

### Phase 7: Containerized Application Model

With the HAL complete and the substrate independent:

1. Multi-VM contexts — each app gets its own `reflex_vm_state_t` with isolated memory regions
2. Holon lifecycle manager — `app deploy`, `app start`, `app stop`, `app remove`
3. Filesystem — LittleFS partition for persistent `.loom`/`.rfxv` storage
4. Manifest-as-cells — `sys.app.<name>` cells reflecting app state in the Tapestry
5. `app autostart` — weave from flash on boot without user intervention
6. Resource quotas per holon — cell count, route count, memory budget, CPU time
7. Shared-VM opt-in — `app deploy --shared-context` for cooperating apps

---

## Part IV — Success Criteria

### v3.0 release gate

- [ ] Zero ESP-IDF `#include` outside `platform/esp32c6/`
- [ ] All existing hardware-validated behaviors pass (cold boot, atlas verify, purpose persistence, mesh trial, heartbeat, temp sensor)
- [ ] At least one non-trivial `.loom` app deployed via `app deploy`, visible in `app list`, surviving reboot via `app autostart`
- [ ] VM fault in one app does not affect another running app
- [ ] `platform/` directory is the only thing that needs to change to port to a new target

### Stretch goals

- [ ] Second platform target (e.g., ESP32-S3 or a non-Espressif RISC-V board) to prove portability
- [ ] Host-side compilation of the substrate for automated testing without hardware
- [ ] Filesystem visible via the shell (`fs ls`, `fs cat`, `fs rm`)

---

## Part V — LMM Analysis

### RAW

What am I actually trying to do here? I'm trying to take a system that grew organically on top of ESP-IDF — calling its functions directly, using its types, relying on its scheduler, depending on its NVS, its Wi-Fi stack, its crypto library — and surgically extract it into something that owns itself.

The obvious risk is that this becomes a six-month refactoring project that produces no new capability. The user just told me purpose-modulated routing is working, the mesh is validated, plasticity persists — and now I'm proposing to touch every file in the codebase to change `esp_err_t` to `reflex_err_t`. That feels like exactly the kind of engineering busywork that kills momentum on a project that's building something genuinely novel.

But the user is right that the dependency is unacceptable. Not because ESP-IDF is bad — it's excellent — but because Reflex OS claims to be an OS. An OS that can only run as a guest on another OS is a framework. Frameworks are useful but they're not what this project is. The naming service, the geometric fabric, the purpose-modulated routing, the Hebbian plasticity — these are kernel-level concepts being implemented in userspace. That mismatch will eventually cap what the system can do.

The deeper issue is the application model. Right now there is no way for a user to write, deploy, and run an application on Reflex OS. You can paste hex into a shell. That's not an OS experience. The containerized holon model the user described — apps as manifolds running in isolated VMs — is the right design, and it requires platform independence because the VM's isolation guarantees only mean something if the substrate beneath it is under Reflex's control.

So the question isn't whether to do this. It's whether the plan is efficient. Am I touching 1,299 call sites when I could touch 30?

Actually, yes. The layered approach means the migration is mechanical at each call site but the *design work* is concentrated in the 7 interface headers. Once `reflex_hal.h` exists, converting `esp_timer_get_time()` to `reflex_hal_time_us()` is a global find-and-replace. The creative work is getting the interfaces right. The migration is grunt work.

The other risk: am I designing too many layers? Seven abstraction layers for an embedded OS on a $5 microcontroller with 4 MB of flash. That sounds heavy. But look at what they cover:

- Types (mechanical, must-do)
- HAL (timers, GPIO, sleep, random — things every OS needs)
- KV (persistent storage — things every OS needs)
- Tasks (scheduling — things every OS needs)
- Radio (mesh — distinctive to this project)
- Crypto (authentication — one function)
- Drivers (peripherals — things every OS needs)
- LP (coprocessor — platform-specific, optional)

These aren't abstractions for abstraction's sake. They're the actual seams in the system. The code already groups along these lines — I'm just making the grouping explicit and contractual.

The execution order matters more than the layer count. The VM first, because it has the fewest deps and is the execution layer for user code. Then the GOOSE runtime (fabric, naming, supervisor), because that's the substrate. Then the atmosphere (mesh), because it's the most complex single file. Then the peripherals. Shell and main last because they're leaf code.

One thing I haven't addressed: the build system. Right now it's pure ESP-IDF CMake. For platform independence, the core codebase needs to build with a standard CMake (or even just a Makefile) that pulls in the platform directory. The ESP-IDF integration becomes a CMakeLists.txt in `platform/esp32c6/` that wraps the standard build in an ESP-IDF component. This isn't in the plan. It should be.

Another gap: testing. The plan has no host-side test story. If the VM and the ternary primitives are truly platform-independent after Phase 1, they should compile and run on the host (Linux/macOS). That gives us CI-level regression testing without flashing a board. The audit flagged this as an honest limit. Phase 1 is the opportunity to close it.

### NODES

1. **Layer 0 (Types)** is the most pervasive change but the most mechanical. It's the foundation everything else builds on.

2. **Layer 1 (HAL)** is the most critical for the substrate. It cuts the deepest ESP-IDF welds: timers, GPIO, sleep, CPU cycles.

3. **Layer 2 (KV)** unlocks purpose persistence, snapshots, Aura key storage, and config — all currently hardwired to NVS.

4. **Layer 3 (Tasks)** is the second most critical. FreeRTOS tasks, queues, and mutexes are the scheduling backbone. Replacing these means Reflex OS owns its concurrency model.

5. **Layer 4 (Radio)** is the most complex single migration — `goose_atmosphere.c` is 368 lines with ESP-NOW, mbedtls, NVS, MAC, random, and timers all interleaved.

6. **Layer 5 (Crypto)** is one function. It's the simplest layer but it unlocks atmosphere independence.

7. **Layers 6+7 (Drivers, LP)** are the most platform-specific. They'll change the most between targets.

8. **Build system independence** is not in the layer plan but must happen alongside Phase 1.

9. **Host-side testing** becomes possible after Phase 1 and should be exploited immediately.

10. **The application model (Phase 7)** is the user-facing payoff. Everything before it is infrastructure.

### REFLECT

The real architectural insight is that the seven layers aren't equal. They fall into three tiers:

**Tier A — Universal (must exist on any platform):** Types, HAL, Tasks, KV. These are the "kernel" of the Reflex abstraction. Without them, nothing runs.

**Tier B — Distinctive (define what makes Reflex OS unique):** Radio, Crypto. These are the mesh and security substrate. Not every platform will have a radio, but the interface must exist (with a null implementation) so the GOOSE atmosphere code compiles everywhere.

**Tier C — Peripheral (platform-specific, optional):** Drivers, LP. These vary per board. The interface exists so services can be written portably, but the implementation is fully platform-specific. A board without an LP core just returns `REFLEX_ERR_NOT_SUPPORTED`.

This tiering means the migration has a natural stopping point for portability. After Tier A, the substrate and VM are platform-independent and testable on a host. After Tier B, the full mesh is portable. Tier C is the long tail of drivers.

The execution order should follow the tiers: A first, then B, then C. Within Tier A, Types first (unblocks everything), then HAL (unblocks the substrate), then Tasks (unblocks concurrency), then KV (unblocks persistence). This is already roughly what the plan says, but the tiering makes the rationale explicit.

The other structural insight: **the build system is Layer 0.5.** It needs to change alongside Types because the platform separation starts there. A `CMakeLists.txt` at the project root that builds the core codebase, with `platform/esp32c6/CMakeLists.txt` wrapping it in ESP-IDF's component model. This enables host-side compilation from day one.

The biggest risk I see is the atmosphere migration (Phase 3). `goose_atmosphere.c` interleaves five different platform dependencies (radio, crypto, timers, NVS, MAC) in a single receive callback. The callback runs in the ESP-NOW task context — replacing the task model while the radio is still ESP-NOW means the callback contract changes. The safest order is: migrate the easy callers first (HAL timer, HAL MAC, HAL random), then KV (NVS), then crypto (mbedtls), then radio last. The radio migration should be atomic — the old ESP-NOW code and the new `reflex_radio` code can't coexist because they both register callbacks.

### SYNTHESIZE

**Key decisions:**

1. **Three-tier layer model:** Tier A (Types, HAL, Tasks, KV) → Tier B (Radio, Crypto) → Tier C (Drivers, LP). Migration follows the tiers in order.

2. **Build system changes in Phase 1:** standard CMake at the root, ESP-IDF wrapper in `platform/esp32c6/`. Host-side compilation of VM + ternary primitives enabled immediately.

3. **VM goes first:** fewest deps, highest portability value, enables host-side testing.

4. **Atmosphere migrates last in Tier B:** it interleaves five dependencies. Migrate them one at a time within the file, radio last and atomically.

5. **One-VM-per-app default, shared-VM opt-in:** comes in Phase 7 after the substrate is independent.

6. **Scaffold cleanup (DMA, ETM, gateway) in Phase 6:** after independence but before the app model, so Phase 7 builds on a clean foundation.

**Action steps:**

1. Create `include/reflex_types.h` and the build system skeleton
2. Create `include/reflex_hal.h` + `platform/esp32c6/reflex_hal_esp32c6.c`
3. Migrate `vm/` — ternary, interpreter, cache, mmu, loader become host-compilable
4. Create `include/reflex_task.h` + `include/reflex_kv.h` + ESP-IDF backends
5. Migrate GOOSE runtime, supervisor, core — the substrate becomes independent of FreeRTOS and NVS
6. Create `include/reflex_radio.h` + `include/reflex_crypto.h` + ESP-IDF backends
7. Migrate atmosphere — radio last, atomically
8. Create `include/reflex_drivers.h` + `include/reflex_lp.h` + ESP-IDF backends
9. Migrate services, drivers, LP heartbeat
10. Migrate shell, main — zero ESP-IDF includes outside `platform/`
11. Clean up scaffolds, fix `goose_library.c` NULL deref
12. Implement containerized application model (multi-VM, lifecycle, filesystem, quotas)

**Success criteria:**

- `grep -r "esp_\|ESP_\|freertos\|nvs_\|mbedtls" --include="*.c" --include="*.h" | grep -v platform/` returns zero results
- All existing hardware-validated behaviors pass unchanged
- VM + ternary primitives compile and pass self-checks on macOS/Linux
- At least one `.loom` app deployed, listed, stopped, restarted, and auto-started across reboot
- Three-board mesh trial re-validated after atmosphere migration

**Resolved tensions:**

- Momentum vs. infrastructure → the VM migrates first and becomes host-testable, giving immediate value even before the substrate migration completes
- Layer count vs. simplicity → the three-tier model (Universal / Distinctive / Peripheral) makes the priority ordering natural and the stopping points clear
- Application model vs. independence → independence comes first because containerization only means something if the substrate beneath it is under Reflex's control
- Build system → changes alongside Layer 0, not deferred, because host-side compilation is the first concrete payoff of independence
