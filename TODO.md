## OTA Updates

- [x] Fix check for updates: fallback to latest when release tag 404
- [x] Add /api/ota/check endpoint and Check for updates to web UI
- [x] Fix device UI blocking: OTA HTTP work moved to dedicated worker task
- [x] Fix web UI blocking: /api/ota/check is now non-blocking (POST starts, GET polls)
- [x] Add HTTP timeout (15s) to prevent indefinite blocking
- [x] Increase OTA display task stack: 2048 → 4096

## Fork Drift Homescreen Theme

- [x] Add FORK_DRIFT to UI_THEME enum and settings wiring
- [x] Create ForkDriftTheme (3×2 cover grid, 2×2 button layout)
- [x] Wire HomeActivity for ForkDrift navigation and menu
- [x] Add i18n and configurator entries

## Configurator

- [x] Fix standard profile missing web_wifi_setup (disabled despite default:true and background_server being on)
- [x] Fix sizeKb mismatches blocking CI: epub_support, home_media_picker, hyphenation, web_pokedex_plugin, xtc_support

## Pre-ship / branch hygiene

- [ ] Flash `fork-drift` to hardware device (`pio run --target upload` or OTA from Android app)
- [ ] Smoke-test with Android app after flash: Wi-Fi API, USB serial, sleep cover pin/clear
  - Android app (ForkDriftApp) is now in shape for testing: release signing, API 34 CI, custom icon, improved empty states
- [ ] Merge `fork-drift` → `master` once device smoke test passes (1173 commits ahead of master)
- [ ] Commit uncommitted `lib/I18n/I18nKeys.h` style revert — `SORTED_LANGUAGE_INDICES` reformatted back to single-line (matches `42a791b` clang-format commit; was regenerated multi-line in `c2c6144`)

## i18n / translations

- [x] Fill in missing translations for new keys across 15 non-English language files:
  - `STR_SLEEP_SOURCE`, `STR_FACTORY_RESET`, `STR_THEME_FORK_DRIFT`, `STR_DARK_MODE`, `STR_BACKGROUND_SERVER_ON_CHARGE`
  - `STR_DISPLAY_QR` and entire `STR_CUSTOMISE_STATUS_BAR` section (13 languages)
  - `STR_VALIDATE_SLEEP_IMAGES` (new key, added to all 15 languages)
- [x] Clean up orphaned keys — removed `STR_SCREENSHOT_BUTTON` and `STR_STATUS_BAR_FULL_PERCENT` family from all affected languages
- [ ] `STR_HIDE` missing in many languages (English fallback used for status bar hide option)

## Technical debt

- [x] `CROSSPOINT_VERSION` macro redefinition warnings — removed redundant fallback `-D` flags from `platformio.ini`; script in `gen_version.py` is now the sole definition point for dynamic environments
- [ ] Hardware smoke test — `hardware-smoke-test.yml` scaffold exists in ForkDriftApp; needs self-hosted runner with device connected via USB and secrets `CROSSPOINT_DEVICE_HOST`, `CROSSPOINT_BLE_TARGET`, `CROSSPOINT_TEST_WIFI_SSID/PASSWORD`

## Sleep Images

- [x] Add "Validate Sleep Images" setting action — scans `/sleep` folder, reports valid image count, pre-populates cache for faster first sleep transition
