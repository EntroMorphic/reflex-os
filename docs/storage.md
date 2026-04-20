# Persistent Storage

Two APIs provide persistent storage, both backed by NVS flash.

## Two-Tier API

### reflex_config (typed, high-level)

Declared in `storage/include/reflex_config.h`. Every getter/setter maps to a single NVS key in the **"reflex"** namespace. Type-safe -- callers cannot store the wrong type under a key.

```c
reflex_config_get_device_name(char *out, size_t len);
reflex_config_set_device_name(const char *value);
reflex_config_get_wifi_ssid(char *out, size_t len);
reflex_config_set_wifi_ssid(const char *value);
reflex_config_get_wifi_password(char *out, size_t len);
reflex_config_set_wifi_password(const char *value);
reflex_config_get_log_level(int32_t *out);
reflex_config_set_log_level(int32_t value);
reflex_config_get_safe_mode(bool *out);
reflex_config_set_safe_mode(bool value);
reflex_config_get_boot_count(int32_t *out);
reflex_config_set_boot_count(int32_t value);
reflex_config_init_defaults(void);  // seeds first-boot values
```

### reflex_kv (flexible, low-level)

Declared in `include/reflex_kv.h`. Open any namespace, store strings, blobs, i32, or u8. Requires explicit open/commit/close.

```c
reflex_kv_open("goose", false, &handle);
reflex_kv_set_str(handle, "purpose", "sentinel");
reflex_kv_commit(handle);
reflex_kv_close(handle);
```

Supported types: `str`, `blob`, `i32`, `u8`.

## Namespaces

| Namespace | Used By | Contents |
|-----------|---------|----------|
| `"reflex"` | reflex_config | device name, WiFi creds, log level, boot count, safe mode |
| `"goose"` | goose_supervisor, goose_runtime, goose_atmosphere | purpose name, aura key, fabric snapshots, atmosphere config |

## Capacity

- NVS uses 4 KB flash sectors.
- Expect ~100 writes per sector before compaction triggers.
- Compaction is handled automatically by the NVS backend.
- Keep values small -- strings under 256 bytes, blobs under 1 KB.

## Common Patterns

| Shell Command | Storage Call |
|---------------|-------------|
| `purpose set <name>` | `reflex_kv_set_str(h, "purpose", name)` in "goose" ns |
| `aura setkey <hex>` | `reflex_kv_set_blob(h, "aura_key", ...)` in "goose" ns |
| `snapshot save` | `reflex_kv_set_blob(h, ...)` in "goose" ns |
| `wifi <ssid> <pass>` | `reflex_config_set_wifi_ssid/password()` |

## Adding a New Config Key

1. Add getter/setter declarations to `storage/include/reflex_config.h`.
2. Implement them in the corresponding `.c` file using `reflex_kv_open("reflex", ...)`.
3. If the key needs a first-boot default, add it to `reflex_config_init_defaults()`.
4. Update `docs/storage.md` and any shell commands in the same commit.

For keys outside the "reflex" namespace, use `reflex_kv` directly with the appropriate namespace string.
