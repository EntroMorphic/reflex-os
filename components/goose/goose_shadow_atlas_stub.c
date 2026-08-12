/**
 * @file goose_shadow_atlas_stub.c
 * @brief Empty shadow atlas for targets that have no generated catalog.
 *
 * goose_shadow_atlas.c is generated from tools/esp32c6.svd, and it was compiled
 * for every target. On the ESP32 that meant `goonies find agency.gpio.bt_select`
 * resolved happily and reported a C6 register address — a name the substrate
 * had no business knowing on that silicon, pointing at an address that means
 * something entirely different there. The Sanctuary Guard whitelist is C6 page
 * layout too, so its judgements were equally meaningless.
 *
 * An empty catalog is the honest state for a target we have not scraped: name
 * resolution falls through and reports not-found, the boot-time weave projects
 * nothing, and the curiosity prober has no candidates. Everything that does not
 * depend on the MMIO catalog — fabric, mesh, supervisor, VM, shell — is
 * unaffected.
 *
 * To give a target a real atlas: obtain its SVD, run tools/goose_scraper.py
 * against it, and add the generated file to the target's source list in
 * CMakeLists.txt alongside the C6 one.
 */

#include "goose.h"

const shadow_node_t shadow_map[] = {
    /* Deliberately empty. A zero-length array is not valid C, so one inert
     * entry is present; shadow_map_count is 0, so nothing ever reads it. */
    {"", 0, 0, 0, 0, 0, GOOSE_CELL_VIRTUAL},
};

const size_t shadow_map_count = 0;

reflex_err_t goose_shadow_resolve(const char *name, uint32_t *out_addr, uint32_t *out_mask,
                                  reflex_tryte9_t *out_coord, goose_cell_type_t *out_type) {
    (void)name; (void)out_addr; (void)out_mask; (void)out_coord; (void)out_type;
    return REFLEX_ERR_NOT_FOUND;
}
