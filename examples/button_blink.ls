// LoomScript Example: Physical Entanglement
// Weaves a physical button input to a LED intent.

weave perception.gpio_in.ctrl -> agency.led.intent
weave sys.origin -> sys.swarm.posture
