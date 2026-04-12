# Nodes of Interest: Ternary Number System Structure

## Node 1: Semantic Model Must Be Independent From Binary Encoding

The ternary machine should define its values and arithmetic independently of how bytes store them.

Why it matters: if semantics and encoding are fused, later optimizations will silently change the model.

## Node 2: Balanced Ternary Fits Reflex OS Better Than Unbalanced Ternary

Balanced ternary gives `-1`, `0`, and `+1` as first-class states.

Why it matters: this matches compare, branch, and control-state semantics naturally.

## Node 3: Storage Density And Runtime Simplicity Are In Tension

Maximum density improves storage efficiency but makes indexing, decode, and debugging harder.

Tension: denser packing versus simpler VM implementation.

## Node 4: Execution Representation Does Not Need To Match Storage Representation

The VM can use a normalized internal form while loading and saving a packed external form.

Why it matters: this may resolve the density versus simplicity tradeoff.

## Node 5: Word Size Should Be Chosen Around the Host Boundary

The ESP32-C6 is a 32-bit MCU, so the VM word should fit comfortably into host-side helper logic when needed.

Dependency: word size affects ALU design, register shape, instruction format, and debugging tools.

## Node 6: 9 Trits Is a Natural Human-Facing Unit

Nine trits is large enough to be meaningful and small enough to reason about.

Why it matters: this can serve as a subunit similar to a byte-like or tryte-like chunk.

## Node 7: 18 Trits Is a Plausible VM Word Size

Eighteen balanced trits stay within a practical 32-bit signed host range and pair cleanly with a 9-trit subunit.

Why it matters: this creates a usable word without forcing 64-bit host machinery everywhere.

## Node 8: Registers Need Clarity More Than Cleverness

If registers are hard to inspect, the shell and debugger will become painful.

Tension: mathematically elegant representation versus debug-friendly representation.

## Node 9: Reserved Invalid States Can Be Useful

If binary packing uses 2-bit slots per trit, one state remains unused.

Why it matters: invalid encoding can help catch corruption, malformed bytecode, or decode bugs.

## Node 10: Instruction And Data Formats May Need Different Views But Shared Units

Opcode streams may have different layout needs than arithmetic words.

Dependency: if the VM uses 18-trit data words, instruction encoding should still reuse compatible packing and conversion helpers.
