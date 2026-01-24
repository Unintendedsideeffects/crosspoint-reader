# CrossPoint + Obsidian Sync - Next Steps

## 1) Finish firmware background server integration
- Wire `BackgroundWebServer::getInstance().loop(...)` into `src/main.cpp` so it runs when USB is connected.
- Gate it behind a new settings toggle (on/off) to avoid unexpected WiFi usage.
- Respect foreground ownership: if File Transfer is active, background server should pause.
- Prevent auto-sleep and keep fast loop delay while background server is running.

## 2) Settings + UI
- Add a setting in `SettingsActivity` to enable/disable "Background server when charging."
- Optional: add a small status indicator (WiFi/charging) in the UI when background server is active.

## 3) Reliability + retry behavior
- Confirm retry interval and scan/connect timeouts in `BackgroundWebServer`.
- Ensure no AP mode is started in background (STA only).
- Validate that WiFi is powered down cleanly when USB disconnects.

## 4) Obsidian sync workflow
- Tag target notes with `xte: true` or `#XTE`.
- Confirm frontmatter fields: `xte_output`, `xte_target_path`, `xte_last_synced`, `xte_last_hash`.
- Decide whether to add a systemd user service for the two watcher scripts.

## 5) Documentation
- Document the Obsidian -> EPUB -> upload pipeline in the vault.
- Add a short section to the CrossPoint docs describing background server behavior.
