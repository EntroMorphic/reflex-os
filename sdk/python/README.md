# Reflex OS Python SDK

Python SDK for serial communication with Reflex OS boards.

## Requirements

- Python 3.8+
- pyserial >= 3.5

## Installation

From the repository root:

```bash
pip install -e sdk/python/
```

Or manually:

```bash
pip install pyserial
```

## Quick Start

```python
from reflex import ReflexNode, discover

# Connect to a known port
with ReflexNode("/dev/cu.usbmodem1101") as node:
    print(node.purpose_get())       # current purpose
    print(node.temp())              # temperature in Celsius
    node.led_on()                   # turn on LED
    node.led_off()                  # turn off LED
    print(node.status())            # system status

# Auto-discover all connected boards
nodes = discover()
for n in nodes:
    print(f"{n['port']}: purpose={n['node'].purpose_get()}")
    n['node'].close()
```

## API Reference

### `ReflexNode(port, baud=115200, timeout=2.0, role=None)`

The optional `role` parameter sets the session's capability level on connect:
- `"observer"` — read-only (status, goonies, temp, telemetry)
- `"agent"` — observer + purpose set/clear, snapshot save/load
- `"operator"` — agent + led, vm run/stop, mesh emit/ping
- `"admin"` — everything (default when role is omitted)

Commands that exceed the session's role raise `AccessDenied`.

| Method | Description |
|--------|-------------|
| `cmd(command, timeout=3.0)` | Send a raw shell command, return response (raises `AccessDenied` if role insufficient) |
| `auth(role)` | Set session role (observer, agent, operator, admin) |
| `purpose_set(name)` | Set active purpose |
| `purpose_get()` | Get current purpose (or None) |
| `purpose_clear()` | Clear purpose |
| `snapshot_save()` | Persist learned state to flash |
| `snapshot_load()` | Restore learned state from flash |
| `snapshot_clear()` | Erase saved learning |
| `temp()` | Read temperature in Celsius (float or None) |
| `led_on()` | Turn on onboard LED |
| `led_off()` | Turn off onboard LED |
| `led_status()` | Return LED state string |
| `reboot()` | Trigger software reboot (connection drops) |
| `sleep(seconds)` | Enter deep sleep for N seconds |
| `services()` | List registered services |
| `status()` | System status summary |
| `goonies_ls()` | List all registered cell names |
| `goonies_find(name)` | Look up a cell by name |
| `goonies_read(name)` | Read a live MMIO register by name (raw hex + ternary) |
| `vitals()` | Display metabolic state (temp, battery, mesh, heap) |
| `vitals_override(vital, state)` | Inject synthetic vital state for testing (-1, 0, +1) |
| `vitals_clear()` | Clear all overrides; resume real hardware readings |
| `telemetry_on()` | Enable push-based substrate telemetry streaming |
| `telemetry_off()` | Disable telemetry streaming |
| `mesh_status()` | Show mesh state |
| `mesh_ping()` | Broadcast ping to peers |
| `vm_info()` | Ternary VM status |
| `config_get(key)` | Read a config value |
| `config_set(key, value)` | Write a config value |
| `heartbeat()` | LP coprocessor pulse count |
| `raw(command, timeout=3.0)` | Send any raw command (not sanitized) |
| `close()` | Close serial connection |

### `discover()`

Auto-discover Reflex OS nodes on available serial ports. Returns a list of
`{'port': str, 'node': ReflexNode}` dicts. Caller must close returned nodes.

## CLI Tool

```bash
# Discover all connected boards
reflex-cli --discover

# Connect to a specific port and print status
reflex-cli /dev/cu.usbmodem1101
```

Install the CLI via `pip install -e sdk/python/` -- this registers the
`reflex-cli` console script.
