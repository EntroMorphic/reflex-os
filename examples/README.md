# Reflex OS Examples

Each example runs against a connected Reflex board via the Python SDK.

## Setup

```bash
pip install pyserial
```

## Examples

| Example | What it demonstrates | Command |
|---------|---------------------|---------|
| **blink** | LED control from Python | `python examples/blink/blink.py` |
| **purpose-demo** | Purpose set/clear, holon activation, snapshots | `python examples/purpose-demo/purpose_demo.py` |
| **multi-node** | Discover all nodes, coordinate purposes, read temps | `python examples/multi-node/multi_node.py` |
| **mesh-temp** | Continuous temperature monitoring across mesh | `python examples/mesh-temp/mesh_temp.py` |
| **custom-cell** | Look up fabric cells, control LED via substrate | `python examples/custom-cell/custom_cell.py` |

All examples accept an optional port argument: `python examples/blink/blink.py /dev/cu.usbmodem1101`
