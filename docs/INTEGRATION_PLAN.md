# Feature Picker Integration Plan

## Overview

This document outlines the atomic component structure for integrating the Feature Picker system into fork-drift. Each component is independent and can be reviewed/merged separately.

## Dependency Graph

```
Core Infrastructure (1)
    ↓
Feature Flags Layer (2a, 2b, 2c, 2d)
    ↓
Build System (3a, 3b)
    ↓
Documentation (4)
```

---

## Component 1: Core Feature Flag Infrastructure

**Purpose:** Establish the base infrastructure for feature flags without changing existing behavior.

**Files:**
- `scripts/generate_build_config.py` (NEW)
- `platformio.ini` (minimal change - just document the pattern)

**What it does:**
- Adds Python script to generate build configurations
- Defines the feature flag pattern and conventions
- No changes to C++ code yet
- All features default to ENABLED (1)

**Testing:**
```bash
python scripts/generate_build_config.py --list-features
python scripts/generate_build_config.py --preset full
# Should generate config with all flags = 1
```

**Review focus:**
- Script logic correctness
- Feature definitions accuracy
- Size estimates validation

**Merge safety:** ✅ SAFE - No behavior changes, pure addition

---

## Component 2a: Extended Fonts Feature Flag

**Purpose:** Make extended fonts optional (replaces OMIT_FONTS).

**Files:**
- `src/main.cpp` (MODIFY)

**Changes:**
```cpp
// Add at top of file
#ifndef ENABLE_EXTENDED_FONTS
#define ENABLE_EXTENDED_FONTS 1  // Default: enabled
#endif

// Replace: #ifndef OMIT_FONTS
// With:    #if ENABLE_EXTENDED_FONTS
```

**What becomes optional:**
- Bookerly 12, 16, 18pt fonts
- Noto Sans 12, 14, 16, 18pt fonts
- OpenDyslexic 8, 10, 12, 14pt fonts

**What stays always-on:**
- Bookerly 14pt (default reading font)
- Ubuntu UI fonts
- Noto Sans 8pt (small text)

**Testing:**
```bash
# Test with flag enabled (default)
pio run -e default
# Test with flag disabled
python scripts/generate_build_config.py --preset minimal
pio run -e custom
```

**Expected size savings:** ~300KB when disabled

**Review focus:**
- Guards are complete (no compilation errors)
- Default fonts still available
- Settings menu gracefully hides unavailable fonts

**Merge safety:** ✅ SAFE - Default behavior unchanged (flag = 1)

---

## Component 2b: PNG/JPEG Image Sleep Feature Flag

**Purpose:** Make PNG/JPEG sleep image support optional (BMP stays always-on).

**Files:**
- `lib/Epub/Epub/converters/ImageDecoderFactory.h` (MODIFY)
- `lib/Epub/Epub/converters/ImageDecoderFactory.cpp` (MODIFY)
- `src/activities/boot_sleep/SleepActivity.cpp` (MODIFY)

**Changes:**
```cpp
#ifndef ENABLE_IMAGE_SLEEP
#define ENABLE_IMAGE_SLEEP 1  // Default: enabled
#endif

#if ENABLE_IMAGE_SLEEP
  // PNG/JPEG decoder includes and instantiation
#endif
```

**What becomes optional:**
- PNGdec library
- picojpeg library
- `.png`, `.jpg`, `.jpeg` sleep image support

**What stays always-on:**
- BMP image support (via display library)
- Cover-based sleep screens
- Default sleep screen

**Testing:**
```bash
# Test BMP-only mode
python scripts/generate_build_config.py --enable extended_fonts
pio run -e custom
# Verify: PNG/JPEG files ignored, BMP files work
```

**Expected size savings:** ~140KB when disabled

**Review focus:**
- BMP support unaffected
- getSupportedFormats() returns correct extensions
- No crashes with PNG/JPEG files when disabled

**Merge safety:** ✅ SAFE - Default behavior unchanged (flag = 1)

---

## Component 2c: Markdown/Obsidian Feature Flag

**Purpose:** Make Markdown rendering optional.

**Files:**
- `src/activities/reader/ReaderActivity.h` (MODIFY)
- `src/activities/reader/ReaderActivity.cpp` (MODIFY)
- `src/main.cpp` (MODIFY - Todo feature)

**Changes:**
```cpp
#ifndef ENABLE_MARKDOWN
#define ENABLE_MARKDOWN 1  // Default: enabled
#endif

#if ENABLE_MARKDOWN
  #include "Markdown.h"
  #include "MarkdownReaderActivity.h"
#endif
```

**What becomes optional:**
- md4c parser library
- HTML/Markdown rendering
- Obsidian features (wikilinks, callouts, embeds)
- `.md` file opening

**What stays always-on:**
- EPUB reader
- TXT reader
- XTC reader

**Graceful degradation:**
- Opening `.md` file shows user-friendly error message
- Todo feature defaults to `.txt` when disabled
- No crashes or confusing behavior

**Testing:**
```bash
# Test Markdown disabled
python scripts/generate_build_config.py --enable extended_fonts --enable image_sleep
pio run -e custom
# Verify: .md files show error, .txt files work
```

**Expected size savings:** ~560KB when disabled

**Review focus:**
- Error message is clear and helpful
- Todo feature fallback works
- No compilation errors
- Header guards are complete

**Merge safety:** ✅ SAFE - Default behavior unchanged (flag = 1)

---

## Component 2d: Background Server Feature Flag

**Purpose:** Make background web server optional.

**Files:**
- `src/network/BackgroundWebServer.cpp` (MODIFY)

**Changes:**
```cpp
#ifndef ENABLE_BACKGROUND_SERVER
#define ENABLE_BACKGROUND_SERVER 1  // Default: enabled
#endif

void BackgroundWebServer::loop(...) {
#if !ENABLE_BACKGROUND_SERVER
  return;  // No-op when disabled
#endif
  // ... existing implementation
}
```

**What becomes optional:**
- Background server during reading
- WiFi staying connected while reading

**What stays always-on:**
- Web server in Home/Library views
- File upload functionality
- OTA updates

**Testing:**
```bash
# Test server disabled
python scripts/generate_build_config.py --preset minimal
pio run -e custom
# Verify: Server works in Home, stops in Reader
```

**Expected size savings:** ~5KB when disabled

**Review focus:**
- Server still works in non-reader activities
- Clean shutdown behavior
- No memory leaks

**Merge safety:** ✅ SAFE - Default behavior unchanged (flag = 1)

---

## Component 3a: GitHub Actions Build Workflow

**Purpose:** Enable cloud-based custom firmware builds.

**Files:**
- `.github/workflows/build-custom.yml` (NEW)

**What it does:**
- Workflow dispatch with feature toggles
- Preset selection (minimal/standard/full)
- Automated build and artifact upload
- Size reporting in job summary

**Dependencies:**
- Component 1 (generate_build_config.py)
- Components 2a-2d (feature flags)

**Testing:**
```bash
# Manual trigger from Actions tab
# Select "standard" preset
# Verify build completes and artifact downloads
```

**Review focus:**
- Workflow syntax correctness
- Build flag generation
- Artifact naming and retention
- Error handling

**Merge safety:** ✅ SAFE - Optional workflow, doesn't affect existing builds

---

## Component 3b: Web Configurator UI

**Purpose:** Provide user-friendly web interface for feature selection.

**Files:**
- `docs/configurator/index.html` (NEW)

**What it does:**
- Interactive feature picker UI
- Real-time size calculator
- Preset buttons
- Build flag generation
- Direct link to GitHub Actions

**Dependencies:**
- Component 1 (for flag definitions)
- Component 3a (GitHub Actions workflow)

**Testing:**
```bash
# Open in browser (via GitHub Pages or file://)
# Toggle features
# Verify size calculations
# Test preset buttons
# Click "Build" button - should open GitHub Actions
```

**Review focus:**
- UI/UX quality
- Size calculation accuracy
- Repository URL detection
- Mobile responsiveness

**Merge safety:** ✅ SAFE - Static HTML, no backend, optional feature

---

## Component 4: Documentation

**Purpose:** Comprehensive user and developer documentation.

**Files:**
- `README.md` (MODIFY - add Custom Builds section)
- `docs/BUILD_CONFIGURATION.md` (NEW)
- `docs/FEATURE_PICKER_TEST_PLAN.md` (NEW)
- `FEATURE_PICKER_IMPLEMENTATION.md` (NEW)

**What it covers:**
- Feature descriptions and size impacts
- Build presets explanation
- Local build instructions
- GitHub Actions workflow usage
- Troubleshooting guide
- Test plan checklist

**Dependencies:**
- All previous components (documents the complete system)

**Review focus:**
- Clarity and completeness
- Accuracy of instructions
- Link validity
- Example commands correctness

**Merge safety:** ✅ SAFE - Documentation only, no code changes

---

## Recommended Merge Order

### Phase 1: Foundation (Low Risk)
1. **Component 1** - Core infrastructure (script only)
2. **Component 4** - Documentation (can be merged anytime)

### Phase 2: Feature Flags (Medium Risk - Test Each)
3. **Component 2a** - Extended Fonts (smallest change)
4. **Component 2b** - Image Sleep (library dependencies)
5. **Component 2d** - Background Server (minimal impact)
6. **Component 2c** - Markdown (largest change, test thoroughly)

### Phase 3: User-Facing Tools (Low Risk)
7. **Component 3a** - GitHub Actions workflow
8. **Component 3b** - Web configurator

### Alternative: Atomic Commits Approach

If you prefer smaller commits, further break down each component:

**Component 2b example:**
- Commit 1: Add flag to ImageDecoderFactory.h
- Commit 2: Add flag to ImageDecoderFactory.cpp
- Commit 3: Update SleepActivity.cpp extensions array
- Commit 4: Update tests (if any)

---

## Integration with fork-drift

### Before Merging

1. **Rebase on latest fork-drift:**
   ```bash
   git fetch origin
   git rebase origin/fork-drift
   ```

2. **Test default build:**
   ```bash
   pio run -e default
   # Verify size hasn't increased
   ```

3. **Test minimal build:**
   ```bash
   python scripts/generate_build_config.py --preset minimal
   pio run -e custom
   # Verify ~1MB savings
   ```

### During Merge

- Merge each component as a separate PR (preferred)
- Or merge all at once with clear commit structure
- Tag feature flag commits with `[feature-flags]`
- Tag infrastructure commits with `[build-system]`

### After Merge

1. **Enable GitHub Pages:**
   - Repository Settings → Pages
   - Source: Deploy from branch
   - Branch: `fork-drift` / `docs`

2. **Update Web Configurator:**
   - Verify repo URL detection works
   - Test workflow link opens correctly

3. **Create Release Notes:**
   - Document new build options
   - Link to Feature Picker
   - Explain presets

---

## Conflict Resolution Strategy

### Expected Conflicts

1. **src/main.cpp:**
   - Font initialization section
   - Solution: Keep fork-drift's fonts, add guards

2. **platformio.ini:**
   - Build flags section
   - Solution: Merge both, keep fork-drift's flags

3. **README.md:**
   - Features section
   - Solution: Add Custom Builds section after Installing

### Conflict Priorities

1. **Keep fork-drift's features** - Don't remove anything
2. **Add guards around new features** - Make them optional
3. **Default to enabled** - All flags default to 1

---

## Testing Matrix

| Configuration | EPUB | TXT | MD | Fonts | Sleep | Size |
|--------------|------|-----|----|----|-------|------|
| Full | ✓ | ✓ | ✓ | All | All | ~6.6MB |
| Standard | ✓ | ✓ | ✗ | All | All | ~6.2MB |
| Minimal | ✓ | ✓ | ✗ | 14pt | BMP | ~5.5MB |
| Custom 1 | ✓ | ✓ | ✓ | 14pt | BMP | ~5.9MB |
| Custom 2 | ✓ | ✓ | ✗ | All | BMP | ~5.8MB |

Test each configuration:
- [ ] Builds without errors
- [ ] Boots on device
- [ ] Core features work
- [ ] Disabled features show proper errors
- [ ] Size matches estimate

---

## Rollback Plan

If issues arise post-merge:

1. **Feature flag broken:**
   - Quick fix: Set default to 1 in code
   - Revert: Revert specific component commit

2. **Build system broken:**
   - Disable custom environment
   - Remove platformio-custom.ini from builds

3. **Critical bug:**
   - Revert entire feature picker merge
   - All commits are atomic and reversible

---

## Success Criteria

- [ ] All components merge cleanly
- [ ] Default build unchanged (size, features)
- [ ] Minimal build saves ~1MB
- [ ] All tests pass
- [ ] Documentation complete
- [ ] GitHub Pages live
- [ ] No regressions reported

---

## Future Enhancements

After successful merge, consider:

1. **Runtime feature detection** - Settings → About screen
2. **Pre-built firmware assets** - Common configs as releases
3. **More feature flags** - OPDS, Web UI, etc.
4. **Build size dashboard** - Track size over time
5. **One-click builds** - Web service with GitHub API

---

**Last Updated:** 2026-02-05
**Status:** Ready for integration
**Integration Branch:** fork-drift
