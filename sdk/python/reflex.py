"""
Reflex OS Python SDK — programmatic interface to Reflex nodes over serial.

Usage:
    from reflex import ReflexNode

    node = ReflexNode("/dev/cu.usbmodem1101")
    node.purpose_set("photography")
    print(node.purpose_get())
    node.snapshot_save()
    print(node.temp())
    node.reboot()
"""

import serial
import time
import re
from typing import Optional, Dict


class ReflexNode:
    """Interface to a single Reflex OS node over serial."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 2.0):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        time.sleep(0.3)
        self.ser.read(self.ser.in_waiting)  # flush boot output

    def close(self):
        self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def cmd(self, command: str, wait: float = 1.0) -> str:
        """Send a shell command and return the response."""
        self.ser.read(self.ser.in_waiting)  # flush
        self.ser.write((command + "\n").encode())
        time.sleep(wait)
        data = self.ser.read(self.ser.in_waiting or 1)
        return data.decode("utf-8", errors="replace")

    # --- Purpose ---

    def purpose_set(self, name: str) -> str:
        out = self.cmd(f"purpose set {name}")
        if "active" not in out:
            raise RuntimeError(f"purpose set failed: {out.strip()}")
        return out.strip()

    def purpose_get(self) -> Optional[str]:
        out = self.cmd("purpose get")
        m = re.search(r'name="([^"]+)"', out)
        return m.group(1) if m else None

    def purpose_clear(self) -> str:
        return self.cmd("purpose clear").strip()

    # --- Snapshot ---

    def snapshot_save(self) -> str:
        return self.cmd("snapshot save", wait=2.0).strip()

    def snapshot_load(self) -> str:
        return self.cmd("snapshot load", wait=2.0).strip()

    def snapshot_clear(self) -> str:
        return self.cmd("snapshot clear").strip()

    # --- Sensors ---

    def temp(self) -> Optional[float]:
        """Read temperature in Celsius."""
        out = self.cmd("temp")
        m = re.search(r"([\d.]+)\s*[°C]", out)
        if not m:
            m = re.search(r"celsius=([\d.]+)", out)
        return float(m.group(1)) if m else None

    # --- LED ---

    def led_on(self):
        self.cmd("led on")

    def led_off(self):
        self.cmd("led off")

    def led_status(self) -> str:
        return self.cmd("led status").strip()

    # --- System ---

    def reboot(self):
        """Trigger software reboot. Connection will drop."""
        self.ser.write(b"reboot\n")
        time.sleep(0.5)

    def sleep(self, seconds: int):
        """Enter deep sleep for N seconds."""
        self.ser.write(f"sleep {seconds}\n".encode())
        time.sleep(0.5)

    def services(self) -> str:
        return self.cmd("services").strip()

    def status(self) -> str:
        return self.cmd("status").strip()

    # --- GOOSE Fabric ---

    def goonies_ls(self) -> str:
        return self.cmd("goonies ls", wait=2.0).strip()

    def goonies_find(self, name: str) -> str:
        return self.cmd(f"goonies find {name}").strip()

    # --- Mesh ---

    def mesh_status(self) -> str:
        return self.cmd("mesh status").strip()

    def mesh_ping(self) -> str:
        return self.cmd("mesh ping", wait=3.0).strip()

    # --- VM ---

    def vm_info(self) -> str:
        return self.cmd("vm info").strip()

    # --- Config ---

    def config_get(self, key: str) -> Optional[str]:
        out = self.cmd(f"config get {key}")
        m = re.search(r"=(.+)", out)
        return m.group(1).strip() if m else None

    def config_set(self, key: str, value: str) -> str:
        return self.cmd(f"config set {key} {value}").strip()

    # --- Heartbeat ---

    def heartbeat(self) -> str:
        return self.cmd("heartbeat").strip()

    # --- Raw ---

    def raw(self, command: str, wait: float = 1.0) -> str:
        """Send any raw command string."""
        return self.cmd(command, wait=wait)


def discover(timeout: float = 2.0) -> list:
    """Auto-discover Reflex OS nodes on available serial ports."""
    import serial.tools.list_ports

    nodes = []
    for port in serial.tools.list_ports.comports():
        if "usbmodem" in port.device or "ttyACM" in port.device:
            try:
                node = ReflexNode(port.device, timeout=timeout)
                out = node.cmd("purpose get", wait=1.0)
                if "purpose" in out.lower() or "reflex>" in out:
                    nodes.append({"port": port.device, "node": node})
                else:
                    node.close()
            except Exception:
                pass
    return nodes


if __name__ == "__main__":
    import sys

    port = sys.argv[1] if len(sys.argv) > 1 else None
    if not port:
        print("Usage: python reflex.py <port>")
        print("       python reflex.py /dev/cu.usbmodem1101")
        sys.exit(1)

    with ReflexNode(port) as node:
        print(f"Connected to {port}")
        print(f"Purpose: {node.purpose_get()}")
        print(f"Temperature: {node.temp()}°C")
        print(f"Services: {node.services()}")
