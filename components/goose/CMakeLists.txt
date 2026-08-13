set(GOOSE_PLATFORM_REQUIRES)
if("${IDF_TARGET}" STREQUAL "esp32c6")
    list(APPEND GOOSE_PLATFORM_REQUIRES esp32c6 ulp)
elseif("${IDF_TARGET}" STREQUAL "esp32")
    list(APPEND GOOSE_PLATFORM_REQUIRES esp32)
endif()

# The shadow atlas is generated from tools/esp32c6.svd. Compiling it for other
# targets made them claim C6 hardware knowledge — on the ESP32,
# `goonies find agency.gpio.bt_select` resolved and reported a C6 address that
# means something else on that silicon. Targets without a scraped catalog get an
# empty one instead, which is the honest state.
if("${IDF_TARGET}" STREQUAL "esp32c6")
    set(REFLEX_ATLAS_SRC "goose_shadow_atlas.c")
else()
    set(REFLEX_ATLAS_SRC "goose_shadow_atlas_stub.c")
endif()

idf_component_register(SRCS "goose_runtime.c" "goose_policy.c" "goose_lattice.c" "goose_registry.c" "goose_supervisor.c" "goose_library.c" "goose_atlas.c" "goose_atmosphere.c" "goose_mmio_sync.c" "goose_telemetry.c" "goose_metabolic.c" ${REFLEX_ATLAS_SRC}
                       INCLUDE_DIRS "include" "../../include"
                       REQUIRES ${GOOSE_PLATFORM_REQUIRES}
                       PRIV_REQUIRES drivers log core)

if(CONFIG_ULP_COPROC_ENABLED AND "${IDF_TARGET}" STREQUAL "esp32c6")
    set(ulp_app_name goose_ulp)
    set(ulp_sources "ulp/lp_pulse.c")
    set(ulp_exp_dep_srcs "goose_runtime.c")
    ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")
endif()
