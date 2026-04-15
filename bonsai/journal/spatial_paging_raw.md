# Raw Thoughts: Spatial Paging (Virtualizing the Loom)

## Stream of Consciousness
The 16KB RTC RAM limit is real. 128 cells is just the beginning. 
If we want thousands of cells (Full silicon mapping + complex virtual manifolds), we need to swap.
Paging in binary systems uses MMU/TLB. In GOOSE, we need "Geometric Swapping".

Where do we swap to? Internal Flash (using NVS or SPIFFS or raw partitions). 
NVS is easy for key-value (CellName -> CellData), but might be slow.
Raw partition might be better for "Pages" of cells.

What is a "Page"?
Maybe a `Region`. If a `Field` is inactive (not being pulsed), its `Regions` can be swapped to flash.
But what if a `Route` crosses from an active field to an inactive one?
That triggers a "Geometric Page Fault".
The Supervisor needs to detect this and "Pull" the region back from flash.

Coordinate Compression:
`reflex_tryte9_t` is currently 9 `int8_t` (9 bytes).
We only need 2 bits per trit. 18 bits total.
If we pack it into 3 bytes (24 bits), we save 6 bytes per cell.
6 * 128 = 768 bytes. Not a huge saving, but every byte in RTC RAM counts.

Better: String Names. `char name[16]` is 16 bytes.
Almost half the cell size is just the name. 
Do we need names in the Loom? No, names are for humans.
Geometric Coordinates are for the system.
If we move `name` to a "Spatial Catalog" in Flash, and keep only `coord`, `state`, `type`, `addr`, `mask` in RTC RAM.
`goose_cell_t` without name: 9 (coord) + 1 (state) + 1 (type) + 4 (addr) + 4 (mask) = 19 bytes.
Saving 16 bytes per cell! 128 * 16 = 2048 bytes. Significant.

LRU (Least Recently Used) for Cells?
If the Loom fills up, which cell do we evict?
Maybe "Least Recently Pulsed"?
Virtual cells (Logic) are easier to swap than Hardware cells (IO).
You can't swap a GPIO pin's physical status out of reality.
So `GOOSE_CELL_HARDWARE_IN/OUT` must be "Pinned" in RTC RAM.
`GOOSE_CELL_VIRTUAL` and `GOOSE_CELL_INTENT` are candidates for paging.

What about `Routes`? 
Routes are pointers to cells. If a cell is evicted, the pointer is dangling.
We need "Geometric Pointers" (Coordinates) instead of C pointers in `goose_route_t`.
Current `goose_route_t`:
```c
typedef struct {
    char name[16];
    goose_cell_t *source;
    goose_cell_t *sink;
    ...
} goose_route_t;
```
If we change `source` and `sink` to `reflex_tryte9_t`, then `goose_process_transitions` must perform a lookup for every route pulse.
Lookup is slow ($O(N)$ currently).
We need a "Spatial Cache" or a Hash Table in RAM for active cells.

## Questions Arising
1. How do we handle "Dirty" cells (state changed since last flash sync)?
2. What is the latency of a "Page Fault" during a 100Hz pulse? (Probably too high).
3. Can we use the LP core to handle the swapping while HP core is sleeping?

## First Instincts
- Move `name` out of `goose_cell_t` into a Flash-based `Atlas`.
- Use a packed 2-bit representation for `reflex_tryte9_t`.
- Implement a `Loom Page` system where 32-cell blocks are the unit of swap.
- Hardware-mapped cells are always in "Page 0" (Non-swappable).
