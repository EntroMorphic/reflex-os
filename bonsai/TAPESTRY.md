# GOOSE Tapestry: The Legit Fabric

## Thesis
In a GOOSE-native operating system, **messaging is an illusion.** What we call "communication" is actually the **routed propagation of state** across the system fabric.

The "Tapestry" is the formal realization of this propagation.

## From Queues to Routes

### 1. The Legacy (Binary) Model
- **Mechanism:** Enqueue Message -> Context Switch -> Dequeue Message -> Execute Action.
- **Flaw:** It introduces temporal jitter, requires buffer management, and hides the "intent" inside a transient packet.

### 2. The GOOSE (Geometric) Model
- **Mechanism:** Source Cell -> Persistent Route -> Sink Cell.
- **Truth:** The "Message" is simply the state currently inhabiting the Route. Execution happens when the Route is "Processed" (either by a Rhythm Field or a Hardware Transition).

## The Global Fabric (The Loom)
The **Global Fabric** is a dedicated GOOSE Field that serves as the "Common Ground" for all system services.

```goose
field FABRIC {
    region TAPESTRY {
        cell signal_0; // Generic interaction lanes
        cell signal_1;
        cell status_led; 
        cell alert_level;
    }
}
```

## Tapestry Rules
1. **No Ownership of Data:** Cells in the Tapestry are "Public." Any region can route *from* them (Perception) or *to* them (Agency).
2. **Persistence:** A state change in the Tapestry persists until another transition overwrites it. 
3. **Composition:** Multiple routes can converge on a single Tapestry cell, using the **Ternary Product Rule** ($A \times B \times C$) to determine the resultant signal (Inhibition, Reinforcement, or Inversion).

## Advanced Propagation

### 1. Geometric Arcing (Inter-System)
State can "Jump the gap" between nodes using the **Atmosphere Field** (ESP-NOW). 
- **Mechanism:** Source Node A -> Radio Route -> Sink Node B.
- **Truth:** The "Air" is just another field in the Tapestry.

### 2. Geometric Flow (DMA)
For high-bandwidth data, a Route governs the **Authority to Flow**.
- **Mechanism:** Source Buffer -> DMA Route -> Sink Peripheral.
- **Truth:** The Tapestry doesn't touch the data; it routes the *permission* for the data to move itself.

## Success Criteria
- The system can perform "Button -> LED" interaction without a single `reflex_message_t` being sent.
- The "Message Bus" task can be retired in favor of a "Fabric Processing" task.
