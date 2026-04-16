idf_component_register(SRCS "wifi.c"
                       INCLUDE_DIRS "include" "../include"
                       REQUIRES esp_wifi esp_event esp_netif core storage esp32c6)
