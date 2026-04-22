#!/usr/bin/env python3
"""
Loom Viewer — Real-time substrate visualization for Reflex OS via Rerun.io.

Connects to a Reflex OS board over serial, enables telemetry streaming,
and renders the GOOSE substrate as a live topology in Rerun.

Usage:
    python tools/loom_viewer.py /dev/cu.usbmodem1101
    python tools/loom_viewer.py /dev/cu.usbmodem1101 --spawn

Architecture:
    SerialReader(Thread) → queue.Queue → MainThread (parse + rr.log)

The firmware pushes #T:-prefixed telemetry lines as state changes occur.
The viewer never polls the board. Observation does not interfere with
the process being observed.
"""

import sys
import time
import queue
import threading
import argparse

try:
    import serial
except ImportError:
    sys.exit("Missing dependency: pip install pyserial")

try:
    import rerun as rr
except ImportError:
    sys.exit("Missing dependency: pip install rerun-sdk")


# --- Cell type names (match goose_cell_type_t enum order) ---
CELL_TYPES = [
    "virtual", "hw_in", "hw_out", "intent", "system",
    "pinned", "proxy", "neuron", "need", "purpose",
]

# Cell type → RGBA color
CELL_COLORS = {
    "virtual":  [180, 180, 180, 255],  # gray
    "hw_in":    [100, 149, 237, 255],   # cornflower blue
    "hw_out":   [30,  144, 255, 255],   # dodger blue
    "intent":   [50,  205, 50,  255],   # lime green
    "system":   [255, 165, 0,   255],   # orange
    "pinned":   [255, 140, 0,   255],   # dark orange
    "proxy":    [186, 85,  211, 255],   # medium orchid
    "neuron":   [148, 103, 189, 255],   # purple
    "need":     [255, 127, 80,  255],   # coral
    "purpose":  [255, 215, 0,   255],   # gold
}

# Coupling type → RGBA color
COUPLING_COLORS = {
    0: [160, 160, 160, 255],  # hardware — gray
    1: [100, 100, 100, 255],  # software — dark gray
    2: [200, 200, 100, 255],  # async — yellow-gray
    3: [220, 50,  50,  255],  # radio — red
    4: [50,  180, 220, 255],  # dma — cyan
    5: [50,  220, 100, 255],  # silicon — green
}

DEFAULT_COLOR = [128, 128, 128, 255]


class LoomState:
    """Tracks the current substrate topology for graph rebuilds."""

    def __init__(self):
        self.cells = {}       # name → {"state": int, "type": int}
        self.routes = []      # list of {"src": str, "sink": str, "coupling": int}
        self.dirty = True     # graph needs rebuild
        self.seq = 0          # monotonic sequence for Rerun timeline

    def tick(self):
        self.seq += 1
        return self.seq

    def upsert_cell(self, name, state, cell_type):
        prev = self.cells.get(name)
        if prev and prev["state"] == state and prev["type"] == cell_type:
            return  # no change
        self.cells[name] = {"state": state, "type": cell_type}
        self.dirty = True

    def remove_cell(self, name):
        if name in self.cells:
            del self.cells[name]
            self.routes = [r for r in self.routes if r["src"] != name and r["sink"] != name]
            self.dirty = True

    def add_route(self, src, sink, coupling=1):
        for r in self.routes:
            if r["src"] == src and r["sink"] == sink:
                return  # already exists
        self.routes.append({"src": src, "sink": sink, "coupling": coupling})
        self.dirty = True

    def log_graph(self):
        """Rebuild and log the full topology to Rerun."""
        if not self.cells:
            return

        node_ids = list(self.cells.keys())
        colors = []
        labels = []
        for name in node_ids:
            cell = self.cells[name]
            type_name = CELL_TYPES[cell["type"]] if 0 <= cell["type"] < len(CELL_TYPES) else "virtual"
            colors.append(CELL_COLORS.get(type_name, DEFAULT_COLOR))
            state_str = {-1: "-", 0: "0", 1: "+"}.get(cell["state"], "?")
            labels.append(f"{name} [{state_str}]")

        rr.log("reflex/topology", rr.GraphNodes(
            node_ids=node_ids,
            colors=colors,
            labels=labels,
        ))

        if self.routes:
            edge_srcs = []
            edge_sinks = []
            edge_colors = []
            for route in self.routes:
                if route["src"] in self.cells and route["sink"] in self.cells:
                    edge_srcs.append(route["src"])
                    edge_sinks.append(route["sink"])
                    edge_colors.append(COUPLING_COLORS.get(route["coupling"], DEFAULT_COLOR))

            if edge_srcs:
                rr.log("reflex/topology", rr.GraphEdges(
                    edges=list(zip(edge_srcs, edge_sinks)),
                    colors=edge_colors,
                    graph_type=rr.GraphType.Directed,
                ))

        self.dirty = False


def parse_initial_inventory(text, state):
    """Parse goonies ls output to seed the initial topology."""
    for line in text.strip().split("\n"):
        line = line.strip()
        if not line or line.startswith("Name") or line.startswith("-"):
            continue
        # Format: "%-20s | (%2d,%2d,%2d)    | %5d | %d"
        parts = line.split("|")
        if len(parts) >= 4:
            name = parts[0].strip()
            try:
                state_val = int(parts[2].strip())
                type_val = int(parts[3].strip())
            except (ValueError, IndexError):
                continue
            state.upsert_cell(name, state_val, type_val)


def serial_reader(ser, q, stop_event):
    """Background thread: reads serial lines, routes #T: to queue."""
    buf = b""
    while not stop_event.is_set():
        try:
            chunk = ser.read(ser.in_waiting or 1)
        except Exception:
            break
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            if text.startswith("#T:"):
                q.put(text)
            else:
                print(text, flush=True)


def send_cmd(ser, cmd, timeout=2.0):
    """Send a shell command and collect response until prompt."""
    ser.read(ser.in_waiting)
    ser.write((cmd + "\n").encode())
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            if b"reflex> " in buf:
                break
        else:
            time.sleep(0.02)
    return buf.decode("utf-8", errors="replace")


def process_telemetry(line, state):
    """Parse a #T: line and log to Rerun. Returns True if graph is dirty."""
    seq = state.tick()
    rr.set_time_sequence("substrate", seq)

    payload = line[3:]  # strip "#T:"
    if not payload:
        return False

    tag = payload[0]
    fields = payload[2:].split(",") if len(payload) > 2 else []

    try:
        if tag == "C" and len(fields) >= 3:
            name, s, t = fields[0], int(fields[1]), int(fields[2])
            state.upsert_cell(name, s, t)
            rr.log("reflex/scalars/cells/" + name, rr.Scalar(float(s)))

        elif tag == "R" and len(fields) >= 4:
            src, sink, s, coupling = fields[0], fields[1], int(fields[2]), int(fields[3])
            state.add_route(src, sink, coupling)
            rr.log("reflex/scalars/routes/" + src + ">" + sink, rr.Scalar(float(s)))

        elif tag == "H" and len(fields) >= 3:
            name, counter, learned = fields[0], int(fields[1]), int(fields[2])
            rr.log("reflex/scalars/hebbian/" + name, rr.Scalar(float(counter)))
            if learned != 0:
                rr.log("reflex/events/learn", rr.TextLog(f"COMMIT {name} orient={learned}"))

        elif tag == "W" and len(fields) >= 3:
            name, src, sink = fields[0], fields[1], fields[2]
            state.add_route(src, sink)
            rr.log("reflex/events/lifecycle", rr.TextLog(f"WEAVE {name}: {src} -> {sink}"))

        elif tag == "A" and len(fields) >= 2:
            name, t = fields[0], int(fields[1])
            state.upsert_cell(name, 0, t)
            rr.log("reflex/events/lifecycle", rr.TextLog(f"ALLOC {name} type={t}"))

        elif tag == "E" and len(fields) >= 1:
            name = fields[0]
            state.remove_cell(name)
            rr.log("reflex/events/lifecycle", rr.TextLog(f"EVICT {name}"))

        elif tag == "B" and len(fields) >= 1:
            balance = int(fields[0])
            rr.log("reflex/scalars/balance", rr.Scalar(float(balance)))

        elif tag == "M" and len(fields) >= 2:
            op, s = fields[0], fields[1]
            rr.log("reflex/mesh/arcs", rr.TextLog(f"{op} state={s}"))

        elif tag == "P" and len(fields) >= 1:
            purpose = fields[0]
            rr.log("reflex/events/lifecycle", rr.TextLog(f"PURPOSE {purpose}"))

        elif tag == "V" and len(fields) >= 2:
            reward, pain = int(fields[0]), int(fields[1])
            rr.log("reflex/scalars/reward", rr.Scalar(float(reward)))
            rr.log("reflex/scalars/pain", rr.Scalar(float(pain)))

    except (ValueError, IndexError):
        pass  # skip malformed telemetry lines

    return state.dirty


def main():
    parser = argparse.ArgumentParser(description="Loom Viewer: live Reflex OS substrate visualization")
    parser.add_argument("port", help="Serial port (e.g., /dev/cu.usbmodem1101)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--spawn", action="store_true", help="Spawn a local Rerun viewer (default)")
    parser.add_argument("--connect", metavar="ADDR", help="Connect to a running Rerun viewer via gRPC")
    args = parser.parse_args()

    # Initialize Rerun
    rr.init("reflex_loom_viewer", spawn=not args.connect)
    if args.connect:
        rr.connect_grpc(args.connect)

    # Open serial
    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    time.sleep(0.3)
    ser.read(ser.in_waiting)

    state = LoomState()

    # Ensure clean state — disable any pre-existing telemetry
    send_cmd(ser, "telemetry off", timeout=2.0)

    # Seed initial topology
    print("[loom_viewer] Requesting initial cell inventory...")
    inventory = send_cmd(ser, "goonies ls", timeout=5.0)
    # Filter out any stray #T: lines from the response
    clean = "\n".join(l for l in inventory.split("\n") if not l.strip().startswith("#T:"))
    parse_initial_inventory(clean, state)
    print(f"[loom_viewer] Seeded {len(state.cells)} cells from goonies ls")

    # Enable telemetry
    send_cmd(ser, "telemetry on", timeout=2.0)
    print("[loom_viewer] Telemetry enabled. Streaming...")

    # Log initial graph
    rr.set_time_sequence("substrate", state.tick())
    state.log_graph()

    # Start serial reader thread
    q = queue.Queue(maxsize=4096)
    stop = threading.Event()
    reader = threading.Thread(target=serial_reader, args=(ser, q, stop), daemon=True)
    reader.start()

    try:
        while True:
            try:
                line = q.get(timeout=0.05)
                dirty = process_telemetry(line, state)
                # Batch: drain the queue before rebuilding the graph
                while not q.empty():
                    try:
                        line = q.get_nowait()
                        dirty = process_telemetry(line, state) or dirty
                    except queue.Empty:
                        break
                if dirty:
                    state.log_graph()
            except queue.Empty:
                pass
    except KeyboardInterrupt:
        print("\n[loom_viewer] Shutting down...")
    finally:
        stop.set()
        try:
            send_cmd(ser, "telemetry off", timeout=1.0)
        except Exception:
            pass
        ser.close()
        print("[loom_viewer] Done.")


if __name__ == "__main__":
    main()
