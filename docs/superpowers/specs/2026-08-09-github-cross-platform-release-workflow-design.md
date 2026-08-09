# GitHub Cross-Platform Release Workflow Design

## Context

The repository currently has one macOS-only GitHub Actions job. That job performs Debug validation and Release packaging serially, so pull-request quality checks, main-branch package generation, and tagged releases share one execution path without explicit release boundaries. The repository already exposes the intended build interface through the `debug` and `release` CMake Presets, pins Qt 6.8.3 and the vcpkg baseline, and provides cross-platform packaging implementations for macOS, Linux, and Windows.

The workflow must exercise those repository-owned interfaces instead of duplicating local build configuration in CI.

## Goals

- Compile and test pull requests on macOS, Linux, and Windows.
- Build, test, verify, and package all three platforms on pushes to `main` and manual runs.
- Build all three packages for `v*` tags and publish them in a GitHub Release.
- Retain ordinary package artifacts for 30 days and tagged-build artifacts for 365 days.
- Generate a SHA256 checksum beside every package.
- Preserve deterministic Qt, vcpkg, CMake, Ninja, and packaging inputs.
- Keep failure diagnostics useful without hiding failures from other matrix platforms.
- Apply least-privilege GitHub token permissions.

## Non-Goals

- No Apple Developer ID signing or notarization.
- No Windows Authenticode signing.
- No Linux distribution-specific installers such as AppImage, DEB, or RPM.
- No new package registry or external artifact storage.
- No change to application source code, plugin ABI, or runtime behavior.
- No automatic deletion of GitHub Releases after one year. Release assets remain until the release is deleted; the 365-day rule applies to Actions artifacts.

## Selected Architecture

Use one event-aware workflow with a three-platform matrix and a separate tag-only publication job.

The matrix keeps the configure, build, test, package, checksum, and diagnostic behavior in one maintainable definition. Event conditions change the build profile and whether packaging runs:

- `pull_request`: use the `debug` Preset and stop after CTest.
- push to `main`: use the `release` Preset, package, checksum, and upload artifacts for 30 days.
- `workflow_dispatch`: use the same Release package path as `main` and retain artifacts for 30 days.
- push of a `v*` tag: require the tag version to equal the CMake project version, build Release packages, retain workflow artifacts for 365 days, and publish a GitHub Release after all matrix jobs succeed.

The release publication job downloads the three matrix artifacts and uploads the packages and checksum files as Release assets. It does not rebuild or modify them.

## Platform Matrix

Use fixed runner families compatible with the selected Qt kits:

| Platform | Runner | Qt host/architecture | Qt root | Package |
| --- | --- | --- | --- | --- |
| macOS | `macos-14` | `mac` / `clang_64` | `Qt/6.8.3/macos` | DMG |
| Linux | `ubuntu-22.04` | `linux` / `gcc_64` | `Qt/6.8.3/gcc_64` | tar.gz |
| Windows | `windows-2022` | `windows` / `win64_msvc2022_64` | `Qt/6.8.3/msvc2022_64` | ZIP |

Linux installs only the native runtime and packaging packages required by the official Qt kit and `tools/deploy.py`, including `patchelf`. Windows initializes the Visual Studio 2022 compiler environment before Ninja configuration. All script-oriented steps use a shell explicitly where platform defaults differ.

## Dependency Preparation And Caching

- Checkout the application and a separate vcpkg tree.
- Checkout vcpkg at `ea1a7396b05637a53bf23c078647ecc0edee4b80` with full history so manifest overrides can resolve historical port trees.
- Use Python 3.12 and `aqtinstall==3.3.0` to install official Qt 6.8.3 binaries with only `qtmultimedia` and `qtshadertools` added.
- Validate the Qt CMake package files before configuration, including cache hits.
- Cache Qt by operating system, Qt architecture, Qt version, module set, and aqtinstall version.
- Cache vcpkg downloads separately from compiled binary archives.
- Include operating system, vcpkg baseline, triplet-relevant architecture, and `vcpkg.json` hash in vcpkg cache keys.
- Use explicit temporary cache roots so cache behavior is identical across runner home-directory conventions.

The workflow repeats pinned values because GitHub expressions cannot import Python constants, while `tests/ci_ctest_checks.py` enforces that workflow pins stay synchronized with repository manifests and setup tooling.

## Build, Test, And Package Flow

Each matrix job performs these stages:

1. Prepare the platform compiler and native packaging prerequisites.
2. Restore or install official Qt 6.8.3.
3. restore vcpkg source downloads and binary archives.
4. Bootstrap the pinned vcpkg checkout.
5. Validate CMake, Ninja, Qt, Python, and the vcpkg executable.
6. Configure with `cmake --preset debug --fresh` for pull requests or `cmake --preset release --fresh` for package-producing events.
7. Build with the matching build Preset.
8. Run CTest with the matching test Preset.
9. For non-PR events, call `./package.sh --skip-build`, which performs the repository's pre-archive integrity verification.
10. Generate a SHA256 checksum for the single platform package.
11. Upload the package and checksum with event-specific retention.

The workflow must reject missing packages, multiple platform packages, a checksum failure, or a packaging verification failure.

## Version And Release Contract

A tag must match `vMAJOR.MINOR.PATCH`. Before the tagged matrix builds proceed, the workflow compares the version after `v` with `./package.sh --version`. A mismatch fails the workflow and prevents release creation.

The Release name uses the tag name. The Release is created only after all three package jobs succeed. It is not marked as a prerelease solely from the version string, and it does not overwrite a pre-existing release implicitly.

The release publication job alone receives `contents: write`. All build jobs and all pull-request execution paths use `contents: read`.

## Failure Handling And Diagnostics

- Set matrix `fail-fast` to `false` so one platform failure does not cancel the remaining platform results.
- Apply a finite timeout suitable for Qt and FFmpeg dependency builds.
- Assign matrix-specific artifact names so parallel uploads cannot collide.
- On configure failure, retain the CMake configure log, vcpkg manifest install log, and available CMake error/configuration logs.
- On test failure, retain the CTest console log and `LastTest.log`.
- Upload diagnostics with `if: failure()` and `if-no-files-found: ignore`; diagnostic upload must not mask the original error.
- Cancel stale branch and pull-request workflow runs in the same concurrency group, but never cancel an in-progress tag release.

## Artifact Retention

- Pushes to `main` and manual runs: `retention-days: 30`.
- `v*` tags: `retention-days: 365`.
- Pull requests do not upload distributable packages.
- Failure diagnostics use the ordinary 30-day retention.
- GitHub Release assets are formal release records and remain available until the Release is deleted.

The repository or organization Actions policy must allow a 365-day maximum. If it is configured lower, the GitHub setting must be raised; the workflow does not silently shorten the requested retention.

## Security And Supply-Chain Boundaries

- Default workflow permissions are `contents: read`.
- The tag publication job has only `contents: write` in addition to required read access.
- Pull requests do not receive secrets and cannot reach the publication job.
- vcpkg, Qt, aqtinstall, and CMake Preset inputs remain version-pinned.
- Third-party workflow actions are limited to narrowly scoped build-environment needs; GitHub-owned actions are preferred.
- Signing and notarization are a future isolated stage that will require protected Secrets and explicit release-environment controls.

## Alternatives Considered

### Separate CI And Release Workflows

This creates a strong visual boundary but either duplicates the expensive dependency/build definition or introduces a reusable workflow solely to reconnect the same matrix. The extra indirection is not justified for the current repository.

### One Workflow Per Platform

This makes platform-specific shell code straightforward but triples trigger, cache, permission, retention, and release coordination logic. It has the highest drift risk and is rejected.

### Keep A macOS-Only Workflow

This leaves existing Linux and Windows packaging code unverified and cannot satisfy the agreed cross-platform release contract.

## Verification Strategy

- Extend `tests/ci_ctest_checks.py` first so the existing workflow fails the new cross-platform contract.
- Parse and inspect the workflow structure rather than relying only on broad text searches where a behavioral relationship matters.
- Run the focused CI contract test after every workflow change.
- Run the repository's packaging regression test because artifact naming and package discovery depend on `tools/deploy.py`.
- Run CMake configure and the full local CTest suite to ensure CI contract changes do not disturb the shared Presets.
- Perform a YAML syntax and GitHub Actions-oriented static validation locally when the required validator is available.
- GitHub-hosted Linux and Windows package jobs are the authoritative native integration verification because those environments cannot be reproduced faithfully on the current macOS workstation.

## Acceptance Criteria

- A pull request starts three Debug build-and-test matrix jobs and no package publication.
- A push to `main` produces verified macOS, Linux, and Windows packages plus checksums, retained for 30 days.
- A manual run follows the same non-publishing Release package path as `main`.
- A matching `v*` tag produces all three packages, retains Actions artifacts for 365 days, and creates one GitHub Release containing all packages and checksums.
- A mismatched or malformed version tag cannot create a Release.
- A platform failure leaves the other platform jobs running and prevents release publication.
- Normal jobs are read-only; only the release publication job can write repository contents.
