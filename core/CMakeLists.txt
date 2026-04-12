idf_component_register(SRCS "boot.c" "log.c" "service_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES spi_flash esp_app_format log)
