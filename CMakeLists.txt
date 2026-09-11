# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(REFLEX_COMMON_COMPONENTS core storage drivers services net vm shell)
set(EXTRA_COMPONENT_DIRS ${REFLEX_COMMON_COMPONENTS} platform/esp32c6 platform/esp32)

# Custom bootloader (Boot0) is ESP32-C6 only
if(NOT DEFINED IDF_TARGET OR "${IDF_TARGET}" STREQUAL "esp32c6")
    set(BOOTLOADER_EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/bootloader_components")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reflex_os)

# REFLEX_OWN_HEAP says "Reflex serves this image's allocations from its own
# region". It is derived from the radio backend rather than being a Kconfig
# option of its own, because that is exactly what it depends on: the shim is
# applied on the 802.15.4 path and not on the Wi-Fi one.
#
# A global -D rather than a CONFIG_ read from sdkconfig.h, so that the files
# that condition on it — goose_metabolic.c and shell/shell.c — do not have to
# include sdkconfig.h to see it. Including it would trade a Tier B dependency
# for a Tier F one and call it progress.
if(CONFIG_REFLEX_RADIO_802154)
    idf_build_set_property(COMPILE_DEFINITIONS "REFLEX_OWN_HEAP=1" APPEND)
endif()
