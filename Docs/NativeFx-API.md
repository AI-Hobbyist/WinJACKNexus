# Native Fx module API (v1)

Each effect builds as its own DLL under `build/bin/nodes/native_fx/<effect>/`.
Its source lives under `src/native_fx/effects/<effect>/`. `Meta.hpp` stays at
the effect root; headers and `Logic.inc` live in `include/`; `Ui.cpp` and
`Dsp.cpp` live in `src/`. `Meta.hpp` declares placement and visual identity.
`Logic.inc` defines the JACK node adapter. `Dsp.cpp` implements that effect's
algorithm, and `Dsp.hpp` adapts it for mixer blocks and parameter updates.
The shared Native Fx API owns the mixer instance. `Ui.cpp` and `Dsp.cpp` are
compiled into the effect DLL; `ui.dll` supplies reusable controls and the host
window. Effect algorithms are no longer built into `common.dll`.
The DLL exports the functions in
[`NativeFxApi.h`](../src/native_fx/include/WinJACKNexus/NativeFx/NativeFxApi.h).
The host resolves them dynamically and checks `wjn_fx_api_version() == 1`.
Each DLL currently exposes one record: `wjn_fx_count() == 1` and
`wjn_fx_meta(0)`.

## Placement

`WjnFxMeta::placements` is a bit mask:

| Value | Meaning |
| --- | --- |
| `WJN_FX_MIXER` | Available inside the mixer's FX chain. |
| `WJN_FX_NODE` | Available as a patchbay node in `Native Fx`. |
| Both bits | Both hosts may instantiate the effect independently. |

The mixer lists only modules with `WJN_FX_MIXER`. The patchbay's node API
enumerates only modules with `WJN_FX_NODE`; a mixer-only DLL exports a node
count of zero and never appears in `Native Fx`. `id` is a stable ASCII key for
presets; `name`, `iconGlyph` and `iconColor` are presentation metadata. Keep
the metadata strings valid until the DLL unloads.

## Mixer lifetime and audio

1. Resolve the v1 symbols, read metadata, and call `wjn_fx_create_mixer(0)`.
2. Call `wjn_fx_prepare(instance, sampleRate)` before audio processing and
   whenever the sample rate changes. The instance owns eight independent
   channel processors, ordered `L`, `R`, `C`, `LFE`, `Ls`, `Rs`, `Lb`, `Rb`.
3. Call `wjn_fx_process(instance, channel, input, output, frames)` with
   channel `0..7`. Input and output may alias. A null input writes silence.
4. On the UI thread, call `wjn_fx_create_mixer_editor(instance, parent)` once.
   The returned widget belongs to the instance. It displays the native
   controls below the node-only channel strip. The chain owns the shared
   selector and passes `-1` for All or `0..7` via `wjn_fx_set_channel`.
5. Destroy the instance with `wjn_fx_destroy_mixer` before unloading its DLL.

`wjn_fx_clone_mixer(source)` creates a separate mixer instance with the same
eight channel parameter values, selected channel and palette. It resets audio
history and creates its editor on demand. The caller owns the clone and must
destroy it with `wjn_fx_destroy_mixer`. The mixer uses this optional v1 symbol
for the chain item's **克隆** command; modules without it show that command
disabled.

Mixer instances contain no JACK client or ports. The host supplies routing and
applies its master/per-module bypass. Parameter edits and `process` must be
serialized by the host; the v1 API does not promise concurrent parameter
mutation and audio processing.

## Node placement

Node-capable DLLs also expose the existing `NodeApi.h` entry points. Node index
zero maps to the DLL's effect. The node adapter owns its JACK client and
eight input/output ports, and its standalone editor retains its channel
selector. The patchbay scans node DLLs recursively, so each DLL appears once.

## Theme and shared controls

`wjn_fx_set_theme` accepts optional color overrides for background, panel,
text, accent and border. A null field keeps that color's default. Call it on
the UI thread; the palette is copied into the instance and applied to its
editor. `UI/Controls.hpp` provides shared drawing calls for buttons, knobs,
sliders, toggles, meters, chart frames, dropdowns and card labels, along with
palette application and the embedded editor container. Effect code can call
any `Controls::draw*` function with its own `Controls::Palette` or
`MeterColors`. `Controls::paletteFor(widget, defaults)` merges host theme
overrides into an effect's preferred colors. `ControlRegistry::create(key,
parent)` creates common Qt controls by key (`button`, `slider`, `dropdown`,
`toggle`, `label`, `card`). A plugin may call `registerFactory(key, factory)`
to inject a custom widget implemented in its own DLL. Keep the registry inside
that plugin's lifetime so the factory is never called after unload. The mixer
currently supplies its dark FX palette when it creates an instance.

### Built-in control API

Include `<WinJACKNexus/UI/Controls.hpp>` and link `WinJACKNexus::UI`.
All drawing functions use the caller's `QPainter`; call them from a widget's
`paintEvent`. Coordinates are in widget pixels. `normalized` values are
clamped to `[0, 1]`. The draw functions paint only: the plugin owns hit
testing, value storage and signal handling. This lets an effect keep its own
interaction model while sharing shape and color conventions.

| Name | C++ call | Result |
| --- | --- | --- |
| Palette | `Controls::Palette` | Five overridable colors: `background`, `panel`, `text`, `accent`, `border`. |
| Apply palette | `applyPalette(QWidget*, const Palette&)` | Sets Qt palette and the `fxBackground`, `fxPanel`, `fxText`, `fxAccent`, `fxBorder` properties. |
| Read color | `themeColor(const QWidget&, const char* key, const QColor& fallback)` | Reads a color property, using `fallback` if absent. |
| Resolve palette | `paletteFor(const QWidget&, const Palette& defaults = {})` | Merges the widget's five theme properties with module defaults. |
| Switch | `drawToggle(QPainter&, const QRectF&, bool on, const QColor& accent, const QColor& offTrack)` | Draws a two-state track and thumb. |
| Toggle widget | `Controls::ToggleButton(QWidget* parent = nullptr)` | Interactive checkable switch using the shared drawing function; call `setChecked` and connect the inherited `toggled(bool)` signal. |
| Button | `drawButton(QPainter&, const QRectF&, const QString& caption, bool selected, const Palette&, int fontSize = 16)` | Draws an outlined action or selection button. |
| Knob | `drawKnob(QPainter&, const QPointF& center, qreal normalized, int radius, const Palette&)` | Draws the circular value arc and pointer. |
| Slider | `drawSlider(QPainter&, const QRectF&, qreal normalized, bool vertical, const Palette&)` | Draws a horizontal or vertical fill and thumb. |
| Chart | `drawChart(QPainter&, const QRectF&, int columns, int rows, const Palette&)` | Draws chart background, border and grid. Draw effect curves separately with `Qt::NoBrush`. |
| Dropdown | `drawDropdown(QPainter&, const QRectF&, const QString& caption, const Palette&, int fontSize = 16)` | Draws the selection field and chevron. |
| Meter colors | `Controls::MeterColors` | Configures active/inactive low, middle and high zones plus border. |
| Level meter | `drawLevelMeter(QPainter&, const QRectF&, double levelDb, const MeterColors& = {})` | Draws a horizontal meter from −60 to +12 dB, with breaks at −12 and 0 dB. |
| Card label | `drawCardLabel(QPainter&, const QRectF&, const QString& title, const QString& glyph, const Palette&)` | Draws a panel label with an icon badge. |

`ControlRegistry` can create these built-in Qt widget names. Call
`registry.create(key, parent)`; the returned widget belongs to `parent` under
normal Qt ownership. The registry itself stays in the plugin instance.

| Key | Widget class | Intended use |
| --- | --- | --- |
| `button` | `QPushButton` | Action or selection button. |
| `slider` | `QSlider` | Interactive value slider. |
| `dropdown` | `QComboBox` | Interactive choice list. |
| `toggle` | `Controls::ToggleButton` | Interactive two-state switch drawn with `drawToggle`; supports `setChecked` and the inherited `toggled` signal. |
| `label` | `QLabel` | Text or icon label. |
| `card` | `QFrame` | Container panel. |

`registry.registerFactory(key, [](QWidget* parent) -> QWidget* { ... })`
registers or replaces a key with a plugin-defined widget factory. The
factory must return a widget whose ownership is compatible with `parent`;
`create` returns `nullptr` for an unknown key. Plugin-defined factories take
precedence over built-ins, so an effect can override a built-in without a host
change. The reusable `ClientNameWidget(QString, ApplyHandler, QWidget*)`
provides the JACK node name editor; `setClientName` updates its text.
`EmbeddedFxEditor(QWidget* editor, int stripHeight, QWidget* parent)` is the
host container that exposes the editor below its node-only channel strip;
`editor()` returns its child editor.

## Adding an effect

Add `Meta.hpp` at the effect root, `Ui.hpp`, `Dsp.hpp` and `Logic.inc` in its
`include/` tree, and `Ui.cpp`, `Dsp.cpp` in `src/`.
Use `UI/Controls.hpp` for reusable controls and register plugin-specific
widgets locally through `ControlRegistry` when needed. Add its mixer processor
adapter in `NativeFxApi.cpp`, select its node adapter in `Library.cpp`, and add
one target entry in `src/native_fx/CMakeLists.txt`. A mixer-only effect needs no
JACK adapter and must report zero nodes. Add loading and editor tests that
exercise the built DLL through its exported API.
