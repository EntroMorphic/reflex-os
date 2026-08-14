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
from typing import Optional, List, NamedTuple


PROMPT = "reflex> "


def _sanitize(value: str) -> str:
    """Strip control characters that could inject commands."""
    return re.sub(r"[\x00-\x1f\x7f]", "", value)


def _token(value: str, field: str) -> str:
    """Sanitize a single-token argument, rejecting embedded whitespace.

    The shell splits on spaces and has no quoting, so a value containing one
    is not a single argument. It used to be silently truncated to the prefix
    (`purpose set my photography` stored "my"); the firmware now refuses it.
    Catching it here gives the caller an error naming the field, instead of a
    device round-trip that comes back as "too many arguments".
    """
    # Whitespace is checked *before* sanitizing: tab is a control character, so
    # stripping first would silently join "a\tb" into "ab" — a quiet
    # transformation of the caller's value, which is the failure this whole
    # helper exists to prevent.
    if any(c.isspace() for c in value):
        raise ValueError(
            f"{field} must not contain whitespace (got {value!r}); "
            "the shell has no quoting, so it cannot carry spaces"
        )
    value = _sanitize(value)
    if not value:
        raise ValueError(f"{field} must not be empty")
    return value


class AccessDenied(PermissionError):
    """Raised when a command is blocked by the session's role."""
    pass


class Outcome(NamedTuple):
    """The ternary result of a command, read from the shell's `#R:` marker.

    `trit` answers one question: did the intended state change occur?

        +1  engaged   it did
         0  latent    nothing was attempted (usage, empty result, no-op)
        -1  withheld  it did not — refused, rejected, or failed

    `reason` qualifies it with a stable token: ok, usage, none, denied,
    invalid, guard, notfound, failed, overflow.

    This exists because the alternative was reconstructing outcomes from
    English. `AccessDenied` used to fire on ``result.startswith("denied:")``,
    which made every human-readable string in the firmware an undocumented
    API — and a binary success flag could not distinguish "I was refused"
    from "I ran and had nothing to report", which is exactly the distinction
    an agent running under ``role="agent"`` needs.
    """
    trit: int
    reason: str

    @property
    def engaged(self) -> bool:
        return self.trit > 0

    @property
    def withheld(self) -> bool:
        return self.trit < 0


_RESULT_RE = re.compile(r"^#R:([+-]?\d+),(\w+)$")


class ReflexNode:
    """Interface to a single Reflex OS node over serial."""

    VALID_ROLES = ("observer", "agent", "operator", "admin")

    def __init__(self, port: str, baud: int = 115200, timeout: float = 2.0,
                 role: Optional[str] = None):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self._lock = threading.Lock()
        #: Outcome of the most recent cmd(), or None on firmware without `#R:`.
        self.last_outcome: Optional[Outcome] = None
        time.sleep(0.3)
        self.ser.read(self.ser.in_waiting)
        if role is not None:
            if role not in self.VALID_ROLES:
                raise ValueError(f"role must be one of {self.VALID_ROLES}")
            self.cmd(f"auth role {role}")

    def close(self):
        self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def cmd_outcome(self, command: str, timeout: float = 3.0):
        """Send a command; return ``(text, Outcome)`` from one locked exchange.

        Prefer this over reading :attr:`last_outcome` after :meth:`cmd` when
        more than one thread shares the node. ``cmd()`` is serialised by the
        internal lock, but ``last_outcome`` is read *after* that lock is
        released, so a concurrent call can overwrite it in between. Here both
        values come back from the same critical section.

        Outcome is None on firmware predating the `#R:` marker.
        """
        with self._lock:
            return self._exchange(command, timeout)

    def cmd(self, command: str, timeout: float = 3.0) -> str:
        """Send a shell command and return the response.

        Reads until the prompt appears or timeout is reached.
        Thread-safe via internal lock.

        The ternary outcome is also stored on :attr:`last_outcome`, which is
        convenient single-threaded but racy across threads — use
        :meth:`cmd_outcome` there.
        """
        text, _ = self.cmd_outcome(command, timeout=timeout)
        return text

    def _exchange(self, command: str, timeout: float):
        """The serial exchange itself. Caller must hold `self._lock`."""
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
        outcome = None
        for line in lines:
            stripped = line.strip()
            if stripped == command.strip():
                continue
            if stripped == PROMPT.strip():
                continue
            if stripped.endswith(PROMPT.strip()):
                stripped = stripped[: -len(PROMPT.strip())].strip()
            m = _RESULT_RE.match(stripped)
            if m:
                # The machine-readable outcome. Consumed here rather than
                # returned, so callers see the same text as before.
                outcome = Outcome(int(m.group(1)), m.group(2))
                continue
            if stripped:
                output.append(stripped)
        result = "\n".join(output)
        self.last_outcome = outcome
        if outcome is not None:
            # Only a role refusal is an access error. `invalid`, `guard`
            # and the rest are ordinary results the caller inspects via
            # `last_outcome`.
            if outcome.reason == "denied":
                raise AccessDenied(result)
        elif result.startswith("denied:"):
            # Firmware predating the `#R:` marker. Kept so an older board
            # still raises, rather than silently returning a refusal
            # string the caller treats as success.
            raise AccessDenied(result)
        return result, outcome

    def auth(self, role: str) -> str:
        """Set the session role (observer, agent, operator, admin)."""
        if role not in self.VALID_ROLES:
            raise ValueError(f"role must be one of {self.VALID_ROLES}")
        return self.cmd(f"auth role {role}")

    # --- Purpose ---

    def purpose_set(self, name: str) -> str:
        name = _token(name, "purpose name")
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
        name = _token(name, "cell name")
        return self.cmd(f"goonies find {name}")

    def goonies_read(self, name: str) -> str:
        name = _token(name, "cell name")
        return self.cmd(f"goonies read {name}")

    # --- Vitals ---

    def vitals(self) -> str:
        return self.cmd("vitals")

    def vitals_override(self, vital: str, state: int) -> str:
        vital = _token(vital, "vital name")
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
        key = _token(key, "config key")
        value = _token(value, "config value")
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
