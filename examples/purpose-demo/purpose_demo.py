"""Demonstrate the purpose system: set, observe effects, clear."""
import sys, time
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdk", "python"))
from reflex import ReflexNode

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1101"

with ReflexNode(port) as node:
    print(f"Connected to {port}")
    print(f"Current purpose: {node.purpose_get()}")
    print()

    # Set purpose to "led" — activates the agency holon
    print("Setting purpose to 'led'...")
    node.purpose_set("led")
    time.sleep(2)
    print(f"Purpose: {node.purpose_get()}")
    print()

    # Clear purpose — deactivates holon
    print("Clearing purpose...")
    node.purpose_clear()
    time.sleep(2)
    print(f"Purpose: {node.purpose_get()}")
    print()

    # Save learned state
    print("Saving snapshot...")
    print(node.snapshot_save())
    print()

    # Restore original
    node.purpose_set("liberated")
    print(f"Restored: {node.purpose_get()}")
