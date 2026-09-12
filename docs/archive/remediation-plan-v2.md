# Remediation Plan v2 — Remaining Gaps

All technical debt is cleared. What follows is feature work and
architectural decisions, ordered by effort-to-impact ratio.

## Execution Order

### R1: TASM Runtime Upload (30 min)
**PRD:** `docs/prd-tasm-upload.md`
Add `--upload <port>` to `tasm.py` so programs can be compiled and sent
to the device in one command without reflashing.

### R2: Service Watchdog (1 hour)
**PRD:** `docs/prd-service-watchdog.md`
Auto-restart crashed services with exponential backoff. Adds reliability
without manual intervention.

### R3: True Deep Sleep (15 min)
**Assessment:** `docs/assessment-deep-sleep.md`
Replace timed-reboot with ESP-IDF sleep calls. 10mA → 7uA.

### R4: Autonomous Reward (1 hour)
**PRD:** `docs/prd-autonomous-reward.md`
Close the Hebbian learning loop. Supervisor evaluates purpose fulfillment
and generates reward/pain signals automatically.

### R5: Mesh Auto-Discovery (2 hours)
**PRD:** `docs/prd-mesh-discovery.md`
Boards with the same Aura key find each other automatically.
Eliminates manual `mesh peer add` on both boards.

### R6: PyPI Publish (15 min)
**PRD:** `docs/prd-pypi-publish.md`
`pip install reflex-os` works from anywhere. Requires PyPI credentials.

## Deferred (Assessed, Not Actioned)

### 802.15.4 Driver Extraction
**Assessment:** `docs/assessment-802154-extraction.md`
Recommendation: accept the shim boundary. Full extraction has high cost
and low user value. The driver works; the abstraction isolates it.

### FreeRTOS Startup Path
**Assessment:** `docs/assessment-freertos-startup.md`
Recommendation: accept FreeRTOS as the scheduling backend. reflex_task.h
already isolates it completely. Replacing it gains independence but risks
stability.

## Blocked (External)

| Item | Blocker |
|------|---------|
| ESP32 (Xtensa) build | ESP-IDF v5.5.1 bootloader linker regression |
| Bravo board recovery | Needs UART adapter (USB-JTAG wedged) |

## Total Effort

| Phase | Items | Time |
|-------|-------|------|
| R1-R3 | Upload, watchdog, sleep | ~2 hours |
| R4-R5 | Reward loop, discovery | ~3 hours |
| R6 | PyPI publish | 15 min |
| **Total** | **6 items** | **~5.5 hours** |
