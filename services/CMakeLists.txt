idf_component_register(SRCS "led_service.c" "button_service.c" "temp_service.c"
                       INCLUDE_DIRS "include" "../include"
                       REQUIRES core drivers goose esp32c6
                       PRIV_REQUIRES esp_driver_tsens)
