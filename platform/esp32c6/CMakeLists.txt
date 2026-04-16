idf_component_register(SRCS "reflex_hal_esp32c6.c" "reflex_task_esp32c6.c" "reflex_kv_esp32c6.c"
                             "reflex_radio_esp32c6.c" "reflex_crypto_esp32c6.c"
                       INCLUDE_DIRS "../../include"
                       REQUIRES driver esp_timer esp_wifi nvs_flash log mbedtls)

target_compile_definitions(${COMPONENT_LIB} PUBLIC "REFLEX_RTC_DATA_ATTR=__attribute__((section(\".rtc.data\")))")
