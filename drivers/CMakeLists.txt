idf_component_register(SRCS "led.c" "button.c"
                       INCLUDE_DIRS "include" "../include"
                       REQUIRES driver core esp32c6)
