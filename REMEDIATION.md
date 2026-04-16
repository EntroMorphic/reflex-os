# Remediation Plan — Atlas Coverage Red-Team

Tracks execution of the nine findings from the red-team review of commit `6bb1f0a` (`feat(atlas): full-surface MMIO name resolution + atlas verify`).

**Status legend:** ☐ pending · ◐ in progress · ☑ done

**Pre-flight (completed before plan finalized):**
- Zero duplicate names in `goose_shadow_atlas.c` (`grep | sort | uniq -d` → 0)
- Catalog rows already sorted in `LC_ALL=C` byte order (`sort -c` → silent, clean)
- 9527 rows match `shadow_map_count`

These results mean Phases A+B are *hardening* (instrumentation + future-regression gates), not *bug fixing*. The only unverified property from my original red-team is the type+coord round-trip — Phase A closes that.

---

## Phase A — Verify correctness + honest language

Close R1, R2, R3 in one commit. These together form "make `atlas verify` actually verify what the output claims."

### ☑ R1 — HIGH — verify type+coord round-trip, not just addr+mask

**Finding.** `atlas verify`'s pass check is `resolve(name) == ESP_OK && addr_matches && mask_matches`. Type and coord are never compared. A scraper bug where (say) a `GOOSE_CELL_HARDWARE_IN` entry is emitted as `GOOSE_CELL_SYSTEM_ONLY` would silently pass.

**Fix.** Add two more equality checks in the verify loop:
- `type == shadow_map[i].type`
- `goose_coord_equal(coord, goose_make_shadow_coord(shadow_map[i].f, shadow_map[i].r, shadow_map[i].c))`

**Validation.** Re-run `atlas verify` on Alpha. Expected: `ok=9527 failed=0` still. If anything fails, that's a real bug — stop and investigate.

### ☑ R2 — HIGH — duplicate-name detection

**Finding.** Binary search silently consolidates duplicate names — both would "resolve" but only to one entry. `atlas verify` can't catch it.

**Fix.** Add a sweep loop before the resolve loop: iterate `i = 1..count` and check `strcmp(shadow_map[i-1].name, shadow_map[i].name) != 0`. Since the catalog is sorted by name, any duplicate is adjacent. Count duplicates separately from resolve failures.

**Validation.** Pre-flight already showed 0 dupes. Verify reports `duplicates=0`. This is a regression gate for future scraper changes.

### ☑ R3 — HIGH — "100% of MMIO" overclaims

**Finding.** Both the `atlas verify` success line and the docs claim *"100% MMIO coverage"*. What I can actually prove is *"100% of the SVD-documented MMIO catalog"*. The SVD doesn't include undocumented registers, eFuse bits outside SVD field schema, test-mode registers, or silicon-revision deltas.

**Fix.** Change wording in three places:
- `reflex_shell_atlas_verify` success line
- `docs/architecture.md` §2 G.O.O.N.I.E.S. & Shadow Paging
- `docs/implementation-status.md` G3 Atlas and Shadow Paging

From *"100% of MMIO shadow catalog"* to *"100% of the SVD-documented MMIO catalog"*.

**Validation.** Visual review of updated strings; no hardware validation needed for doc-only changes.

---

## Phase B — Scraper invariants

Close R5, R6. These harden the scraper against future changes that could silently break the binary-search resolver.

### ☑ R5 — MEDIUM — assert ASCII + explicit byte-order sort

**Finding.** Python's default `sort()` uses Unicode code-point ordering. `goose_shadow_resolve` uses `strcmp` (byte order). These match for pure ASCII but diverge on non-ASCII. The current SVD is pure ASCII — safe today, undefined tomorrow.

**Fix.** In `tools/goose_scraper.py`:
1. Assert every name is pure ASCII before sorting: `assert n["name"].isascii(), f"non-ASCII name: {n['name']}"`.
2. Sort explicitly with a byte-key: `shadow_nodes.sort(key=lambda x: x["name"].encode("ascii"))`.

The sort output is byte-identical for ASCII-only input (pre-flight confirmed), so the regenerated `goose_shadow_atlas.c` should match the current committed state exactly.

**Validation.** Regenerate the catalog; diff against committed; expect no changes. Build clean.

### ☑ R6 — MEDIUM — scraper ↔ header struct sync discipline

**Finding.** `tools/goose_scraper.py` emits positional C initializers (`{"name", addr, mask, f, r, c, type}`) whose order depends on `shadow_node_t`'s field layout in `goose.h`. If someone reorders the struct without updating the scraper, the regenerated file fails to compile.

**Fix.** Add reciprocal comments:
- In `goose_scraper.py`, at the emit block: *"Positional initializer order must match `shadow_node_t` in `components/goose/include/goose.h`"*.
- In `goose.h` above `shadow_node_t`: *"Field order is consumed by `tools/goose_scraper.py` — update both together or regeneration will break."*

**Validation.** Doc-only. No build changes.

---

## Phase C — Verify UX polish

Close R4, R9. These make the verify output nicer for humans without changing its correctness properties.

### ☑ R4 — MEDIUM — no progress indication / ~100 ms shell monopoly

**Finding.** `atlas verify` processes 9527 entries in a tight loop with no output until completion. On a slow terminal or a scripted capture with a short window, the user might think the shell hung. Also: the entire verification holds the shell task for ~100 ms (no scheduler yield), though it doesn't take `loom_authority` so it doesn't starve the supervisor pulse.

**Fix.** Two tiny changes:
1. Yield every 1000 entries: `if ((i % 1000) == 0) vTaskDelay(0);` — lets FreeRTOS schedule higher-priority tasks between chunks.
2. Emit a progress dot every 1000 entries: `if ((i % 1000) == 0) { putchar('.'); fflush(stdout); }`.

**Validation.** Run `atlas verify` on Alpha; observe dots followed by summary.

### ☑ R9 — LOW — redundant output line *(absorbed into Phase A output rewrite)*

**Finding.** The two-line verify output *"total=9527 ok=9527 failed=0"* followed by *"100% of MMIO shadow catalog resolves cleanly."* is redundant AND R3 says the second line is wrongly worded anyway.

**Fix.** Single-line summary that's correct per R3: *"ATLAS VERIFY: ok=9527/9527 (100% of SVD-documented MMIO catalog); duplicates=0, failures=0"*. Folds R3's wording and R2's dup count into one line.

**Validation.** Visual review of updated output on Alpha.

---

## Phase D — Docs

Close R7, R8. These record the behavior changes from commit `6bb1f0a` so the next reader (including future-me) is not surprised.

### ☐ R7 — LOW — shell output schema change in CHANGELOG

**Finding.** `goonies find` now emits `[live]` or `[shadow]` labels, and the shadow form has a completely different format (`addr=0x... mask=0x... type=...`). Any regex-parser of the old output breaks. Nobody is currently parsing, but the schema change is real.

**Fix.** Add an entry under `## [Unreleased]` → `### Changed` in `CHANGELOG.md` describing the new output schema.

### ☐ R8 — LOW — serial shell information surface expansion in SECURITY.md

**Finding.** `goonies find` now reveals the MMIO address + bit mask + ontological type for 9527 SVD-documented registers, not just the ~104 pre-woven atlas cells. The serial shell is already trust-equivalent to JTAG so this isn't a new attack surface, but it IS a broader information surface accessible through a single interactive command.

**Fix.** Add a paragraph to `SECURITY.md` §1 (The Sanctuary Guard) documenting that the serial shell exposes full SVD-documented MMIO information via `goonies find`, and that the shell itself is a trusted interface — not a defect, but honest scope documentation.

---

## Execution log

- **Phase A** — R1, R2, R3, R9 — `atlas verify` now does full round-trip (name + addr + mask + type + coord via `goose_make_shadow_coord`) and sweeps for adjacent-name duplicates in a separate pass. Output rewritten to a single line that absorbs R3's honest "SVD-documented" wording and R9's dedup. `docs/architecture.md` §2 and `docs/implementation-status.md` G3 both updated with the narrower coverage claim. Validated on Alpha: `ok=9527/9527 (100% of SVD-documented MMIO catalog); duplicates=0, failures=0`.
- **Phase B** — R5, R6 — scraper now asserts every name is pure ASCII at scrape time and sorts via explicit `encode("ascii")` byte key; reciprocal sync-discipline comments added to both `tools/goose_scraper.py` (emit block) and `shadow_node_t` in `goose.h`. Regenerated catalog diff is limited to the new header comment block — the 9527 data rows are byte-identical to the prior state, confirming that (a) Python default sort and byte-order sort coincide for the current ASCII-only catalog, and (b) the ASCII invariant is satisfied today.
- **Phase C** — R4 — `atlas verify` emits a progress dot every 1000 entries and calls `vTaskDelay(0)` between chunks so higher-priority tasks run. Validated on Alpha: exactly ten dots (9527/1000 → ceil(10)) followed by the summary line; no regression.
