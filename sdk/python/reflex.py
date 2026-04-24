"""
Reflex OS Python SDK — programmatic interface to Reflex nodes over serial.

Usage:
    from reflex import ReflexNode, discover

    node = ReflexNode("/dev/cu.usbmodem1101")
    node.purpose_set("photography")
    print(node.purpose_get())
    node.snapshot_save()
    print(node.temp())
    node.reboot()
"""

import serial
import serial.tools.list_ports
import time
import re
import threading
from typing import Optional, List


PROMPT = "reflex> "


def _sanitize(value: str) -> str:
    """Strip control characters that could inject commands."""
    return re.sub(r"[\x00-\x1f\x7f]", "", value)


class ReflexNode:
    """Interface to a single Reflex OS node over serial."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 2.0):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self._lock = threading.Lock()
        time.sleep(0.3)
        self.ser.read(self.ser.in_waiting)

    def close(self):
        self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def cmd(self, command: str, timeout: float = 3.0) -> str:
        """Send a shell command and return the response.

        Reads until the prompt appears or timeout is reached.
        Thread-safe via internal lock.
        """
        with self._lock:
            self.ser.read(self.ser.in_waiting)
            self.ser.write((command + "\n").encode())
            buf = b""
            deadline = time.time() + timeout
            while time.time() < deadline:
                chunk = self.ser.read(self.ser.in_waiting or 1)
                if chunk:
                    buf += chunk
                    if PROMPT.encode() in buf:
                        break
                else:
                    time.sleep(0.05)
            text = buf.decode("utf-8", errors="replace")
            # Strip the echo of the command and trailing prompt
            lines = text.split("\n")
            output = []
            for line in lines:
                stripped = line.strip()
                if stripped == command.strip():
                    continue
                if stripped == PROMPT.strip():
                    continue
                if stripped.endswith(PROMPT.strip()):
                    stripped = stripped[: -len(PROMPT.strip())].strip()
                if stripped:
                    output.append(stripped)
            return "\n".join(output)

    # --- Purpose ---

    def purpose_set(self, name: str) -> str:
        name = _sanitize(name)
        if not name:
            raise ValueError("purpose name must not be empty")
        out = self.cmd(f"purpose set {name}")
        if "active" not in out:
            raise RuntimeError(f"purpose set failed: {out}")
        return out

    def purpose_get(self) -> Optional[str]:
        out = self.cmd("purpose get")
        m = re.search(r'name="([^"]+)"', out)
        return m.group(1) if m else None

    def purpose_clear(self) -> str:
        return self.cmd("purpose clear")

    # --- Snapshot ---

    def snapshot_save(self) -> str:
        return self.cmd("snapshot save", timeout=5.0)

    def snapshot_load(self) -> str:
        return self.cmd("snapshot load", timeout=5.0)

    def snapshot_clear(self) -> str:
        return self.cmd("snapshot clear")

    # --- Sensors ---

    def temp(self) -> Optional[float]:
        """Read temperature in Celsius."""
        out = self.cmd("temp")
        m = re.search(r"([\d.]+)", out)
        return float(m.group(1)) if m else None

    # --- LED ---

    def led_on(self):
        self.cmd("led on")

    def led_off(self):
        self.cmd("led off")

    def led_status(self) -> str:
        return self.cmd("led status")

    # --- System ---

    def reboot(self):
        """Trigger software reboot. Connection will drop."""
        with self._lock:
            self.ser.read(self.ser.in_waiting)
            self.ser.write(b"reboot\n")
            time.sleep(0.5)

    def sleep(self, seconds: int):
        """Enter deep sleep for N seconds."""
        with self._lock:
            self.ser.read(self.ser.in_waiting)
            self.ser.write(f"sleep {int(seconds)}\n".encode())
            time.sleep(0.5)

    def services(self) -> str:
        return self.cmd("services")

    def status(self) -> str:
        return self.cmd("status")

    # --- GOOSE Fabric ---

    def goonies_ls(self) -> str:
        return self.cmd("goonies ls", timeout=5.0)

    def goonies_find(self, name: str) -> str:
        name = _sanitize(name)
        return self.cmd(f"goonies find {name}")

    def goonies_read(self, name: str) -> str:
        name = _sanitize(name)
        return self.cmd(f"goonies read {name}")

    # --- Vitals ---

    def vitals(self) -> str:
        return self.cmd("vitals")

    def vitals_override(self, vital: str, state: int) -> str:
        vital = _sanitize(vital)
        if state not in (-1, 0, 1):
            raise ValueError("state must be -1, 0, or 1")
        return self.cmd(f"vitals override {vital} {state}")

    def vitals_clear(self) -> str:
        return self.cmd("vitals clear")

    # --- Telemetry ---

    def telemetry_on(self) -> str:
        return self.cmd("telemetry on")

    def telemetry_off(self) -> str:
        return self.cmd("telemetry off")

    # --- Mesh ---

    def mesh_status(self) -> str:
        return self.cmd("mesh status")

    def mesh_ping(self) -> str:
        return self.cmd("mesh ping", timeout=5.0)

    # --- VM ---

    def vm_info(self) -> str:
        return self.cmd("vm info")

    # --- Config ---

    def config_get(self, key: str) -> Optional[str]:
        key = _sanitize(key)
        out = self.cmd(f"config get {key}")
        m = re.search(r"=(.+)", out)
        return m.group(1).strip() if m else None

    def config_set(self, key: str, value: str) -> str:
        key = _sanitize(key)
        value = _sanitize(value)
        return self.cmd(f"config set {key} {value}")

    # --- Heartbeat ---

    def heartbeat(self) -> str:
        return self.cmd("heartbeat")

    # --- Raw ---

    def raw(self, command: str, timeout: float = 3.0) -> str:
        """Send any raw command string (not sanitized)."""
        return self.cmd(command, timeout=timeout)


def discover() -> List[dict]:
    """Auto-discover Reflex OS nodes on available serial ports.

    Returns list of {'port': str, 'node': ReflexNode} dicts.
    Caller is responsible for closing returned nodes.
    """
    nodes = []
    for port in serial.tools.list_ports.comports():
        if any(k in port.device for k in ("usbmodem", "ttyACM", "usbserial", "ttyUSB")):
            node = None
            try:
                node = ReflexNode(port.device, timeout=2.0)
                out = node.cmd("purpose get", timeout=2.0)
                if "purpose" in out.lower() or "reflex" in out.lower():
                    nodes.append({"port": port.device, "node": node})
                    node = None
            except Exception:
                pass
            finally:
                if node is not None:
                    node.close()
    return nodes


def main():
    """CLI entry point."""
    import sys

    port = sys.argv[1] if len(sys.argv) > 1 else None
    if not port or port in ("--help", "-h"):
        print("Usage: reflex-cli <port>")
        print("       reflex-cli /dev/cu.usbmodem1101")
        print()
        print("Or auto-discover:")
        print("       reflex-cli --discover")
        sys.exit(0 if port in ("--help", "-h") else 1)

    if port == "--discover":
        nodes = discover()
        for n in nodes:
            purpose = n["node"].purpose_get() or "(none)"
            print(f"  {n['port']}: purpose={purpose}")
            n["node"].close()
        print(f"{len(nodes)} node(s) found")
        return

    with ReflexNode(port) as node:
        print(f"Connected to {port}")
        print(f"  Purpose: {node.purpose_get()}")
        print(f"  Temp: {node.temp()}")
        print(f"  Services: {node.services()}")


if __name__ == "__main__":
    main()
