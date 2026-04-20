if(CONFIG_REFLEX_RADIO_802154)
    # 802.15.4 mode: no WiFi, no net component
    idf_component_register()
    return()
endif()

idf_component_register(SRCS "wifi.c"
                       INCLUDE_DIRS "include" "../include"
                       REQUIRES esp_wifi esp_event esp_netif core storage esp32c6)
