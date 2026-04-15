# Raw Thoughts: Phase 23 (Atmospheric Discovery)

## Stream of Consciousness
We have a local naming service, but it's a "walled garden." If I have two XIAOs, Node A doesn't know Node B has a `agency.relay.fan`. To melt faces, Node A should just be able to call `goonies_resolve("remote.node_b.agency.relay.fan")` and have it *just work*. But how? We can't broadcast 9,531 shadow nodes per board—that would saturate the 2.4GHz band instantly. Discovery must be reactive.

When a resolution fails locally, we need to broadcast a "Geometric Query." 
Node A: "Who has `agency.relay.fan`?"
Node B: "I do! My MAC is XX:XX:XX. It's at my coord (1,5,2)."

Then Node A creates a **Ghost Cell**. This cell lives in Node A's Loom but its "Physical Agency" is a Radio Arc to Node B. If Node A sets its Ghost Cell to +1, it sends an ESP-NOW packet to Node B, and Node B's Supervisor applies the state to the real relay.

## Friction Points
- **Latency:** Discovery involves a round-trip radio flight. A real-time 1000Hz field will glitch if it waits for discovery. We need "Placeholder Cells" that stay at Neutral (0) until the remote resource responds.
- **Identity:** How do we handle "Node B"? Is it a MAC address? A human name? We need a `sys.identity.name` cell on every board.
- **Security (Red-Team Bait):** What if Node C lies and says it has the relay? What if Node C sends 10,000 discovery responses to crash Node A's Ghost Registry? We need **Aura-signed Discovery**.

## First Instincts
- New Arc Packet Type: `GOOSE_ARC_QUERY` and `GOOSE_ARC_ADVERTISE`.
- The `goonies_resolve` function becomes asynchronous or returns a "Pending" status.
- We need a `sys.net.discovery_timeout` setting.
