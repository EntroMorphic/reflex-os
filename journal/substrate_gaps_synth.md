# Synthesis: All-Seeing Atlas v2.0 (The Exhaustive Specification)

## Architecture
Moving from "Representative Registers" to "Exhaustive Atomic Intents." We will map 100% of the ESP32-C6's technical capability by scraping its SVD down to the bitfield level and providing $O(log N)$ discovery.

## Key Decisions
1. **Bitmasking is First-Class:** Every shadow node will include a 32-bit `bit_mask`. Multiple names can map to the same address if they have different masks (e.g. `uart.conf.tx_en` and `uart.conf.rx_en`).
2. **Binary Search Registry:** The Shadow Atlas will be generated as a sorted C array in Flash. The resolver will use a 12-step binary search to find any name in under 1ms.
3. **Ontological Scraper:** A Python tool `tasm_atlas_gen.py` will handle the SVD-to-GOOSE transformation using a semantic overlay.

## Implementation Spec
### 1. Shadow Node Structure (Update)
```c
typedef struct {
    const char *name;
    uint32_t addr;
    uint32_t bit_mask;   // New: Granular control
    int8_t f, r, c;
    goose_cell_type_t type;
} shadow_node_t;
```

### 2. Binary Search Resolver
```c
esp_err_t goose_shadow_resolve(const char *name, ...) {
    int low = 0, high = TOTAL_NODES - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        int res = strcmp(name, shadow_map[mid].name);
        if (res == 0) return SUCCESS;
        if (res > 0) low = mid + 1;
        else high = mid - 1;
    }
    return NOT_FOUND;
}
```

### 3. The GOONIES Scraper (`tools/goose_scraper.py`)
- **Input:** `esp32c6.svd`, `goose_zones.json`
- **Output:** `components/goose/goose_shadow_atlas.c` (Sorted by name)

## Success Criteria
- [ ] 100% of registers from the SVD are addressable by G.O.O.N.I.E.S. name.
- [ ] Sub-register bitfields (like `UART_CONF0_RXFIFO_RST`) have their own named cells.
- [ ] Search time for a name remains under 1,000 cycles (~12 iterations).
- [ ] No increase in RAM usage (names remain in Flash).
