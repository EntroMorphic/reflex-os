idf_component_register(SRCS "led_service.c" "button_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES core drivers goose)
