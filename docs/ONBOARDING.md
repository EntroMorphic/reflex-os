# Developer Onboarding

From "I want to contribute" to "I submitted my first PR" in 10 steps.

## 1. Fork & Clone

```bash
git clone https://github.com/YOUR_USERNAME/reflex-os.git
cd reflex-os
```

## 2. Run Host Tests (no hardware needed)

```bash
make test
# Expected: 26 passed, 0 failed
```

If this passes, your toolchain is working.

## 3. Install ESP-IDF (for firmware builds)

```bash
# Follow: https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c6/get-started/
# Then:
source ~/path-to/esp-idf/export.sh
```

## 4. Build Firmware

```bash
idf.py set-target esp32c6
idf.py build
```

## 5. Flash a Board (if you have hardware)

```bash
idf.py -p /dev/cu.usbmodem1101 flash
screen /dev/cu.usbmodem1101 115200
# You should see: reflex>
```

## 6. Try the Python SDK

```bash
pip install -e sdk/python/
reflex-cli --discover                           # find boards
reflex-cli /dev/cu.usbmodem1101                 # connect and print status
```

Or use the SDK in Python directly (see `sdk/python/README.md`):

```python
from reflex import ReflexNode
with ReflexNode("/dev/cu.usbmodem1101") as node:
    print(node.status())
    node.led_on()
```

## 7. Understand the Architecture

Read these in order:
1. `docs/architecture.md` — layers, kernel, substrate
2. `docs/USER_MANUAL.md` — Section 4 (Key Concepts)
3. `include/reflex_hal.h` — the HAL interface
4. `include/reflex_task.h` — the task interface

## 8. Pick an Area

| Area | Start here |
|------|-----------|
| Substrate/GOOSE | `components/goose/goose_runtime.c` |
| Kernel | `kernel/reflex_freertos_compat.c` |
| Shell commands | `shell/shell.c` |
| Platform/drivers | `platform/esp32c6/reflex_hal_esp32c6.c` |
| VM | `vm/interpreter.c` |
| New platform | `docs/PLATFORM_BACKEND.md` |

## 9. Make a Change

```bash
# Edit code
make test                    # Verify tests still pass
make format                  # Auto-format (requires clang-format)
git add -p && git commit     # Commit with descriptive message
```

## 10. Submit a PR

```bash
git push origin your-branch
# Open PR on GitHub — CI validates automatically
```

## Resources

- `docs/DEBUGGING.md` — crash decode, common failures
- `docs/PLATFORM_BACKEND.md` — adding new chip support
- `docs/USER_MANUAL.md` — user-facing documentation
- `docs/paper-novel-contributions.md` — academic framing
- `SECURITY.md` — security model
- `CONTRIBUTING.md` — code style, commit conventions
