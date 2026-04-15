# Nodes of Interest: Substrate Gaps (Scraper & Latency)

## Node 1: The SVD Semantic Gap
SVD files are flat and technical. GOONIES names are hierarchical and ontological. There is no 1-to-1 mapping.
*Why it matters:* Automated mapping is the only way to achieve face-melting completeness, but it risks "Name Decay" where the auto-generated names are unreadable or logically disconnected.

## Node 2: Flash Latency vs. Search Complexity
$O(N)$ lookup in Flash is a latency bomb. $O(log N)$ requires a sorted manifest.
*Tension with RAM:* We can't sort 2000 names in RAM. We must sort them at build-time in Flash. But binary search on Flash strings involves multiple Flash cache misses. 

## Node 3: The Host-Ternary Cognitive Dissonance
The Supervisor lives in the "Host Mind" (C code) but regulates the "Ternary Body" (Loom).
*Dependency:* The Supervisor needs high-level logging/telemetry, which TASM currently lacks. Moving it to TASM makes it "Geometric" but turns it into a "Black Box" for developers.

## Node 4: Authority of Name vs. Authority of Coord
Currently, names can be hijacked if the resolver isn't careful. v2.3 added a Shadow Check, but what if two shadow peripherals share a name? (Inconsistent SVDs).
*Tension:* Completeness (mapping everything) vs. Integrity (ensuring names are unique and protected).

## Node 5: The "LoomScript" Barrier
Writing `tasm` is too hard for rapid exploration.
*Decision point:* Should we focus on making the substrate *faster* or the application layer *easier*? Gaps focus on the former; Money on the Table focuses on the latter.
