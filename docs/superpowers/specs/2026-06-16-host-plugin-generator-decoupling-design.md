# Host Plugin Generator Decoupling Design

## Context

The project already moved most PlayPlugin playback internals behind focused helpers. The next useful decoupling target is outside the playback path:

- `AppController::initPlugins()` mixes QML-facing app readiness with plugin directory discovery and plugin bootstrapping.
- `PluginTemplateGenerator` mixes option parsing, template rendering, directory creation, file writing, and icon copying.

Both areas can be improved without changing public QML APIs, plugin ABI, generated plugin behavior, or package layout.

## Goals

- Move plugin directory discovery out of `AppController`.
- Keep `AppController` as the QML-facing application facade.
- Split plugin generator rendering from filesystem writes.
- Preserve generated plugin output verified by `PluginGeneratorBackendSmokeTest`.
- Add architecture checks so the new boundaries do not regress.

## Non-Goals

- Do not change `IAppPlugin`.
- Do not replace `PluginManager`.
- Do not change generated plugin source text except where required by equivalent refactoring.
- Do not redesign the QML plugin generator UI.
- Do not convert the generator to an external template language in this phase.

## Design

### PluginPathResolver

`PluginPathResolver` is an app-layer helper that owns runtime plugin directory selection. It is a value-style utility with static methods. It should:

- Build the candidate plugin directory list from `QCoreApplication::applicationDirPath()`.
- Select the first existing candidate that contains at least one platform plugin library file.
- Return a structured `PluginPathResolution` containing the selected directory and candidate count.

`AppController::initPlugins()` should call this helper, then remain responsible for logging, `PluginManager::loadAll()`, applying language, and emitting `pluginsReadyChanged()`.

### PluginTemplateRenderer

`PluginTemplateRenderer` owns pure text rendering for generated plugin files. It should:

- Expose escaping helpers for C++/JSON/QML/XML strings.
- Render header, source, metadata JSON, CMake, QML, and translation text.
- Accept a shared `PluginGeneratorOptions` value type.

It should not touch the filesystem.

### PluginScaffoldWriter

`PluginScaffoldWriter` owns filesystem writes for generated plugin scaffolds. It should:

- Create the plugin directory and optional `qml`/`assets` directories.
- Write rendered text files.
- Copy the optional icon asset.
- Return structured success/failure maps matching the current QML-facing generator contract.

It should not validate option semantics or render template text.

### PluginTemplateGenerator

`PluginTemplateGenerator` remains the QML-facing orchestrator. It should:

- Parse and validate QML input into `PluginGeneratorOptions`.
- Derive defaults such as display name, description, icon, output dir, icon asset name, and plugin id.
- Delegate text rendering to `PluginTemplateRenderer`.
- Delegate filesystem writes to `PluginScaffoldWriter`.

## Ownership And Lifetime

- `PluginPathResolver`, `PluginTemplateRenderer`, and `PluginScaffoldWriter` are non-QObject helpers with no cross-thread state.
- `PluginTemplateGenerator` remains a QObject owned by Qt/QML as before.
- `PluginManager` ownership of loaded plugin loaders and plugin instances does not change.

## Testing

- Add `tests/host_plugin_generator_decoupling_checks.py` to guard the new boundaries.
- Keep `PluginGeneratorBackendSmokeTest` as the behavior check for generated output.
- Run existing CTest after implementation.

## Risks

- Plugin path resolution touches startup behavior. Keep candidate order identical to the current `AppController::initPlugins()` logic.
- Generator splitting can accidentally alter generated files. Rely on `PluginGeneratorBackendSmokeTest` and existing string checks to catch output regressions.
