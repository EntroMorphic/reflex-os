# LMM: Shell Capabilities

Applying the Lincoln Manifold Method to `shell/shell.c` — what the shell can
do today, what bounds it, and which of those bounds are worth moving.

Written after a full audit-and-remediation pass over the shell (commits
`95bfa94`..`52c05c7`), so the observations below are measured rather than
recalled.

---

## Phase 1: RAW

I have spent a day inside this shell and I still cannot type a command
correctly on the first try. There is no backspace. Not "backspace is awkward" —
the byte `0x7F` is appended to the line buffer and echoed, so `statuX␡s`
dispatches as `unknown command: statuX\x7fs`. Every typo costs the whole line.
That is the actual daily experience of the primary interface to this OS.

And I think that ergonomic gap is not cosmetic, because it is causally
connected to the worst bug I found today. `aura setkey` takes 32 hex
characters, typed blind, with no correction and no echo you can verify against
anything. Then it decoded them with `strtoul`, which maps a typo to zero
silently. The interface made mistakes likely and the parser made them
invisible. Those are the same failure, viewed from two layers.

Second thing nagging me. The shell's authority is enormous and its
expressiveness is nil. It can name-resolve all 12,738 MMIO registers, read live
hardware, write cell state directly, load CRC32-verified bytecode and execute
it, and weave LoomScript fragments that introduce routes *and start pulse
tasks*. That last one extends the running system. And yet there is no way to
say "do this, then that," no variables, no way to pass an argument containing a
space. `purpose set my purpose` silently sets the name to `my` and discards the
rest. Enormous blast radius, zero composition.

Third. The 255-character line limit sits in front of a 1024-byte buffer. The
buffer is allocated. The loop just stops early at `len < 255`. Because of that
one constant, `loom load` and `vm loadhex` — the only two ways to extend a
running board — cap at **122 bytes** of payload. The most powerful primitives
in the system are throttled by an arbitrary number, against memory already
paid for.

Fourth, and this is the one I keep circling. Who is the shell actually for?
I have been assuming "a developer at a terminal." But every piece of real
automation today goes through the Python SDK, and the SDK does not read prose —
it *scrapes* prose. `temp()` regexes `42.7` out of `temp=42.7C state=0`.
`AccessDenied` fires on `result.startswith("denied:")`. Which means every
human-readable string in this shell is a load-bearing API surface, and nobody
has ever treated it that way.

I have direct evidence of how fragile that is, from today, and it is slightly
embarrassing: my own hardware test asserted that a refusal did not contain the
substring `"provisioned"`. The refusal message reads `key not provisioned`. My
check broke on English. That is precisely the failure mode every SDK consumer
is exposed to, and I walked straight into it while writing the tests meant to
prevent this class of thing.

Fifth. Should the shell grow at all? There are already two programmable
substrates in this tree — the ternary VM with TASM, and LoomScript. The
scripting story arguably belongs there, not in a command line. Maybe the right
move is to stop treating the shell as a language and treat it as a *port*.

Sixth, a worry about identity. If I add composition, the obvious model is sh:
sequencing, exit codes, `&&`. But exit codes are binary — zero is success,
non-zero is failure — and this OS's entire thesis is that two values lose
information the third one carries. The kernel policy layer already publishes
engaged/latent/withheld precisely because "a single priority number cannot
express that difference." Importing sh semantics into a ternary OS would be
adopting the ontology the project exists to argue against.

Seventh, and this bothers me more the longer I look at it: the shell already
produces exactly three outcome classes. A command succeeds and prints a result.
A command is refused and prints `denied: requires operator`. A command does
nothing and prints a usage line. Success, refusal, no-op. That is a trit. It is
sitting right there in the code, flattened into undifferentiated text, and the
SDK is left to reconstruct it with string matching.

Eighth. ~92% of `shell.c` still has no automated coverage. I extracted the role
policy and the input parsers because they were pure logic; the 50 handlers
remain untested except by a hardware script that checks happy paths thinly. I
should be honest that "the shell is tested" is not true — "the shell's decisions
are tested" is true.

---

## Phase 2: NODES

1. **There is no line editing at all.** Backspace is stored as a literal byte.
   Verified on hardware: `statuX␡s` → `unknown command: statuX\x7fs`. A typo
   costs the entire line, on every interface to this OS.

2. **The input assumes a machine; the output assumes a human.** A program can
   emit a perfect line effortlessly and then must scrape prose to understand
   the reply. A human can read the reply easily and cannot comfortably type the
   request. Both users get the half that suits them least.

3. **TENSION: Ergonomics vs. the real consumer.** The SDK is where automation
   actually lives, so effort spent on line editing serves the rarer user. But
   the human at the terminal is who *develops* the system, and today's key-
   provisioning bug traces directly to that ergonomic gap.

4. **The human-readable strings are an undocumented API.** `temp()` regexes a
   float out of prose; `AccessDenied` depends on `startswith("denied:")`. Any
   message I reword is a silent breaking change for every SDK consumer.

5. **Evidence that this is not hypothetical:** my own test broke on
   `"provisioned"` matching inside `"key not provisioned"`. Written by someone
   with the source open, on the same day, specifically to catch this family of
   bug.

6. **The shell already produces a trit and throws it away.** Success, refusal
   (`denied:`), and no-op (usage line) are three distinct outcome classes,
   collapsed into text with no machine-visible marker.

7. **Authority is high; expressiveness is zero.** 12,738-register name
   resolution, live MMIO reads, direct cell writes, bytecode load-and-run,
   runtime LoomScript weaving — against one verb, ≤8 tokens, 255 characters, no
   quoting, no composition.

8. **The 122-byte extension ceiling is an accident.** `REFLEX_SHELL_LINE_MAX`
   is 1024; the input loop stops at `len < 255`. The two commands that can
   extend a running board are throttled by a constant, against a buffer already
   allocated.

9. **TENSION: Composition vs. ontology.** The obvious model for sequencing is
   sh, whose result type is binary. This OS publishes engaged/latent/withheld
   in its kernel policy layer specifically because two values lose information.
   Importing exit codes would contradict the thesis.

10. **Silent truncation is still live in the argument path.** `purpose set my
    purpose` sets `my` and discards the rest without a word — the same
    silent-failure family as the coercion bugs just removed, in a different
    layer.

11. **The scripting story may not belong in the shell.** TASM and LoomScript
    already exist as programmable substrates, with `prd-tasm-compiler.md` and
    `prd-tasm-upload.md` written. A command line is not obviously the right
    place to add a third.

12. **TENSION: Build now vs. build when needed.** The same tension
    `lmm-role-based-access.md` resolved by separating classification from
    ceremony. Most of what could be built here is speculative; a small part is
    not.

13. **Coverage is honest only if stated precisely.** The shell's *decisions*
    (role policy, input parsing) are host-tested and mutation-proven. Its 50
    *handlers* are not.

14. **`esp_console` already exists.** ESP-IDF ships linenoise and argtable —
    history, completion, editing, structured argument parsing. The shell is
    hand-rolled instead. That is a large capability already paid for and not
    drawn on.

---

## Phase 3: REFLECT

The node that reorganises the rest is Node 2, and Node 6 is what makes it
actionable.

**Core insight: the shell's input is designed for a machine and its output is
designed for a human, and it should be the other way around — or, better,
each should be explicit about which it is serving.**

Look at what each user actually needs.

A *human* needs to type comfortably — editing, history, completion — and can
read anything. A *program* needs to parse reliably — stable markers, explicit
outcome classes, no natural language in the contract — and can emit a perfect
line with no help at all. Today the shell gives the human a strict,
unforgiving input path and the program a prose output path. Each gets the
other's half.

That reframing collapses a long menu of possible work into two coherent
directions, and they are independent. Serving the human is ergonomics:
linenoise, backspace, history, completion. Serving the program is contract:
an explicit, machine-visible result. Neither blocks the other, and they can be
sequenced by which pays sooner.

**The program side pays sooner, and it is cheaper.**

Because of Node 6, the expensive part is already done — the shell *knows* the
outcome class at the moment it decides it. `reflex_shell_dispatch` knows it
refused. A handler that prints usage knows nothing happened. A handler that
succeeded knows it succeeded. That information exists and is discarded at the
last instant, in favour of English. Recovering it does not require new logic,
only a marker.

And the marker should be a trit, which is where the practical need and the
project's identity actually meet — a rarer alignment than it sounds.

```
+1  engaged   the command did the thing
 0  latent    nothing happened (usage, no-op, empty result)
-1  withheld  refused (role denied, guard refused, validation rejected)
```

This is not decoration. It is *strictly more informative than an exit code*, and
the difference is load-bearing for exactly the consumer that matters most. An
agent under `role="agent"` needs to distinguish "I was refused" from "I ran and
there was nothing to report" — a binary status cannot, and today the SDK
guesses by matching English prefixes. The kernel policy layer already argued
this case for scheduling stances: `latent` means undecided and is deliberately
distinct from `withheld`. Command outcomes have the same three-valued shape,
and the shell is already computing it.

The telemetry stream shows the pattern to follow. Substrate state is already
emitted as `#T:` lines that the Loom Viewer consumes without parsing prose.
A result marker is the same idea applied to command outcomes: `#R:<trit>`
emitted after dispatch, ignorable by a human reading a terminal, decisive for
a parser. It costs one `printf` in one function and breaks nothing, because it
*adds* a line rather than changing any existing one.

That is the whole intervention on the program side. Everything else in the
possibility space is either much more expensive or much less certain:

- **Structured output for every command** (key=value or JSON per response)
  would end screen-scraping entirely, but it is a rewrite of 50 handlers and a
  breaking change for the Loom Viewer. The result marker captures most of the
  value for a fraction of the cost, and can be extended per-command later where
  it earns its place.
- **Command composition** (`;`, `&&`, variables) is the direction I am most
  confident is *wrong* to take now. Node 9 is real: doing it in sh's image
  contradicts the ontology, and doing it natively is research, not plumbing.
  If sequencing is ever needed, a trit-aware `&&` — what does "and" mean when
  the left side is `latent`? — is a genuinely interesting question and
  genuinely not urgent.
- **A chunked upload protocol** (`loom load begin/chunk/end`) properly removes
  the 122-byte ceiling and is the honest answer for TASM upload at scale. But
  Node 8 says most of that ceiling is an accident: raising `len < 255` to
  `len < REFLEX_SHELL_LINE_MAX - 1` gives 8× the payload for one constant,
  against memory already allocated. Do the constant now; do the protocol when
  something actually needs more than a kilobyte.
- **Quoting** matters only because of Node 10, and the defect there is the
  *silence*, not the missing feature. `purpose set my purpose` should say it
  ignored the extra tokens. That is an error message, not a parser.

On the human side, the sequencing is dictated by cost. Backspace is a handful
of lines and removes the retype tax immediately. `esp_console` with linenoise
(Node 14) is the real fix — history, completion, editing, all already shipped
with the IDF — but it replaces the input loop, which is the one part of the
shell that is genuinely load-bearing and now carries a hard-won newline-echo
fix. That is worth doing deliberately, not opportunistically.

One last thing REFLECT clarified. Node 13 is a claim about honesty, not a task.
"The shell is tested" is false; "the shell's decisions are tested" is true. The
50 handlers are mostly thin wrappers over subsystems that *are* tested
elsewhere, so extracting them wholesale would be motion without much value. The
parsers and formatters were the parts worth extracting, and they are extracted.
I should stop describing this as a gap and start describing it accurately.

---

## Phase 4: SYNTHESIZE

### The landscape

| Bound | Value today | Set by | Movable? |
|---|---|---|---|
| Input line | 255 chars | `len < 255`, against a 1024 buffer | **Yes — one constant** |
| Extension payload | 122 bytes | line limit, halved for hex | Yes, follows the above |
| Tokens per command | 8 | `char *argv[8]` | Yes, not currently binding |
| Argument with a space | impossible | `strtok(line, " ")` | Only with a real tokenizer |
| Line editing | none | input loop ignores `0x08`/`0x7F` | **Yes — a few lines** |
| History / completion | none | hand-rolled loop | Yes, via `esp_console` |
| Composition | none | one verb per line | Deliberately not now |
| Outcome class | prose only | no result marker | **Yes — one `printf`** |
| Concurrent sessions | 1 | single blocking loop | Not worth it |
| Authority ceiling | very high | 23 commands, 4 roles | Already correct |

Two independent axes, not one: **contract** (what a program can rely on) and
**ergonomics** (what a human can type). The shell is currently weak on both,
for opposite reasons.

### Key decisions

1. **Emit a ternary result marker after every dispatch.** `#R:+1` / `#R:0` /
   `#R:-1` for engaged / latent / withheld, following the existing `#T:`
   telemetry convention. Additive — no existing output changes.
2. **Do not restructure per-command output.** Screen-scraping ends where it
   costs most (the outcome class) without rewriting 50 handlers or breaking the
   Loom Viewer.
3. **Raise the line bound to the buffer that already exists.** `len < 255` →
   `len < REFLEX_SHELL_LINE_MAX - 1`. 8× payload for `loom load` and
   `vm loadhex`, zero new memory.
4. **Add backspace handling.** `0x08`/`0x7F` decrement `len` and emit
   `"\b \b"`. Small, and it removes a daily tax on the people building this.
5. **Defer `esp_console`.** It is the right long-term answer for the human
   side, and it replaces a loop that just earned a subtle correctness fix.
   Deliberate, not opportunistic.
6. **Defer composition entirely.** In sh's image it contradicts the ontology;
   natively it is research. Revisit only if a concrete workflow demands it.
7. **Make silent truncation loud.** Extra tokens on `purpose set` and
   `config set device_name` should produce an error, not a quiet prefix.
8. **Stop calling handler coverage a gap.** Describe it accurately: the
   shell's decisions are tested; its handlers are thin wrappers over tested
   subsystems.

### Action plan

**Phase 1 — contract and ergonomics (small, independent, do now)**
- `#R:<trit>` emitted in `reflex_shell_dispatch`; refusal is `-1` at the gate.
- Handlers return an outcome so `latent` is distinguishable from `engaged`;
  default `engaged` so no handler must change at once.
- Line bound raised to `REFLEX_SHELL_LINE_MAX - 1`.
- Backspace handling in the input loop.
- SDK: `AccessDenied` keyed on `#R:-1` rather than `startswith("denied:")`;
  the prose prefix stays as a fallback for older firmware.
- Tests: host cases for the outcome mapping; `validate_shell.py` asserts a
  marker on every command it already exercises, and asserts a `122`→`~500`
  byte payload now loads.

**Phase 2 — when something needs it**
- Chunked upload (`loom load begin/chunk/end`) when a fragment exceeds a
  kilobyte.
- `esp_console`/linenoise when interactive use becomes common enough to
  justify replacing the input loop.

**Phase 3 — research, not plumbing**
- Trit-aware composition. The interesting question is what `&&` means when the
  left operand is `latent`. Worth a design doc before a line of code.

### Success criteria

- The SDK never string-matches English to determine an outcome.
- An agent under `role="agent"` can distinguish refused from ran-and-did-nothing.
- `loom load` accepts a fragment ≥ 500 bytes.
- A typo can be corrected without retyping the line.
- No existing consumer (Loom Viewer, SDK, `validate_shell.py`) breaks.
- Every message reworded in future is provably not an API change.

### Tensions and how they are handled

| Tension | Resolution |
|---|---|
| Ergonomics vs. the real consumer (Node 3) | Both, sequenced by cost. The contract fix is one `printf`; backspace is a few lines. Neither trades against the other. |
| Composition vs. ontology (Node 9) | Deferred, explicitly. The ternary result marker is laid down now so that if composition ever arrives it composes over trits, not exit codes. |
| Build now vs. when needed (Node 12) | Same resolution as `lmm-role-based-access.md`: do the classification, defer the ceremony. Here the "classification" is the outcome trit; the ceremony is structured per-command output and chunked upload. |
| Prose is an API vs. prose is for humans (Nodes 4, 5) | Stop overloading one channel. Humans keep the prose; programs get the marker. |

### What this does not do

It does not make the shell a language, and it should not. Composition for this
machine belongs in TASM and the SDK. What Phase 1 buys is a shell that is
honest about its outcomes and tolerable to type into — roughly thirty lines,
against an interface every other capability in this OS is reached through.

---

## Addendum: what implementation found

Phase 1 shipped. Two things the synthesis above got wrong, recorded because the
gap between the plan and the hardware is the useful part.

**"Raise the line bound — one constant, zero new memory" was wrong.** The
transport buffer had to grow with it. `USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT()`
sets `rx_buffer_size = 256`, and the old `len < 255` was almost certainly sized
to that rather than chosen freely. Raising only the line bound made a 600-
character `vm loadhex` arrive *short*, which the parser then rejected as
odd-length hex — a silent truncation in the transport, which is the same
failure class the audit had just finished removing from the parsers. Caught
only by sending a real payload to a real board; nothing in the host suite could
have seen it. The driver's RX buffer is now sized from
`REFLEX_SHELL_LINE_MAX`, and payloads are verified end-to-end at 200 and 500
bytes on both targets.

**Marking outcomes exposed handlers that lie.** `vm loadhex` with a malformed
image printed `vm load failed` and reported `#R:+1,ok`, because the failure
path had no marker and the default is engaged. That is the predictable cost of
defaulting to success — the same trade the plan accepted so no handler had to
change at once — and it means the marker's accuracy is only as good as the
sweep for failure paths. Three were missed on the first pass and found by
reading outcomes back off a live board.

The claim to keep honest: every outcome the hardware suite asserts is verified;
unmarked paths still default to `+1,ok` and may be wrong. That is a known,
bounded gap, not a finished job.
