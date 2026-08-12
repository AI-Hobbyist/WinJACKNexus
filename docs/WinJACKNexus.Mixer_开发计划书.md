# PureMixer Architecture & Development Plan

> Project: **PureMixer**
> Positioning: **A Lightweight & Decoupled Virtual Audio Mixer**
> Stack: **JUCE Framework (C++20), Native JACK2 API, ASIO SDK, juce::dsp**
> Planning baseline: the repository currently contains third-party JUCE sources but no product engine or UI implementation. This plan treats PureMixer as a greenfield application built around a real-time-safe audio engine.

## 1. Executive Summary

PureMixer is a high-performance digital mixer focused on **control routing and signal processing decoupling**. Its goal is not to become a VST host, DAW, or effect-rack environment. Instead, it provides a deterministic virtual console that connects physical or virtual audio I/O through JACK2 or ASIO, applies lightweight built-in DSP, exposes precise routing control, and keeps CPU usage low under dense channel and metering workloads.

The core design philosophy is:

- **No external VST hosting burden**: built-in gain, EQ, filter, pan, send, metering, and summing are first-class engine features. Plugin scan, plugin sandboxing, plugin delay compensation, and third-party plugin UI lifecycle are intentionally out of scope for the first architecture.
- **Realtime audio first**: the audio callback must not allocate, lock, perform blocking I/O, invoke UI code, parse configuration, or resize containers. All dynamic changes are prepared on non-audio threads and committed to the audio thread through immutable snapshots or lock-free queues.
- **Backend isolation**: JACK2 and ASIO are treated as interchangeable device backends behind a strict `AudioBackend` boundary. The mixer engine should not contain backend-specific routing, callback, or device enumeration logic.
- **Control/data separation**: UI controls write normalized parameter intents into a control layer. The DSP graph consumes sample-accurate or block-smoothed parameter state. UI redraw frequency and audio processing frequency are independent.
- **Channel-format awareness**: channel strips are not hardcoded as mono/stereo. Mono, Stereo, 2.1, 5.1, and 7.1 are represented by explicit channel layouts and bus maps, allowing panning, metering, aux sends, and master summing to adapt to the active format.
- **Operational UI modes**: the product must work as a normal embedded desktop application, a detachable floating meter bridge, a tray-resident background mixer, and a secondary-display kiosk console.

The planned system has five main subsystems:

- `AudioEngine`: realtime mixer graph, channel state snapshots, DSP execution, summing, meter extraction.
- `BackendLayer`: native JACK2 and ASIO adapters plus device/session lifecycle.
- `ControlModel`: user-facing parameters, automation-ready state, validation, serialization, undoable edits.
- `UI`: JUCE vector channel strips, meter rendering, floating windows, tray integration, kiosk mode.
- `Persistence`: `.mixer` JSON project files, backend preferences, channel layout templates, window layout state.

## 2. System Architecture

### 2.1 High-Level Module Layout

Recommended source layout:

```text
Source/
  App/
    PureMixerApplication.h/.cpp
    MainWindow.h/.cpp
    TrayController.h/.cpp
    WindowStateCoordinator.h/.cpp
  Audio/
    AudioEngine.h/.cpp
    AudioBackend.h
    AudioSettings.h/.cpp
    JackAudioBackend.h/.cpp
    AsioAudioBackend.h/.cpp
    DeviceClock.h
    RealtimeTypes.h
  Mixer/
    MixerGraph.h/.cpp
    ChannelStrip.h/.cpp
    ChannelLayout.h/.cpp
    InputChannel.h/.cpp
    SubmixChannel.h/.cpp
    MasterChannel.h/.cpp
    AuxChannel.h/.cpp
    AuxSendMatrix.h/.cpp
    SoloMuteResolver.h/.cpp
  DSP/
    GainStage.h
    Eq3Band.h/.cpp
    ParametricEq.h/.cpp
    LowCutFilter.h
    Panner.h/.cpp
    SpatialPanner.h/.cpp
    LevelMeterProbe.h/.cpp
  Control/
    ParameterId.h
    ParameterStore.h/.cpp
    ControlSnapshot.h
    CommandQueue.h
    ProjectState.h/.cpp
    MixerProjectFile.h/.cpp
  UI/
    Components/
      ChannelStripComponent.h/.cpp
      MeterComponent.h/.cpp
      SpatialPannerComponent.h/.cpp
      ParametricEqWindow.h/.cpp
      AuxSendComponent.h/.cpp
    Theme/
      SkinManager.h/.cpp
      SkinPackage.h/.cpp
      SkinStyleTokens.h
      SkinAssetCache.h/.cpp
    Views/
      MixerConsoleView.h/.cpp
      FloatingMeterBridge.h/.cpp
      KioskConsoleView.h/.cpp
```

### 2.2 Signal Flow

The audio path is built from four channel-strip types:

```text
Backend Inputs
  |
  v
Input Channel Strip(s)
  |
  +--> Pre-Fader Aux Tap(s) --> Aux Send Pan/Gain --> Aux Channel Summing Bus
  |
  v
Input Fader/Pan
  |
  +--> Post-Fader Aux Tap(s) --> Aux Send Pan/Gain --> Aux Channel Summing Bus
  |
  v
Direct Output Target
  |
  +--> Main Mix Bus
  |
  +--> Submix Channel Strip(s)
          |
          v
      Main Mix Bus or another valid downstream Submix
  |
  +--> Backend Output
  |
  v
Master Channel Strip
  |
  v
Backend Outputs

Aux Channel Summing Bus
  |
  v
Aux Channel Strip
  |
  +--> Direct Output Target: Main Mix Bus, Submix, or Backend Output
  |
  v
Dedicated Aux Output, Submix, or Main Return
```

Per input channel:

```text
Input Buffer
  -> Input Gain
  -> Mute/Solo Resolver
  -> Low-Cut 80Hz
  -> 3-Band Quick EQ
  -> Optional Parametric EQ
  -> Meter Probe Pre-Fader
  -> Pre-Fader Aux Sends
  -> Fader
  -> Pan or Spatial Panner
  -> Meter Probe Post-Fader
  -> Post-Fader Aux Sends
  -> Direct Output Target: Main Mix Bus, Submix, or Backend Output
```

Per aux channel:

```text
Aux Send Contributions
  -> Aux Summing Buffer
  -> Aux Input Gain/Trim
  -> Low-Cut 80Hz
  -> 3-Band Quick EQ
  -> Optional Parametric EQ
  -> Aux Fader
  -> Aux Pan or Spatial Panner
  -> Aux Meter Probe
  -> Direct Output Target: Main Mix Bus, Submix, or Backend Output
```

Per submix channel:

```text
Input/Aux/Submix Contributions
  -> Submix Summing Buffer
  -> Submix Input Gain/Trim
  -> Low-Cut 80Hz
  -> 3-Band Quick EQ
  -> Optional Parametric EQ
  -> Submix Fader
  -> Submix Pan or Spatial Panner
  -> Submix Meter Probe
  -> Direct Output Target: Main Mix Bus, downstream Submix, or Backend Output
```

Per master channel:

```text
Main Mix Contributions
  -> Master Summing Buffer
  -> Master Insert DSP Chain
  -> Master EQ/Low-Cut if enabled
  -> Master Fader
  -> Master Peak/RMS Meter Probe
  -> Backend Output
```

### 2.3 Audio Thread and UI Thread Decoupling

The realtime audio callback owns only the current immutable processing snapshot:

```cpp
struct EngineSnapshot
{
    double sampleRate;
    int blockSize;
    std::vector<InputChannelRuntime> inputs;
    std::vector<AuxChannelRuntime> auxes;
    std::vector<SubmixChannelRuntime> submixes;
    MasterChannelRuntime master;
    RoutingMatrix routing;
    SoloMuteState soloMuteState;
};
```

Rules:

- The UI thread never mutates `EngineSnapshot` in place.
- Control edits are written to `ParameterStore` on the message thread.
- A non-audio preparation step builds the next snapshot, allocates all required buffers, prepares all `juce::dsp` processors, and validates channel layouts.
- The audio thread atomically swaps `std::shared_ptr<const EngineSnapshot>` or an equivalent RCU-style handle at a block boundary.
- Parameters that change frequently, such as faders, pans, send levels, and EQ gains, use lock-free parameter slots plus `juce::SmoothedValue<float>` inside each runtime processor.
- Structural changes, such as adding an Aux channel or changing 5.1 to 7.1, are snapshot rebuilds, not ad hoc audio-thread mutations.

Prohibited in the audio callback:

- Memory allocation and container resizing.
- Mutex acquisition or condition-variable waits.
- Logging to disk or console.
- Backend device enumeration.
- UI callbacks, component invalidation, window creation, or popup creation.
- JSON/XML parsing or project serialization.

### 2.4 Backend Layer: JACK2 and ASIO Isolation

Define one backend interface:

```cpp
struct AudioDeviceSettings
{
    BackendKind backendKind;
    double requestedSampleRate = 48000.0;
    int requestedBlockSize = 128;
    bool followExternalClock = true;
    bool allowBackendRestartOnApply = true;
};

class AudioBackend
{
public:
    virtual ~AudioBackend() = default;
    virtual BackendInfo getBackendInfo() const = 0;
    virtual std::vector<DeviceInfo> enumerateDevices() = 0;
    virtual void open(const BackendOpenConfig&) = 0;
    virtual void close() = 0;
    virtual void start(AudioProcessCallback*) = 0;
    virtual void stop() = 0;
    virtual BackendPortMap getPortMap() const = 0;
    virtual void refreshPortMapAsync() = 0;
};
```

Audio device and performance settings:

- Sample Rate and Buffer Size are user-facing settings exposed from Audio Settings.
- The effective sample rate and buffer size are always reported from the active backend after negotiation.
- The requested settings are saved in `.mixer`, but the engine must tolerate a backend returning different effective values.
- Applying settings is never handled inside the audio callback. The UI dispatches a command, the backend is stopped or reconfigured on the control/backend thread, processors are prepared with the new `juce::dsp::ProcessSpec`, and then a new engine snapshot is committed.
- If a backend cannot apply the requested value, the UI must show the effective value and a clear status message.
- Buffer Size changes may affect latency, CPU headroom, and meter update smoothness; the UI should display expected latency in milliseconds.
- Changing Sample Rate invalidates DSP coefficients and meter integration windows, so all filters, EQ processors, panners, and meter probes must be re-prepared.

Backend-specific policy:

| Backend | Sample Rate behavior | Buffer Size behavior |
| --- | --- | --- |
| JACK2 | Follow JACK server effective sample rate; display as externally controlled unless the platform integration can request a server change | Follow JACK server buffer size; react to JACK buffer-size callbacks |
| ASIO | Request selected sample rate when supported by the driver; otherwise use driver panel or report unsupported value | Request selected buffer size when supported, or open ASIO control panel for driver-owned settings |
| Null/Test | Fully controlled by test configuration | Fully controlled by test configuration |

JACK2 adapter responsibilities:

- Native `jack_client_t` lifecycle.
- JACK port registration, activation, physical/virtual port discovery.
- Dynamic JACK client and port name support, including ports whose visible names change after server restart, device reconnect, bridge restart, or user-side JACK graph edits.
- JACK graph callbacks for port registration, port rename, port connect/disconnect, and shutdown events; callbacks enqueue lightweight notifications only, while full map rebuilds happen outside the realtime callback.
- Stable route resolution through `BackendPortIdentity`, using ordered matching by explicit UUID/metadata when available, canonical `client:port` name, configured aliases, direction, channel index, and last-known layout hints.
- Buffer-size and sample-rate callbacks.
- Reconnect handling when JACK server restarts.
- Optional auto-connect policy through explicit user settings.

JACK port identity model:

```cpp
struct BackendPortIdentity
{
    BackendKind backend;
    juce::String stableId;        // JACK UUID/metadata when available; empty if unavailable
    juce::String canonicalName;   // Current client:port name
    juce::String displayName;     // UI label, may change at runtime
    juce::StringArray aliases;    // Previous names and user-defined aliases
    PortDirection direction;
    int channelIndex;
};

struct BackendPortBinding
{
    BackendKind backend;
    juce::String preferredStableId;
    juce::String preferredCanonicalName;
    juce::StringArray fallbackAliases;
    PortDirection requiredDirection;
    int expectedChannelIndex;
};
```

Binding rules:

- Persist `BackendPortBinding`, not raw JACK display names.
- Treat JACK `client:port` names as mutable identifiers. They are useful for matching, but not sufficient as the only persisted route key.
- When a JACK port is renamed, keep audio processing on the active port handle if JACK keeps it valid, update the control model display name asynchronously, and persist the new canonical name on the next project save.
- When a port disappears, mark the binding unresolved, keep the channel strip and route state intact, and show the UI as disconnected instead of deleting routes.
- When a matching port appears later, reconnect automatically only if the user's auto-reconnect policy allows it.
- Manual rebinding in the UI must add the previous name to `fallbackAliases` so future launches can recover from driver, bridge, or session-manager naming changes.

ASIO adapter responsibilities:

- ASIO driver loading through the Steinberg ASIO SDK.
- Device enumeration, channel capability discovery, buffer switch callbacks.
- Sample format conversion where required.
- Driver panel invocation from UI thread only.
- Recovery from device reset and driver removal.

The engine receives only normalized interleaved or planar buffers defined by `AudioBufferView`. Backend-specific buffer ownership and callback formats are hidden.

### 2.5 Thread Model

```text
Message/UI Thread
  - JUCE components
  - parameter editing
  - popup EQ windows
  - tray and window mode changes
  - project load/save command dispatch

Audio Callback Thread
  - backend audio callback
  - DSP processing
  - summing and routing
  - lock-free meter sample publishing

Engine Preparation Thread
  - snapshot rebuilds
  - processor preparation
  - buffer pool allocation
  - route validation

Persistence Thread
  - project/session save
  - settings save
  - crash-safe temporary file writes
```

Metering data flows from audio to UI through a single-producer single-consumer ring buffer or atomic meter frames. UI reads the latest complete frame and drops stale frames instead of back-pressuring the audio thread.

### 2.6 Project File Format: `.mixer`

PureMixer project files use the `.mixer` extension and store JSON data. The format is intended to be human-inspectable, diff-friendly, and forward-migratable, while still being strict enough for reliable session restore.

File contract:

- Extension: `.mixer`.
- Data type: UTF-8 JSON.
- Root object includes `format`, `formatVersion`, `application`, `createdAt`, `modifiedAt`, and `project`.
- Unknown fields must be preserved where practical during load/save round trips to allow forward compatibility.
- Loading must validate schema version, required fields, channel IDs, route references, backend bindings, and channel layout compatibility before committing a new engine snapshot.
- Saving must be crash-safe: write to a temporary sibling file, flush, then atomically replace the target where the platform allows it.
- Autosave files should use `.mixer.autosave` or a hidden temporary suffix, but the canonical user-facing project file remains `.mixer`.

Minimal structure:

```json
{
  "format": "PureMixerProject",
  "formatVersion": 1,
  "application": {
    "name": "PureMixer",
    "version": "0.1.0"
  },
    "project": {
    "name": "Untitled",
    "masterLayout": "stereo",
    "audioSettings": {
      "backendKind": "jack2",
      "requestedSampleRate": 48000,
      "requestedBlockSize": 128,
      "followExternalClock": true,
      "allowBackendRestartOnApply": true
    },
    "limits": {
      "maxInputChannels": 128,
      "maxAuxChannels": 32,
      "maxSubmixChannels": 64,
      "maxVisibleMeterChannels": 256
    },
    "backend": {
      "kind": "jack2",
      "autoReconnect": true
    },
    "channels": {
      "inputs": [],
      "auxes": [],
      "submixes": [],
      "master": {
        "id": "master",
        "name": "Master",
        "layout": "stereo",
        "faderDb": 0.0,
        "mute": false,
        "lowCut80Hz": {
          "enabled": false
        },
        "quickEq": {
          "lowDb": 0.0,
          "midDb": 0.0,
          "highDb": 0.0
        },
        "parametricEq": {
          "bands": []
        },
        "meter": {
          "peak": true,
          "rms": true,
          "peakHold": true,
          "overload": true
        },
        "outputBinding": null
      }
    },
    "ui": {
      "windowMode": "main",
      "skin": {
        "packageId": "puremixer.default.dark",
        "packagePath": null
      },
      "floatingMeters": {},
      "kiosk": {}
    }
  }
}
```

Default empty project semantics:

- Create zero Input channels, zero Aux channels, and zero Submix channels.
- Always create one non-deletable `MasterChannelState`.
- The default Master is Stereo, named `Master`, with fader at `0 dB`, mute off, quick EQ flat, 80Hz low-cut off, parametric EQ present but inactive, and Peak/RMS meter enabled.
- The Master output binding may be `null` until the user selects a JACK/ASIO output, or it may be auto-bound to the backend's default stereo output when an explicit auto-bind preference is enabled.

The `.mixer` file should persist:

- Input, Aux, Submix, and Master channel definitions.
- Default empty project state, including one non-deletable Stereo Master channel with fader, EQ, and Peak/RMS meter.
- Requested audio settings: backend kind, requested sample rate, requested buffer size, external-clock policy, and whether applying changes may restart the backend.
- Last effective sample rate and buffer size may be cached for UI display, but the active backend remains authoritative at runtime.
- Channel modes: Mono, Stereo, 2.1, 5.1, 7.1.
- User-configurable soft limits for maximum Input channels, Aux channels, Submix channels, and meter visibility.
- Gain, fader, mute, solo, low-cut, quick EQ, parametric EQ, pan, and spatial pan states.
- Aux channel list and full Aux send matrix including On/Off, send level, independent pan, and Pre/Post state.
- Submix channel list and direct output-target assignments.
- JACK/ASIO backend selection and backend port bindings.
- JACK dynamic interface-name metadata: stable IDs, canonical names, display names, fallback aliases, direction, and expected channel index.
- Window layout, selected skin package reference, floating meter configuration, tray preference, and kiosk display choice.

The `.mixer` file should not persist:

- Live meter values, Peak Hold values, RMS history, or overload latch state unless an explicit diagnostic snapshot export is added later.
- Raw audio buffers.
- Backend-owned transient port handles.
- Absolute paths to SDKs or build tools.

Recommended loader pipeline:

```text
Read JSON
  -> Validate root format and version
  -> Migrate older formatVersion if needed
  -> Resolve stable object IDs
  -> Validate backend bindings without requiring ports to be currently connected
  -> Validate direct output targets and reject Submix routing cycles
  -> Build ControlModel state
  -> Prepare EngineSnapshot off the audio thread
  -> Atomically commit snapshot
  -> Restore UI window state
```

### 2.7 Skin Package Format: `.mixerskin`

PureMixer supports custom skin packages. A skin package changes the visual style of all controls without changing audio behavior, routing state, or project DSP data.

Skin package contract:

- Extension: `.mixerskin`.
- Package type: either an extracted `.mixerskin` directory or a `.mixerskin` zip-compatible package read directly by the application.
- Manifest file: `manifest.json`, encoded as UTF-8 JSON.
- Required manifest fields: `format`, `formatVersion`, `packageId`, `name`, `author`, `style`, and `assets`.
- Asset root: `assets/`.
- Supported asset formats: PNG for raster textures and SVG for scalable vector replacement artwork.
- A built-in default skin must always be available as a fallback.
- Missing or invalid skin assets fall back per-control to the default skin instead of breaking the UI.
- `.mixer` files store the selected skin package reference, not embedded replacement images.

Package layout:

```text
MyDarkPro.mixerskin/
  manifest.json
  assets/
    fader_cap.png
    knob_background.png
    meter_bg.svg
    logo.png
```

The same structure may also be distributed as a zip-compatible `.mixerskin` package:

```text
MyDarkPro.mixerskin
  /manifest.json
  /assets/fader_cap.png
  /assets/knob_background.png
  /assets/meter_bg.svg
  /assets/logo.png
```

Minimal `manifest.json`:

```json
{
  "format": "PureMixerSkin",
  "formatVersion": 1,
  "packageId": "example.console.dark",
  "name": "Example Console Dark",
  "author": "Example",
  "style": {
    "colors": {
      "background": "#141414",
      "panel": "#202020",
      "text": "#E8E8E8",
      "accent": "#49A078",
      "warning": "#E5B567",
      "overload": "#E64545"
    },
    "fonts": {
      "defaultFamily": "system",
      "meterFamily": "system",
      "scale": 1.0
    },
    "metrics": {
      "channelStripWidth": 96,
      "controlRadius": 18,
      "meterWidth": 18,
      "borderRadius": 4
    },
    "meterColors": {
      "rmsLow": "#35C46A",
      "rmsMid": "#D9B43A",
      "peak": "#F5F5F5",
      "overload": "#FF3030"
    }
  },
  "assets": {
    "consoleLogo": "assets/logo.png",
    "knobBackground": "assets/knob_background.png",
    "faderCap": "assets/fader_cap.png",
    "buttonOn": "assets/button-on.png",
    "buttonOff": "assets/button-off.png",
    "meterBackground": "assets/meter_bg.svg"
  }
}
```

Skin package contents:

- Base settings: colors, fonts, spacing, control dimensions, meter color scale, border widths, corner radius, shadows, opacity, and animation timing.
- Replacement textures: knobs, fader caps, buttons, switches, meter backgrounds, panner handles, EQ nodes, channel headers, console custom logo, and optional panel backgrounds.
- PNG and SVG assets are both valid. Raster PNG is preferred for photorealistic hardware-style parts; SVG is preferred for scalable meter scales, icons, and line-art panels.
- Density settings: compact, normal, and kiosk-oriented sizing profiles.
- Meter styling: RMS bar, Peak overlay, Peak Hold marker, overload indicator, scale ticks, and label treatment.

All UI controls must resolve their visual style through `SkinManager`. Components may draw vector fallbacks, but they should not hardcode product colors, fonts, control sizes, or bitmap file paths. Switching skins at runtime invalidates style caches and repaints UI components without restarting the audio engine.

## 3. Channel Strip & DSP Specifications

### 3.1 Channel Layout Model

Supported channel modes:

| Mode | Channels | Canonical speaker map |
| --- | ---: | --- |
| Mono | 1 | C |
| Stereo | 2 | L, R |
| 2.1 | 3 | L, R, LFE |
| 5.1 | 6 | L, R, C, LFE, Ls, Rs |
| 7.1 | 8 | L, R, C, LFE, Ls, Rs, Lrs, Rrs |

Core types:

```cpp
enum class ChannelMode
{
    mono,
    stereo,
    twoPointOne,
    fivePointOne,
    sevenPointOne
};

struct ChannelLayout
{
    ChannelMode mode;
    std::array<SpeakerRole, 8> speakers;
    int channelCount;
};
```

Every processing stage receives a `ChannelLayout` and must either support it directly or declare a deterministic fallback. For example, Standard Pan applies only to mono/stereo. Surround layouts use `SpatialPanner`.

### 3.2 Channel Strip Data Structures

Shared base model:

```cpp
struct ChannelStripState
{
    ChannelId id;
    juce::String name;
    ChannelLayout layout;
    bool mute;
    bool solo;
    float inputGainDb;
    float faderDb;
    Eq3BandState quickEq;
    LowCutState lowCut80Hz;
    ParametricEqState parametricEq;
    MeterState meter;
};

enum class OutputTargetKind
{
    mainMix,
    submix,
    backendOutput
};

struct OutputTarget
{
    OutputTargetKind kind = OutputTargetKind::mainMix;
    SubmixId submixId;
    BackendOutputBinding backendOutput;
};
```

Input channel:

```cpp
struct InputChannelState : ChannelStripState
{
    BackendInputBinding inputBinding; // Wraps BackendPortBinding; supports dynamic JACK names.
    OutputTarget outputTarget;        // Main Mix by default; may target Submix or backend output.
    PanState pan;
    SpatialPanState spatialPan;
    std::vector<AuxSendState> sends;
};
```

Aux channel:

```cpp
struct AuxChannelState : ChannelStripState
{
    AuxId auxId;
    OutputTarget outputTarget; // Main Mix, Submix, or direct backend output.
    PanState pan;
    SpatialPanState spatialPan;
};
```

Submix channel:

```cpp
struct SubmixChannelState : ChannelStripState
{
    SubmixId submixId;
    OutputTarget outputTarget; // Main Mix by default; may target downstream Submix or backend output.
    PanState pan;
    SpatialPanState spatialPan;
};
```

Master channel:

```cpp
struct MasterChannelState : ChannelStripState
{
    BackendOutputBinding outputBinding; // Stable binding with dynamic-name fallback matching.
    bool overloadLatched;
    double overloadResetTimeSeconds;
};
```

Backend bindings are intentionally separate from channel layout. A channel can retain its mixer state while its physical JACK endpoint is temporarily unresolved. This is required for JACK sessions where interface names are assigned by bridges, patchbays, USB reconnect order, or external session managers.

### 3.3 Channel Capacity and User Limits

PureMixer supports dynamic channel counts for Input, Aux, and Submix channels. The architecture must not impose a fixed compile-time limit such as 16, 32, or 64 channels. Users can raise or lower project limits at runtime, subject to resource validation and realtime-safety constraints.

Recommended settings:

```cpp
struct ChannelCapacitySettings
{
    int maxInputChannels = 128;
    int maxAuxChannels = 32;
    int maxSubmixChannels = 64;
    int maxVisibleMeterChannels = 256;
    bool allowLimitChangesWhileEngineRunning = true;
    bool warnBeforeLargeAuxMatrix = true;
};
```

Behavior:

- `maxInputChannels`, `maxAuxChannels`, and `maxSubmixChannels` are soft project limits, not engine compile-time constants.
- Users can edit these limits from Preferences or Project Settings at any time.
- Raising a limit only changes validation and UI add-channel availability; it should not rebuild the audio graph until channels are actually added.
- Lowering a limit below the current channel count must not delete channels automatically. The UI should warn the user and either reject the change or keep existing channels while preventing additional creation above the new limit.
- Limit changes are saved into the `.mixer` project file.
- The engine must still reject channel creation if backend ports, memory allocation, or snapshot preparation fail.
- The UI should display estimated Aux matrix size before applying very large configurations, because send count grows as `inputChannels * auxChannels`.
- Hard defensive caps may exist internally to prevent integer overflow or pathological memory allocation, but they should be high implementation safety bounds, not product-tier channel limits.

Example capacity policy:

| Setting | Default | User editable | Notes |
| --- | ---: | --- | --- |
| Max Input Channels | 128 | Yes | Can be raised if CPU, memory, and backend I/O allow |
| Max Aux Channels | 32 | Yes | Large values multiply the Aux send matrix |
| Max Submix Channels | 64 | Yes | Direct group buses and stem-routing destinations |
| Max Visible Meter Channels | 256 | Yes | UI virtualization should handle larger sessions |
| Internal Safety Cap | Implementation-defined | No | Prevents overflow and invalid allocation sizes |

### 3.4 Input Channel Specification

Each Input Channel provides:

- Channel mode: Mono, Stereo, 2.1, 5.1, or 7.1.
- Input Gain: pre-DSP trim, recommended range `-60 dB` to `+24 dB`, default `0 dB`.
- Mute: removes the channel from main and aux post-fader contribution unless a pre-fader send is explicitly configured to ignore mute. Default behavior should be mute-affects-sends for broadcast predictability.
- Solo: resolved globally by `SoloMuteResolver`, supporting solo-in-place in milestone 1 and optional PFL/AFL modes in later milestones.
- Low-Cut: fixed 80Hz high-pass filter, default 12 dB/oct or 24 dB/oct selectable in preferences.
- Quick EQ: High/Mid/Low gain controls on the main strip.
- Parametric EQ: advanced popup editor on right-click.
- Aux Sends: one send control per existing Aux channel, dynamically reflected in the strip.
- Direct output target: Main Mix by default, with selectable Submix or backend output target.
- Pan: Standard Pan for mono/stereo; Spatial Panner Pad for surround formats.
- Meter: Peak + RMS nested meter with hold and overload state.

### 3.5 Master Channel Specification

The Master Channel is the final output stage for the main mix and must remain deterministic:

- Every project, including an otherwise empty new project, owns exactly one non-deletable Master Channel.
- The default Master Channel is Stereo and includes a fader, quick EQ, right-click parametric EQ access, 80Hz low-cut toggle, and Peak/RMS nested meter.
- Supports all project output layouts: Stereo, 2.1, 5.1, 7.1.
- Provides master fader, master mute, optional quick EQ, optional parametric EQ, and master metering.
- Owns overload latch state for main outputs.
- Publishes final output meter values after all DSP and fader stages.
- Does not support Aux Sends in the first implementation. Master-to-aux routing can be considered later only if a monitor-control use case is explicitly defined.

Master processing order:

```text
Main Sum -> Master DSP -> Master Fader -> Output Meter -> Backend Output
```

### 3.6 Aux Send Channel Specification

Aux channels are first-class buses, not hidden arrays:

- Creating an Aux channel appends a new `AuxChannelState`.
- Every input channel receives a corresponding `AuxSendState`.
- Removing an Aux channel marks related sends inactive, rebuilds the routing snapshot, and compacts only after a safe UI confirmation if project compatibility matters.
- Aux channels may be routed to Main Mix, a Submix, or a dedicated backend output.
- Aux channel format defaults to the current master layout but may be restricted to Mono/Stereo in the initial milestone if surround aux mixing is deferred.

Aux send state:

```cpp
struct AuxSendState
{
    AuxId targetAux;
    bool enabled;
    bool preFader;
    float sendLevelDb;
    PanState pan;
    SpatialPanState spatialPan;
};
```

Send processing rules:

- `enabled == false`: contribute silence.
- `preFader == true`: tap after input gain, mute/solo resolution, low-cut, and EQ, but before channel fader and main pan.
- `preFader == false`: tap after fader and channel pan.
- Send pan is independent from channel pan.
- Send level uses smoothing to avoid zipper noise.
- Dynamic Aux count is reflected by rebuilding input-channel send arrays in the control model and then committing a prepared engine snapshot.

### 3.7 Submix Channel Specification

Submix channels are group buses that can be selected as the direct output target of Input channels, Aux channels, or other Submix channels. They are intended for workflows such as drums bus, vocals bus, game capture bus, monitor grouping, surround stem grouping, and broadcast program grouping.

Each Submix Channel provides:

- Channel mode: Mono, Stereo, 2.1, 5.1, or 7.1.
- Input gain/trim, mute, solo, fader, quick EQ, 80Hz low-cut, parametric EQ, pan/spatial panner, and Peak/RMS metering.
- Direct output target selector:
  - Main Mix Bus.
  - Another downstream Submix.
  - Backend physical/virtual output.
- Optional participation in Solo/Mute resolution according to console policy.
- Same JACK/ASIO dynamic output binding behavior as Master and Aux when routed directly to backend output.

Routing rules:

- Input and Aux channels default to `Main Mix`.
- A channel may be assigned directly to a Submix instead of Main Mix.
- A Submix may feed Main Mix, a downstream Submix, or a backend output.
- The routing graph must be acyclic. The snapshot builder must reject direct or indirect loops such as `Submix A -> Submix B -> Submix A`.
- A channel routed directly to a backend output does not also feed Main Mix unless an explicit duplicate-output feature is added later.
- Changing a channel's direct output target is a structural route change and is applied through a prepared snapshot swap.
- Submixes do not replace Aux Sends. Aux Sends remain parallel sends; Submix assignment is the primary channel output path.

### 3.8 Aux Matrix Routing Logic

The logical matrix is:

```text
InputChannel[N] x AuxChannel[M] -> AuxSendState[N][M]
```

At runtime, it becomes precomputed route descriptors:

```cpp
struct AuxRouteRuntime
{
    ChannelId source;
    AuxId target;
    bool enabled;
    bool preFader;
    LayoutTransform layoutTransform;
    SmoothedGain sendGain;
    PanProcessor sendPan;
};
```

The snapshot builder must precompute:

- Source and destination buffer indices.
- Layout transform from source channel mode to target aux mode.
- Whether panning uses stereo pan law or spatial distribution.
- Whether the send tap reads from pre-fader or post-fader scratch buffers.

No route lookup by map/string should happen inside the audio callback.

### 3.9 juce::dsp Implementation

Recommended processors:

- Gain: `juce::dsp::Gain<float>` plus `juce::SmoothedValue<float>` for faders and sends.
- Low-Cut: `juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>`.
- Quick EQ: three biquad sections or a `juce::dsp::ProcessorChain`:
  - Low shelf: default 100Hz.
  - Mid bell: default 1kHz, configurable Q.
  - High shelf: default 10kHz.
- Parametric EQ: vector or fixed maximum array of peaking/shelf/high-pass/low-pass bands. Allocate maximum bands at prepare time and bypass inactive bands.
- Meter probe: non-mutating analysis stage placed at defined tap points.

Processor preparation:

```cpp
juce::dsp::ProcessSpec spec
{
    sampleRate,
    static_cast<juce::uint32>(maximumBlockSize),
    static_cast<juce::uint32>(layout.channelCount)
};
```

Parametric EQ popup behavior:

- Right-click on the quick EQ area or EQ curve affordance opens a floating `ParametricEqWindow`.
- The window edits the same `ParametricEqState` as the channel strip.
- Frequency response rendering is computed on the UI thread from copied coefficient state.
- Audio thread receives coefficient updates through prepared parameter changes; direct coefficient mutation from the UI is disallowed.

### 3.10 Panning and Spatial Positioning

Stereo Standard Pan:

- Equal-power pan law by default.
- Configurable center attenuation: `-3 dB`, `-4.5 dB`, or `-6 dB`.
- Mono input to stereo output distributes by pan position.
- Stereo input pan supports balance mode first; true stereo width can be added later.

Surround Spatial Panner:

- 5.1 and 7.1 strips automatically replace Standard Pan with a 2D/3D panner pad.
- 2D mode controls azimuth and front/back depth.
- 3D-ready state includes elevation even if the first renderer flattens it.
- LFE receives no normal panned full-range signal by default. LFE contribution must be explicit through bass-management policy or dedicated send.

Spatial state:

```cpp
struct SpatialPanState
{
    float x;          // -1 left to +1 right
    float y;          // -1 rear to +1 front
    float z;          // -1 below to +1 above, reserved for 3D renderers
    float divergence; // 0 point source to 1 spread
};
```

## 4. UI/UX & Multi-State Display Design

### 4.1 Main Console Design

The main mixer view is an operational console, not a landing page. It should prioritize density, legibility, and repeated use:

- Horizontal channel strip layout with stable strip widths.
- Input channels, Aux channels, Submix channels, and Master channel visually grouped.
- Compact controls with consistent vertical ordering:
  1. Channel name and layout badge.
  2. Input gain.
  3. Low-Cut and quick EQ.
  4. Parametric EQ access.
  5. Aux send stack.
  6. Direct output target selector.
  7. Pan or spatial panner.
  8. Peak/RMS meter.
  9. Mute/Solo.
  10. Fader.
- Aux send rows use On/Off toggle, send level knob, pan control, and Pre/Post segmented switch.
- Surround panner pad replaces the pan knob only when the channel layout requires it.
- Project Settings expose editable channel capacity limits: Max Input Channels, Max Aux Channels, Max Submix Channels, and Max Visible Meter Channels.
- Add-channel buttons are disabled with a clear status reason when the current project reaches its configured soft limit.
- When users raise limits, creation controls become available immediately. When users lower limits below the current count, the UI must keep existing channels and block only future additions above the configured limit unless the user explicitly deletes channels.

Audio Settings panel:

- Backend selector: JACK2, ASIO, or Null/Test when available.
- Sample Rate selector with common values such as 44.1kHz, 48kHz, 88.2kHz, 96kHz, and backend-reported supported rates.
- Buffer Size selector with backend-reported block sizes and a latency readout in milliseconds.
- JACK mode clearly labels Sample Rate and Buffer Size as externally controlled when they are owned by the JACK server.
- ASIO mode provides driver-specific controls and an Open Driver Panel action where the driver owns the setting.
- Applying changes must show whether the backend will restart, whether audio will momentarily stop, and what effective values were accepted.

### 4.2 Vector Component System

Use JUCE vector drawing for scalable controls, with every visual property resolved through the active skin:

- `ChannelStripComponent`: fixed-width responsive strip.
- `RotaryControl`: gain/EQ/send knobs with text value tooltip.
- `ToggleButton`: mute, solo, low-cut, send enable.
- `SegmentedControl`: Pre-Fader / Post-Fader switch.
- `MeterComponent`: Peak + RMS nested meter.
- `SpatialPannerComponent`: XY pad, optional Z control.
- `EqCurveComponent`: frequency response preview.
- `OutputTargetSelector`: menu or compact selector for Main Mix, Submix, or backend output assignment.

All control values must be model-driven. Components should not own authoritative audio state.

### 4.3 Theme & Skin System

PureMixer supports runtime skin switching. A skin can change the appearance of every visible control, including knobs, faders, buttons, switches, meters, panners, EQ graph nodes, channel strip backgrounds, headers, menus, popups, floating meter windows, tray status menu styling where supported by the platform, and kiosk surfaces.

Core classes:

```cpp
class SkinManager
{
public:
    void loadBuiltInSkin();
    Result loadSkinPackage(const juce::File& mixerSkinPackage);
    const SkinStyleTokens& getTokens() const;
    const SkinAssetCache& getAssets() const;
    void setActiveSkin(SkinPackageId);
};
```

Skin behavior:

- Every control must render from skin tokens and optional replacement textures.
- Vector drawing remains the fallback path for all controls.
- Bitmap replacement textures are optional per control and may be DPI-scaled variants.
- Skin changes are UI-only and must not touch the audio engine, channel routing, DSP state, or backend state.
- Runtime skin switching invalidates UI style caches, reloads images on a non-audio thread, and repaints affected components.
- The active skin can be stored as a global preference and optionally overridden per `.mixer` project.
- If the selected skin package is missing, incompatible, or fails validation, PureMixer loads the built-in default skin and reports the issue in the UI.
- Skin packages must not execute code. They are declarative JSON plus static assets only.

### 4.4 Metering UI: Peak + RMS Nested Meter

Each channel meter displays:

- RMS as a wide filled bar.
- Peak as a narrow bright overlay column.
- Peak Hold marker with configurable decay/hold time.
- Overload indicator latched when sample peak crosses the configured threshold, usually `-0.1 dBFS` or `0 dBFS`.
- Per-channel reset on click or global reset from the meter bridge.

Meter frame:

```cpp
struct MeterFrame
{
    ChannelId channelId;
    std::array<float, 8> peakDb;
    std::array<float, 8> rmsDb;
    std::array<float, 8> peakHoldDb;
    bool overload;
    uint64_t audioFrameCounter;
};
```

Audio thread computes meter primitives. UI only interpolates display ballistics and paints.

### 4.5 60/120FPS Rendering Logic

Rendering policy:

- Default UI repaint: 60 FPS for active mixer controls.
- Meter bridge: 60 FPS default, 120 FPS optional for high-refresh displays.
- Kiosk mode: 60 FPS minimum target, 120 FPS if the monitor and GPU allow stable rendering.
- Background tray mode: reduce UI timers to idle rate or stop repainting hidden windows.

Implementation:

- Use JUCE `Timer` or `HighResolutionTimer` only where appropriate; avoid high-frequency timers when windows are hidden.
- Meter data is pulled, not pushed, by UI components.
- Expensive EQ response paths are cached and invalidated only on EQ parameter changes.
- Avoid repainting entire console for single-meter updates; repaint affected meter bounds.

### 4.6 Multi-State Display Design

PureMixer supports four UI states:

| State | Purpose | Behavior |
| --- | --- | --- |
| Embedded Main UI | Normal mixer operation | Full console in main window |
| Floating Meter Window | Broadcast-style monitoring | Always-on-top optional, resizable meter bridge |
| System Tray Background | Silent routing and processing | Engine continues, windows hidden, tray menu exposes restore/mute/quit |
| Kiosk Fullscreen Console | Secondary screen control surface | Borderless fullscreen, target display selection, locked layout option |

Window coordination:

```cpp
class WindowStateCoordinator
{
public:
    void showMainConsole();
    void showFloatingMeters();
    void enterTrayMode();
    void enterKioskMode(DisplayId);
    void leaveKioskMode();
};
```

Kiosk requirements:

- Borderless fullscreen on selected secondary display.
- No accidental close from standard window chrome.
- Escape sequence or configured shortcut to exit.
- Persist selected display and layout scale.
- Continue audio processing across mode switches without backend restart.

Floating meter requirements:

- Independent window lifecycle from main console.
- Optional always-on-top.
- Channel visibility filters: inputs, auxes, submixes, master, selected only.
- Shows compact engine status: backend kind, effective sample rate, effective buffer size, and overload count.
- Supports 60 FPS default and optional 120 FPS meter rendering independent from the main console refresh rate.
- Meter frame source shared with main UI, not duplicated from the audio engine.

Tray requirements:

- Tray icon reflects engine state: running, muted, backend disconnected, overload.
- Context menu: Show Console, Show Meters, Mute All, Reset Overloads, Backend Settings, Quit.
- On close, preference controls whether PureMixer exits or minimizes to tray.

## 5. Development Roadmap

### Milestone 0: Repository and Build Foundation

Goal: establish a reproducible C++20 JUCE application skeleton.

Deliverables:

- CMake-based JUCE project or Projucer-generated project with committed build files.
- `Source/` module layout.
- JUCE module configuration including `juce_audio_basics`, `juce_audio_devices`, `juce_audio_processors` if needed, `juce_dsp`, `juce_gui_basics`, and `juce_gui_extra`.
- Compiler settings for C++20, warnings, and platform defines.
- Basic application window with placeholder mixer console.
- CI build for at least Windows Release and Debug.

Acceptance criteria:

- Clean build from a fresh checkout.
- Application starts and exits without device access.
- No product source files live under `third_party/`.

### Milestone 1: Engine Skeleton and Backend Abstraction

Goal: create the realtime-safe audio engine boundary before adding visible mixer complexity.

Deliverables:

- `AudioBackend` interface.
- Null/test backend for deterministic offline processing tests.
- Initial JACK2 backend adapter with dynamic client/port name discovery.
- Initial ASIO backend adapter stub or Windows-only implementation depending on SDK availability.
- `AudioDeviceSettings` model for requested/effective sample rate and buffer size.
- `AudioEngine` with prepare/start/stop and immutable snapshot swap.
- Default empty project with one non-deletable Stereo Master channel.
- Fixed stereo input-to-master pass-through path.

Acceptance criteria:

- Engine can process silent and generated test buffers offline.
- Backend callbacks call only engine realtime APIs.
- Sample-rate and buffer-size changes trigger backend/control-thread reconfiguration and DSP re-prepare outside the audio callback.
- No allocations or locks in the steady-state process callback.
- Backend layer can be compiled out per platform/configuration.
- JACK route bindings survive port rename, temporary disconnect, and reconnect by using stable IDs plus alias-based fallback matching.

### Milestone 2: Core Mixer Graph

Goal: implement Input, Aux, and Master channel runtime structures with deterministic summing.

Deliverables:

- `ChannelLayout` for Mono, Stereo, 2.1, 5.1, 7.1.
- `InputChannelRuntime`, `AuxChannelRuntime`, `SubmixChannelRuntime`, `MasterChannelRuntime`.
- Main bus, Submix bus, and output-target routing.
- Mute/Solo resolver.
- Fader and input gain smoothing.
- Snapshot builder for channel add/remove/layout changes.

Acceptance criteria:

- Multiple input channels sum into master without clipping beyond expected floating-point headroom.
- Input and Aux channels can route directly to a selected Submix instead of Main Mix.
- Submix-to-Submix routing rejects direct and indirect cycles during snapshot preparation.
- Mute and solo behavior is covered by unit tests.
- Layout mismatch handling is explicit and tested.
- Structural changes occur through snapshot rebuilds.

### Milestone 3: Built-In DSP

Goal: ship lightweight DSP that covers the required console controls.

Deliverables:

- 80Hz low-cut filter.
- High/Mid/Low quick EQ.
- Parametric EQ runtime with fixed maximum band allocation.
- EQ coefficient update pipeline.
- Standard stereo panner.
- Surround spatial panner for 5.1/7.1 routing.

Acceptance criteria:

- Gain, filter, and EQ changes are smoothed or safely committed without zipper noise.
- EQ bypass and active-band changes do not allocate in the audio callback.
- Frequency-response calculations are available to UI without touching audio-owned state.
- Panning behavior is covered by energy and routing tests.

### Milestone 4: Aux Send Matrix

Goal: make Aux channels dynamic and fully controllable from each input channel.

Deliverables:

- `AuxSendMatrix` control model.
- Automatic send creation for each input when Aux channels are added.
- Send On/Off, level, independent pan, and Pre/Post state.
- Pre-fader and post-fader tap buffers.
- Aux channel summing and direct output-target routing.
- Aux channel direct output assignment to Main Mix, Submix, or backend output.

Acceptance criteria:

- Adding/removing Aux channels updates every input strip and engine snapshot.
- Pre-fader sends remain stable while input fader moves.
- Post-fader sends track input fader and channel pan.
- Send pan is independent from channel pan.
- Matrix routing tests cover at least stereo and 5.1 layouts.

### Milestone 5: Metering Engine

Goal: implement broadcast-grade metering primitives without burdening the UI.

Deliverables:

- Peak detector.
- RMS detector with configurable integration window.
- Peak Hold with decay/hold timing.
- Overload latch and reset.
- Lock-free meter frame publisher.
- Offline tests for meter values and overload behavior.

Acceptance criteria:

- Metering does not allocate or block in the audio callback.
- UI can drop frames without affecting audio.
- Peak/RMS values match deterministic test signals within tolerance.
- Overload state persists until reset.

### Milestone 6: Main UI Console

Goal: provide a usable mixer interface for all required channel strip controls.

Deliverables:

- Main `MixerConsoleView`.
- Input, Aux, Submix, and Master strip components.
- Gain, mute, solo, low-cut, quick EQ, fader, pan/spatial panner controls.
- Dynamic Aux send controls per input channel.
- Output target selector for Input, Aux, and Submix channel strips.
- Project Settings UI for editable Input/Aux/Submix channel soft limits.
- Audio Settings UI for backend selection, sample rate, buffer size, effective latency, and driver panel access.
- Skin Settings UI for selecting built-in skins or loading `.mixerskin` packages.
- Embedded Peak + RMS meters.
- Basic `.mixer` JSON project save, load, and restore.

Acceptance criteria:

- UI reflects current Input, Aux, and Submix channel counts without restart.
- Users can modify Input/Aux/Submix channel soft limits while the application is running.
- Control changes reach the engine through the parameter model.
- Audio setting changes clearly distinguish requested values from backend-effective values.
- Main console remains readable at common desktop resolutions.
- No UI component directly mutates audio runtime structures.
- All controls render through `SkinManager` tokens and assets, with built-in vector fallback when a replacement texture is missing.
- Saved `.mixer` files restore channel strips, Aux matrix state, Submix output assignments, DSP parameters, backend binding metadata, and window layout.
- Lowering a limit below the current channel count does not delete channels; it only prevents additional channel creation until the session is back under the limit or the limit is raised again.

### Milestone 7: Parametric EQ Window and Frequency Response

Goal: complete advanced EQ editing.

Deliverables:

- Right-click behavior on EQ section.
- Floating `ParametricEqWindow`.
- Multi-band parametric EQ controls.
- Frequency response graph with draggable nodes.
- Band enable/type/frequency/Q/gain controls.

Acceptance criteria:

- Opening/closing EQ windows does not interrupt audio.
- Multiple channel EQ windows can be managed without state collision.
- Frequency curve matches current EQ parameters.
- Band edits are undoable and persisted.

### Milestone 8: Multi-State Display Modes

Goal: support operational deployment modes beyond the main window.

Deliverables:

- Floating meter bridge.
- Independent floating meter window with Peak/RMS display, channel filters, always-on-top option, effective sample rate, effective buffer size, and backend status.
- System tray controller.
- Background processing mode.
- Kiosk fullscreen console on selected display.
- Window/layout persistence.
- Runtime skin switching for main console, floating meter window, and kiosk view.

Acceptance criteria:

- Audio continues during transitions between main, floating, tray, and kiosk states.
- Floating meter window can be shown without main console.
- Floating meter window renders from shared meter frames and does not duplicate audio-engine analysis.
- Tray menu exposes essential actions.
- Kiosk mode is borderless, display-selectable, and recoverable.
- Skin changes repaint UI surfaces without restarting audio.

### Milestone 9: Performance, Reliability, and Release Hardening

Goal: make PureMixer reliable under realistic routing loads.

Deliverables:

- CPU profiling under representative channel counts.
- Realtime allocation/lock audit.
- Backend disconnect/reconnect recovery.
- Sample-rate and buffer-size change tests for JACK callbacks, ASIO requested settings, DSP re-prepare, and `.mixer` restore.
- JACK dynamic interface-name regression tests covering renamed ports, recreated clients, unresolved bindings, and auto-reconnect policy.
- Stress tests for Aux matrix rebuilds.
- Crash-safe `.mixer` project save with temporary-file write and atomic replacement.
- `.mixer` schema validation and migration tests.
- `.mixerskin` package validation tests covering `manifest.json` parsing, extracted-directory loading, zip-compatible package loading, PNG/SVG assets, missing assets, invalid package IDs, DPI variants, and fallback rendering.
- Release packaging for target platforms.

Acceptance criteria:

- Stable CPU usage under agreed benchmark sessions.
- No known realtime allocations in steady-state processing.
- Backend errors surface clearly to UI without crashing the engine.
- Session restore from `.mixer` accurately recreates channel layouts, sends, Submix routing, EQ, windows, backend choices, and dynamic JACK binding aliases.
- Session restore reselects the saved skin package when available and falls back to the built-in default skin when unavailable.
- Session restore applies requested sample rate and buffer size where supported, while displaying backend-effective values when different.

### Suggested Benchmark Targets

Initial performance targets should be validated and adjusted on real hardware:

| Scenario | Sample rate | Block size | Target |
| --- | ---: | ---: | --- |
| 16 stereo inputs, 4 auxes, quick EQ, meters | 48kHz | 128 | Low single-digit CPU on modern desktop CPU |
| 32 stereo inputs, 8 auxes, quick EQ, meters | 48kHz | 128 | No dropouts, stable UI at 60 FPS |
| 8 surround 5.1 inputs, 4 surround auxes | 48kHz | 256 | No realtime allocation, predictable route rebuild |
| Kiosk + floating meters active | 48kHz | 128 | Audio priority unaffected by UI rendering |

### Initial Definition of Done

PureMixer is considered ready for first public preview when:

- JACK2 and ASIO backend paths are usable on their target platforms.
- New empty projects include a non-deletable Stereo Master with fader, EQ, and Peak/RMS meter.
- Input, Aux, Submix, and Master channel strips support the specified controls.
- Audio Settings support sample-rate and buffer-size configuration with JACK/ASIO-specific behavior.
- Mono, Stereo, 2.1, 5.1, and 7.1 layouts are represented in the model and routed predictably.
- Aux sends are dynamic, independently pannable, and support Pre/Post switching.
- Input/Aux channel outputs can be assigned directly to Submix channels, and Submix routing prevents cycles.
- Quick EQ, 80Hz low-cut, and advanced parametric EQ are implemented.
- Peak/RMS nested meters with Peak Hold and Overload indicators are available in main and floating views.
- Custom `.mixerskin` packages can restyle all controls through base settings and replacement textures.
- Main, floating, tray, and kiosk UI states can transition without stopping the audio engine.
- Project state is saved as JSON in `.mixer` files and persists enough information to restore a working mixer session.
- The audio callback passes realtime-safety audit for allocations, locks, and blocking calls.

## 6. Prototype Completion Status (2026-08-04)

The current JUCE application is a visual and interaction prototype. It now covers the console, compact surround metering, Meter Bridge, Parametric EQ, Aux Send, spatial panner, channel fader control, and Compressor / Gate editing surfaces. These views use representative values only; they are not connected to a realtime audio graph yet.

Completed prototype behavior:

- Main strips include per-strip meter source switching: left-click a meter to alternate `IN` and `OUT`; right-click still opens Meter Bridge.
- Meter Bridge exposes an independent `IN` / `OUT` selector on every channel card.
- Aux Send output meter prototypes expose per-send `IN` / `OUT` selectors and adapt to stereo, 5.1, or 7.1 channel counts.
- Compressor / Gate editor exposes an `IN` / `OUT` selector for its signal panel; GR and Gate meters retain their top-down reduction display direction.
- Surround meters use compact contiguous per-channel bars. They retain layout-dependent 5.1/7.1 channel counts and never consume more horizontal space than the stereo-meter area.

### 6.1 Logic Implementation Plan

The prototype meter-source controls must become model-backed settings rather than UI-local booleans. Implement the following in order:

1. Add `MeterTapPoint { input, preFader, postFader, output }` and persist a selected tap point per channel in `ProjectState` / `.mixer`. The prototype `IN` state maps to `input`; `OUT` maps to `output` until advanced tap selection is exposed.
2. Extend `MeterFrame` with the selected-tap frame, or publish independently-addressable frames for each supported tap. Preserve channel layout, peak, RMS, peak hold, overload, and a monotonically increasing frame counter.
3. Insert `LevelMeterProbe` instances at input and output boundaries of Input, Aux, Submix, and Master runtime chains. Insert dedicated reduction probes after Compressor and Gate stages; their values are reduction magnitudes and therefore render top-down.
4. Publish meter frames through the existing planned lock-free SPSC/atomic latest-frame transport. Audio callbacks write only primitive values; UI selects and interpolates the requested frame without allocation or locking.
5. Bind all main-console, Meter Bridge, Aux, and Dynamics `IN` / `OUT` controls to `ParameterStore` commands. Synchronize changes across every visible window for the same channel ID and persist them on save.
6. Replace hard-coded display values with `MeterFrame` reads, then add deterministic tests for every tap point, surround channel ordering, output-vs-input differences, peak hold, overload reset, and top-down GR/Gate rendering.
7. Add accessibly named JUCE controls and keyboard navigation for meter source selection; retain right-click meter bridge behavior without consuming the primary toggle action.

### 6.2 Prototype Phase: Complete

**Status: completed on 2026-08-04.**

The current application is the approved interaction and visual prototype for the mixer workflow. It demonstrates the intended screen hierarchy, channel-layout-dependent presentation, context-menu entry points, floating editor windows, meter source selection, and representative state changes. The UI is ready to serve as the reference for the production implementation.

The completed prototype scope includes:

- Main console strips for mono, stereo, 5.1, 7.1, Aux, Submix, and Master, including compact layout-aware surround meters beside the fader.
- Parametric EQ with knob controls, draggable graph nodes, input/output meters, and channel-control selection for multichannel strips.
- Compressor / Gate controls with active/bypass states, input/output display selection, top-down GR/Gate displays, and per-channel control selection.
- 5.1/7.1 spatial panner previews and editors, intensity radar display, and layout-matched vertical output meters.
- Aux Send editor with layout-matched output meter prototypes.
- Meter Bridge with input/output selection, peak/loudness/dynamics views, LRA and dBTP, recording/reset controls, and a history window featuring selectable channels, metrics, time windows, scan/rolling modes, legends, CSV export, and clearing history.
- Native floating-window interaction behavior for EQ, dynamics, spatial panner, fader control, Aux Send, Meter Bridge, and meter-history views.

Prototype boundaries that remain intentionally non-production:

- Meter values, history curves, DSP response, and channel states are representative UI data rather than audio-engine output.
- EQ, dynamics, routing, meter-source, and per-channel settings are not yet persisted in a project model or applied to a realtime DSP graph.
- CSV export is a prototype interaction until it is backed by captured meter-frame history.
- The Debug build uses a controlled application-window handoff at shutdown to avoid a JUCE/native-peer teardown issue observed during prototype closure. Production work must replace this with deterministic ownership teardown and a clean Debug CRT/sanitizer shutdown.

### 6.3 Production Handoff Order

The prototype is complete; the next work is implementation, not further prototype expansion. Execute the following order alongside Milestones 1–9:

1. Establish persistent channel IDs, `ChannelLayout`, parameter state, and window/controller ownership so every prototype action addresses real model state.
2. Implement the realtime mixer graph, routing, EQ, dynamics, and spatial panner processing paths with snapshot-based reconfiguration.
3. Add input/output/reduction meter probes and lock-free frame publishing, then replace all representative meter and history data with engine frames.
4. Persist routing, DSP, meter tap, and UI-window state in `.mixer`; make CSV export consume recorded meter frames.
5. Bind prototype controls to undoable parameter commands, with shared updates across the console and floating windows.
6. Remove the prototype-only Debug shutdown handoff, verify normal native-window destruction, and require Debug CRT plus AddressSanitizer clean-close checks before release hardening.

## 7. WinJACKNexus 合并实施阶段

### M3：建立 Mixer 模块并改名

1. 新增 `modules/WinJACKNexus.Mixer` 和顶层 `add_subdirectory`。
2. 将 PureMixer 应用入口、窗口和 `MixerConsoleView` 迁入 Mixer。
3. 将 `PureMixerCore` 的调用改为 Common API，删除或停用独立 `PureMixerCore` target。
4. 将产品标识、target、窗口标题、资源和测试名称统一为 `Mixer` / `WinJACKNexus.Mixer`。
5. 先接入 `NullAudioBackend` 保持原型可运行，再接 Common 的真实 JACK backend。

**验收**：Mixer 可独立启动；没有 `PureMixer` target、命名空间或产品标题残留；引擎 smoke tests 迁入统一测试目标并通过。

### M5：Mixer 接入真实 JACK 流

1. 用 Common 的 JACK 音频输入/输出和 client/port 能力替换 Null backend 的生产路径。
2. 将 JACK 输入映射到 Mixer channel strip，将 Mixer 输出写入 Common 的 JACK audio output ports。
3. 将 Common 的 JACK MIDI 输入/输出接入 Mixer 的 MIDI 控制或回环测试路径。
4. 将 Common 的 MeterFrame 接入 Mixer UI，自绘电平表只读快照。
5. 接入静音重置、历史曲线、预设和 CSV 记录；配置格式由 Mixer 的项目模型拥有。
6. 进行 Mono/Stereo/5.1/7.1 等通道布局验证，并确认 Adapter、Mixer 与 MeterBridge 同时运行时的 client/port 命名不冲突。

**验收**：真实 JACK 音频可完成输入、混音、输出和计量闭环；真实 JACK MIDI 可完成输入、映射/回环和输出；无 JACK 服务时能给出可理解的错误状态，不崩溃且 Null backend 仍可用于开发。

## 8. WinJACKNexus 模块落地边界

### 8.1 目标目录与应用迁移

Mixer 的应用层目标目录为：

```text
modules/WinJACKNexus.Mixer/
  CMakeLists.txt
  Source/
    Main.cpp
    App/
      MixerApplication.h/.cpp
      MixerMainWindow.h/.cpp
    Model/
      MixerProject.h/.cpp
      MixerViewState.h
    UI/
      MixerConsoleView.h/.cpp
      MixerMainComponent.h/.cpp
    Engine/
      MixerSession.h/.cpp
```

迁移 `ref/PureMixer` 时，以下内容属于 Mixer：应用入口、主窗口、混音器页面、通道条组合、快捷操作、界面状态和项目工作流。`NullAudioBackend` 只保留为 Mixer 的开发/测试后端，不作为 Common 的生产默认后端。

Mixer 只依赖 Common，不保留 `PureMixerCore` 独立 target，也不在 Mixer 内复制 Common 已提供的 `AudioEngine`、DSP、JACK、MIDI 或电平计量实现。target、命名空间、窗口标题、资源和测试名称统一使用 `WinJACKNexus.Mixer` / `Mixer`。

### 8.2 应用级测试与独立运行

- Mixer 单测覆盖 `MixerGraph` 路由、通道布局、增益/声像、空后端 smoke test、项目状态和 `.mixer` 配置。
- Mixer UI 验收覆盖无数据、正常电平、过载 Peak hold、窄窗口、横向滚动、历史和 CSV 操作，以及 `Common + Mixer` 主题覆盖。
- Mixer 必须能够单独启动和关闭，不要求 Adapter 或 MeterBridge 同时运行。
- 与 Adapter、MeterBridge 并行运行时，Mixer 的 JACK client/port 命名、线程生命周期和资源释放必须符合 Common 的跨 APP 约定。
