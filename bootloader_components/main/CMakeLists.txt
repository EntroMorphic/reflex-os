if("${IDF_TARGET}" STREQUAL "esp32c6")
    # Reflex Boot0 — our own second-stage bootloader. C6 only: it speaks to
    # LP_AON registers, the C6 MMU layout and the RISC-V ROM entry points.
    idf_component_register(SRCS "reflex_boot0.c"
                        REQUIRES bootloader bootloader_support soc esp_rom esp_hw_support hal)
    idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
    target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
else()
    # Every other target uses ESP-IDF's stock second-stage bootloader.
    #
    # We would rather not override at all here, but this directory is named
    # `main`, so it shadows the bootloader's own main component whatever the
    # target — the registration therefore has to reproduce what that component
    # does, not merely name the same source file.
    #
    # It previously omitted BOOTLOADER_LINKER_SCRIPT, which is what places
    # bootloader code near the ROM routines it calls. Without it the Xtensa
    # link failed inside IDF's own bootloader_panic.c with "dangerous
    # relocation: windowed longcall crosses 1GB boundary", and the
    # -Wl,--no-check-sections that used to sit here was an attempt to silence
    # that rather than a fix. It also required only bootloader_support, missing
    # `bootloader`, and dropped the -u bootloader_hooks_include undefined-symbol
    # reference that keeps the hook table linked in.
    #
    # Kept deliberately in step with
    # components/bootloader/subproject/main/CMakeLists.txt; if that file gains
    # a property, this branch needs it too.
    idf_component_register(SRCS "$ENV{IDF_PATH}/components/bootloader/subproject/main/bootloader_start.c"
                        REQUIRES bootloader bootloader_support)
    idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
    target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u bootloader_hooks_include")
endif()
