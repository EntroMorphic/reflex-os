idf_component_register(SRCS "ternary.c" "interpreter.c" "loader.c" "syscall.c" "task_runtime.c" "cache.c" "mmu.c" "programs/programs.c" "programs/blink.c" "programs/count.c" "programs/respond.c"
                       INCLUDE_DIRS "include" "programs" "../include"
                       REQUIRES esp_timer storage goose core esp32c6)

set(TASM_PY "${CMAKE_CURRENT_LIST_DIR}/../tools/tasm.py")
set(TASM_DIR "${CMAKE_CURRENT_LIST_DIR}/../examples/tasm")
set(TASM_OUT "${CMAKE_CURRENT_LIST_DIR}/programs")

foreach(prog blink count respond)
    add_custom_command(
        OUTPUT "${TASM_OUT}/${prog}.c"
        COMMAND python3 "${TASM_PY}" "${TASM_DIR}/${prog}.tasm" "${TASM_OUT}/${prog}.c"
        DEPENDS "${TASM_DIR}/${prog}.tasm" "${TASM_PY}"
        COMMENT "TASM: ${prog}.tasm -> ${prog}.c"
    )
endforeach()

add_custom_target(tasm_compile DEPENDS
    "${TASM_OUT}/blink.c"
    "${TASM_OUT}/count.c"
    "${TASM_OUT}/respond.c"
)
add_dependencies(${COMPONENT_LIB} tasm_compile)
