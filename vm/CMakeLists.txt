idf_component_register(SRCS "ternary.c" "interpreter.c" "loader.c" "syscall.c" "task_runtime.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer storage)
