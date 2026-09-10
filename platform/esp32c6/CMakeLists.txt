if(NOT "${IDF_TARGET}" STREQUAL "esp32c6")
    idf_component_register()
    return()
endif()

set(REFLEX_PLATFORM_SRCS "reflex_hal_esp32c6.c" "../reflex_crypto.c" "../reflex_log_format.c"
    "reflex_intr_espidf_shim.c")

# The shim is compiled unconditionally, and that is deliberate.
#
# --wrap=esp_intr_alloc is applied to every C6 link, so the wrapper has to
# exist in every C6 build or the reference the linker rewrites has nothing to
# resolve to. A wrapper compiled only behind REFLEX_OWN_ENTRY breaks the
# ordinary build with an undefined reference; that has happened twice in this
# work, for esp_startup_start_app and for xPortStartScheduler. Without the
# flag the wrapper is a straight pass-through to __real_.

# KV backend selection: raw flash (802.15.4 mode) or NVS (WiFi mode)
if(CONFIG_REFLEX_RADIO_802154)
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_kv_flash.c")
else()
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_kv_esp32c6.c")
endif()

# Task backend selection:
# Default: FreeRTOS pass-through (reflex_task_esp32c6.c)
# CONFIG_REFLEX_KERNEL_SCHEDULER: Reflex-owned scheduler + port assembly
if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    # The standalone-scheduler path is compiled here but not yet reached.
    #
    # reflex_sched.c, reflex_startup.c, reflex_trap.c, reflex_vectors.S and
    # reflex_kernel_test.c are the Reflex-owned scheduler, its entry path and
    # its self-test. Nothing calls them today —
    # reflex_task_kernel.c still delegates to FreeRTOS — so the linker discards
    # every one of them, which is exactly why they are listed. Four of them
    # were in no CMakeLists at all and had therefore never been through a
    # compiler; `goose_weave_loom` was in the same position once, and its
    # hardening turned out to have been "verified by inspection only".
    #
    # Compiling without linking is the whole point at this stage: it costs
    # nothing in the image, and it means the scheduler cannot rot silently
    # while the work to reach it is still ahead. They define only
    # reflex_-prefixed symbols in .text with no fixed placement, so there is
    # no conflict with ESP-IDF's own startup or vectors.
    # Exactly one task backend: both define the whole of reflex_task.h.
    if(CONFIG_REFLEX_TASK_BACKEND_REFLEX)
        list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_task_reflex.c")
    else()
        list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_task_kernel.c")
        # The TCB-offset asserts, which are the only remaining user of a
        # FreeRTOS header. They validate reflex_portasm.S's context-switch
        # wrap, which is never called where Reflex owns scheduling.
        list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_freertos_tcb_assert.c")
    endif()
    list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_sched.c"
                                     "../../kernel/reflex_freertos_compat.c"
                                     "../../kernel/reflex_portasm.S"
                                     "../../kernel/reflex_startup.c"
                                     "../../kernel/reflex_trap.c"
                                     "../../kernel/reflex_vectors.S"
                                     "../../kernel/reflex_kernel_test.c"
                                     "../../kernel/reflex_kqueue.c")
else()
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_task_esp32c6.c")
endif()

# spi_flash is for reflex_kv_flash.c, the 802.15.4 build's key-value store.
#
# It used raw esp_rom_spiflash_* to avoid any ESP-IDF component, and that choice
# was the direct cause of it persisting nothing: raw ROM flash access while the
# CPU runs XIP from the same flash does not reach the medium. esp_flash_* does,
# and where Reflex owns the machine the store swaps in esp_flash's own no-OS
# os_func layer so that path needs no scheduler. The dependency is the price of
# a store that keeps what it is given; it is recorded in
# docs/independence-dependency-map.md rather than hidden.
set(REFLEX_PLATFORM_REQUIRES ieee802154 spi_flash)
if(NOT CONFIG_REFLEX_RADIO_802154)
    list(APPEND REFLEX_PLATFORM_REQUIRES nvs_flash)
endif()
set(REFLEX_PLATFORM_PRIV_REQUIRES)

# Radio backend selection
if(CONFIG_REFLEX_RADIO_802154)
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_radio_802154.c")
else()
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_radio_esp32c6.c")
    list(APPEND REFLEX_PLATFORM_REQUIRES esp_wifi)
endif()

idf_component_register(SRCS ${REFLEX_PLATFORM_SRCS}
                       INCLUDE_DIRS "../../include" "../../kernel"
                       REQUIRES ${REFLEX_PLATFORM_REQUIRES}
                       PRIV_REQUIRES ${REFLEX_PLATFORM_PRIV_REQUIRES})

# Coexistence may only be compiled out when nothing else uses the radio.
#
# The blob it removes is 10,130 bytes and 5 of the image's 19 blob symbols, and
# reflex_radio_802154.c replaces its whole contribution to the receive path with
# one COEX_PTI register write. That substitution is a fixed priority, and a fixed
# priority is only correct when there is nothing to arbitrate against.
#
# Reflex runs Wi-Fi exactly when the ESP-NOW backend is selected: net/CMakeLists.txt
# compiles wifi.c only when CONFIG_REFLEX_RADIO_802154 is off. So the condition is
# the radio backend, not CONFIG_ESP_WIFI_ENABLED — which is set on every C6 build
# because the silicon has Wi-Fi, and which an earlier version of this guard tested,
# consequently refusing every 802.15.4 build.
#
# Placed after idf_component_register on purpose. At the top of this file it runs
# during ESP-IDF's early requirements-expansion pass, where a message(FATAL_ERROR)
# surfaces as a CMake syntax error from a generated temp file and names neither
# this check nor this component.
if(NOT CONFIG_ESP_COEX_SW_COEXIST_ENABLE AND NOT CONFIG_REFLEX_RADIO_802154)
    message(FATAL_ERROR "CONFIG_ESP_COEX_SW_COEXIST_ENABLE=n is only valid with the 802.15.4 radio backend: the ESP-NOW backend runs Wi-Fi on the same radio and needs the coexistence arbiter. Enable coexistence, or select CONFIG_REFLEX_RADIO_802154.")
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC "REFLEX_RTC_DATA_ATTR=__attribute__((section(\".rtc.data\")))")

if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    # Kernel hooks via linker --wrap:
    #   xPortStartScheduler — creates supervisor task + tick ISR
    #   rtos_int_enter/exit — our port assembly owns interrupt context switching
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=xPortStartScheduler"
        "-Wl,--wrap=rtos_int_enter"
        "-Wl,--wrap=rtos_int_exit"
        "-Wl,--undefined=__wrap_rtos_int_enter"
        "-Wl,--undefined=__wrap_rtos_int_exit")
endif()
