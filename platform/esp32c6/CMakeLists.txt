set(REFLEX_PLATFORM_SRCS "reflex_hal_esp32c6.c" "reflex_kv_esp32c6.c"
                        "reflex_crypto_esp32c6.c")

# Task/scheduler backend selection
if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_sched.c"
                                     "../../kernel/reflex_task_kernel.c"
                                     "../../kernel/reflex_trap.c"
                                     "../../kernel/reflex_startup.c"
                                     "../../kernel/reflex_vectors.S"
                                     "../../kernel/reflex_kernel_test.c"
                                     "../../kernel/reflex_app_startup.c"
                                     "../../kernel/reflex_freertos_shim.c")
else()
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_task_esp32c6.c")
endif()

set(REFLEX_PLATFORM_REQUIRES driver nvs_flash log)
set(REFLEX_PLATFORM_PRIV_REQUIRES esp_driver_tsens)

# Radio backend selection
if(CONFIG_REFLEX_RADIO_802154)
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_radio_802154.c")
    list(APPEND REFLEX_PLATFORM_REQUIRES ieee802154)
else()
    list(APPEND REFLEX_PLATFORM_SRCS "reflex_radio_esp32c6.c")
    list(APPEND REFLEX_PLATFORM_REQUIRES esp_wifi)
endif()

idf_component_register(SRCS ${REFLEX_PLATFORM_SRCS}
                       INCLUDE_DIRS "../../include" "../../kernel"
                       REQUIRES ${REFLEX_PLATFORM_REQUIRES}
                       PRIV_REQUIRES ${REFLEX_PLATFORM_PRIV_REQUIRES})

target_compile_definitions(${COMPONENT_LIB} PUBLIC "REFLEX_RTC_DATA_ATTR=__attribute__((section(\".rtc.data\")))")

# FreeRTOS is the scheduling HAL. The Reflex kernel scheduler
# operates as a supervisor task on top of FreeRTOS, managing
# purpose-modulated priorities and holon lifecycle.
