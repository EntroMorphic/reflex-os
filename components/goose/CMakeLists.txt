idf_component_register(SRCS "goose_runtime.c" "goose_supervisor.c" "goose_library.c" "goose_gateway.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES vm drivers esp_log core)

if(CONFIG_ULP_COPROC_ENABLED)
    ulp_embed_binary(goose_ulp "ulp/lp_pulse.c")
endif()
