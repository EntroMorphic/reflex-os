if(NOT "${IDF_TARGET}" STREQUAL "esp32")
    idf_component_register()
    return()
endif()

set(REFLEX_PLATFORM_SRCS "reflex_hal_esp32.c" "reflex_kv_esp32.c"
                        "reflex_crypto_esp32.c" "reflex_task_esp32.c"
                        "reflex_radio_esp32.c")

set(REFLEX_PLATFORM_REQUIRES driver nvs_flash log esp_timer esp_wifi)

idf_component_register(SRCS ${REFLEX_PLATFORM_SRCS}
                       INCLUDE_DIRS "../../include" "../../kernel"
                       REQUIRES ${REFLEX_PLATFORM_REQUIRES})

# ESP32 has limited RTC slow RAM — don't place fabric data there
target_compile_definitions(${COMPONENT_LIB} PUBLIC "REFLEX_RTC_DATA_ATTR=")
