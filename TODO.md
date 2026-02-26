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
