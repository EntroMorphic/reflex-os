/**
 * @file goose_policy.c
 * @brief Pure substrate decisions. No hardware, no ESP-IDF, no locks.
 *
 * See goose_policy.h for why these live apart from the code that applies them.
 */

#include "goose_policy.h"

bool goose_policy_cell_evictable(int cell_type, int namespace_trit) {
    /* The namespace is the retention policy; type is not.
     *
     * Two values leak into type on the paging path and neither says anything
     * about whether a cached copy of a register may be reclaimed:
     * goose_fabric_alloc_cell stamps PINNED as the default for every system
     * weave, and goose_fabric_set_agency copies SYSTEM_ONLY out of the atlas
     * for sys.* registers. Letting either veto eviction stalled the Loom —
     * measured on hardware, vetoing PINNED stopped eviction after 64 cells and
     * additionally vetoing SYSTEM_ONLY stopped it after 139, in both cases
     * leaving the substrate unable to page anything further until reset. */
    if (namespace_trit == GOOSE_POLICY_NS_SHADOW ||
        namespace_trit == GOOSE_POLICY_NS_PEER) {
        return true;
    }

    /* Namespace 0 — seeds and the boot atlas weave. These are identity, and
     * here the types were set deliberately, so the original rule stands. */
    if (cell_type == GOOSE_POLICY_TYPE_PINNED ||
        cell_type == GOOSE_POLICY_TYPE_SYSTEM_ONLY ||
        cell_type == GOOSE_POLICY_TYPE_PURPOSE) {
        return false;
    }
    return cell_type != GOOSE_POLICY_TYPE_HARDWARE_IN &&
           cell_type != GOOSE_POLICY_TYPE_HARDWARE_OUT;
}

int goose_policy_disposition(bool purpose_active, bool has_domain_route,
                             bool in_any_holon, bool in_active_holon,
                             bool pained) {
    /* The purpose_active guard on the holon branch is load-bearing. A holon
     * with a non-empty domain deactivates whenever no purpose is set, so
     * without it a field reads WITHHELD on a board that has simply never been
     * told what to do. "Nobody has declared a purpose" is the definition of
     * undecided, not of suppressed. */
    if (purpose_active && in_any_holon && !in_active_holon) {
        return GOOSE_POLICY_WITHHELD;
    }
    /* Pain narrows attention rather than halting work: fields on the purpose
     * domain stay engaged, everything else is held back. */
    if (pained && !has_domain_route) {
        return GOOSE_POLICY_WITHHELD;
    }
    if (purpose_active && has_domain_route) {
        return GOOSE_POLICY_ENGAGED;
    }
    return GOOSE_POLICY_LATENT;
}

int goose_policy_aggregate(int engaged_count, int withheld_count) {
    /* Precedence, not unanimity. Requiring every field to be withheld made -1
     * unreachable in practice and reported "latent" while a field was
     * demonstrably being held back. */
    if (engaged_count > 0)  return GOOSE_POLICY_ENGAGED;
    if (withheld_count > 0) return GOOSE_POLICY_WITHHELD;
    return GOOSE_POLICY_LATENT;
}

int goose_policy_mesh_state(goose_mesh_window_t *w, uint32_t delta,
                            uint32_t isolated_ticks, uint32_t sparse_threshold) {
    if (!w) return GOOSE_POLICY_LATENT;
    /* Defensive: the caller's isolated_ticks is derived from two divisors and
     * can degenerate to 0 under a retune, which would make `idle >= 0` always
     * true and reset the window every scan. reflex_tuning.h floors it, and so
     * does this, because a pure function should not depend on its caller
     * having remembered to. */
    if (isolated_ticks < 2) isolated_ticks = 2;

    if (delta == 0) w->idle_ticks++;
    else            w->idle_ticks = 0;

    w->window_arcs += delta;
    if (++w->window_ticks >= isolated_ticks) {
        w->window_last  = w->window_arcs;
        w->window_arcs  = 0;
        w->window_ticks = 0;
    }

    if (w->idle_ticks >= isolated_ticks) return GOOSE_POLICY_WITHHELD; /* isolated */

    /* Count the window in progress alongside the last completed one, so a board
     * that has just heard a beacon reports connected immediately rather than
     * waiting out the remainder of the window. Judged over a beacon period
     * because a single ~1s scan of a quiet mesh sees nothing: the old per-scan
     * threshold needed ~5 arcs/sec against an actual ~0.1, so +1 could not
     * occur at all. */
    if ((w->window_arcs + w->window_last) >= sparse_threshold) return GOOSE_POLICY_ENGAGED;
    return GOOSE_POLICY_LATENT;
}

uint16_t goose_policy_snap_entry_count(size_t route_count, size_t max_routes) {
    return (uint16_t)(route_count < max_routes ? route_count : max_routes);
}

uint32_t goose_policy_replay_slot(uint32_t nonce, const uint8_t *mac, uint32_t slot_count) {
    /* FNV-1a over all six MAC bytes, folded with the nonce and finished with a
     * Fibonacci multiply and a shift-xor, so no input bit is confined to a bit
     * position the caller's reduction discards.
     *
     * The bug this replaces was not a weak hash but a hash that dropped one of
     * its two inputs entirely: the MAC contributed only to bits 8-23 of a value
     * that was then masked to 6 bits. Mixing downward is the whole point. */
    if (mac == NULL || slot_count == 0) {
        return 0;
    }

    uint32_t h = 2166136261u; /* FNV offset basis */
    for (int i = 0; i < 6; i++) {
        h ^= (uint32_t)mac[i];
        h *= 16777619u; /* FNV prime */
    }
    h ^= nonce;
    h *= 0x9E3779B1u;
    h ^= h >> 16;

    /* Modulo rather than a mask: the slot count is tunable and nothing
     * constrains it to a power of two. */
    return h % slot_count;
}

bool goose_policy_loom_route_acceptable(int orientation, unsigned coupling) {
    if (orientation < -1 || orientation > 1) {
        return false;
    }
    /* A whitelist, not a blacklist. A future coupling mode added to the enum
     * must be considered deliberately before the wire can request it, rather
     * than becoming reachable the moment it is defined. */
    return coupling == GOOSE_POLICY_COUPLING_SOFTWARE;
}

int goose_policy_clamp_learned_orientation(int value) {
    if (value < -1 || value > 1) {
        return 0;
    }
    return value;
}

int goose_policy_clamp_hebbian_counter(int value, int counter_max) {
    if (counter_max < 0) {
        return 0;
    }
    if (value > counter_max) {
        return counter_max;
    }
    if (value < -counter_max) {
        return -counter_max;
    }
    return value;
}

bool goose_policy_key_material_plausible(const uint8_t *key, size_t len) {
    if (key == NULL || len == 0) {
        return false;
    }
    /* All bytes identical covers both the all-zero case (a latched register
     * XORed with itself) and a constant fill. */
    for (size_t i = 1; i < len; i++) {
        if (key[i] != key[0]) {
            return true;
        }
    }
    return false;
}
