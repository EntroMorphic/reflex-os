# Reflections: Substrate Gaps (Scraper & Latency)

## Core Insight
The Tapestry is not a flat list of pins; it is a **Hierarchical Search Engine** for physical reality. The "Face-Melting" factor isn't just the 2000 registers, it's the fact that they are **discoverable by intuition**.

## Resolved Tensions
- **O(N) vs. O(log N) → Resolution:** We will use **Build-time Lexicographical Sorting**. If the Shadow Atlas is sorted by name in Flash, we can perform a binary search using only the `strcmp` of the middle element. This keeps latency to $log_2(2000) \approx 11$ comparisons. This is perfectly acceptable for Real-time name resolution (which only happens once per cell during "Page In").
- **SVD Tech vs. GOOSE Ontology → Resolution:** The "Scraper" must use a **Semantic Overlay**. A simple JSON file that maps SVD-Peripheral-Names to GOOSE-Zones (e.g. `GPIO -> agency.gpio`). The scraper then applies these prefixes automatically.

## Hidden Assumptions
- **Assumption:** Every register in the SVD should be a Cell.
- **Challenge:** Many registers are 32-bit bitfields where each bit does something different. Mapping one 32-bit register to one 1-bit Ternary Trit (+1/0/-1) loses 99% of the hardware control.
- **New Understanding:** We need **Sub-cell Bitmasking**. The Atlas should define a name, an address, *and* a bitmask. `agency.rmt.ch0.enable` should point to the same address as `agency.rmt.ch0.idle` but with a different bitmask. This is how we reach 100% MMIO granularity.

## What I Now Understand
To truly "unmask" the machine, the Shadow Atlas must become a **Sub-register Manifest**. We aren't just mapping addresses; we are mapping **Atomic Intents**. The scraper needs to descend into the bitfield level of the SVD.
