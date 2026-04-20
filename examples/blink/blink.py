"""Blink the onboard LED via Python SDK."""
import sys, time
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdk", "python"))
from reflex import ReflexNode

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1101"

with ReflexNode(port) as node:
    print(f"Connected to {port}")
    for i in range(10):
        node.led_on()
        print("LED on")
        time.sleep(0.5)
        node.led_off()
        print("LED off")
        time.sleep(0.5)
    print("Done.")
