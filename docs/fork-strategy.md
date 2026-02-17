# Fork Strategy & Branch Management

This document outlines the relationship between this repository and the upstream [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) project, and explains the purpose of the `fork-drift` branch.

## Why This Fork Exists

While the upstream CrossPoint Reader project provides a stable and high-quality firmware for the Xteink X4, this fork exists to:

1.  **Maintain the Feature Picker:** Support the [web-based build configurator](https://unintendedsideeffects.github.io/crosspoint-reader/configurator/) which allows users to generate custom firmware builds.
2.  **Accelerate Feature Iteration:** Provide a staging ground for experimental features, community-driven enhancements, and rapid bug fixes that may not yet be ready for the main upstream repository.
3.  **Experimental Drift:** Enable architectural experiments and "drifting" features that prioritize specific community needs or hardware optimizations.

## Upstream Relationship

This repository maintains a close relationship with the [official upstream repository](https://github.com/crosspoint-reader/crosspoint-reader).

-   The `master` branch is kept synchronized with the upstream `master` branch.
-   Stable features developed in this fork are periodically submitted to the upstream project via Pull Requests.
-   Upstream changes are regularly merged into the `fork-drift` branch to ensure compatibility.

## Branch Strategy

### `master`
The `master` branch is the stable base of this repository. It is intended to remain in sync with the upstream project. Use this branch if you want the most stable experience equivalent to the official release.

### `fork-drift`
The `fork-drift` branch is the primary active development branch for this fork. It contains:
-   **Active Bug Fixes:** Patches for issues currently being tracked in [issues.md](../issues.md).
-   **New Features:** Experimental plugins and UI enhancements (e.g., `home_media_picker`).
-   **Drifting Changes:** Modifications that deviate from upstream to support specific fork-only capabilities like the web configurator integration.

**Note:** The `fork-drift` branch may experience frequent changes and is where most active development occurs.

## Contribution Workflow

If you would like to contribute to this fork:

1.  **Target the `fork-drift` branch:** All new features and bug fixes should be submitted as Pull Requests against the `fork-drift` branch.
2.  **Synchronize with Upstream:** If your change is a general improvement intended for all CrossPoint users, we will work with you to prepare it for upstreaming after it has been validated in `fork-drift`.
3.  **Issue Tracking:** Please refer to [issues.md](../issues.md) for current priorities and known regressions being addressed in this fork.

---

For more details on building custom firmware, see [BUILD_CONFIGURATION.md](BUILD_CONFIGURATION.md).
