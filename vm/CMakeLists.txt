idf_component_register(SRCS "ternary.c" "interpreter.c" "loader.c" "syscall.c" "task_runtime.c" "cache.c" "mmu.c" "programs/programs.c"
                       INCLUDE_DIRS "include" "programs" "../include"
                       REQUIRES esp_timer storage goose core esp32c6)
