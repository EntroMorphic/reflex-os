# Holon Lifecycle

Reference for the holon system in `goose_supervisor.c`.

## What a Holon Is

A holon is a named group of fields with a domain tag, managed as a lifecycle
unit by the supervisor. The supervisor activates or deactivates holons based on
the current purpose, controlling whether member fields retain boosted priority.

## API

```c
reflex_err_t reflex_holon_create(const char *name, const char *domain);
reflex_err_t reflex_holon_add_field(const char *holon_name, goose_field_t *field);
```

- `name`: identifier (max 15 chars).
- `domain`: purpose-matching tag. Empty string means always active.

## Activation Rules

A holon is active if:
- `domain` is empty (`""`), OR
- A purpose is active and the purpose string contains `domain`.

Domain matching uses `strstr` against the current purpose name.

## Boot-Time Holons

| Name        | Domain  | Meaning                          |
|-------------|---------|----------------------------------|
| `autonomy`  | `""`    | Always active (core survival)    |
| `comm`      | `mesh`  | Active when purpose contains "mesh" |
| `agency`    | `led`   | Active when purpose contains "led"  |

Created in `goose_supervisor_init()`.

## Effect of Activation

Fields in at least one active holon keep their computed priority (base +
purpose boost + Hebbian boost, clamped to `PULSE_MAX_PRIORITY` of 15).

## Effect of Deactivation

If a field belongs to one or more holons but ALL of them are inactive, the
field's priority is forced to `PULSE_BASE_PRIORITY` (10), regardless of other
boosts.

## Constants

| Name              | Value | Notes                          |
|-------------------|-------|--------------------------------|
| `MAX_HOLONS`      | 8     | Maximum holon slots            |
| `MAX_HOLON_FIELDS`| 4     | Maximum fields per holon       |

## Lifecycle Evaluation

`goose_kernel_policy_tick()` runs at 1 Hz via the kernel policy callback. On
each tick it compares the current purpose against `s_last_purpose`. When the
purpose changes, each holon's active state is re-evaluated and transitions are
logged:

```
[reflex.kernel] holon agency activated (domain=led)
[reflex.kernel] holon comm deactivated (domain=mesh)
```
