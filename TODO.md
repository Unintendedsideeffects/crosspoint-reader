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
- [ ] Merge `fork-drift` → `master` once device smoke test passes (~20 commits ahead of master; all Android-facing APIs missing from master)

## i18n / translations

- [ ] Fill in missing translations for new keys (currently English fallback):
  - `STR_SLEEP_SOURCE` — missing in all non-English languages
  - `STR_FACTORY_RESET` — missing in RO, CA, UK, BE, IT, PL, FI, DA
  - `STR_THEME_FORK_DRIFT` — missing in BE, IT, PL, FI, DA
  - `STR_DARK_MODE`, `STR_DISPLAY_QR`, `STR_CUSTOMISE_STATUS_BAR` — several languages missing
- [ ] Clean up orphaned keys present in non-EN translations but absent from EN (`STR_STATUS_BAR_FULL_PERCENT` etc.) — add EN strings or remove orphans

## Technical debt

- [ ] `CROSSPOINT_VERSION` macro redefinition warnings at compile time — consolidate definition point
- [ ] Hardware smoke test — hook into Android app's self-hosted runner workflow for nightly end-to-end validation
