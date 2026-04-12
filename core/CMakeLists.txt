idf_component_register(SRCS "boot.c" "log.c" "service_manager.c" "event_bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES spi_flash esp_app_format log esp_timer)
