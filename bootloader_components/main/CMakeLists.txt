if("${IDF_TARGET}" STREQUAL "esp32c6")
    idf_component_register(SRCS "reflex_boot0.c"
                        REQUIRES bootloader bootloader_support soc esp_rom esp_hw_support hal)
    idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
    target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
else()
    idf_component_register(SRCS "$ENV{IDF_PATH}/components/bootloader/subproject/main/bootloader_start.c"
                        REQUIRES bootloader_support)
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--no-check-sections")
endif()
