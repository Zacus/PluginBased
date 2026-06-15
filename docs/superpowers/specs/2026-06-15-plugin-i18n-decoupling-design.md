# Plugin i18n Decoupling Design

## Background

Plugin QML currently refreshes translated text by importing the host QML module:

```qml
import PluginBased 1.0

text: AppController.currentLanguage, qsTr("Playlist")
```

This makes plugin UI depend on a host business module only to observe language changes. That coupling is not desirable for an enterprise plugin boundary. A plugin should be able to use its own QML module plus shared UI components without importing `PluginBased 1.0` for translation refresh.

## Goals

1. Plugin QML must not depend on `PluginBased 1.0` for language switching.
2. Plugin QML visible text should use normal Qt translation calls: `qsTr(...)`.
3. Plugin C++ visible text should continue to use `tr(...)`.
4. The host application remains the owner of language selection, translator installation, fallback behavior, and persistence.
5. `QtQuickComponents` remains independent from host language services.
6. Runtime language switching should refresh already loaded host and plugin QML pages.

## Non-Goals

1. This design does not introduce per-plugin `.qm` loading yet.
2. This design does not move translation ownership out of the host resource file.
3. This design does not redesign plugin metadata or plugin discovery.
4. This design does not remove all host-side `AppController.currentLanguage` bindings where they are still needed for C++ invokable values.

## Recommended Architecture

Use `QQmlEngine::retranslate()` as the runtime QML translation refresh mechanism.

The host language service continues to apply language changes by installing or removing `QTranslator` instances. After the selected translator state changes, the host calls `QQmlEngine::retranslate()` on the application QML engine. QML bindings that use `qsTr(...)` are then reevaluated by the engine without each plugin importing a host singleton.

Plugin QML should look like this:

```qml
Text {
    text: qsTr("Playlist")
}
```

Plugin QML should not look like this:

```qml
import PluginBased 1.0

Text {
    text: AppController.currentLanguage, qsTr("Playlist")
}
```

The host may still use `AppController.currentLanguage` for values that do not participate in QML's translation system, such as `PluginManager.pluginCardName(index)` and `PluginManager.pluginDescriptionAt(index)`. Those values come from C++ invokable calls, so QML needs an explicit dependency to recompute them after language changes.

## Component Boundaries

### PluginBased Host

Responsibilities:

- Own persisted language configuration.
- Validate supported language IDs.
- Install and remove translators.
- Emit language change notifications.
- Call `QQmlEngine::retranslate()` after translator changes.
- Refresh host-owned plugin card labels and descriptions.

The host should expose language controls to its own QML only. Plugin QML should not import `PluginBased 1.0` just for i18n.

### Plugins

Responsibilities:

- Use `qsTr(...)` for QML visible strings.
- Use `tr(...)` for C++ visible strings.
- Avoid importing `PluginBased 1.0` for language refresh.
- Continue to import `QuickUI.Components 1.0` for shared visual components.

Plugins can still use explicit host APIs in the future if a plugin contract is intentionally added, but i18n refresh should not require that dependency.

### QtQuickComponents

Responsibilities:

- Provide reusable visual and interaction components.
- Use `ComponentTheme` for visual tokens.
- Avoid depending on `PluginBased` or any host language service.
- Prefer externally supplied visible text properties.
- Keep unavoidable built-in defaults in English.

The component library should not install translators and should not own the app language setting.

## Runtime Flow

1. User selects a language in the host toolbar.
2. `AppController.setLanguage(languageName)` delegates to `AppLanguageService`.
3. `AppLanguageService` validates, applies fallback rules, installs/removes translators, persists the normalized language, and emits `languageChanged()`.
4. `AppController` emits `currentLanguageChanged()` for host QML dependencies.
5. `main.cpp` connects the language service signal to the QML engine and calls `engine.retranslate()`.
6. Loaded QML, including plugin QML loaded through `Loader`, reevaluates `qsTr(...)`.
7. Host QML bindings that explicitly depend on `AppController.currentLanguage` refresh C++ plugin card name and description invokable calls.

## Implementation Plan

1. In `main.cpp`, connect `AppLanguageService::languageChanged` to `QQmlEngine::retranslate()`.
2. Remove `import PluginBased 1.0` from plugin QML files where it is only used for language refresh.
3. Replace plugin QML expressions like `AppController.currentLanguage, qsTr("...")` with plain `qsTr("...")`.
4. Keep host QML `AppController.currentLanguage` dependencies where needed for C++ invokable refresh.
5. Update architecture tests:
   - Require `main.cpp` to call or connect `engine.retranslate()`.
   - Require plugin QML files not to import `PluginBased 1.0` for i18n.
   - Require plugin QML visible strings to use `qsTr(...)`.
6. Build and run CTest.
7. Manually verify language switching with a plugin page already open.

## Testing Strategy

Automated checks:

- Static architecture checks ensure plugin QML does not import `PluginBased 1.0` for language refresh.
- Static checks ensure host language switching triggers `QQmlEngine::retranslate()`.
- Existing i18n checks continue to verify translator resource wiring and plugin translation contexts.
- Existing component tests confirm `QtQuickComponents` remains host-independent.

Manual checks:

- Launch the app in English.
- Open `PlayPlugin`.
- Switch to Chinese and confirm plugin page text updates.
- Switch back to English and confirm plugin page text updates.
- Confirm plugin cards on the home page update language.

## Risks and Mitigations

Risk: `QQmlEngine::retranslate()` refreshes QML `qsTr(...)`, but not arbitrary C++ invokable return values.

Mitigation: Keep explicit host dependencies for plugin card name and description until those values are exposed through a model role or signal-aware API.

Risk: A future plugin may need its own translation resource.

Mitigation: This design does not block plugin-owned `.qm` files. A later design can add a plugin translation provider contract through metadata and plugin lifecycle hooks.

Risk: Component library controls may accidentally introduce fixed visible text.

Mitigation: Prefer text properties supplied by consumers. Add component-level tests or review checks when introducing built-in visible strings.

## Acceptance Criteria

1. Plugin QML files no longer import `PluginBased 1.0` solely for i18n refresh.
2. Runtime language switching refreshes already loaded plugin QML pages.
3. Plugin cards still refresh names and descriptions on the host home page.
4. `QtQuickComponents` has no dependency on `PluginBased` or host language services.
5. Build and CTest pass for `PluginBased`.
