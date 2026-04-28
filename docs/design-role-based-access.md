# Role-Based Access (RBA)

## The Problem

Reflex OS has one trust level: God. The serial shell has unrestricted access to every command, every register, every configuration. This is fine for a single developer on a lab bench. It breaks for:

- **Multiple operators** — an intern shouldn't be able to `aura setkey` or `reboot`
- **Remote access** — if the shell is ever exposed over WiFi/BLE, God mode is a security hole
- **AI agents** — an LLM issuing commands via the SDK should have guardrails
- **Deployed boards** — a field-installed board should restrict access to maintenance operations

## Design

### Roles

Four roles, ordered by privilege:

| Role | Tag | Purpose |
|------|-----|---------|
| **admin** | `A` | Full access. Firmware updates, key provisioning, security configuration. The current God mode. |
| **operator** | `O` | Day-to-day control. Set purpose, manage snapshots, control LED, run VM programs, read/write config. |
| **observer** | `V` | Read-only. Status, telemetry, goonies find/read, vitals, temp. Cannot mutate state. |
| **agent** | `G` | AI/automation. Can set purpose, observe telemetry, read GOONIES. Cannot reboot, sleep, override vitals, or provision keys. |

### Command Classification

Every shell command gets a minimum role:

| Command | Min Role | Rationale |
|---------|----------|-----------|
| `help` | observer | Harmless |
| `status` | observer | Read-only |
| `reboot` | admin | Destructive |
| `sleep` | admin | Takes board offline |
| `led on/off/status` | operator | State mutation, non-destructive |
| `goonies ls` | observer | Read-only |
| `goonies find` | observer | Read-only |
| `goonies read` | observer | Read-only (MMIO read) |
| `atlas verify` | observer | Read-only diagnostic |
| `temp` | observer | Read-only |
| `purpose set` | operator | State mutation |
| `purpose get` | observer | Read-only |
| `purpose clear` | operator | State mutation |
| `snapshot save` | operator | Persistence mutation |
| `snapshot load` | operator | State mutation |
| `snapshot clear` | admin | Destructive (erases learned state) |
| `vm info` | observer | Read-only |
| `vm run` | operator | Starts execution |
| `vm stop` | operator | Stops execution |
| `vm list` | observer | Read-only |
| `vm loadhex` | admin | Loads arbitrary code |
| `heartbeat` | observer | Read-only |
| `aura setkey` | admin | Security-critical |
| `mesh mac` | observer | Read-only |
| `mesh emit` | operator | Sends mesh traffic |
| `mesh query` | observer | Read-only query |
| `mesh posture` | operator | Sends mesh traffic |
| `mesh stat` | observer | Read-only |
| `mesh status` | observer | Read-only |
| `mesh ping` | operator | Sends mesh traffic |
| `mesh peer add` | admin | Modifies peer registry |
| `mesh peer ls` | observer | Read-only |
| `services` | observer | Read-only |
| `config get` | observer | Read-only |
| `config set` | admin | Persistence mutation |
| `telemetry on/off` | observer | Telemetry is observation, not mutation |
| `vitals` | observer | Read-only |
| `vitals override` | admin | Synthetic state injection |
| `vitals clear` | admin | Resets metabolic state |
| `bonsai` | operator | Experiment execution |

The **agent** role maps to operator-level for most commands, but explicitly blocks: `reboot`, `sleep`, `aura setkey`, `vm loadhex`, `config set`, `vitals override`, `vitals clear`, `snapshot clear`, `mesh peer add`.

### Authentication

**Serial shell (local):** Defaults to admin (backward-compatible). An optional `auth <pin>` command elevates from observer to a higher role. The PIN is a 4-8 digit numeric code stored in NVS.

**SDK (Python):** The `ReflexNode` constructor accepts an optional `role` parameter. Role is sent as the first command after connection: `auth role <role> <pin>`. Without auth, the SDK operates as observer.

**Mesh (remote):** Mesh peers operate at observer level by default. Aura-authenticated peers can be promoted to operator via `mesh peer add <name> <mac> <role>`.

### Storage

NVS namespace `auth`:
- `auth/pin_admin` — 4-8 digit admin PIN (default: not set = no PIN required)
- `auth/pin_operator` — operator PIN
- `auth/default_role` — default role for unauthenticated sessions (default: `admin` for backward compat, change to `observer` when deploying)

### Implementation

**Per-session state:** Each shell session has a `current_role` (uint8_t: 0=observer, 1=agent, 2=operator, 3=admin). Stored in the shell's session context, not global.

**Command dispatch:** Each entry in `s_commands[]` gets a `min_role` field:

```c
typedef struct {
    const char *name;
    shell_cmd_fn_t handler;
    uint8_t min_role;   /* 0=observer, 1=agent, 2=operator, 3=admin */
} shell_cmd_t;
```

Before dispatching, the shell checks `session->role >= cmd->min_role`. If not, print `access denied: requires <role>` and return.

**New shell commands:**

| Command | Description |
|---------|-------------|
| `auth <pin>` | Elevate session to the highest role matching the PIN |
| `auth role` | Show current session role |
| `auth set admin <pin>` | Set admin PIN (requires admin) |
| `auth set operator <pin>` | Set operator PIN (requires admin) |
| `auth default <role>` | Set default role for new sessions (requires admin) |
| `auth clear` | Drop session to default role |

### Backward Compatibility

- **No PIN set = no restriction.** If `auth/pin_admin` is empty in NVS, all sessions start as admin. This is the current behavior. RBA is opt-in.
- **First `auth set admin <pin>` activates RBA.** Once an admin PIN is set, new sessions start at the default role (observer). Existing sessions are unaffected until reconnect.
- **SDK auto-detection:** If the SDK sends `auth role` and gets `observer`, it knows RBA is active and can prompt for credentials.

### Threat Model

This is **not** cryptographic security. It's **operational safety** — preventing accidental destructive operations and providing role separation for multi-user environments. The serial console is still physically accessible. A determined attacker with physical access can bypass RBA by flashing new firmware.

The value is:
1. An intern can't accidentally `reboot` or `aura setkey`
2. An AI agent can set purpose and observe but can't brick the board
3. A deployed board defaults to observer, requiring a PIN for mutations
4. Audit trail: `auth` events are visible in telemetry

### Files to Modify

| File | Change |
|------|--------|
| `shell/shell.c` | Add `min_role` to `shell_cmd_t`, session role state, `auth` command, dispatch check |
| `include/reflex_shell.h` | Add role constants, session struct |
| `sdk/python/reflex.py` | Add `role` parameter, `auth()` method |
| `sdk/python/README.md` | Document auth API |
| `README.md` | Add `auth` to shell table, security note |
| `SECURITY.md` | Add RBA section |
| `CHANGELOG.md` | Add entry |

### Verification Plan

1. `idf.py build` — compiles with new dispatch
2. `make test` — existing 26 tests pass
3. On-device: default behavior unchanged (no PIN set = admin)
4. On-device: `auth set admin 1234` → new sessions start as observer
5. On-device: `reboot` blocked as observer → `access denied: requires admin`
6. On-device: `auth 1234` → elevated to admin → `reboot` works
7. On-device: `purpose set led` works as operator but not observer
8. On-device: `auth clear` → drops back to default role
9. SDK: `ReflexNode(port, role="operator", pin="5678")` → elevated session
