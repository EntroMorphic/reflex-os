# Red-Team Plan: Phase 4 (Regional Geometry)

## Objective

Falsify the "Regional Geometry" claim by identifying edge cases where the model collapses into binary conflict, creates undefined states, or fails to maintain ontological integrity.

## Tensions to Test

### 1. Route Collision (Hardware vs Software)
**Scenario:** If a software task is actively asserting a pin level (Experiment 2) and a hardware route is established for the same pin (Experiment 4), who wins?
**GOOSE Expectation:** The hardware route should be dominant as it is a structural change to the Region's geometry.
**Failure Signal:** The LED flickers or shows an inconsistent "mixed" state, or software continues to succeed in changing the physical state despite the route.

### 2. The "Neutral" Void
**Scenario:** When a route is "Detached" (orientation 0), does the hardware region return to a known stable state or a floating/undefined state?
**GOOSE Expectation:** Detaching a route should restore the "Neutral" identity of the cell (returning it to GPIO/Software control).
**Failure Signal:** The LED remains in its last hardware-driven state (latched) or enters a high-impedance float that is sensitive to noise.

### 3. Inversion Integrity
**Scenario:** Is `-1` (Inverted) a true geometric reflection of the source, or just a logical NOT?
**GOOSE Expectation:** Orientation should be independent of the signal content.
**Failure Signal:** Inversion only works for certain frequencies or signal types, or introduces timing skew not present in the $+1$ orientation.

## Red-Team Results (Phases 1-4)

### RT-4.1: The Collision Test
**Result:** SUCCESS (Falsifier Identified)

When `bonsai exp4 connect` (hardware route) was issued while `bonsai exp2` (software pulse) was running:
1.  The physical LED followed the hardware 1kHz pulse (dimly lit).
2.  The `exp2 status` continued to report `led=off` or `led=on` based on its own internal state.
3.  The `led status` command reported `led=on` (the last written software state), which was **physically false** because the hardware route was dominant.

**Finding: "Shadow Agency"**
Hardware regional routing on the C6 creates a "Shadow Agency" where physical reality is decoupled from the software's register-view. The GPIO Matrix literally disconnects the software output register from the pad.

### RT-4.2: The "Hold" Integrity Test
**Result:** PASS
The hardware route persisted across all shell commands and inactivity. The orientation of the region is a structural property that survives until re-oriented.

### RT-4.3: Boundary Violation
**Result:** NOT TESTED
This requires more invasive MMIO access that may risk board stability. Deferred to Phase 7 (Runtime Substrate).

## Consolidated Findings: Phases 1-4

1.  **Ternary Isomorphism works.** Geometric rules ($A \times B$) produce stable, rhyming behaviors that are more expressive than binary equivalents.
2.  **Neutral (0) is a real gate.** In all compositions, the neutral state correctly inhibited propagation, proving it is not just a null placeholder.
3.  **Regional sovereignty is absolute.** Once a Region is routed (Phase 4), software register writes are physically masked. GOOSE must account for this decoupling in its attention model.
4.  **The "Driver" is a Route.** Every configuration step in the C6 can be mapped to an orientation of a state cluster.
