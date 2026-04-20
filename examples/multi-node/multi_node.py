"""Discover all nodes and coordinate their purposes."""
import sys
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdk", "python"))
from reflex import discover

print("Discovering Reflex nodes...")
nodes = discover()

if not nodes:
    print("No nodes found. Check USB connections.")
    sys.exit(1)

print(f"Found {len(nodes)} node(s):\n")

for n in nodes:
    purpose = n["node"].purpose_get() or "(none)"
    print(f"  {n['port']}: purpose={purpose}")

# Set all nodes to the same purpose
print("\nSetting all nodes to purpose 'swarm'...")
for n in nodes:
    n["node"].purpose_set("swarm")
    print(f"  {n['port']}: set to swarm")

# Read temperature from all
print("\nTemperatures:")
for n in nodes:
    temp = n["node"].temp()
    print(f"  {n['port']}: {temp}°C")

# Clean up
for n in nodes:
    n["node"].purpose_set("liberated")
    n["node"].close()

print("\nDone. All nodes restored to 'liberated'.")
