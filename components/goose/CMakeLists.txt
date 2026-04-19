set(GOOSE_PLATFORM_REQUIRES)
if("${IDF_TARGET}" STREQUAL "esp32c6")
    list(APPEND GOOSE_PLATFORM_REQUIRES esp32c6 ulp)
elseif("${IDF_TARGET}" STREQUAL "esp32")
    list(APPEND GOOSE_PLATFORM_REQUIRES esp32)
endif()

idf_component_register(SRCS "goose_runtime.c" "goose_supervisor.c" "goose_library.c" "goose_gateway.c" "goose_atlas.c" "goose_atmosphere.c" "goose_shadow_atlas.c"
                       INCLUDE_DIRS "include" "../../include"
                       REQUIRES ${GOOSE_PLATFORM_REQUIRES}
                       PRIV_REQUIRES drivers log core)

if(CONFIG_ULP_COPROC_ENABLED AND "${IDF_TARGET}" STREQUAL "esp32c6")
    set(ulp_app_name goose_ulp)
    set(ulp_sources "ulp/lp_pulse.c")
    set(ulp_exp_dep_srcs "goose_runtime.c")
    ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
endif()
