idf_component_register(SRCS "boot.c" "log.c" "service_manager.c" "event_bus.c" "fabric.c"
                       INCLUDE_DIRS "include" "../include"
                       REQUIRES log esp_timer vm esp32c6)

execute_process(COMMAND git describe --always --dirty
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
                OUTPUT_VARIABLE REFLEX_GIT_VERSION
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(NOT REFLEX_GIT_VERSION)
    set(REFLEX_GIT_VERSION "unknown")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE "REFLEX_VERSION=\"${REFLEX_GIT_VERSION}\"")
