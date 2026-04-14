idf_component_register(SRCS "goose_runtime.c" "goose_supervisor.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES vm drivers esp_log)
