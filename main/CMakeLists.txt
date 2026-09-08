# reflex_app_entry.c lives here so --wrap can find it.
#
# It provides __wrap_esp_startup_start_app, taking the application entry point
# away from FreeRTOS. --wrap turns the call in esp_system's startup.c into an
# undefined reference to __wrap_..., which is enough to pull this object out of
# libmain.a the way any archive member is pulled — no WHOLE_ARCHIVE needed.
#
# An earlier version defined esp_startup_start_app outright instead. That failed
# twice over: in the platform component the object was never linked at all,
# because both it and libfreertos.a(app_startup.c.obj) were archive members and
# FreeRTOS's won on link order, silently; and forced in with WHOLE_ARCHIVE it
# then collided, because app_startup.c.obj enters the link regardless.
idf_component_register(SRCS "main.c" "reflex_app_entry.c"
                       PRIV_REQUIRES core storage drivers services net vm shell goose esp32c6
                       INCLUDE_DIRS "")

# Redirect ESP-IDF's call to its own startup into Reflex's, when Reflex is
# taking the entry point. --wrap acts on the reference rather than the symbol,
# so FreeRTOS's definition can stay in the image unreferenced instead of
# colliding with a second one.
if(CONFIG_IDF_TARGET_ESP32C6)
    target_link_options(${COMPONENT_LIB} INTERFACE
                        "-Wl,--wrap=esp_startup_start_app")
endif()
