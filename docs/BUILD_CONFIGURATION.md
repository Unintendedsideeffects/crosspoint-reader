# Build Configuration Guide

CrossPoint Reader supports customizable firmware builds, allowing you to include only the features you need and save precious flash memory space on your ESP32-C3 device.

## Table of Contents

- [Quick Start](#quick-start)
- [Feature Reference](#feature-reference)
- [Build Presets](#build-presets)
- [Local Build Instructions](#local-build-instructions)
- [GitHub Actions Builds](#github-actions-builds)
- [Flash Memory Considerations](#flash-memory-considerations)
- [Troubleshooting](#troubleshooting)

## Quick Start

### Using the Feature Picker (Easiest)

1. Visit [Feature Picker](https://unintendedsideeffects.github.io/crosspoint-reader/configurator/)
2. Select your desired features or choose a preset
3. Click "Build on GitHub Actions"
4. Wait ~5-10 minutes for the build to complete
5. Download the firmware artifact and flash to your device

### Using Command Line (Local Builds)

```bash
# Generate configuration for standard preset
python scripts/generate_build_config.py --preset standard

# Build custom firmware
pio run -e custom

# Flash to device
pio run -e custom --target upload
```

## Feature Reference

### Extended Fonts

**Flag:** `ENABLE_EXTENDED_FONTS`
**Size Impact:** ~300KB
**Default:** Enabled

Includes additional font sizes and OpenDyslexic font support for better reading accessibility.

**What's included when enabled:**
- Bookerly 12pt, 16pt, 18pt (Regular, Bold, Italic, Bold-Italic)
- Noto Sans 12pt, 14pt, 16pt, 18pt (Regular, Bold, Italic, Bold-Italic)
- OpenDyslexic 8pt, 10pt, 12pt, 14pt (Regular, Bold, Italic, Bold-Italic)

**What's always included:**
- Bookerly 14pt (default reading font)
- Ubuntu 10pt, 12pt (UI fonts)
- Noto Sans 8pt (small UI text)

**When disabled:**
- Only the default 14pt reading size and UI fonts are available
- Settings will not show unavailable font sizes

**Use case:** Disable if you only read at 14pt and want maximum space savings.

---

### PNG/JPEG Sleep Images

**Flag:** `ENABLE_IMAGE_SLEEP`
**Size Impact:** ~140KB
**Default:** Enabled

Enables PNG and JPEG format support for custom sleep screen images.

**What's included when enabled:**
- PNG decoder (using PNGdec library)
- JPEG decoder (using picojpeg library)
- Support for `.png`, `.jpg`, `.jpeg` sleep images

**What's always included:**
- BMP image support (native to display library)
- Cover-based sleep screens (from EPUB covers)

**When disabled:**
- Sleep images folder only accepts `.bmp` files
- PNG/JPEG images in the sleep folder are ignored
- Cover sleep screens still work (covers are converted to display format)

**Use case:** Disable if you only use BMP sleep images or cover-based sleep screens.

---

### Markdown/Obsidian

**Flag:** `ENABLE_MARKDOWN`
**Size Impact:** ~560KB
**Default:** Enabled

Full Markdown rendering with Obsidian vault compatibility.

**What's included when enabled:**
- Markdown parser (md4c library)
- HTML renderer for Markdown
- Obsidian-specific features:
  - Wikilinks (`[[Page]]`)
  - Callouts (note, warning, tip, etc.)
  - Embedded notes
  - Metadata frontmatter
- Markdown extensions:
  - Tables
  - Task lists
  - Strikethrough
  - Footnotes

**When disabled:**
- `.md` files show "Markdown support not available" message
- Todo feature defaults to `.txt` format instead of `.md`
- Obsidian vaults cannot be opened

**Use case:** Disable if you only read EPUB/TXT files and never use Markdown notes.

---

### Background Web Server

**Flag:** `ENABLE_BACKGROUND_SERVER`
**Size Impact:** ~5KB
**Default:** Enabled

Keeps the WiFi file management server running in the background while reading.

**What's included when enabled:**
- Background web server continues running during reading
- File uploads possible while reading a book
- WiFi stays connected in reading mode (if USB plugged in)

**When disabled:**
- Web server only runs in Home/Library views
- Reading automatically stops the web server
- Minimal power/memory impact

**Use case:** Disable for slightly lower memory usage if you never upload files while reading.

---

## Build Presets

### Minimal Preset

**Size:** ~5.5MB (~1.1MB savings from full build)

```bash
python scripts/generate_build_config.py --preset minimal
```

**Features:**
- ✗ Extended Fonts
- ✗ PNG/JPEG Sleep
- ✗ Markdown/Obsidian
- ✗ Background Server

**Best for:**
- Devices with very limited flash space
- Users who only need basic EPUB reading at 14pt
- Maximum storage for books

---

### Standard Preset (Recommended)

**Size:** ~6.2MB

```bash
python scripts/generate_build_config.py --preset standard
```

**Features:**
- ✓ Extended Fonts
- ✓ PNG/JPEG Sleep
- ✗ Markdown/Obsidian
- ✗ Background Server

**Best for:**
- Most users
- Good balance of features and flash space
- Includes essential reading features

---

### Full Preset

**Size:** ~6.6MB (current default)

```bash
python scripts/generate_build_config.py --preset full
```

**Features:**
- ✓ Extended Fonts
- ✓ PNG/JPEG Sleep
- ✓ Markdown/Obsidian
- ✓ Background Server

**Best for:**
- Users who want all features
- Devices with adequate flash space remaining
- Power users who use Markdown/Obsidian

---

## Local Build Instructions

### Prerequisites

- PlatformIO Core or VS Code with PlatformIO IDE
- Python 3.11+
- USB-C cable for flashing

### Generate Configuration

The `generate_build_config.py` script creates a `platformio-custom.ini` file with your selected features.

**Using presets:**
```bash
# Minimal build
python scripts/generate_build_config.py --preset minimal

# Standard build
python scripts/generate_build_config.py --preset standard

# Full build
python scripts/generate_build_config.py --preset full
```

**Custom feature selection:**
```bash
# Start from minimal, add specific features
python scripts/generate_build_config.py --enable extended_fonts --enable image_sleep

# Start from full, remove specific features
python scripts/generate_build_config.py --preset full --disable markdown

# Enable only markdown
python scripts/generate_build_config.py --enable markdown
```

**List available features:**
```bash
python scripts/generate_build_config.py --list-features
```

### Build and Flash

```bash
# Build the custom firmware
pio run -e custom

# Build and flash in one command
pio run -e custom --target upload

# Build and monitor serial output
pio run -e custom --target upload --target monitor
```

### Verify Build Size

After building, check the firmware size:

```bash
# On Linux/macOS
ls -lh .pio/build/custom/firmware.bin

# Or use PlatformIO
pio run -e custom -t size
```

---

## GitHub Actions Builds

GitHub Actions provides cloud-based builds without requiring local build tools.

### Using the Feature Picker

1. Go to [Feature Picker](https://unintendedsideeffects.github.io/crosspoint-reader/configurator/)
2. Configure your features
3. Click "Build on GitHub Actions"
4. Sign in to GitHub if prompted
5. Run the workflow
6. Wait for the build (typically 5-10 minutes)
7. Download the artifact from the Actions page

### Manual Workflow Trigger

1. Go to your fork's Actions tab
2. Select "Build Custom Firmware" workflow
3. Click "Run workflow"
4. Select your branch
5. Choose preset or toggle individual features
6. Click "Run workflow"
7. Wait for completion
8. Download the `custom-firmware` artifact

### Artifact Contents

The downloaded artifact contains:
- `firmware.bin` - Flash this to your device
- `partitions.bin` - Partition table (usually not needed for OTA)
- `platformio-custom.ini` - Configuration used for this build

---

## Flash Memory Considerations

### ESP32-C3 Flash Layout

The ESP32-C3 in the Xteink X4 has:
- **Total flash:** 16MB
- **Available for firmware:** ~6.4MB
- **Firmware partition:** 6MB (0x600000 bytes)
- **OTA partition:** 6MB (second firmware slot)

### Size Guidelines

| Build Type | Size | Flash Usage | Books Space |
|------------|------|-------------|-------------|
| Minimal | ~5.5MB | 86% | Maximum |
| Standard | ~6.2MB | 97% | Good |
| Full | ~6.6MB | 103%* | Tight |

*Note: Full build exceeds partition size but compression may allow it to fit. Test before deploying.

### Tips for Managing Flash Space

1. **Start with Standard preset** - best balance for most users
2. **Disable unused features** - save space for more books
3. **Use BMP sleep images** - if you don't need PNG/JPEG
4. **Skip Markdown** - largest single feature at ~560KB
5. **Monitor OTA updates** - custom builds may be larger than default

---

## Troubleshooting

### Build Fails with "firmware.bin is too large"

**Problem:** The configured features result in firmware larger than the partition.

**Solutions:**
1. Disable one or more features
2. Use a smaller preset (Standard instead of Full)
3. Specifically disable large features like Markdown (~560KB)

Example:
```bash
python scripts/generate_build_config.py --preset standard
```

### Feature Not Working After Flash

**Problem:** A feature you expected to be enabled isn't working.

**Check:**
1. Verify the feature was enabled in `platformio-custom.ini`
2. Rebuild and flash again
3. Clear device settings (may have cached old behavior)

### Markdown Files Show Error

**Problem:** "Markdown support not available" message appears.

**Cause:** `ENABLE_MARKDOWN=0` in your build.

**Solution:** Rebuild with Markdown enabled:
```bash
python scripts/generate_build_config.py --enable markdown
pio run -e custom --target upload
```

### Sleep Images Not Loading

**Problem:** PNG/JPEG sleep images not displaying.

**Cause:** `ENABLE_IMAGE_SLEEP=0` in your build.

**Solutions:**
1. Rebuild with image sleep enabled:
   ```bash
   python scripts/generate_build_config.py --enable image_sleep
   pio run -e custom --target upload
   ```
2. Or convert your images to BMP format (works in all builds)

### GitHub Actions Build Fails

**Problem:** Workflow fails during build.

**Common causes:**
1. Invalid feature combination (rare)
2. Branch has compilation errors
3. Repository not properly forked

**Solutions:**
1. Check the Actions log for specific errors
2. Try a known-good preset (standard)
3. Ensure your fork is up to date with upstream

### How to Check Current Build Configuration

Unfortunately, there's no runtime feature detection yet. To know what features are in your current firmware:

1. Check the GitHub Actions build summary (if built on Actions)
2. Check your local `platformio-custom.ini` file
3. Try using a feature - disabled features show error messages

*Future enhancement: Settings → About screen will show enabled features*

---

## Advanced Usage

### Modifying Build Flags Directly

If you need fine-grained control, edit `platformio-custom.ini` directly:

```ini
[env:custom]
extends = base
build_flags =
  ${base.build_flags}
  -DCROSSPOINT_VERSION="${crosspoint.version}-custom"
  -DENABLE_EXTENDED_FONTS=1
  -DENABLE_IMAGE_SLEEP=1
  -DENABLE_MARKDOWN=0
  -DENABLE_BACKGROUND_SERVER=0
```

### Adding Your Own Feature Flags

To add a new optional feature:

1. Add the flag definition in your code:
   ```cpp
   #ifndef ENABLE_MY_FEATURE
   #define ENABLE_MY_FEATURE 1
   #endif
   ```

2. Wrap the feature code:
   ```cpp
   #if ENABLE_MY_FEATURE
   // Feature implementation
   #endif
   ```

3. Add to `generate_build_config.py`:
   ```python
   'my_feature': Feature(
       name='My Feature',
       flag='ENABLE_MY_FEATURE',
       size_kb=100,
       description='My custom feature'
   )
   ```

4. Update presets as needed

---

## Related Documentation

- [README.md](../README.md) - Main project documentation
- [USER_GUIDE.md](../USER_GUIDE.md) - User guide for operating CrossPoint
- [Feature Picker](https://unintendedsideeffects.github.io/crosspoint-reader/configurator/) - Web-based configuration tool

---

**Questions or Issues?**

- Check [Troubleshooting](#troubleshooting) section above
- Open an issue on GitHub
- Consult the main [README.md](../README.md)
