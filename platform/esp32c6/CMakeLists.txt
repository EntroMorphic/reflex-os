set(REFLEX_PLATFORM_SRCS "reflex_hal_esp32c6.c" "reflex_kv_esp32c6.c"
                        "reflex_crypto_esp32c6.c")

# FreeRTOS is always the task backend — it's the scheduling HAL.
# When CONFIG_REFLEX_KERNEL_SCHEDULER is enabled, the shim inserts
# the Reflex kernel hook at xPortStartScheduler. The standalone
# kernel code (reflex_sched.c, vectors, etc.) remains in kernel/
# as infrastructure for a future bare-metal Reflex OS.
list(APPEND REFLEX_PLATFORM_SRCS "reflex_task_esp32c6.c")
if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    list(APPEND REFLEX_PLATFORM_SRCS "../../kernel/reflex_freertos_shim.c")
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

if(CONFIG_REFLEX_KERNEL_SCHEDULER)
    # Wrap xPortStartScheduler to insert the Reflex kernel banner.
    # FreeRTOS runs as the scheduling HAL; the Reflex kernel manages
    # policy (purpose-modulated priorities, holon lifecycle).
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=xPortStartScheduler")
endif()
