# Code Review Findings

## Critical Bugs
- [x] **BleWifiProvisioner**: WiFi QR code parsing fails on escaped characters (e.g. `\;` in SSID).
- [x] **BackgroundWebServer**: Falls back to blocking NTP sync on the main thread if task creation fails, freezing UI.
- [x] **HalPowerManager**: `Lock` only supports one lock at a time; concurrent locks fail, potentially causing performance regressions.
- [x] **KOReader Sync**: `ProgressMapper` uses 0-based indexing for `DocFragment`, breaking compatibility (KOReader expects 1-based).

## Safety & Performance
- [x] **Image Extraction**: `ChapterHtmlSlimParser` blindly overwrites existing cached images, causing unnecessary SD card writes.
- [x] **Heap Usage**: `CssParser` reads entire files into memory; optimized to avoid redundant copies by handling comments during parsing.
- [x] **Logging**: `ImageBlock` and `JpegToFramebufferConverter` use `Serial.printf` instead of the project `LOG_*` macros.

## Regressions
- [x] **Recent Books**: Metadata (title/author) is lost/empty when migrating from older store versions.
- [x] **EPUB Covers**: Guide-based cover fallback was removed, breaking covers for older EPUBs.
- [x] **Navigation**: `ReaderActivity` ignores `fromTab` parameter.

## Code Quality
- [x] **Dead Code**: `RecentBooksStore::getDataFromBook` is unused. (Fixed by restoring usage)
