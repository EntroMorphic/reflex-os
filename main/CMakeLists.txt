# WHOLE_ARCHIVE, and reflex_app_entry.c lives here because of it.
#
# reflex_app_entry.c defines esp_startup_start_app to take the application entry
# point away from FreeRTOS. That works only if the linker takes the definition
# unconditionally: an archive member is pulled in solely to resolve an undefined
# symbol, so with the file in the platform component both it and
# libfreertos.a(app_startup.c.obj) were archive members and FreeRTOS's won on
# link order — the override was silently not linked at all. Whole-archiving main
# is what makes the definition win.
idf_component_register(SRCS "main.c" "reflex_app_entry.c"
                       PRIV_REQUIRES core storage drivers services net vm shell goose esp32c6
                       INCLUDE_DIRS ""
                       WHOLE_ARCHIVE)

# Redirect ESP-IDF's call to its own startup into Reflex's, when Reflex is
# taking the entry point. --wrap acts on the reference rather than the symbol,
# so FreeRTOS's definition can stay in the image unreferenced instead of
# colliding with a second one.
if(CONFIG_IDF_TARGET_ESP32C6)
    target_link_options(${COMPONENT_LIB} INTERFACE
                        "-Wl,--wrap=esp_startup_start_app")
endif()
