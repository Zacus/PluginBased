# Build And Packaging Hardening

## Context

The arm64 macOS Release build completes and produces a valid DMG, but a packaged startup loads zero business plugins because the bundle omits `plugins.json`, plugin metadata sidecars, and theme JSON files. Release CTest also removes side-effecting `assert(...)` expressions through `NDEBUG`, causing seven crashes and making other tests silently ineffective. The packaging log additionally shows incorrect Homebrew Qt paths, non-idempotent rpath edits, a macOS/Linux platform-definition mismatch, and avoidable compiler/linker warnings.

## Goals

- Produce a self-contained macOS bundle containing the plugin manifest, metadata, themes, QML modules, libraries, and Qt runtime plugins.
- Resolve Qt plugin/QML/library locations from Qt's own query tool when available, with a layout-compatible fallback.
- Make repeated rpath repair and repeated packaging deterministic and free of ignored duplicate-rpath errors.
- Keep assertions active for test targets in Release without changing production target assertion policy.
- Identify macOS as a POSIX crash-handler platform rather than Linux.
- Remove the observed CoreAudio ignored-result warning, Qt QML policy warnings, and duplicate PlayPlugin SDK links where the dependency is already transitive.

## Non-Goals

- No plugin ABI/API changes.
- No media-pipeline ownership, threading, playback, or rendering behavior changes.
- No migration to CMake install/deploy APIs in this repair.
- No replacement of the existing assert-based test suite with a new test framework.

## Proposed Design

`tools/deploy.py` will expose small helpers for Qt install paths and application runtime resources. Platform packagers will call the same resource-copy helper so the manifest, sidecars, and themes follow each platform's existing runtime layout. Qt whitelist entries retain their `plugins/`, `qml/`, or `frameworks/` category when choosing both source and destination.

The macOS rpath helper will parse current `LC_RPATH` entries, remove only absolute build paths, and add the bundle Frameworks rpath only when absent. Failures that are not benign will remain visible.

A test-only CMake interface target will undefine `NDEBUG`. Each directory that creates executable tests will link this target only to those tests, preserving Release production semantics. Existing Release crashes are the functional regression test; a small configuration check will guard the target wiring.

macOS and Linux will share the existing signal/backtrace implementation under a neutral POSIX compile definition and log message. QML policy choices will be explicit at the top level. The CoreAudio callback will explicitly discard the diagnostic read result, and redundant direct PlayPlugin links will be removed only after confirming they are provided by `media_sdk_playback_session`.

## Compatibility And Invariants

- C++17 and Qt 6 remain unchanged.
- Plugin filenames, manifest schema, sidecar schema, and lookup locations remain unchanged.
- Production targets keep their normal Release `NDEBUG` behavior.
- The audio callback remains allocation-free apart from pre-existing behavior and does not add logging or locking.
- Packaging remains invokable through `./package.sh` with the current arguments.

## Alternatives

- Switching to `cmake --install` plus Qt deployment APIs would reduce custom deployment code, but it is a larger cross-platform migration and is deferred.
- Copying only the missing JSON files would restore plugins but leave Qt path and repeatability failures intact.

## Verification

- Focused Python packaging tests with temporary fake Qt/build trees.
- Release rerun of the seven previously crashing tests, followed by all 53 tests.
- Debug all-test run.
- Release build with warning review.
- Two consecutive macOS package runs.
- Dependency, code-signing, and DMG checksum verification.
- Cocoa startup confirming themes resolve inside `Contents/Resources/themes` and two plugins load.
