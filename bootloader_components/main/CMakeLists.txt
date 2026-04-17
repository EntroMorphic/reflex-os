idf_component_register(SRCS "reflex_boot0.c"
                    REQUIRES bootloader bootloader_support hal soc esp_rom esp_hw_support)

idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
