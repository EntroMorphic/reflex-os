idf_component_register(SRCS "shell.c"
                       INCLUDE_DIRS "include"
                       REQUIRES vm core net esp_driver_usb_serial_jtag vfs esp_netif)
