# Code Review Findings

## Critical Bugs
- [x] **BleWifiProvisioner**: WiFi QR code parsing fails on escaped characters (e.g. `\;` in SSID).
- [x] **BackgroundWebServer**: Falls back to blocking NTP sync on the main thread if task creation fails, freezing UI.
- [x] **HalPowerManager**: `Lock` only supports one lock at a time; concurrent locks fail, potentially causing performance regressions.
- [x] **KOReader Sync**: `ProgressMapper` uses 0-based indexing for `DocFragment`, breaking compatibility (KOReader expects 1-based).
- [x] **HomeActivity**: `bufferRestored` is used uninitialized in `render()`, causing unpredictable behavior/crashes.
- [x] **Bitmap**: Memory leak - `atkinsonDitherer` and `fsDitherer` are never deleted in destructor. (Verified they ARE deleted)
- [x] **Bitmap**: `colorsUsed` calculation for 4bpp images is hardcoded to 256 instead of 16.

## Safety & Performance
- [x] **Image Extraction**: `ChapterHtmlSlimParser` blindly overwrites existing cached images, causing unnecessary SD card writes.
- [x] **Heap Usage**: `CssParser` reads entire files into memory; optimized to avoid redundant copies by handling comments during parsing.
- [x] **Logging**: `ImageBlock` and `JpegToFramebufferConverter` use `Serial.printf` instead of the project `LOG_*` macros.
- [x] **PngToBmpConverter**: `parsePngHeaders` assumes IDAT follows immediately after PLTE/tRNS; fails on standard PNGs with other metadata chunks. (Verified it correctly skips unknown chunks)
- [x] **HomeActivity**: `loadRecentCovers` accesses SD card without `SpiBusMutex` protection (race condition with display task).
- [x] **TodoActivity**: `loadTasks` reads file byte-by-byte; optimized with buffered reading.

## Regressions
- [x] **Recent Books**: Metadata (title/author) is lost/empty when migrating from older store versions.
- [x] **EPUB Covers**: Guide-based cover fallback was removed, breaking covers for older EPUBs.
- [x] **Navigation**: `ReaderActivity` ignores `fromTab` parameter.
- [x] **HomeActivity**: `loadRecentBooks` no longer respects `homeRecentBooksCount` limit from theme metrics.

## Code Quality
- [x] **Dead Code**: `RecentBooksStore::getDataFromBook` is unused. (Fixed by restoring usage)
