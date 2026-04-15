idf_component_register(SRCS "goose_runtime.c" "goose_supervisor.c" "goose_library.c" "goose_gateway.c" "goose_atlas.c" "goose_atmosphere.c" "goose_shadow_atlas.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES drivers log core esp_wifi)

if(CONFIG_ULP_COPROC_ENABLED)
    ulp_embed_binary(goose_ulp "ulp/lp_pulse.c")
endif()
