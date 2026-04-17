# Reflex OS Standalone Build Configuration
#
# This CMake toolchain file configures a standalone RISC-V build
# that does not depend on the ESP-IDF build system. It uses the
# same GCC toolchain (riscv32-esp-elf) but invokes it directly.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=standalone.cmake -B build-standalone
#   cmake --build build-standalone
#
# Currently builds: the VM layer + ternary primitives (host-testable).
# The full kernel build requires the linker script and startup
# assembly, which are tracked as future work.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

# Find the Espressif RISC-V toolchain
# Users can override by setting RISCV_TOOLCHAIN_PATH
if(NOT RISCV_TOOLCHAIN_PATH)
    # Try common locations
    find_program(RISCV_GCC riscv32-esp-elf-gcc
        PATHS
            $ENV{HOME}/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin
            /opt/riscv/bin
    )
    if(RISCV_GCC)
        get_filename_component(RISCV_TOOLCHAIN_PATH ${RISCV_GCC} DIRECTORY)
    endif()
endif()

if(RISCV_TOOLCHAIN_PATH)
    set(CMAKE_C_COMPILER ${RISCV_TOOLCHAIN_PATH}/riscv32-esp-elf-gcc)
    set(CMAKE_ASM_COMPILER ${RISCV_TOOLCHAIN_PATH}/riscv32-esp-elf-gcc)
else()
    # Fall back to system compiler (for host testing)
    message(STATUS "RISC-V toolchain not found — using host compiler for testing")
endif()

set(CMAKE_C_FLAGS "-march=rv32imc -mabi=ilp32 -Os -Wall -Wextra" CACHE STRING "" FORCE)
