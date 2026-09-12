# Archive — retired documents

Documents whose work is finished. They are kept rather than deleted because a
plan is evidence of why the code looks the way it does, and a reader tracing a
decision benefits from the document that drove it.

**These are history. Do not treat anything here as current.** For what is true
now, see [`../implementation-status.md`](../implementation-status.md).

An earlier convention deleted completed trackers outright and left their history
in the git log. Archiving is the better trade: `git log --follow` still works,
and the document stays readable without a reader having to know it ever existed.

| Document | Retired | Why, and what carries it now |
|---|---|---|
| [`prd-mmio-sync.md`](prd-mmio-sync.md) | 2026-09-12 | Built. `components/goose/goose_mmio_sync.c` ships it, the supervisor emits through `goose_mmio_sync_emit`, and `mesh stat` counts `rx_mmio_sync`/`tx_mmio_sync`. The living design note is [`../design-mmio-sync.md`](../design-mmio-sync.md). |
| [`remediation-plan.md`](remediation-plan.md) | 2026-09-12 | All four gaps closed: KV sector compaction, the integration test, the MMIO sync layer, and the work that followed them. |
| [`remediation-plan-v2.md`](remediation-plan-v2.md) | 2026-09-12 | Both items shipped: TASM runtime upload (`tasm.py --upload`) and the service watchdog (`reflex_service_watchdog_tick`, running at 1 Hz off the supervisor pulse). |
| [`user-manual-sow.md`](user-manual-sow.md) | 2026-09-12 | Delivered. The manual it specified is [`../USER_MANUAL.md`](../USER_MANUAL.md). |
| [`REMEDIATION.md`](REMEDIATION.md) | 2026-09-12 | Complete, and verified so: purpose-modulated routing is in `goose_runtime.c`, the `purpose` shell command exists, and the CHANGELOG records both items shipped. Lived at the repository root, indexed nowhere. |
| [`v3-bootloader-plan.md`](v3-bootloader-plan.md) | 2026-09-12 | Achieved. `bootloader_components/main/reflex_boot0.c` **is** the second-stage bootloader: it uses no `bootloader_init`, no `bootloader_utility_*`, no `bootloader_support` high-level function, and depends only on ROM entry points, SOC constants and thin cache/MMU wrappers. The plan's "Phase 0" self-description — mechanism still ESP-IDF's — was true in April 2026 and is not now. |

## What is *not* archived

One document that looked orphaned is a live plan, not history, and stays in
`docs/` with an index entry:

- `cortexm_port.md` — scoping for the first non-Espressif target. Unstarted, so
  nothing has superseded it.

`v3-bootloader-plan.md` was nearly kept for the same reason, on the strength of
its own "Phase 0" status line. The code disagreed: read the source before
trusting a plan's description of itself.
