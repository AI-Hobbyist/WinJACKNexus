# Theme diagnostic boundaries (2026-10-04 source review)

Catalog menu failures are user-visible: `ThemeCatalog::createMenu` → app tray
`showMessage`. Refresh, activate and Dev reload callbacks now translate a display
copy. `refresh`, `activate`, `restoreSaved`, cached diagnostics and saved IDs retain
their original strings. The three canonical Catalog failures have four-language
draft templates; known ThemeCore/Package failures use the existing adapter.

The following source candidates have no current application display consumer;
retain their canonical API/cache/technical content. This finding does not claim
runtime verification or replace the final S0 reconciliation.

| Source | Content | Authoritative current call path |
| --- | --- | --- |
| ThemeCatalog.cpp | default DLL notice; duplicate ID and first-discovery fragments; failed override restoration; failed selection persistence; failed previous-theme restoration | Written only to `diagnostics_`; `diagnostics()` has no application call site. Startup `restoreSaved` failure is emitted by `main.cpp` through `qWarning`, not a dialog. |
| ThemeCatalog.cpp | `(Dev)` | Technical development-directory marker appended to raw package name; name, author and ID remain original. |
| ThemeBackground.cpp | invalid decoded dimensions, scaled-image budget, missing image resource | Optional `QString* diagnostic` from `paint`. All application call sites omit this parameter: patchbay, mixer/overview, SEL main/monitor/aux/fx, meter windows and ThemeBackgroundOverlay. No diagnostic reaches UI text. Qt decoder error remains opaque. |
| ThemeManager.cpp | missing font, failed registration, multiple families | Raw `FontState::diagnostic` exposed by `fontDiagnostic()`. Only ThemeAdapterTests reads it; no application consumer. Cache decisions and font fallback remain canonical. |
| ThemeCore.cpp | `User overrides`, `Per-theme user preferences` | Internal override package metadata, used for user preference storage/export rather than control labels. Preserve serialized metadata. |

Open import/font/image/export dialogs use an owned Qt QFileDialog following the
existing patchbay workflow. Their title and single wildcard filter follow the
current language; filename text, cursor and both selection directions survive
filter refresh. No directory/model/metadata refresh occurs on language change.
Four workflows and actual Catalog menu failure callbacks have fixture source
checks, awaiting the module's single Release/Windows execution.
