# CrossPoint Reader — Firmware Coding Rules

ESP32-C3 firmware. PlatformIO / ESP-IDF / FreeRTOS / Arduino framework.
Build: `pio run`. Host tests: `test/run_host_tests.sh`.

## Critical hardware patterns

### SPI mutex — MUST follow this exactly

All SD card access (`Storage.*`) must be wrapped in `SpiBusMutex::Guard guard;`.
**Never call `server->send()` while a guard is alive.** The guard must be in its own
scope block so the destructor fires before any network send.

```cpp
// CORRECT — explicit scope closes guard before network send
bool ok = false;
{
  SpiBusMutex::Guard guard;
  ok = Storage.mkdir(path);
}                          // guard destroyed here
server->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "fail");

// WRONG — guard still alive when send() is called
SpiBusMutex::Guard guard;
bool ok = Storage.mkdir(path);
server->send(ok ? 200 : 500, "text/plain", "...");  // deadlock risk
```

`scripts/check_hardware_patterns.py` (runs in CI) enforces this.

### Logging — use structured macros, never Serial

```cpp
// CORRECT
LOG_ERR("TAG", "Something failed: %s", reason);
LOG_WRN("TAG", "Non-fatal: %d", value);
LOG_INF("TAG", "Started");
LOG_DBG("TAG", "Detail: %s", info);

// WRONG
Serial.printf("Something failed: %s\n", reason);
```

Exception: `main.cpp` uses `Serial` directly for the screenshot binary protocol only.

### SD card access — use Storage, never SD directly

```cpp
// CORRECT — Storage manages the SPI mutex internally
Storage.mkdir(path);
Storage.exists(path);
Storage.openFileForRead(path);

// WRONG — bypasses SPI mutex management
SD.mkdir(path);
SD.open(path);
```

### Settings persistence — always check the return value

```cpp
// CORRECT
if (!SETTINGS.saveToFile()) {
  LOG_WRN("TAG", "Failed to persist settings to SD card");
}

// WRONG — silent failure; settings lost on next boot
SETTINGS.saveToFile();
```

## Architecture notes

- **Activities**: FreeRTOS tasks using `onEnter` / `loop` / `onExit` pattern (`src/activities/`).
- **Settings**: Binary format v4, 39 fields. `SETTINGS_COUNT` in `CrossPointSettings.cpp` must
  match the `writePod`/`writeString` call count in `saveToFile()`.
- **OTA**: `markFactoryResetPending()` writes `/.factory-reset-pending`; read by `applyPendingFactoryReset()` on boot.
- **Feature flags**: Defined in `include/FeatureFlags.h`. Cascade rules (e.g. disabling INTEGRATIONS
  also disables KOREADER_SYNC) live there. The configurator, CI workflow, and test scripts must all
  stay in sync — validated by `scripts/check_feature_key_sync.py`.

## Build environments

| env | purpose |
|-----|---------|
| `default` | development, LOG_LEVEL=debug |
| `gh_latest` | rolling dev channel, LOG_LEVEL=error |
| `gh_nightly` | nightly, LOG_LEVEL=error |
| `gh_release` | production release |
| `custom` | user-configured via `generate_build_config.py` |

`platformio-custom.ini` is generated — regenerate with:
```
python3 scripts/generate_build_config.py --profile standard
```

## Local clang LSP false positives

The local clang LSP has no ESP-IDF headers. Ignore errors for:
`LOG_*`, `FsFile`, `std::string` ("did you mean String"), `deserializeJson`.
The real build uses `pio run`.
