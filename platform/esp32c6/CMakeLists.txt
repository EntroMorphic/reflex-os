if(NOT "${IDF_TARGET}" STREQUAL "esp32c6")
    idf_component_register()
    return()
endif()

set(REFLEX_PLATFORM_SRCS "reflex_hal_esp32c6.c" "reflex_kv_esp32c6.c"
                        "reflex_crypto_esp32c6.c")

# Task backend: FreeRTOS (production default).
# Alternative: CONFIG_REFLEX_KERNEL_SCHEDULER enables the Reflex kernel
# backend in kernel/reflex_task_kernel.c (all 13 reflex_task.h functions).
list(APPEND REFLEX_PLATFORM_SRCS "reflex_task_esp32c6.c")
if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    # Kernel hook + port assembly replacement
    list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_freertos_compat.c"
                                     "../../kernel/reflex_portasm.S")
endif()

set(REFLEX_PLATFORM_REQUIRES driver nvs_flash log ieee802154)
set(REFLEX_PLATFORM_PRIV_REQUIRES esp_driver_tsens)

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
