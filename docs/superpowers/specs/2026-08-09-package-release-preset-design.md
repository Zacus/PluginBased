# Package Release Preset Design

## Context

The project now defines `debug` and `release` CMake Presets. The current packaging entry point still defaults to `build/` and invokes `cmake --build <dir> --config Release`. With a single-config generator, `--config Release` does not convert an already configured Debug tree into a Release tree, so the default packaging command can package Debug artifacts.

## Decision

Make `release` the packaging script's default Preset and `build-release/` its default build directory. When the caller does not pass a custom build directory, `package.sh` will configure and build through:

```bash
cmake --preset release
cmake --build --preset release --parallel <jobs>
```

Keep the existing positional custom-build-directory interface for compatibility. A custom directory remains caller-managed: the script builds it with `cmake --build <dir> --parallel <jobs>` and rejects a non-Release CMake cache before packaging. `--skip-build` also validates that the selected tree is configured as Release, preventing accidental Debug packages.

`QT_DIR` remains the explicit deployment-tool override, but defaults to `QT_ROOT` when `QT_DIR` is unset. `tools/deploy.py` continues receiving an explicit build directory and does not need a Preset-specific interface.

## Alternatives

- Preset-only packaging would be simpler but would remove the established custom-directory workflow.
- Changing only the default directory would avoid `build/`, but would leave stale configuration guidance and would not prevent custom Debug trees from being packaged.
- The selected hybrid keeps compatibility while making the safe Release path the default.

## Error Handling And Compatibility

- A missing default Release cache is configured automatically through the `release` Preset.
- A missing custom build cache fails with an actionable message because no Preset-to-directory mapping is known for arbitrary paths.
- A Debug or otherwise non-Release cache fails before build/deployment, including in `--skip-build` mode.
- Existing `--qt-dir`, `--skip-build`, `--build-only`, `--no-verify`, `--config`, and positional build-directory arguments remain supported.
- Deployment staging layouts and package formats remain unchanged.

## Tests And Documentation

Add focused static regression checks for the default Release Preset, `build-release/`, custom-directory compatibility, Release-cache validation, and `QT_ROOT` fallback. Run those checks in RED before changing `package.sh`, then run the packaging regression suite, shell syntax validation, CMake Preset parsing, and the full configured CTest suite. Update `BUILD.md` so its default, skip-build, and custom-directory examples match the executable behavior.
