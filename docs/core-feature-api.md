# Core/Feature API

This branch introduces a dedicated core-facing feature API to isolate platform
bootstrapping from compile-time feature wiring.

## Goals

- Keep core boot logic stable even when feature flags evolve.
- Expose one runtime catalog for feature observability (`/api/plugins`, logs).
- Validate feature dependencies in a single place.
- Centralize all runtime feature queries in `FeatureCatalog`.

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
- `include/FeatureFlags.h`
  - Compile-time feature toggles (`ENABLE_*` macros).
  - Used by `FeatureCatalog` descriptors and `#if` guards in Registration units.

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
5. Register the feature in `CoreBootstrap.cpp` (`registerFeature()` call).
6. Update tests in `test/HostTests.cpp`.
