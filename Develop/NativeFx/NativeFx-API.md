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
[`NativeFxApi.h`](../../src/native_fx/include/WinJACKNexus/NativeFx/NativeFxApi.h).
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
lists only modules with `WJN_FX_NODE`; a mixer-only DLL does not advertise a
Node module and never appears in `Native Fx`. Node placement follows the
[Node v4 contract](../Node/Node-API.md), with creation-free capability and GUI
metadata and an explicit instance mode. S2 advertises JACK only; S4 adds VIRT
after the internal processing path is implemented. Mixer placement remains
independent of these graph capabilities. `id` is a stable ASCII key for
presets; `name`, `iconGlyph` and `iconColor` are presentation metadata. Keep
the metadata strings valid until the DLL unloads.

### Required node boundary (v1)

For `WJN_FX_NODE`, the DLL must also export
`wjn_fx_node_boundary_version() == WJN_FX_NODE_BOUNDARY_VERSION` (currently 1),
`wjn_fx_node_validate_layout(node)`, `wjn_fx_node_client_name(node)`,
`wjn_fx_node_set_client_name(node, utf8Name)`, `wjn_get_node_client_id(node)`
and `wjn_show_node_gui(node)`. `node` is the `INode*` from `wjn_create_node_v4`.
The patchbay checks the exports before listing or creating the module. After
`INode::init()`, it requires layout validation and a nonempty client name.

The node owns one JACK client with exactly eight audio inputs `in_L`, `in_R`,
`in_C`, `in_LFE`, `in_Ls`, `in_Rs`, `in_Lrb`, `in_Rrb` and matching `out_*` outputs
in that order. `wjn_fx_node_validate_layout` checks the registered port names,
directions and audio types, returning 1 only when they match. It returns 0
before initialization or for a mismatched node.

The patchbay creates the right-sidebar `ClientNameWidget` itself and calls the
name getter/setter. Native Fx node settings widgets are not embedded there;
effect parameters belong in the mandatory standalone GUI. The setter accepts
UTF-8, returns 1 on success and 0 on rejection, and must preserve JACK
connections when an active client is renamed. The getter returns a DLL-owned
string valid until the next name mutation or node destruction.

The current Delay, Limiter and Reverb modules reject rename requests while
their JACK client is open. This is a module-level limitation: the host keeps
the node active and displays a failed-apply status. Other modules may support
live rename by reopening and reconnecting their client.

`wjn_show_node_gui` opens the standalone editor and returns 1 on success.
The mixer editor is a separate widget embedded in the chain. Providing one
does not replace the node's standalone GUI. Mixer-only modules need no node
boundary exports.

## Mixer lifetime and audio

1. Resolve the v1 symbols, read metadata, and call `wjn_fx_create_mixer(0)`.
2. Call `wjn_fx_prepare(instance, sampleRate)` before audio processing and
   whenever the sample rate changes. The instance owns eight independent
   channel processors, ordered `L`, `R`, `C`, `LFE`, `Ls`, `Rs`, `Lrb`, `Rrb`.
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

### Parameter snapshots and project files

The optional `wjn_fx_parameter_snapshot_*` exports provide a versioned binary
snapshot of all eight channels. Check `wjn_fx_parameter_snapshot_version() == 1`,
allocate `wjn_fx_parameter_snapshot_size()` bytes, then call
`wjn_fx_export_parameter_snapshot`. The blob starts with
`WjnFxParameterSnapshotHeader`: `formatVersion`, `parameterBytes`,
`channelMask`, a reserved word and the stable `moduleId`. Import validates the
version, size and module ID. The mask selects channels to replace; channels
outside it retain their parameters. The module retains its DSP history. The
host calls `wjn_fx_set_parameter_change_callback` to learn when an editor has
changed parameters and should persist a new snapshot. A null callback removes
the notification.

The main `.wjnproj` document stores mixer and Native Fx node settings as a
Base64 encoded JSON object in `settingsBase64`. Decode the field and parse the
result as JSON before restoring a node. Older projects with a direct `settings`
object remain readable. The Native Fx snapshot bytes inside mixer JSON have
their own version and module validation; the outer Base64 encoding only
packages the JSON for the project file.

### Live analysis and presets

`wjn_fx_read_analysis` returns the latest eight input and output peak values
from the audio instance. The optional live analysis bridge adds eight gain
reduction values and a module-specific spectrum frame. The host installs a
`WjnFxLiveAnalysisReader` with `wjn_fx_set_live_analysis_reader`; the reader
resolves the current audio instance for each call. Pass the spectrum buffer
and its byte capacity to `wjn_fx_read_live_analysis`. A zero result means no
frame was available. The host resolves optional preset exports dynamically:
`wjn_fx_preset_count`, `wjn_fx_preset_name`, `wjn_fx_current_preset` and
`wjn_fx_select_preset`. Preset indices and names must agree with the editor.

## Node placement

Node-capable DLLs expose [Node SDK v3](../Node/Node-API.md) entry points.
`wjn_create_node(void)` creates the DLL's single effect node; no Node count
or type index remains. Native Fx metadata/mixer indices are separate and retain
the existing Native Fx ABI. The node adapter owns its JACK client and
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
`MeterStyle` or `CurveStyle`. `Controls::paletteFor(widget, defaults)` merges host theme
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
clamped to `[0, 1]`; the bipolar Pan knob instead takes a value in `[-1, 1]`.
The draw functions paint only. The shared numeric editor and reset helper
below provide the readout and double-click interaction; the plugin supplies
hit rectangles and the callback that writes its parameter state.

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
| Pan knob | `drawPanKnob(QPainter&, const QPointF& center, qreal pan, qreal radius, const Palette&)` | Draws a bipolar Pan knob. `-1` is L100%, `0` is center, `+1` is R100%; its accent arc starts at center and grows toward the selected side. |
| Slider | `drawSlider(QPainter&, const QRectF&, qreal normalized, bool vertical, const Palette&)` | Draws a horizontal or vertical fill and thumb. |
| Chart | `drawChart(QPainter&, const QRectF&, int columns, int rows, const Palette&)` | Draws chart background, border and grid. Draw effect curves separately with `Qt::NoBrush`. |
| Dropdown | `drawDropdown(QPainter&, const QRectF&, const QString& caption, const Palette&, int fontSize = 16)` | Draws the selection field and chevron. |
| Meter colors | `Controls::MeterColors` | Configures active/inactive low, middle and high zones plus border. |
| Level meter | `drawLevelMeter(QPainter&, const QRectF&, double levelDb, const MeterColors& = {})` | Draws a horizontal meter from −60 to +12 dB, with breaks at −12 and 0 dB. |
| Configurable meter | `drawMeter(QPainter&, const QRectF&, double levelDb, std::optional<double> peakDb, const MeterStyle&)` | Configures range, any number of color zones and ticks, label, horizontal or vertical orientation, and a peak marker. `showPeak` defaults to true. Set `fillOriginDb` to fill from a reference value toward `levelDb`, as on gain-reduction meters. |
| Meter scale | `drawMeterScale(QPainter&, const QRectF&, const MeterStyle&)` | Draws the same ticks and labels without a meter track, for a separate scale alongside one or more channel bars. `ticksLeft`, `ticksAbove`, `tickBandPixels`, `tickFontPixelSize`, `tickColor` and `labelColor` control placement and appearance. |
| Meter ballistics | `MeterBallistics::advance(inputDb, elapsedSeconds, holdSeconds)` | Smooths the visible level and holds then decays an independent peak. Pass elapsed frame time based on the application FPS and the global peak hold setting; read `levelDb()` and `peakDb()` for `drawMeter`. |
| Curve path | `drawCurvePath(QPainter&, const QPainterPath& strokePath, const CurveStyle&, const QPainterPath* fillPath = nullptr)` | Draws an optional fill path separately, then draws the stroke with `Qt::NoBrush`. `fillBrush` accepts a gradient and `showStroke` permits fill-only layers. Effect code supplies its DSP-specific curve geometry. |
| Polar curve | `drawPolarCurve(QPainter&, QPointF center, qreal radius, std::span<const float> values, const CurveStyle&)` | Configures stroke and optional fill, line style and value range. The caller places any legend text. |
| Exact value | `editNumericValue(QWidget* owner, const QRect& readout, double currentValue, const NumericControlSpec&, std::function<void(double)> apply)` | Overlays the painted readout with a selected numeric value. Enter commits a valid value; Escape or focus loss cancels. The owner paints the readout again after the editor closes. |
| Reset value | `resetNumericValue(const NumericControlSpec&, const std::function<void(double)>& apply)` | Applies the bounded default, typically from a knob or fader double-click handler. |
| Card label | `drawCardLabel(QPainter&, const QRectF&, const QString& title, const QString& glyph, const Palette&)` | Draws a panel label with an icon badge. |

Use `drawPanKnob` for classic stereo balance or mono-to-stereo Pan. The caller
stores the Pan value, handles pointer input and labels L/C/R. At center the
pointer faces up and the accent arc has zero length. The shared painter is
used by both Main and AUX SEL details; each retains its own Pan value.

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

## Optional theme namespace exports (v1)

`NativeFxApi.h` declares `uint32_t wjn_fx_theme_api_version(void)` and
`const char* wjn_fx_theme_namespace_json(uint32_t index)`. These exports are
optional and have their own version (`WJN_FX_THEME_API_VERSION == 1`); the
existing Native Fx ABI version is unchanged. The index follows
`wjn_fx_count()`/`wjn_fx_meta(index)`, and an invalid index returns null.
Each returned pointer is DLL-owned UTF-8 JSON. The host copies and validates
it before unloading the DLL. A module without both exports continues to load;
an incomplete, unsupported or invalid theme descriptor disables only its
theme capability. The descriptor format, size limits, token types and stable
module ID/namespace rules are in [Theme-v1-Contract.md](../../Docs/Theme-v1-Contract.md).
The host registers one scope per metadata index and retains a reference until
that module use is released. Matching Node and Native Fx descriptors for the
same stable ID share one registration. Existing effect DLLs receive these
exports individually after S12; S06 only establishes the API and host.
`mod_gate.dll` is the first migrated production effect. It exports both
optional theme functions for metadata index zero; its descriptor shares the
stable `gate` ID and `--wjn-theme-native-fx-gate-` scope with its Node entry.
The real-window `theme_gate_native_fx` test checks dual registration,
independent reference release, shipped-default pixel equality, and a live
token change without JACK connections.
`mod_graphic_eq.dll` is the second migrated production effect. Its index-zero
descriptor shares the `graphic-eq` scope with the Node entry and declares
the 31 effect-band controls as sliders, plus independent input spectrum,
output spectrum, and response chart series. `theme_graphic_eq_native_fx`
checks dual registration, default-package pixel equality, and live token
changes in the actual Windows editor without JACK connections.
`mod_deesser.dll` is the third migrated production effect. Its index-zero
descriptor shares the `deesser` scope with its Node entry. It declares knob
and toggle controls plus separate response bell, input spectrum, and output
spectrum chart series. `theme_deesser_native_fx` checks both API leases,
shipped-default pixel equality, and live editor token changes.
`mod_compressor.dll` is the fourth migrated effect. Its index-zero descriptor
shares `compressor` with the Node entry and declares knob/toggle controls,
transfer and level-fill chart series, and the reduction meter zone.
`theme_compressor_native_fx` checks both leases and actual-window default
pixel equality and token repaint without JACK connections.
`mod_limiter.dll` is the fifth migrated effect. Its descriptor shares the
`limiter` scope with the Node entry, declares the transfer curve, and names
low/mid/high active and inactive level meter colors plus the reduction zone.
`theme_limiter_native_fx` checks dual leases, index bounds, default-package
pixel equality, and live editor recoloring.
`mod_delay.dll` is the sixth migrated effect. Its index-zero descriptor
shares `delay` with the Node entry and declares the primary tap, repeated
tap, and baseline chart series. `theme_delay_native_fx` verifies the dual
leases and actual-window default and modified screenshots.
`mod_parametric_eq.dll` is the seventh migrated effect. Its descriptor
shares `parametric-eq` with its Node entry, declares four separately named
band curves and knob controls, input/processed spectra, total response,
filter knob, and toggle. `theme_parametric_eq_native_fx` validates the dual
leases, default-package pixel equality, and live color changes.
`mod_reverb.dll` completes the eight production effects. Its `reverb`
descriptor is shared with the Node entry and declares violet/cyan knobs,
toggle, and scene accent, shape-light, and glow elements. It has no dB chart
series or level meter. `theme_reverb_native_fx` verifies both leases and the
real-window default and modified theme screenshots. All eight DLLs now
provide the optional v1 theme exports.

## Adding an effect

Add `Meta.hpp` at the effect root, `Ui.hpp`, `Dsp.hpp` and `Logic.inc` in its
`include/` tree, and `Ui.cpp`, `Dsp.cpp` in `src/`.
Use `UI/Controls.hpp` for reusable controls and register plugin-specific
widgets locally through `ControlRegistry` when needed. Add its mixer processor
adapter in `NativeFxApi.cpp`, select its node adapter in `Library.cpp`, and add
one target entry in `src/native_fx/CMakeLists.txt`. A mixer-only effect needs no
JACK adapter and must report zero nodes. Add loading and editor tests that
exercise the built DLL through its exported API.

## Shared standard channel names

Common `StandardChannelNames.hpp` owns the vocabulary L/C/R/Lw/Rw/Ls/Rs/Lrb/Rrb/LFE/Ltf/Rtf/Ltm/Rtm/Ltr/Rtr. `ChannelNames.hpp` supplies Qt labels and meter role names. Vocabulary order is not sample order: existing 5.1/7.1 buffers remain L/R/C/LFE/Ls/Rs[/Lrb/Rrb]; mono is M. Mixer ports, FX selectors, MAIN/AUX/monitor controls, spatial panner labels, Native Fx, adapter ports, VST standard-name ports and probe defaults reuse this source. Adapter audio ports are directly named M or their standard speaker names, replacing capture_N/playback_N. VST standard mode retains in_/out_ prefixes; Native Fx retains in_/out_ prefixes. Existing custom probe names are retained. After changed port names, reconnect affected ports and save the project.
# Optional i18n v1 extension (S1)

The required Native Fx v1 API and `WjnFxMeta` layout remain unchanged.
`NativeFxApi.h` includes `WinJACKNexus/Common/I18nApi.h`. Hosts dynamically
probe `wjn_fx_i18n_api_version()` and
`wjn_fx_i18n_descriptor_json(uint32_t index)`; version is 1, invalid index
returns NULL. Node-capable effects may expose both Node and FX descriptor
exports, using the same moduleId/domains/locate root. Lifecycle exports
attach/change/detach apply once per DLL, not once per instance or host.

The exact C signatures, statuses, buffer sizes, formatter, domain isolation,
threading and unload rules are frozen in [I18n API v1](../../Docs/I18n-API.md).
The ExampleNativeFx supplies its own four-language locate package and
refreshes the actual shared-control GainPanel from the host service. DSP,
parameters, client/port names and user names retain their identity. Without
a host, it uses local English. `i18n_contract_tests` verify two host leases,
late editor creation, live text refresh preserving gain, UI rendering,
detach and real DLL unload without a JACK server. Example-only lifecycle
and query probes do not add requirements to production effects.

### Built-in i18n migration (S5 in progress, unverified)

The eight built-in DLLs compile a private `NativeFx/I18n.hpp` display bridge. Each descriptor derives its stable module ID from Meta.hpp, owns `nativefx.<id>`, and uses that DLL directory’s `locate/`. Node and mixer registration share the existing optional v1 exports and host lease; audio/DSP and preset indices remain unchanged. QObject display callbacks use guarded owners and a copied callback list during language notification. Detached or standalone display falls back to complete local English call-site strings. Current implementation is partial and has no S5 Release or real-window acceptance evidence.

The shared `Controls::drawElidedText` declaration has the same arguments as `drawText`, with per-line elision and full native tooltips for QWidget paint rectangles. Its S5 call-site verification remains pending; see Docs/UI-API.md.

The FX host registers each discovered library through `I18nManager::registerLibrary(..., Extension::nativeFx)` before creating instances. Node and mixer leases share the same canonical DLL/domain registration. Existing list items and preset captions refresh without rebuilding the chain, selecting a preset or replacing the editor. Destroy instance-owned widgets and instances before releasing the last host lease and unloading the library.

`wjn_fx_preset_name(index)` returns a current-language display caption on the GUI thread. Copy its borrowed UTF-8 bytes immediately; the pointer is valid until the next name query for that index or DLL unload. Preset indices, Meta.id, serialized parameters and SDK metadata remain stable. Native Fx community example window titles now refresh from their name key while preserving client names; detached windows use local English. Actual DLL/window checks are being added in `Develop/NativeFx/NativeFxI18nTests.cpp`; no S5 PASS is claimed yet.
