# Build Identity Design

## Context

PluginBased currently repeats the application version as string literals in `CMakeLists.txt`, `app/main.cpp`, `AppController`, packaging helpers, and platform-generated metadata. The package filename is derived from the CMake cache, but the running application and startup log use separately maintained values. A support report can therefore identify a nominal version such as `1.0.0` without identifying the exact source revision or whether the binary came from a modified worktree.

The project needs a build identity that is precise enough for diagnosis without exposing internal build details throughout the ordinary user interface.

## Goals

- Keep one manually maintained product version.
- Identify the exact Git revision used for every Git-backed build.
- Distinguish clean, dirty, and source-export builds.
- Generate machine-readable build information without requiring it at application startup.
- Show ordinary users only a concise product version and build number.
- Make complete diagnostic information easy to copy for support.
- Carry the same build identity through the executable, logs, native platform metadata, and packages.
- Give tagged CI releases a strict, reusable version-validation contract.

## Non-Goals

- No change to plugin versions, plugin API version, plugin ABI version, or metadata schema.
- No release-version service or external version database.
- No manually maintained `VERSION`, `version.xml`, or `build-info.json` source file.
- No username, hostname, absolute source path, remote URL, or other workstation identity in build metadata.
- No mandatory build timestamp that would make identical source inputs produce different metadata.
- No requirement that ordinary users understand Git, compiler, or Qt details.

## Alternatives Considered

### Static `version.xml`

A checked-in XML file is easy to inspect but creates another mutable source that can drift from CMake, package names, native metadata, and release tags. XML also adds no benefit to the existing CMake, C++, Qt, JSON, and Python toolchain. This option is rejected.

### Git-Derived Version Only

`git describe` provides useful development identities but cannot be the product-version authority for source archives, shallow checkouts, or repositories without an appropriate tag history. It also conflates release compatibility with source identity. This option is rejected.

### Product Version Plus Build Identity

The selected design keeps the stable product version in CMake and adds generated Git/build metadata. This separates user-facing release semantics from diagnostic source identity and matches the repository's existing CMake and packaging architecture.

## Version Sources And Invariants

`project(PluginBased VERSION 1.0.0)` remains the only manually maintained product-version source.

The following values derive from `PROJECT_VERSION`:

- `QCoreApplication::applicationVersion()`
- the stable version shown in the application UI
- `package.sh --version`
- package filenames
- macOS `CFBundleShortVersionString` and `CFBundleVersion`
- Windows VERSIONINFO product and file versions
- expected release tag `v<PROJECT_VERSION>`

Git contributes build identity, never product-version authority:

- full commit SHA
- short commit SHA
- exact tag when present
- source-tree state: `clean`, `dirty`, or `unknown`

Plugin implementation versions and the plugin API/ABI versions remain independent from the host product version.

## Generated Artifacts

The build generates two artifacts under the active binary directory:

- `generated/BuildInfoData.h`: private compile-time constants consumed by C++.
- `build-info.json`: a machine-readable diagnostic copy consumed by packaging and verification tools.

Neither file is committed. A repository-owned CMake module and generation script own the schema, Git queries, validation, escaping, and write-if-different behavior. The generator runs during initial configuration and before builds so a new commit or dirty-state change is reflected without requiring the developer to delete the build tree. It writes temporary files and replaces outputs only when content changes.

The application links a small, non-`QObject` `BuildInfo` C++ component. Business code does not run Git commands and does not parse the external JSON. The generated header is a private implementation input, so changing its layout is not a public ABI change.

## Build Information Schema

`build-info.json` uses schema version 1:

```json
{
  "schemaVersion": 1,
  "productName": "PluginBased",
  "productVersion": "1.0.0",
  "displayVersion": "1.0.0+g7c3c1dd",
  "gitCommit": "7c3c1dd000000000000000000000000000000000",
  "gitShortCommit": "7c3c1dd",
  "gitTag": "",
  "gitTreeState": "clean",
  "buildType": "Release",
  "platform": "macOS",
  "architecture": "arm64",
  "compiler": "AppleClang 17.0.0",
  "qtVersion": "6.8.3"
}
```

The packaged JSON contains the diagnostic fields above, but normal UI does not display them all. Branch and CI ref names are not persisted because the commit and exact release tag are sufficient to identify source while avoiding disclosure of internal branch naming.

No timestamp, username, hostname, source path, binary path, or Git remote is included.

## Display Version Rules

- Clean exact tag matching the product version: `1.0.0`.
- Clean non-tag build with Git metadata: `1.0.0+g7c3c1dd`.
- Dirty local build: `1.0.0+g7c3c1dd.dirty`.
- Build without Git metadata: `1.0.0+unknown`.

The short commit uses a fixed, documented length and is a diagnostic build number, not a monotonically increasing platform build counter.

## User And Diagnostic Surfaces

Normal users see only:

```text
PluginBased
版本 1.0.0
构建 7c3c1dd
```

The application window title remains concise and uses the stable product version only. A toolbar About entry opens a small dialog containing the product name, product version, short build number, and a `复制诊断信息` action.

The copied diagnostic summary includes display version, full commit, source-tree state, build type, platform, architecture, compiler, and Qt version. It excludes branch or CI ref names, paths, machine identity, and remote URLs.

Command-line behavior is split by audience:

- `--version` prints `PluginBased 1.0.0 (build 7c3c1dd)` and exits.
- `--build-info` prints the complete diagnostic JSON and exits.

After logger initialization, startup emits one structured build-identity block containing the diagnostic summary. Release support can therefore obtain the build number from the About dialog, command line, log file, or packaged JSON.

## Platform And Package Integration

- macOS embeds the stable product version and identifier in `Info.plist`; `build-info.json` is placed in `PluginBasedApp.app/Contents/Resources`.
- Windows embeds the stable product version in VERSIONINFO; `build-info.json` is placed at the archive root.
- Linux places `build-info.json` at the archive root.
- The executable always uses the compiled `BuildInfo`; removal or alteration of the external JSON cannot change runtime identity or prevent normal startup.
- The packaging tool copies the already generated JSON and never regenerates Git metadata.
- The package verifier requires the JSON, validates schema and field types, and checks its product version against the CMake cache and package name.

This makes one configured and built binary tree the immutable source of the application binary, JSON metadata, and package identity.

## Local, Exported, And Official Builds

Normal local builds follow tolerant rules:

- Git available and clean: record the commit and clean state.
- Git available and dirty: allow the build and mark it dirty.
- Git unavailable: allow the build, mark Git fields unknown, and emit a clear CMake status message.

Local Release builds remain tolerant so developers can test packaging from modified sources. They are identified as non-official by their dirty or unknown state.

Official tagged CI builds enable strict validation:

- Git commit must be known.
- the source tree must be clean.
- the tag must match `vMAJOR.MINOR.PATCH`.
- the tag version must equal `PROJECT_VERSION`.
- the generated JSON and compiled identity must agree.

Failure of any strict condition stops packaging and prevents GitHub Release creation. CI may inject the expected tag for detached-HEAD validation through a documented variable, but it cannot override the product version.

## Failure Behavior

- Missing Git is a warning for ordinary builds and a hard error for official builds.
- Invalid or malformed injected Git fields are rejected rather than copied into C++ or JSON.
- File-generation failure stops the build with the failing output path and operation.
- Generated files are escaped at the generator boundary so valid Git tags cannot produce invalid C++ or JSON.
- Missing, malformed, or version-inconsistent `build-info.json` stops packaging verification.
- Diagnostic JSON failure does not introduce a runtime fallback path because the application identity is compiled in.

## C++ And Compatibility Boundaries

- Use C++17 and Qt 6.8.3.
- `BuildInfo` is an immutable value-oriented component without ownership, thread-affinity, callback, or shutdown concerns.
- Generated data remains private to the application target.
- `AppController` exposes only read-only values and a clipboard action to QML; it remains the UI facade.
- No public plugin header or virtual interface changes.
- No user configuration schema changes.

## Verification Strategy

Test generation with temporary real Git repositories rather than source-text assertions:

- clean repository
- dirty repository
- detached HEAD
- source directory without `.git`
- explicit CI tag injection for detached HEAD
- invalid injected metadata
- output unchanged when inputs are unchanged

Test the C++ component for stable product version, display version, concise CLI string, full diagnostic summary, and JSON agreement. Test command-line `--version` and `--build-info` in an offscreen Qt environment.

Extend packaging regression tests for all three platform layouts. Verify that packages copy `build-info.json`, and that missing, malformed, or mismatched metadata is rejected. Inspect macOS `Info.plist` locally. Windows VERSIONINFO and native Windows package behavior are validated authoritatively by the later GitHub Actions matrix.

Run the focused build-identity tests, packaging tests, fresh Debug and Release builds, full CTest, and a local verified package before restoring the cross-platform Workflow implementation.

## Acceptance Criteria

- Changing `project(VERSION)` updates all application, native metadata, command-line, and package product versions after configuration.
- A source-backed build identifies its exact commit and tree state.
- A source export without `.git` still builds and identifies itself as unknown.
- Ordinary UI shows only product version and short build number.
- Support can copy complete, privacy-safe diagnostic information from the About dialog.
- `--version`, `--build-info`, startup logs, compiled C++ data, and packaged JSON agree.
- All three package layouts contain a valid `build-info.json`.
- Official release validation rejects unknown, dirty, malformed, or version-mismatched sources.
- Plugin ABI/API and plugin versions are unchanged.
