# Core/Feature API

This branch introduces a dedicated core-facing feature API to isolate platform
bootstrapping from compile-time feature wiring.

## Goals

- Keep core boot logic stable even when feature flags evolve.
- Expose one runtime catalog for feature observability (`/api/plugins`, logs).
- Validate feature dependencies in a single place.
- Preserve existing `FeatureManifest` call sites for compatibility.

## Components

- `src/core/features/FeatureCatalog.h/.cpp`
  - Owns the canonical runtime list of features.
  - Exposes lookup, enabled count, build string, JSON export.
  - Validates `requiresAll` and `requiresAny` dependency rules.
- `src/core/features/FeatureLifecycle.h/.cpp`
  - Provides explicit startup hook points for optional features.
  - Uses a registry table keyed by feature id (`kLifecycleHooks`) so startup
    work for each feature is declarative and does not require touching
    `main.cpp` or switch-style dispatch code.
  - `main.cpp` calls each stage once in order; each hook internally gates
    its work behind compile-time feature flags.
  - Stages: `onStorageReady`, `onSettingsLoaded`, `onFontSetup`.
- `src/core/CoreBootstrap.h/.cpp`
  - Initializes and validates the feature system from core setup.
  - Stores initialization status for future startup policies.
- `src/FeatureManifest.h`
  - Keeps `hasX()` compile-time helpers.
  - Delegates runtime methods to `core::FeatureCatalog`.

## Startup Integration

`main.cpp` calls three lifecycle stages after the relevant system is ready:

```cpp
core::CoreBootstrap::initializeFeatureSystem(usbConnectedAtBoot);
// ... SD card init ...
core::FeatureLifecycle::onStorageReady();
// ... settings load ...
core::FeatureLifecycle::onSettingsLoaded(renderer);
// ... inside setupDisplayAndFonts(), after builtin fonts ...
core::FeatureLifecycle::onFontSetup(renderer);
```

Feature-specific `#if` blocks for startup (user fonts, dark mode, KOReader
credentials) live inside `FeatureLifecycle.cpp` rather than scattered
across `main.cpp`.

## Adding a Feature

1. Add the compile-time flag in `include/FeatureFlags.h`.
2. Add the descriptor entry in `FeatureCatalog.cpp`.
3. Add dependency metadata (`requiresAll` / `requiresAny`) if needed.
4. Add startup behavior to the appropriate `FeatureLifecycle` hook.
5. Add/update `FeatureManifest::hasX()` compatibility helper.
6. Update tests in `test/HostTests.cpp`.
