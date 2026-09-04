if(NOT "${IDF_TARGET}" STREQUAL "esp32c6")
    idf_component_register()
    return()
endif()

set(REFLEX_PLATFORM_SRCS "reflex_hal_esp32c6.c" "../reflex_crypto.c" "../reflex_log_format.c")

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
    list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_task_kernel.c"
                                     "../../kernel/reflex_sched.c"
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

set(REFLEX_PLATFORM_REQUIRES ieee802154)
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
