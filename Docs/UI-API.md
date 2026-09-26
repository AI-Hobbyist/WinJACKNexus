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
| `Palette`, `applyPalette`, `paletteFor`, `themeColor` | Shared surface colors and per-widget host theme overrides. |
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
| `TextLabel`, `ProgressField` | Shared label and progress display. The label preserves native selectable text, pixmaps and rich text where those features are requested. |
| `ListField`, `TableField`, `LogField` | Shared item and text views with common painted rows, text and scrollbars. Callers retain Qt item models, editing and selection signals. |
| `GroupPanel`, `TabField`, `ScrollField`, `SplitField`, `DockPanel` | Shared group, tab, scroll, split and dock containers. Tab close buttons and splitter handles use shared painting. `DockPanel` draws a plain title bar and places its collapse arrow at the canvas-facing edge midpoint; the same edge remains draggable for width changes. It omits title-bar float and close buttons. `sidebarControlHeight` is the common 28 px height for patchbay sidebar fields and actions. |
| `DialogButtonBar`, `ScrollBarField` | Dialog role/standard button behavior backed by shared buttons, and a shared painted scrollbar. |
| `ClientNameWidget`, `EmbeddedFxEditor` | Shared node name editor and mixer-hosted effect editor shell. |

Application content should instantiate these controls through this API.
The system main menu, tray menu, context menus and control popup menus use
Qt's native menu components.

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
| [`MeterLevelBar`](../src/ui/include/WinJACKNexus/UI/MeterLevelBar.hpp) | Vertical level and peak bar with configurable range, marks, unit and theme. It draws through `Controls::drawMeter`. |
| [`FxLevelMeterWidget`](../src/ui/include/WinJACKNexus/UI/FxLevelMeterWidget.hpp) | Input/output effect meter. `setLevels` accepts dB values; its bar, held peak and numeric readout update on the application frame timer. |
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
[`FxSettingsWindow`](../src/ui/include/WinJACKNexus/UI/FxSettingsWindow.hpp)
hosts the mixer FX chain and module-provided editors; a native effect keeps
its own processing and parameter snapshot under the Native Fx API. UI widgets
are created and destroyed on the Qt UI thread. Destroy an effect editor
before unloading the DLL that supplies its code.

The Node module export contract and project JSON envelope are specified in
[`Node-API.md`](Node-API.md); the Native Fx lifecycle, snapshots and live
analysis are specified in [`NativeFx-API.md`](NativeFx-API.md). Changes to an
exported UI signature or behavior require updating this document and its
call-site tests before the API can be considered frozen.
