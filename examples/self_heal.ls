// LoomScript: Autonomous Self-Healing Demo
// Setting sys.need.light to +1 triggers the Supervisor to find a light-capable node.

// We don't weave the LED route here. The OS does it for us.
weave sys.origin -> sys.need.light
