idf_component_register(SRCS "storage.c" "config.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash core)
