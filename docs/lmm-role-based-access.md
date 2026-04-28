# LMM: Role-Based Access (RBA)

Applying the Lincoln Manifold Method to the design in `design-role-based-access.md`.

---

## Phase 1: RAW

Four roles, PIN-based auth, per-command dispatch. It's clean on paper. But I have doubts.

First, who are we actually building this for? Today there's one user: Tripp, on a serial cable, on a lab bench. The design lists four personas (intern, remote operator, AI agent, deployed board) but none of them exist yet. The intern hasn't been hired. The remote shell doesn't exist. The AI agent is a future phase. The deployed board is a research prototype. Am I designing for real users or imagined ones?

Second, the PIN mechanism feels wrong for an embedded system. A 4-8 digit PIN sent in cleartext over a serial cable provides zero actual security. Anyone with a logic analyzer or a USB sniffer captures the PIN in one session. And the serial console is physical access — if you have the cable, you have the board. So the PIN doesn't protect against physical attackers (they can flash firmware) and doesn't protect against network attackers (there's no network shell). Who does it protect against? Accidental misuse. An operator types `reboot` when they meant `status`. That's a fat-finger problem, not a security problem.

Third, four roles might be over-engineering. The design has admin, operator, observer, agent — but the difference between operator and agent is thin (agent = operator minus a few destructive commands). Do we need four levels or two? The essential split is: "can mutate" vs. "can only observe." That's two roles. Everything else is gradation within mutation permissions.

Fourth, the `auth` command flow is awkward. You connect, you're observer, you type `auth 1234`, you're admin. But the PIN is stored in NVS. If the board reboots and NVS is cleared (or if the user runs `snapshot clear` which might wipe the wrong namespace), the PIN is gone and the board is locked in observer mode with no way to elevate. Recovery requires reflashing. That's a bricking vector disguised as a safety feature.

Fifth, I worry about the complexity cost. The current shell is 43 commands in a flat dispatch table. Adding a role field to each entry is trivial. But the auth command adds NVS reads, PIN comparison, session state management, and a new failure mode (wrong PIN, locked out, PIN lost). The shell today is ~1000 lines. Auth could add 100-150 lines plus a new NVS namespace. Is that complexity justified by the users who don't exist yet?

Sixth — and this is what pulls me back — the AI agent use case is real and coming soon. The Python SDK exists. The Loom Viewer exists. The moment someone writes a script that calls `node.reboot()` in a loop, or an LLM that decides `vitals override heap -1` is a good idea, the God-mode problem becomes concrete. The agent role isn't hypothetical — it's the next interface after the serial shell.

Seventh, there's a simpler framing: instead of role-based access with authentication, what about **command confirmation**? Destructive commands require a second `confirm` before executing. `reboot` → "are you sure? type 'confirm reboot'" → `confirm reboot` → reboots. No PINs, no roles, no NVS state. Just a speed bump before dangerous operations. This handles the fat-finger problem without the complexity of auth.

But confirmation doesn't solve the AI agent problem. An LLM will happily type `confirm reboot` after `reboot`. The point of roles is that the agent CANNOT escalate — it's not about confirming intent, it's about limiting capability.

Eighth, the mesh peer role assignment (`mesh peer add <name> <mac> <role>`) is interesting but opens a remote privilege escalation path. If I can add a peer at operator level, and that peer sends `ARC_OP_SYNC` packets, can the peer mutate remote state? The current mesh doesn't execute shell commands — it syncs cell state. But future MMIO sync (Phase 32) could allow cross-board writes. Role on mesh peers is forward-looking but not immediately useful.

---

## Phase 2: NODES

1. **The primary use case is the AI agent, not human operators.** Humans on serial cables are trusted by physical access. The agent — a script or LLM sending commands via the SDK — is the first real consumer of access control.

2. **TENSION: Security vs. operational safety.** PINs provide operational safety (fat-finger protection) not security (physical access = game over). The design document acknowledges this, but the implementation (NVS-stored PINs, auth command flow) carries security ceremony that doesn't match the threat model.

3. **Two roles might be enough.** The essential split: can-mutate vs. can-observe. Admin/operator/agent distinctions are gradations of mutation privilege that may not justify the complexity. Counter-argument: the agent role blocking `reboot`/`sleep`/`aura setkey` is meaningfully different from operator.

4. **Four roles ARE justified when the fourth is "agent."** Observer (read-only), operator (mutate), admin (destructive + security) map cleanly to human use cases. Agent is operator-minus-destructive, which is the AI guardrail. This is a real distinction — an LLM should be able to `purpose set` but not `reboot`.

5. **PIN recovery is a bricking risk.** If the admin PIN is lost and the default role is observer, the board is locked. Recovery requires reflashing. This needs a hardware escape hatch (hold BOOT button during reset = admin override) or a timeout (PIN expires after N hours of no admin session).

6. **The dispatch change is trivial.** Adding `uint8_t min_role` to the command table is ~2 lines per command. The gating check is 3 lines. The structural change is cheap; the ceremony (auth commands, NVS, PIN management) is the expensive part.

7. **TENSION: Opt-in vs. default-secure.** Backward compatibility demands opt-in (no PIN = admin). But the moment someone sets a PIN and forgets it, the experience degrades. And if the default is always admin, most deployments will never enable RBA.

8. **The SDK is the natural auth point.** The serial shell is a human interface where typing `auth 1234` is reasonable. The SDK is a programmatic interface where passing `role="agent"` in the constructor is cleaner. These are different auth UX patterns for different interfaces.

9. **Telemetry should log role transitions.** `#T:U,<role>` — visible in the Loom Viewer. Who did what, when. This is more valuable than the access control itself.

10. **TENSION: Build now vs. build when needed.** The AI agent doesn't exist yet. Building RBA now is speculative infrastructure. But the command table classification (which command needs what role) is useful documentation even without enforcement. And the enforcement is trivial once the classification exists.

11. **The mesh role is premature.** Mesh peers don't execute shell commands. Role on mesh is infrastructure for Phase 32 (MMIO sync writes) which hasn't been designed. Building it now would be speculative.

12. **Command confirmation is complementary, not alternative.** Confirmation protects against fat-fingers (human). Roles protect against capability (agent). Both have value. Roles without confirmation: an admin can still fat-finger `reboot`. Confirmation without roles: an agent can `confirm reboot`. Best: roles for capability + confirmation for admin destructive ops.

---

## Phase 3: REFLECT

The central tension is Node 10: build now or build when needed? The LMM process is resolving this for me.

The command classification table in the design document IS the valuable artifact. It's the thinking: which commands are read-only, which mutate state, which are destructive, which are security-critical. That classification has value even if enforcement is never built — it documents the blast radius of every command.

But enforcement is cheap. The structural change is 2 bytes per command entry + 3 lines of gating. The expensive part — PIN management, NVS ceremony, auth flow — is the part I'm least sure about.

**Core insight: separate the classification from the ceremony.**

Phase 1: Classify every command by role. Add `min_role` to the dispatch table. Add session state (`current_role`). Gate dispatch on role. Ship with `default_role = admin` (backward compatible). No PINs, no auth command, no NVS. The infrastructure is in place but invisible.

Phase 2 (later, when the SDK agent exists): Add `auth` command, PIN storage, SDK `role` parameter. Turn it on.

This gives us:
- Immediate: the classification is documented and enforced in code
- Immediate: the SDK can pass `role="observer"` to self-limit (no PIN needed — it's voluntary capability restriction)
- Deferred: PIN-based auth for when there's a real multi-user scenario

The SDK voluntary role is actually the killer feature. An AI agent script does:

```python
with ReflexNode(port, role="agent") as node:
    node.purpose_set("led")   # works
    node.reboot()             # blocked — agent can't reboot
```

No PIN. No auth ceremony. The agent declares its own capability ceiling. The OS enforces it. This is **capability-based**, not **identity-based** — and that's a better fit for an embedded system where the trust boundary is the cable, not the credential.

**Revised architecture:**

1. **Roles stay at four.** Observer, agent, operator, admin. The agent/operator distinction matters for AI guardrails.
2. **Default role: admin.** Backward compatible. No behavior change until someone opts in.
3. **Session role set by `auth role <role>`.** No PIN. Voluntary capability restriction. An honest agent says "I'm an agent" and gets agent-level access. A dishonest agent says "I'm an admin" and gets admin. That's fine — if you control the cable, you control the trust.
4. **PIN authentication deferred.** When remote access (WiFi shell, BLE shell) arrives, PINs become meaningful because the trust boundary moves from the cable to the network. Until then, PINs are ceremony without substance.
5. **Mesh roles deferred.** Until Phase 32 introduces cross-board writes, mesh peers don't need roles.

This cuts the implementation in half. No NVS namespace, no PIN storage, no recovery problem, no bricking risk. Just:
- `min_role` on each command (classification)
- `session->role` (default admin, settable via `auth role <role>`)
- Dispatch gate (3 lines)
- SDK `role` parameter

~50 lines of new code. The entire value of RBA with none of the ceremony.

---

## Phase 4: SYNTHESIZE

### Revised Design: Capability-Based Roles

**What changed from the original design:**

| Aspect | Original | Revised |
|--------|----------|---------|
| Authentication | PIN-based, NVS-stored | Voluntary capability declaration |
| Complexity | ~150 lines + NVS namespace | ~50 lines, no NVS |
| Recovery risk | Lost PIN = locked board | No PINs = no lockout |
| Threat model | Fat-finger + agent guardrails | Agent guardrails (fat-finger via confirmation, deferred) |
| Mesh roles | Included | Deferred to Phase 32 |
| Default | Admin (opt-in RBA) | Admin (same) |

### Roles (unchanged)

| Role | Level | Can do |
|------|-------|--------|
| observer (V) | 0 | Read-only: status, goonies, temp, telemetry, vitals |
| agent (G) | 1 | Observer + purpose set/clear, snapshot save/load |
| operator (O) | 2 | Agent + led, vm run/stop, mesh emit/ping/posture, bonsai |
| admin (A) | 3 | Everything: reboot, sleep, aura, config set, vm loadhex, vitals override, snapshot clear, mesh peer add |

### Implementation

**shell_cmd_t gains min_role:**

```c
typedef struct {
    const char *name;
    shell_cmd_fn_t handler;
    uint8_t min_role;
} shell_cmd_t;
```

**Session state:**

```c
static uint8_t s_session_role = 3;  /* default: admin */
```

**Dispatch gate (in shell_exec):**

```c
if (s_session_role < cmd->min_role) {
    printf("denied: requires %s\n", role_names[cmd->min_role]);
    return;
}
```

**New command: `auth`**

```
auth              — show current role
auth role <role>  — set session role (observer, agent, operator, admin)
```

No PIN. No escalation restriction. This is voluntary capability restriction — the caller declares what they are. When PIN auth is needed (remote shell), it layers on top without changing the dispatch infrastructure.

**SDK:**

```python
# Self-limiting agent
with ReflexNode(port, role="agent") as node:
    node.purpose_set("led")   # works (agent >= agent)
    node.reboot()             # raises AccessDenied (agent < admin)
```

The SDK sends `auth role agent` on connect. The dispatch gate enforces it.

**Telemetry:**

```
#T:U,<role>   — role transition event
```

### Files to Modify

| File | Change |
|------|--------|
| `shell/shell.c` | Add `min_role` to each command entry, dispatch gate, `auth` command, session state |
| `include/reflex_shell.h` | Role constants |
| `components/goose/include/goose_telemetry.h` | Add `goose_telem_auth()` |
| `components/goose/goose_telemetry.c` | Implement `goose_telem_auth()` |
| `sdk/python/reflex.py` | Add `role` parameter to constructor, `auth()` method |
| `sdk/python/README.md` | Document role parameter |
| `README.md` | Add `auth` to shell table |
| `SECURITY.md` | Add RBA section |
| `CHANGELOG.md` | Add entry |

### Success Criteria

1. Default behavior unchanged (no PIN, admin, all commands work)
2. `auth role observer` → `reboot` prints `denied: requires admin`
3. `auth role agent` → `purpose set led` works, `reboot` denied
4. `auth role admin` → everything works
5. SDK `ReflexNode(port, role="agent")` → agent-level access
6. Telemetry `#T:U,agent` on role change
7. 26 host tests still pass

### What the LMM Changed

The original design was identity-based: PINs, NVS, recovery risks, ceremony. The LMM exposed that the threat model doesn't justify credentials (physical access = game over). The real need is capability restriction for AI agents — and that's voluntary, not enforced by secrets.

Separating classification from ceremony cut the implementation in half. The command table classification is the durable value; PIN auth layers on later when remote access creates a real trust boundary. Build the bones now, add the skin when there's weather.
