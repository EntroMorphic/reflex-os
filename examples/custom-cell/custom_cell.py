"""Register a custom cell and observe its state in the fabric."""
import sys, time
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdk", "python"))
from reflex import ReflexNode

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1101"

with ReflexNode(port) as node:
    print(f"Connected to {port}")

    # Look up an existing cell
    print("\nLooking up agency.led.intent...")
    out = node.goonies_find("agency.led.intent")
    print(f"  {out}")

    # Control LED via fabric (set purpose to activate agency holon)
    print("\nActivating 'led' purpose...")
    node.purpose_set("led")
    time.sleep(2)

    # Turn LED on via direct command
    node.led_on()
    print("LED on (via substrate route)")
    time.sleep(2)

    node.led_off()
    print("LED off")

    # Check heartbeat (LP core status)
    print(f"\nHeartbeat: {node.heartbeat()}")

    # Restore
    node.purpose_set("liberated")
    print(f"\nRestored purpose: {node.purpose_get()}")
