"""Read temperature from all mesh nodes and display aggregate."""
import sys, time
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdk", "python"))
from reflex import discover

print("Mesh Temperature Monitor")
print("========================\n")
print("Discovering nodes...")

nodes = discover()
if not nodes:
    print("No nodes found.")
    sys.exit(1)

print(f"Monitoring {len(nodes)} node(s). Ctrl+C to stop.\n")

try:
    while True:
        temps = []
        for n in nodes:
            t = n["node"].temp()
            if t is not None:
                temps.append((n["port"], t))

        if temps:
            avg = sum(t for _, t in temps) / len(temps)
            print(f"[{time.strftime('%H:%M:%S')}]", end="")
            for port, t in temps:
                short = port.split("/")[-1]
                print(f"  {short}={t:.1f}°C", end="")
            print(f"  avg={avg:.1f}°C")

        time.sleep(5)
except KeyboardInterrupt:
    print("\nStopped.")
finally:
    for n in nodes:
        n["node"].close()
