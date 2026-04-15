# Raw Thoughts: Inter-System Tapestry (ESP-NOW)

## Stream of Consciousness
The user wants Node A to "bleed" into Node B. Not just sending messages, but state propagation. 
In local GOOSE, a `Route` is `Source -> Orientation -> Sink`.
For Inter-System, the `Route` crosses the air.
`NodeA:CellX -> [ESP-NOW] -> NodeB:CellY`.

How do we represent this in the coordinate map?
Existing coordinates are 9-trits: `(Field, Region, Cell)`.
If I change a cell at `(1, 1, 1)` on Node A, does it automatically mean `(1, 1, 1)` on Node B?
Or do we need a "World" coordinate?
Maybe the `Field` component encodes the "Discovery" of other nodes.
Wait, ESP-NOW uses MAC addresses. That's very binary. 
In GOOSE, everything should be geometric. 
Maybe nodes are "Regions" in a "Meta-Field".
Field: `Mothership` (0).
Regions in `Mothership`: `LocalNode` (0), `Peer1` (+1), `Peer2` (-1).
But that only allows 2 peers if we stay in 1 trit.
The 9-trit system has 3 trits for Field. We can have many nodes.

Latency is the killer. If I route a local button to a remote LED, I expect it to feel "fast enough" but not instant.
The `Loom` lives in RTC RAM. If I'm syncing cells, I'm writing to RTC RAM on the remote side.

Avoid binary packets:
Instead of `struct packet { char cmd; int val; }`, we should send "Geometric Updates".
"Coordinate (F,R,C) now has state S."
Actually, if it's a `Route`, then Node A *pushes* to Node B.
Node B is just a `Sink`.

What if we treat the "Air" as a `Field` itself?
The `Atmosphere Field`.
Nodes "project" their regions into the Atmosphere.
Other nodes "subscribe" or "weave" those projections into their local Loom.

Security/Red-Team:
If anyone can "bleed" into my Loom, they can control my hardware.
I need "Authenticated Geometry".
Or maybe certain Regions are "Private" and others are "Public".
The `goose_cell_type_t` has `GOOSE_CELL_SYSTEM_ONLY`.

How to implement the radio pulse?
It shouldn't be in the 100Hz `REACTIVE` loop because radio is slow and power-hungry.
Maybe `GOOSE_RHYTHM_DISTRIBUTED` at 5Hz or 10Hz.
Or event-driven? `GOOSE_COUPLING_ASYNC`.
When `Cell.state` changes locally, *if* it is a source for a "Remote Route", trigger ESP-NOW send.

What about reliability?
If a packet is lost, the states are out of sync.
The `Supervisor` (Immune System) should handle this.
"Posture Check": Node A and Node B occasionally exchange Loom hashes?
No, too binary.
Maybe "State Pressure". Node A keeps asserting the state until Node B acknowledges?
Wait, "acknowledgment" is also a state change in the reverse direction.
`NodeA:CellX -> NodeB:CellY` (Forward Route)
`NodeB:CellY_Ack -> NodeA:CellX_Pressure` (Back Route)

## Questions Arising
1. How do we map MAC addresses to Geometric Coordinates?
2. Do we allow many-to-many propagation or just point-to-point?
3. How does "Discovery" work in a geometric sense? (Nodes appearing in the field).
4. What happens when a node goes out of range? (Geometric Decay?)

## First Instincts
- Reserve `Field` value `+1` (or a specific tryte range) for "The Shared Sky".
- Use `ESP-NOW` as the "Physics" of the Sky.
- Implement a `GOOSE_COUPLING_RADIO` route type.
- A `Radio Bridge` fragment that handles the ESP-NOW overhead but presents as local cells.
