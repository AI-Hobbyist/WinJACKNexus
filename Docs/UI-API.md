# UI API (v1)

The shared Qt UI library is `WinJACKNexus::UI` (`ui.dll`). Link the CMake
target `WinJACKNexus::UI` and include public headers from
[`src/ui/include/WinJACKNexus/UI`](../src/ui/include/WinJACKNexus/UI).
The exported surface is marked `WINJACK_UI_API`; implementation details in
`src/ui/src` are not an API. This document records the v1 contract for
application content controls.

## Shared controls

[`Controls.hpp`](../src/ui/include/WinJACKNexus/UI/Controls.hpp) is the
common drawing and interaction layer. Native Fx editors and mixer views use
it rather than copying a meter, curve, knob, fader or numeric editor into a
module. Each draw function accepts the caller's `QPainter` and logical Qt
coordinates; it does not own a widget or schedule repainting.

| API | Purpose |
| --- | --- |
| `Palette`, `applyPalette`, `paletteFor`, `themeColor`, `themeTokenColor` | Shared surface colors and per-widget host theme overrides. `themeTokenColor(scope, role, fallback)` resolves the active v1 theme's scoped color token; it uses `fallback` while the built-in skin is active so the current module appearance is preserved. Pass its result through `paletteFor` when a widget property override must retain precedence. |
| `drawSurface`, `drawText`, `drawMarker`, `drawButton`, `ButtonStyle`, `drawStyledButton`, `drawToggle`, `ToggleButton`, `drawDropdown`, `drawCardLabel` | Common rounded surface, label, circular graph marker, button, switch, list and card primitives. `drawSurface` accepts a brush, optional border and corner radius; `drawText` accepts the caller's font, color and alignment; `drawMarker` accepts fill, radius and optional outline; `ButtonStyle` sets active and inactive colors, radius and typography. |
| `drawKnob`, `drawPanKnob`, `drawSlider`, `drawFader` | Normalized rotary and linear controls. `drawFader` uses zero at the top of its track. |
| `MeterStyle`, `MeterZone`, `MeterTick`, `drawMeter`, `drawMeterScale` | Configurable meter range, orientation, colored zones, ticks, labels and peak marker. |
| `MeterBallistics` | Elapsed-time level smoothing and peak hold. |
| `CurveStyle`, `drawCurvePath`, `drawPolarCurve`, `drawChart`, `ChartGridStyle`, `drawChartGrid` | Configurable stroke/fill and common plot geometry. Grid divisions or normalized tick positions, color, pen style, width and background are configurable. |
| `NumericControlSpec`, `editNumericValue`, `resetNumericValue` | Exact numeric entry on readout click and default restoration on double-click. |
| `ControlRegistry` | Instance-local factory map for module-specific widgets. |
| `DragTreeWidget` | Reusable sidebar tree drag source. Its caller supplies a MIME-data factory for the selected item; the tree owns the drag and transfers the data to `QDrag`. Patchbay keeps its node payload format outside the UI library. |
| `TextField`, `SearchField`, `ReadOnlyField`, `ChoiceField`, `ActionButton` | Shared settings controls. Text fields draw their surface while Qt retains text editing; search enables clearing and disables predictive input; read-only fields reject edits. `ChoiceField` draws its surface with `drawDropdown`, leaving an editable field to draw its text once, and `ActionButton` uses `drawButton`. Dark and light themes supply their colors. `ChoiceField::setManualFilterEnabled(true)` enables case-insensitive contains filtering by typing; it is disabled by default. |
| `IntegerField`, `DecimalField` | Self-drawn themed numeric surfaces and step arrows with Qt numeric editing and step behavior. Callers supply their own range, step, suffix, value and commit behavior. |
| `ToggleField`, `RangeSlider` | Self-drawn settings checkbox and horizontal or vertical slider, using Qt only for input, focus and accessibility behavior. Callers configure label, range, orientation and value; the shared theme supplies painted indicator, track and handle colors. |
| `IconActionButton` | Self-drawn icon button using `drawButton`, with icon and text beside or below. It retains `QToolButton` action binding, check state and accessibility behavior. |
| `ZoomControl` | Self-drawn patchbay zoom control with magnifier plus, percentage readout and magnifier minus. `zoomInRequested`, `zoomOutRequested` and `zoomResetRequested` report clicks; `setZoomFactor(qreal)` updates the readout. |
| `TextLabel`, `ProgressField` | Shared label and progress display. The label preserves native selectable text, pixmaps and rich text where those features are requested. |
| `ListField`, `TableField`, `LogField` | Shared item and text views with common painted rows, text and scrollbars. Callers retain Qt item models, editing and selection signals. |
| `GroupPanel`, `TabField`, `ScrollField`, `SplitField`, `DockPanel` | Shared group, tab, scroll, split and dock containers. Tab close buttons and splitter handles use shared painting. `DockPanel` draws a plain title bar and places its collapse arrow at the canvas-facing edge midpoint; the same edge remains draggable for width changes, including when embedded settings use native child windows. It omits title-bar float and close buttons. `sidebarControlHeight` is the common 28 px minimum height for shared text, choice, numeric, action and toggle controls, including settings dialogs; callers may make a control taller. |
| `DialogButtonBar`, `ScrollBarField` | Dialog role/standard button behavior backed by shared buttons, and a shared painted scrollbar. |
| `ClientNameWidget`, `EmbeddedFxEditor` | Shared node name editor and mixer-hosted effect editor shell. |

Application content should instantiate these controls through this API.
The system main menu, tray menu, context menus and control popup menus use
Qt's native menu components.

`TrayController::setThemeLoadingProgress(int)` updates the shared
`ProgressField` beneath the quick access bar. Values are clamped to 0–100;
100 hides the bar after theme initialization. Call it from the GUI thread.

`TrayController::setQuickAccessFileMenu(QMenu*)` binds the patchbay's native
File menu to every quick access bar, sharing its existing project actions.
Call it from the GUI thread; ownership stays with the caller. A null or
destroyed menu disables the entry, and newly created bars retain the binding.

The SEL MAIN polar legend uses Input and Output. Input measures the selected
strip after gain and before FX; Output measures that strip after FX and its
fader, before the MAIN routing matrix and MIX switch. Both measurements retain
the selected strip's channel layout, preserving the direction of each channel
instead of displaying the stereo downmix. Mute and solo still
affect Output; disabling MIX keeps the polar measurement available while
preventing that strip's signal from reaching the actual MAIN audio outputs.

`MeterStyle` defaults to a `-72..+12 dB` horizontal scale with three zones
and `showPeak = true`. Supply a finite peak value to `drawMeter` when the
source is a Peak meter; `std::nullopt` means no peak measurement is available.
The convenience `drawLevelMeter` uses the current level as its default peak
marker, so it always draws a peak line.
Zone boundaries are in the same units as `minimumDb` and `maximumDb`, in
ascending order. Use `fillOriginDb` for a reduction meter whose fill starts
at zero. `drawMeterScale` draws only ticks and labels when the filled meter
and scale occupy separate rectangles. `MeterBallistics::advance(inputDb,
elapsedSeconds, holdSeconds)` takes seconds, then exposes `levelDb()` and
`peakDb()` for painting.

`CurveStyle` selects stroke color, width, pen style, fill color or brush,
visible parts, label and numeric range. `drawCurvePath` draws a filled path
separately from its stroke and paints the stroke with `Qt::NoBrush`.
`drawPolarCurve` accepts at least three radius samples around a full circle;
its `minimum` and `maximum` map samples into the supplied radius. The mixer
and AUX panners share grid, handle and curve presentation through
[`MixerPolarPlot.hpp`](../src/ui/include/WinJACKNexus/UI/MixerPolarPlot.hpp).
The `drawMixerPolarCurves` overload taking `themeScope` resolves input and
output curve strokes from `chart-series-polar-input-stroke` and
`chart-series-polar-output-stroke`, using `sel-main` or `sel-aux` as the scope;
the existing signature retains `sel-main` behavior.
`drawMixerPolarGrid`, `drawMixerPolarReferencePoint`, `drawMixerPolarHandle`
and `drawMixerPolarCenter` also accept a `themeScope` overload. Their original
signatures continue to use `sel-main`. The scoped forms resolve polar grid,
reference, handle and center marker colors independently for `sel-main` and
`sel-aux`; the curve painter also resolves zero ring, axis label, legend and
overrange colors from the same scope. Reference gain and handle selection
still determine the existing alpha and geometry.
`drawChartGrid` clips to its bounds; explicit `verticalFractions` and
`horizontalFractions` override uniform column and row divisions.

For a value readout, pass its widget owner, logical `QRect`, current value,
valid range, default and decimal count to `editNumericValue`. It returns an
overlay `QLineEdit` or null for invalid arguments. Enter applies an acceptable
in-range number through the callback and closes the overlay. Escape or focus
loss closes it without changing the value. The owner paints its normal
readout again. A double-click handler calls `resetNumericValue` with the same
specification and an apply callback. The caller owns the underlying parameter
state and its audio commit; the shared helper owns only the temporary editor.

## Theme package memory API

[`ThemePackage.hpp`](../src/ui/include/WinJACKNexus/UI/ThemePackage.hpp) provides
the v1 `.wjnskin` package layer. `loadFromFile` closes the source file before
returning; `loadFromBytes` accepts a ZIP already in memory. Both validate the
archive and metadata before assigning `result`, so a failed load preserves its
previous snapshot. `create` builds a package from metadata, CSS bytes and an
`assets/` map; `withImportedAsset` reads a local image or font into a new
in-memory work copy. `exportToBytes` writes a standard Stored ZIP, and
`saveToFile` writes it atomically. Resource names are ZIP logical paths;
neither loading nor import creates an extraction directory.

The root `style.css` carries global tokens. Optional `host/`, `node/`, and
`native_fx/` folders carry one CSS file per module. `styleCss()` returns the
root CSS followed by module CSS in sorted path order; the parser accepts their
successive `:root` blocks. Legacy single-file packages remain valid, and
importing an asset preserves the module files in the archive. The original
`create` overload creates a single-file package; its new overload accepts a
map of module CSS paths and bytes alongside the global CSS and assets.

`resourceRef(name)` returns a `ResourceRef` with the package ID, logical path
and shared bytes. It remains usable after the originating `ThemePackage` is
released. This identity must accompany inherited module image references so
an `assets/` path resolves in its source package. A plain `resource(name)`
convenience call returns the bytes without provenance. Structural ZIP limits,
allowed paths, metadata schema and theme capability version are specified in
[`Theme-v1-Contract.md`](Theme-v1-Contract.md). CSS parsing, theme activation,
image decoding and font registration are separate layers; this API only owns
validated package bytes. `theme_package_tests` is the non-GUI call-site test
for the package API.

[`ThemeCore.hpp`](../src/ui/include/WinJACKNexus/UI/ThemeCore.hpp) provides the
S04 parser and resolution layer. `ThemeRegistry::builtIn` loads the embedded v1
registry. `ThemeCore::snapshot` returns a shared immutable `ThemeSnapshot` with
resolved values, resource references, source theme ID and diagnostics. A held
snapshot remains valid after a switch. `setFullTheme` activates a full package
and clears prior fragments and overrides; `applyFragment` adds a module scope
layer, with later fragments winning the same token; `setOverride` adds a user
layer. Missing module values inspect declared parent tokens before their own
built-in defaults. A missing module background image inherits the current
patchbay image; explicit `none` stops inheritance. An inherited or `var()`
referenced image keeps the source package's resource reference. Malformed CSS
is rejected without changing the active snapshot or emitting `themeChanged`.
Bad typed values and unknown tokens produce line-numbered diagnostics; bad
values fall back through the remaining layers. `beginPreview`, `cancelPreview`,
`commitPreview` and `restoreDefault` manage candidate snapshots. This layer
exposes the pinned prior snapshot through `previewBaseSnapshot` until cancel
or commit and emits `previewCompleted` after commit for font cleanup. It does
not decode images, register fonts or paint widgets. `theme_core_tests`
exercises these public call paths without a GUI.

`activeFullTheme` returns the installed complete package, or an invalid
package for built-in defaults. `declaredValue(token)` returns the winning raw
declaration from an override, fragment, or full package; empty means inherited
or registered default. Authoring UI uses this distinction for background mode.
`exportFullTheme(metadata, result)` writes resolved values, embedded image/font
bytes, and unknown declarations from absent modules to a portable package.
`exportFragment(namespacePrefix, metadata, result)` selects the exact owning
namespace, resolves inherited values and `var()` references, and embeds that
scope's image bytes. A module fragment excludes global font. Both methods
validate the result through `ThemePackage::create` before returning it.

[`ThemeManagementDialog.hpp`](../src/ui/include/WinJACKNexus/UI/ThemeManagementDialog.hpp)
is the shared-control authoring window. `openPackage` validates a candidate;
`preview`, `cancelPreview`, and `apply` route through the process `ThemeCore`;
`exportToFile` writes full, DLL default, or module fragment packages.
Import and export dialogs start in the main executable's `themes/` directory.
The window displays package metadata and diagnostics, edits color, font and
background settings, and opens the homepage only on an explicit click.
`theme_management_tests` exercises the actual Windows popup and saves its
screenshots in `tests/preview/S10/`.

`hasUserOverrides`, `exportUserOverrides` and `restoreUserOverrides` form the
S11 per-theme preference boundary. The exported fragment retains raw user
values and copies referenced image/font bytes; restoring replaces the current
user layer without changing the installed full theme. The catalog saves one
such fragment per stable theme ID and uses a preview transaction to roll back
a failed switch.

[`ThemeCatalog.hpp`](../src/ui/include/WinJACKNexus/UI/ThemeCatalog.hpp)
scans direct `.wjnskin` files and Dev theme folders in the EXE-adjacent
`themes/` directory, case-insensitively by package extension. `refresh` validates full packages and CSS,
sorts filenames deterministically, rejects duplicate IDs after the first
valid file, and returns diagnostic messages for rejected files. Entry labels
add author, then ID when names repeat. `activate` switches by ID, retains the
prior selection on failure, and saves per-ID user overrides through the
injected `QSettings`. `restoreSaved` loads the selected package on startup;
missing packages fall back to the required `wjn_default_theme.dll` with a diagnostic.
The DLL is adjacent to the EXE; if it is absent or invalid, startup fails.
The menu has one DLL default entry and marks folder themes with `(Dev)`.
`createMenu` builds a native Qt `QMenu` with exclusive checked theme actions,
a refresh action for the selected Dev theme, and a management action.
`theme_catalog_tests` opens that menu on the Windows
display and saves its screenshot under `tests/preview/S11/`.

`ThemeRegistry::withTokens` validates a combined built-in/module token table
without changing the source registry. `ThemeCore::refreshRegisteredNamespaces`
rebuilds its active registry from `ThemeNamespaceRegistry` descriptors and
reparses saved full/fragment CSS so previously unknown module declarations
become active after registration. The registry calls this after registering
or releasing a descriptor, outside its mutex; the resulting snapshot emits
`themeChanged`. A removed module token no longer participates in drawing,
while its declaration remains in the theme package for later re-registration.
The patchbay popup test checks registration, module isolation, unload and
retained CSS revalidation.
`PatchbayCanvas::setClientThemeScopes` binds a loaded Node type's descriptor
scope to its runtime client ID; absent node roles fall back to `patchbay`.
`setClientColorOverride` applies a runtime instance title color above that
scope, and an invalid `QColor` clears it so the current theme shows again.

[`ThemeBackground.hpp`](../src/ui/include/WinJACKNexus/UI/ThemeBackground.hpp)
exposes `ThemeBackground::paint(QPainter&, QRectF, scope, overlay, diagnostic)`
for GUI-thread drawing from the active snapshot's in-memory image resource.
Call with `overlay=false` before module components for the base-image mode, or
`overlay=true` after components for the upper-image mode; only the selected
mode paints. Size, proportional scale, repeat, position, opacity and Gaussian blur are resolved
within the module's logical-pixel viewport. Upper-image mode ignores blur.
All background image options outside `global` and the `patchbay` root inherit
their corresponding `patchbay` tokens by default. Explicit scoped declarations
override this inheritance; the default skin and editable template use references
to the patchbay options so patchbay changes propagate to other views.
`componentOpacity(scope)` supplies the base mode's foreground opacity and
returns `1` in upper-image mode. The renderer accepts a diagnostic output for
missing, invalid or oversized images. `theme_background_tests` exercises the
call site with actual Windows mixer and SEL windows. Decoded and prepared
images use bounded caches that clear when the installed theme changes.
`ThemeBackgroundOverlay(scope, parent)` is a transparent, input-pass-through
child for QWidget-based hosts; construct it after the host controls. It follows
parent resizing and theme changes without a separate refresh timer.

[`ThemeManager.hpp`](../src/ui/include/WinJACKNexus/UI/ThemeManager.hpp)
adapts the v1 snapshot to existing `ThemePalette`, `${key}` QSS variables
and `setVariable`/`removeVariable` callers. The application installs one
process `ThemeCore` with `installThemeCore`; subsequently constructed legacy
managers follow its current snapshot. `watchApplication` records the live
`QApplication` and immediately applies the current snapshot's mapped colors
and global font, including a theme restored before the manager was created.
A manager can also call
`bindThemeCore` for an explicit core and `applyToApp` or `applyToWidget` for
immediate application. Theme changes update existing widgets and inherited
fonts for new windows. The manager retains each widget's size, weight and
other font settings while replacing the family; a widget using a dedicated
icon font sets `wjnPreserveFontFamily=true`. The system UI font comes from
`QFontDatabase::systemFont(GeneralFont)`. The first application explicitly
sets it even when the application's default already compares equal, clearing
Qt's per-widget-class defaults. It is restored for `system`, invalid font
data, preview cancellation and default restoration. Native Qt stylesheets inherit
this family by omitting `font-family`; never emit the literal family `inherit`.
Custom TTF/OTF bytes are
registered with `QFontDatabase::addApplicationFontFromData`, keyed by content
hash and held through previews; no font file is extracted. `fontDiagnostic`
reports registration failure or multiple font families. `theme_adapter_tests`
exercises palette and stylesheet mapping, memory font registration, fallback,
preview rollback and mixed text metrics. The 1.5× DPI UI test runs through
the actual Windows window platform; offscreen testing has been abandoned.

`ThemeManager` is allocated by value in distributed modules. Preserve its object
layout across overlay releases; additional state belongs behind its existing
internal shared pointer. The runtime meterbridge regression opens, paints,
closes and recreates the window through the DLL's actual menu and node exports,
including verification against the previously distributed meterbridge DLL.

The mixer has no category cards or category title bands. Input, AUX and
Listen/Master retain separate channel areas with full-height separators styled
by `--wjn-theme-mixer-resize-handle` (Input/AUX) and
`--wjn-theme-mixer-category-divider` (AUX/Listen). Drag the Input/AUX separator to resize both
areas; each area retains its own horizontal scrollbar. The former input, AUX
and master group accent tokens have been removed from the registry and template.
The monitor strip also has no card background or outline; its `monitor-surface`
and `monitor-border` tokens were removed from the mixer namespace. Its yellow
fader, meter and SEL button remain available.

[`ThemeNamespaceRegistry.hpp`](../src/ui/include/WinJACKNexus/UI/ThemeNamespaceRegistry.hpp)
is the S06 host registry for optional Node/Native Fx theme descriptors. Call
`registerNodeModule` or `registerNativeFxModule` with a loaded DLL's dynamic
symbol resolver and stable source path. The returned move-only `Lease` owns
the registration until `reset` or destruction; release it before unloading
the DLL. `scopeFor(kind, source, index)` binds each exported type to its
validated namespace. `descriptor(moduleId)` returns a shared immutable copy
of all JSON fields and token definitions, safe after DLL unload. Matching
dual-API descriptors share references by stable module ID and exact JSON;
conflicting descriptors are rejected. `registerInternal` supports built-in
descriptor tests without a DLL. Legacy modules without either optional theme
export succeed with an empty lease. `theme_namespace_tests` covers versions,
multi-node indices, conflicts, compatibility and unload lifetime.

## Animation and global settings

[`ApplicationSettings.hpp`](../src/ui/include/WinJACKNexus/UI/ApplicationSettings.hpp)
is the single source for the 30, 60 and 120 FPS choices. `setFrameRate`
normalizes any other value to 60, persists the choice and emits
`frameRateChanged`. `bindRefreshTimer(QTimer&, QObject*)` configures a precise
timer and updates its interval when the setting changes. Continuous UI
sampling, ballistics and repaint requests belong on such a timer; input
events update state without starting a second repaint loop. The integer
timer interval is an approximation of the selected FPS, not a real-time
display deadline.
`AnimatedWidget::setFrameRate` and `MeterArrayWidget::setFrameRate` update
that application setting. They do not create per-widget frame-rate overrides;
child meters and the array timer follow the same 30, 60 or 120 FPS choice.

The same settings object owns `peakValueRefreshMilliseconds` (8..1000 ms,
default 50) and `peakHoldSeconds` (0..10 s, default 1). Numeric peak readouts
sample at the former interval while their bars animate every display
frame. Peak markers use the latter duration. These preferences are stored in
the application's `QSettings` entry.

[`PaintableWidget`](../src/ui/include/WinJACKNexus/UI/PaintableWidget.hpp)
gates painting by visibility and rendering state. A subclass implements
`paintContent`; `requestRepaint` schedules a repaint when allowed.
[`AnimatedWidget`](../src/ui/include/WinJACKNexus/UI/AnimatedWidget.hpp)
adds a frame counter and a timer bound to the application setting. Override
`advanceFrame` for per-frame state; call `startAnimation` to request continuous
frames and `stopAnimation` to release the timer. Keep audio-thread work out of
paint and timer callbacks; pass snapshots into UI-owned state instead.

## Reusable meter and chart widgets

| Widget | Public contract |
| --- | --- |
| [`MeterWidget` / `PeakMeterWidget` / `MeterArrayWidget`](../src/ui/include/WinJACKNexus/UI/MeterWidget.hpp) | Scalar and multi-channel meters with range, thresholds, marks and channel labels. The array shares one animation timer and follows the application frame rate. `MidiVelocityWidget` specializes the array for 0..127. |
| [`MeterLevelBar`](../src/ui/include/WinJACKNexus/UI/MeterLevelBar.hpp) | Vertical level and peak bar with configurable range, marks, unit and theme. It draws through `Controls::drawMeter`. The `wjnThemeScope` widget property selects scoped v1 color tokens for level zones, border, peak, labels, and ticks; an unset property uses `global`. The meter bridge host sets it to `meter-bridge`. |
| [`FxLevelMeterWidget`](../src/ui/include/WinJACKNexus/UI/FxLevelMeterWidget.hpp) | Input/output effect meter. `setLevels` accepts dB values; its bar, held peak and numeric readout update on the application frame timer. Input/output readouts display −∞ at the silent meter floor. |
| [`HistoryChartWidget`](../src/ui/include/WinJACKNexus/UI/HistoryChartWidget.hpp) / [`HistoryLegendWidget`](../src/ui/include/WinJACKNexus/UI/HistoryLegendWidget.hpp) | Time-series chart and interactive series legend. Curves use the common `CurveStyle` painter. |
| [`MetricReadout`](../src/ui/include/WinJACKNexus/UI/MetricReadout.hpp) / [`MetricStatisticsWidget`](../src/ui/include/WinJACKNexus/UI/MetricStatisticsWidget.hpp) | Numeric measurement presentation and grouped statistics. Set `setPeakValueRefreshEnabled(true)` on peak readouts to sample their numbers at the global peak refresh interval; invalid values clear immediately. |

`MeterWidget`, `MeterLevelBar` and `FxLevelMeterWidget` calculate level and peak
decay from elapsed time between display callbacks. A delayed callback is capped
at 100 ms of visual motion, and a peak begins decaying only after its configured
hold time.

## Host windows and lifetime

[`MixerWindow`](../src/ui/include/WinJACKNexus/UI/MixerWindow.hpp) accepts a
`DigitalMixerState`, a topology commit callback and optional meter, polar
and effect-analysis readers. The callbacks remain valid for the lifetime of
the window. [`AuxSettingsWindow`](../src/ui/include/WinJACKNexus/UI/AuxSettingsWindow.hpp)
edits the source's AUX state in its own top-level window.
Its polar reader runs only while the source AUX master and selected target
send are both enabled. Disabling either switch clears both polar curves
immediately; disabled AUX routes do not return processor polar snapshots.
[`FxSettingsWindow`](../src/ui/include/WinJACKNexus/UI/FxSettingsWindow.hpp)
hosts the mixer FX chain and module-provided editors; a native effect keeps
its own processing and parameter snapshot under the Native Fx API. UI widgets
are created and destroyed on the Qt UI thread. Destroy an effect editor
before unloading the DLL that supplies its code.

The Node module export contract and project JSON envelope are specified in
[`Node-API.md`](../Develop/Node/Node-API.md); the Native Fx lifecycle, snapshots and live
analysis are specified in [`NativeFx-API.md`](../Develop/NativeFx/NativeFx-API.md). Changes to an
exported UI signature or behavior require updating this document and its
call-site tests before the API can be considered frozen.

### FX chain presets

`FxSettingsWindow` saves complete, independent chains under
`<application directory>/fx_chain_presets/*.json`. Save overwrites the selected
preset; Save As (or Save without a selection) asks for a display name and creates
a new UUID-named file. Files use `format=WinJACKNexus.FxChainPreset`, `version=1`,
`name`, `enabled`, and ordered `effects` containing `moduleId`, `enabled`,
`preset`, and `parameters` (including each Native Fx eight-channel snapshot).
Missing modules retain their saved data when a preset is loaded and saved again.
Selecting a preset commits it to the current channel. Refresh rescans the folder
without applying a chain; malformed or unsupported files are omitted with a log
warning. Writes are atomic, and failures leave the previous file intact.

Preset actions use registered `sel-fx` tokens
`control-preset-button-{text,surface,border,hover-surface,hover-border}`,
inheriting the corresponding ordinary button colors. Existing shared
ActionButton, ChoiceField and TextField controls are reused.

The quick access File menu shares project actions with the patchbay File menu
but excludes the close-window action.

### Custom LUFS references and history PNG export

The meter card's Custom reference opens an editor for a name, integrated loudness
(-70..0 LUFS), optional upper target, and maximum true peak (-60..12 dBTP).
Saving creates an atomic, independent JSON file in
`<application directory>/lufs_presets`. Refresh scans that directory and updates
all cards through the application's regular refresh cycle, preserving their
selection. The shared Common helper `MeterCustomLoudnessPresets.hpp` owns the
process-wide catalog; its refresh, lookup, revision and save functions are called
on the GUI thread. Custom IDs survive project reload and drive the actual history
reference lines, including optional lower/upper targets. Files use
`format=WinJACKNexus.LufsPreset`, `version=1`, `id`, `name`,
`targetIntegratedLufs`, optional `alternateTargetHighLufs`, and
`maximumTruePeakDbtp`; invalid or duplicate files are skipped with a log warning.

The history Export action prompts for a PNG filename and atomically writes the
current visible chart (axes, enabled curves and reference lines). Cancel writes
nothing. Sidebar fields, actions, switches and preset refresh controls use the
shared sidebar control height. Preset dropdown popups explicitly theme their
background, text and selection with registered `meter-bridge` tokens
`control-preset-popup-{surface,text,border,selected-surface,selected-text}`.
Custom preset actions use `control-preset-action-{surface,text,border}`.

The tracked `wjn_preset_tests` target verifies both preset editors, actual Native
Fx snapshots and missing-module roundtrips, LUFS file refresh and chart targets,
popup rendering, equal field heights, and PNG writes into a manually selected
subdirectory (plus cancellation). Run `ctest -C Release -R ^preset_tests$` in the
existing build tree. A generated template archive is also imported when present.

The display settings CSV directory remains the real recording root, independent
of the PNG Save As location. Snapshot refresh must not overwrite that field while
it has keyboard focus; the directory command commits on editingFinished.

AUX route lists display the custom source and destination channel names. Cached per-source AUX metadata follows target renames by bus ID; routing and self-route exclusion remain ID based. The accessible description exposes the same route labels drawn in the list.

Mixer strips show their custom names at the top. Bottom CH/AUX/MON/MAIN identity labels are hidden for every strip; internal routing IDs and control colors are retained. These removed labels have no dedicated theme tokens.

Mixer custom name backgrounds and strip outlines use the channel color. Name text switches between `--wjn-theme-mixer-control-name-text-dark` and `--wjn-theme-mixer-control-name-text-light` according to background brightness.

Monitor output is silent when no strip has PFL/AFL selected; enabling MIX alone does not feed monitor output. Mixer meters and faders use reclaimed footer space. AUX route names elide to available width and native Qt tooltips show the full source and target names.

Input and AUX PFL monitor taps sample after input gain and before FX, mute, solo gating and fader processing. AFL continues to sample the post-FX, post-fader path and follows mute. AUX pre-fader send taps retain their existing post-FX behavior.

`AuxSettingsWindow::setChannelMeterReadCallback(std::function<float(bool, std::size_t)>)` supplies each source channel peak independently; `bool` selects pre/post and the index follows L/R/C/LFE/Ls/Rs/Lb/Rb (mono index 0). It takes precedence over the legacy aggregate meter callback. Mixer AUX views use the per-channel provider and display silence when no provider exists rather than copying the strip peak to every channel. Each channel retains independent meter ballistics and peak hold.

Horizontal meter scale labels are centered on their tick coordinates, including minimum and maximum endpoints. Callers should allow 15 px of space beside the track for endpoint labels; this applies to scales above and below the meter.

## Shared standard channel names

Common `StandardChannelNames.hpp` owns the vocabulary L/C/R/Lw/Rw/Ls/Rs/Lrb/Rrb/LFE/Ltf/Rtf/Ltm/Rtm/Ltr/Rtr. `ChannelNames.hpp` supplies Qt labels and meter role names. Vocabulary order is not sample order: existing 5.1/7.1 buffers remain L/R/C/LFE/Ls/Rs[/Lrb/Rrb]; mono is M. Mixer ports, FX selectors, MAIN/AUX/monitor controls, spatial panner labels, Native Fx, adapter ports, VST standard-name ports and probe defaults reuse this source. Adapter audio ports are directly named M or their standard speaker names, replacing capture_N/playback_N. VST standard mode retains in_/out_ prefixes; Native Fx retains in_/out_ prefixes. Existing custom probe names are retained. After changed port names, reconnect affected ports and save the project.

SEL MAIN and SEL AUX per-channel controls render and accept pointer input only for channels present in the current source layout (mono 1, stereo 2, 5.1 6, 7.1 8). Unused rows are hidden, including their knobs, meters, and mute/solo controls.

## Sub Mix

`MixerStripKind::subMix` is a bus with mono/stereo/5.1/7.1 layout, Gain, FX, green fader, M/S and mutually exclusive PFL/AFL. Its MAIN panner and channel trims act on its own output before downstream mixing. `DigitalMixerState::subMixes()` exposes the buses. `setSend(sourceId,send,error)` accepts AUX or Sub Mix targets; Sub Mix sends must be post-fader. `setSubMix(sourceId,targetId,error)` adds an enabled post-fader send (target 0 clears all Sub Mix sends). Multiple enabled Sub Mix sends are supported. `hasSubMix(sourceId)` reports whether any are enabled. Self routing and cycles (including disabled edges and mixed AUX/Sub Mix paths) are rejected transactionally. A Sub Mix cannot send to AUX. MAIN and Monitor have no sends.

Any enabled Sub Mix send suppresses the source's direct MAIN contribution. An AUX with at least one enabled Sub Mix send hides its own output ports; disabling all such sends restores them. Sub Mix outputs remain exposed. Routes and per-send panners are saved by stable ID in the existing sends array; optional version-1 Sub Mix bus fields preserve old project compatibility. Removing a bus removes incoming routes. Reconnect affected ports and save existing projects when AUX output visibility changes.

SEL adds a Sub Mix tab for all strips except MAIN/Monitor. It reuses `AuxSettingsWindow::setSubMixMode(bool)` to show the shared send list, per-send panner and channel controls, with no Pre/Post or bus management buttons and permanently post-fader sampling. Sub Mix SEL has MAIN, FX and Sub Mix tabs without AUX. The detail header includes a categorized strip selector before MAIN. The shared send controls use the existing `aux` theme namespace; the green fader thumb/track/grip, Sub Mix scrollbar and adjustable divider have registered mixer tokens. IN/AUX/Sub Mix use independently scrollable regions with default widths 6:2:2 and two draggable dividers.

## Mixer channel overview

The native “查看大屏” menu contains “通道详情” (the SEL window) and “通道总览”. Overview is a separate window with three equal rows: Inputs, AUX and Sub Mix/Monitor/Master, preserving each category's strip order. Each row has an always-visible independent horizontal scrollbar using the existing mixer track and per-category thumb tokens, and scrolls horizontally and shows compact strips with channel names, a meter with peak hold, SEL and an editable fader. No M/S, routing or FX controls are added. Meter samples, ballistics and frame cadence come from the main mixer refresh; no separate timer is used. Fader and meter colors, channel name colors and SEL colors inherit the existing mixer tokens. Overview category headers use `overview-title-{input,aux,submix}-{surface,text,border}`; outer group cards use `overview-card-{surface,border}`, all in the mixer namespace.

Overview SEL resolves the channel's stable identity, switches the existing SEL window and aligns that strip to the leading edge of its main mixer region. Scroll ranges can extend with trailing space so even the last channel can be first in view. The three regions scroll independently. Compact fader travel reserves thumb clearance below the title and above the value readout. Double-clicking the fader resets to 0 dB; clicking its value opens the shared numeric editor with the main mixer’s -72 to +12 dB range. Numeric edits resolve stable channel identity at commit time. Overview edits commit to the same mixer model and remain synchronized with edits in the main mixer. Closed overview windows can be reopened; removed strips and changed ordering are reconciled during the main refresh.


Overview backgrounds have an independent `mixer-overview` namespace and a separate entry in the theme background editor. The eleven `--wjn-theme-mixer-overview-background-*` tokens mirror the existing image, size, position, repeat, scale, opacity, blur, overlay and component-opacity controls. Their default inheritance is directly from patchbay. Base backgrounds render beneath overview content; overlay backgrounds render above it through the shared input-transparent overlay, while component opacity applies to all three cards. Compact controls continue using mixer tokens.

## Independent mixer views and channel selection

`MixerWindow::openView(View)` opens and returns just the requested top-level view: `mixer=0`, `channelDetails=1`, or `channelOverview=2`. A hidden main mixer remains the shared controller, and closing it does not destroy the detail or overview window. The node owns the controller until explicit window destruction or shutdown. All views continue using the application's refresh cadence and actual mixer model. The quick-access mixer button opens a native Qt menu with exactly 调音台 / 通道详情 / 通道总览; each entry opens that view directly, including when the mixer has never been shown. `TrayController::mixerDetailsAction()` and `mixerOverviewAction()` expose the corresponding actions; availability follows the loaded Digital Mixer node.

The detail header's shared `IconActionButton` precedes MAIN and opens a native categorized menu: Inputs / AUX / Sub Mix / Listen/Master. Subitems use current custom names and stable identity, and are rebuilt when opened to reflect renamed, reordered, added or removed strips. Listen/Master contains MAIN and Monitor. Monitor uses the same detail window with only MAIN and its existing read-only plot and common sidebar. Header icons are vector microphone / send / summing / speaker / headphones artwork for Input / AUX / Sub Mix / MAIN / Monitor respectively. The selector uses `--wjn-theme-sel-main-control-channel-selector-{surface,text,border,accent}` and icons use `--wjn-theme-sel-main-control-header-icon`; all are registered colors.

The overview third row has fixed compact Monitor and MASTER strips on the **right**, outside the Sub Mix horizontal scroll area. Both have the actual fader, level/peak-hold meter and SEL, including double-click reset and manual numeric entry. Their controls inherit the mixer monitor/master tokens. Scrolling Sub Mix leaves these two strips stationary.

MASTER channel details have no input Gain label, knob or numeric readout; Input/AUX/Sub Mix retain their input Gain controls.

## SEL sidebar actions

When a process ThemeCore is installed, patchbay construction preserves the application stylesheet applied from the restored theme. Its legacy light/dark stylesheet is a fallback only when no valid process core exists. Startup sidebar buttons and choice fields therefore use the same selected theme as after a manual theme switch.

Input, AUX and Sub Mix detail sidebars use two rows of three shared-drawn buttons: M / S / MIX and PFL / AFL / FX. MASTER uses two rows of one button: M and FX, with no input Gain, Solo, MIX or listen controls. M toggles explicit mute, S toggles SIP solo, MIX enables routing to MAIN, and PFL/AFL are mutually exclusive listen selections. PFL samples after gain before mute/fader; AFL follows mute/fader. FX enables or bypasses the existing effect chain and synchronizes an open FX editor. Buttons use the same geometry for painting and hit detection; MIX uses the main mixer's `--wjn-theme-mixer-control-button-mix-accent`; `--wjn-theme-sel-main-control-fx-accent` is registered alongside existing M/S/PFL/AFL accents. Changes commit through the mixer model; failed commits restore button state. SEL input/output meter readouts display −∞ at the silent meter floor.

All four SEL strip types have an 音量 knob bound directly to the strip's fader position. Input/AUX/Sub Mix place it beside input Gain (labelled 增益); MASTER has this volume knob alone. Dragging updates the actual fader, main/overview fader edits repaint the same knob, double-click resets to 0 dB, and clicking its dB value opens the shared -72 to +12 dB numeric editor. The bottom knob position is −∞, matching the fader. Switching strips cancels knob dragging and numeric edits resolve the original stable strip identity. The volume knob shares the existing knob palette with Gain and the other SEL knobs, including `--wjn-theme-sel-main-control-knob-indicator-normal` and its global knob inheritance; it has no separate volume color token.
