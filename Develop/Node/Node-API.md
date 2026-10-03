# Node module API (v2)

Node modules are Windows DLLs loaded by the patchbay. The exported interface is
declared in [`NodeApi.h`](../../src/node/include/WinJACKNexus/Node/NodeApi.h), and
an implementation example is in
[`ExampleNode.cpp`](ExampleNode.cpp). The host looks up
exports dynamically and accepts modules whose `wjn_get_node_api_version()`
returns the string `"2"`.

## Discovery and lifetime

| Export | Contract |
| --- | --- |
| `wjn_get_node_count()` | Number of nodes advertised by this DLL. A mixer-only Native Fx module returns zero. |
| `wjn_get_node_name(index)` / `wjn_get_node_category(index)` | Display strings for an index below the count. Native effects use category `Native Fx`. |
| `wjn_get_node_icon_glyph(index)` / `wjn_get_node_icon_color(index)` | Optional visual metadata; a host must tolerate missing exports. |
| `wjn_create_node(index)` | Creates one independent `INode` owned by the caller. Return null on failure or invalid index. |
| `wjn_destroy_node(node)` | Destroys an instance created by this same DLL. Destroy instances before unloading the DLL. |

After creation, the host calls `INode::init()` and checks its boolean result.
Before destruction, it calls `INode::shutdown()`. `info()` reports name,
category, version, author and description through `NodeInfo`. The legacy
native window hooks `createWindow(HWND parent)` and `destroyWindow(HWND)` are
part of `INode`; a module without such a window returns null. Keep callbacks
and strings valid while their owning module or node is alive.

## Settings and editors

The patchbay may ask a module to create a Qt `QWidget` editor with
`wjn_create_settings_widget(node, parent)` and release it with
`wjn_destroy_settings_widget(node, widget)`. The `parent` is the Qt owner of
the editor. Native HWND settings views use `wjn_create_settings_view` and
`wjn_destroy_settings_view`; a module can return null if it has no HWND view.
The host checks whether these optional exports are present.

For persistence, implement `wjn_get_node_settings_json(node)` to return a
UTF-8 JSON object and `wjn_set_node_settings_json(node, json)` to restore it.
The host copies the returned string immediately. Return nonzero from the
setter on success and zero for malformed or incompatible state. A failed
restore prevents the patchbay from creating that saved node. The main
`.wjnproj` file packages non-VST settings as Base64 encoded JSON in
`settingsBase64`; decoding must produce a JSON object. Legacy project files
with a direct `settings` object remain readable. Keep the decoded JSON as the
module's settings format; Base64 is only the outer project representation.

The Digital Mixer stores input and AUX strips in visual left-to-right order
in its settings arrays. Its JACK short port names are `<strip name>_<channel>`;
the independent monitor ports remain `MONITOR_<channel>`. Reordering strips
changes patchbay pin order without replacing their JACK ports. Renaming a
strip renames its existing JACK ports in place so live connections remain
attached to the same strip and channel.

The patchbay also probes optional host-specific exports such as
`wjn_prepare_node_for_canvas`, `wjn_show_node_gui` and
`wjn_get_node_client_id`. Modules that do not provide them must still work
through the required v2 discovery and lifetime exports.

## Optional theme namespace exports (v1)

`NodeApi.h` declares `uint32_t wjn_get_node_theme_api_version(void)` and
`const char* wjn_get_node_theme_namespace_json(uint32_t index)`. These are
independent optional exports with version `WJN_NODE_THEME_API_VERSION == 1`;
the required Node v2 ABI is unchanged. The index identifies a node type in
`wjn_get_node_count()`, never a runtime instance, and an invalid index returns
null. The host copies DLL-owned UTF-8 JSON immediately and checks the
[descriptor contract](../../Docs/Theme-v1-Contract.md). Missing exports leave legacy
modules loadable. Bad theme data disables only the theme capability. The
descriptor's `moduleId` is stable and is used to deduplicate the same
namespace exported through Native Fx and Node APIs, regardless of their
different enumeration indices. Registration remains live until its host
module lease is released. Existing Node DLL exports are added one module at
a time after S12. The bundled `wjn_example_node.dll` now exports the optional
v1 descriptor for `module-winjacknexus-example-node`. It declares its
host-drawn node card background token, inherits the patchbay card default,
and returns null for out-of-range type indices. `theme_namespace_tests` loads
the real DLL, registers it without creating a node instance, and checks scope
release.
`mod_patchbay.dll` likewise exports `module-winjacknexus-patchbay` for its
single Node type. Its host-drawn card background token inherits the patchbay
default; the patchbay window's existing `patchbay` scope remains the source of
its own UI colors. `theme_patchbay_node_namespace` tests the real DLL's
version, index boundary, copied descriptor, scope, and unload.
`mod_digital_mixer.dll` exports the same optional API for its single
`module-winjacknexus-digital-mixer` Node type. Its host-drawn card background
inherits the patchbay default; the mixer window keeps its built-in `mixer`
theme scope. `theme_mixer_node_namespace` checks the real DLL without JACK.
`mod_meterbrigde.dll` exports `module-winjacknexus-meterbridge` for its one
Node type. The node card token inherits the patchbay default while its own
window continues to use the built-in `meter-bridge` scope. The real DLL and
UI model/window tests are run without JACK client connections.
`mod_vst_host.dll` exports `module-winjacknexus-vst-host` for its one Node
type. Its host-drawn card background inherits the patchbay default.
`theme_vst_host_node_namespace` validates the real DLL independently of
plug-in scanning or audio connections.
`mod_adapter.dll` exports four independently indexed descriptors:
`module-winjacknexus-wasapi-in`, `module-winjacknexus-wasapi-out`,
`module-winjacknexus-system-midi-in`, and
`module-winjacknexus-system-midi-out`. Each uses its own host-drawn card
background token. The real DLL test verifies all four type-to-scope mappings
without opening audio or MIDI devices.
`mod_meterbridge_probe.dll` exports five independent type descriptors:
`module-winjacknexus-meter-mono`, `module-winjacknexus-meter-stereo`,
`module-winjacknexus-meter-5-1`, `module-winjacknexus-meter-7-1`, and
`module-winjacknexus-meter-custom`. Their host card tokens inherit the
patchbay default. The real DLL test checks every index without connecting
JACK ports.
The Gate Native Fx DLL now also exports the optional Node theme functions.
Its Node type maps to the same `gate` descriptor as Native Fx metadata index
zero; `theme_gate_native_fx` verifies the two registration leases share one
scope and release independently.
The Graphic EQ Native Fx DLL also exports the optional Node theme functions.
Its Node type and Native Fx metadata index zero share the `graphic-eq`
descriptor; `theme_graphic_eq_native_fx` verifies both registration leases,
index bounds, shipped-default appearance, and live UI token changes.
The De-Esser Native Fx DLL likewise exports the optional Node theme API.
Its Node type and Native Fx metadata index zero use one `deesser` descriptor;
`theme_deesser_native_fx` checks both leases and the real editor.
The Compressor Native Fx DLL uses the same dual-entry pattern for its one
Node type and Native Fx metadata index. `theme_compressor_native_fx` checks
their shared `compressor` descriptor and real-window theme changes.
The Limiter Native Fx DLL likewise exports the optional Node theme API for
its one Node type. `theme_limiter_native_fx` validates the shared `limiter`
descriptor and real editor without creating a JACK client.
The Delay Native Fx DLL also exports the optional Node theme API for its
single Node type. `theme_delay_native_fx` checks its shared `delay`
descriptor and live editor recoloring without JACK connections.
The Parametric EQ Native Fx DLL exports the optional Node theme functions
for its single Node type. Its `parametric-eq` descriptor is shared with the
Native Fx metadata entry; `theme_parametric_eq_native_fx` checks the leases
and the actual Windows editor.
The Reverb Native Fx DLL exports the optional Node theme functions for its
single Node type, sharing `reverb` with the Native Fx metadata entry.
`theme_reverb_native_fx` verifies the real DLL and editor. All eight existing
Native Fx DLLs now have matching Node and Native Fx theme descriptors.

## Native Fx nodes

A Native Fx DLL can expose both this Node API and the
[`Native Fx API`](../NativeFx/NativeFx-API.md). Its node adapter owns the JACK client and
eight input/output channels ordered `L`, `R`, `C`, `LFE`, `Ls`, `Rs`, `Lb`,
`Rb`. Its standalone editor keeps the channel selector; the mixer placement
uses a separate, host-owned processing instance without a JACK client.
Native Fx node placement also requires the versioned Native Fx node boundary
exports. The patchbay owns its client-name-only right sidebar, validates the
registered port layout after initialization, and requires a standalone GUI
entry point. Ordinary Node modules retain optional editors and module-defined
JACK port layouts.

Build and test in the repository's existing `build` tree. Test module
discovery, settings round-trip and editor lifetime through the exported API.

## Shared standard channel names

Common `StandardChannelNames.hpp` owns the vocabulary L/C/R/Lw/Rw/Ls/Rs/Lrb/Rrb/LFE/Ltf/Rtf/Ltm/Rtm/Ltr/Rtr. `ChannelNames.hpp` supplies Qt labels and meter role names. Vocabulary order is not sample order: existing 5.1/7.1 buffers remain L/R/C/LFE/Ls/Rs[/Lrb/Rrb]; mono is M. Mixer ports, FX selectors, MAIN/AUX/monitor controls, spatial panner labels, Native Fx, adapter ports, VST standard-name ports and probe defaults reuse this source. Adapter audio ports are directly named M or their standard speaker names, replacing capture_N/playback_N. VST standard mode retains in_/out_ prefixes; Native Fx retains in_/out_ prefixes. Existing custom probe names are retained. After changed port names, reconnect affected ports and save the project.

## Sub Mix

`MixerStripKind::subMix` is a bus with mono/stereo/5.1/7.1 layout, Gain, FX, green fader, M/S and mutually exclusive PFL/AFL. Its MAIN panner and channel trims act on its own output before downstream mixing. `DigitalMixerState::subMixes()` exposes the buses. `setSend(sourceId,send,error)` accepts AUX or Sub Mix targets; Sub Mix sends must be post-fader. `setSubMix(sourceId,targetId,error)` adds an enabled post-fader send (target 0 clears all Sub Mix sends). Multiple enabled Sub Mix sends are supported. `hasSubMix(sourceId)` reports whether any are enabled. Self routing and cycles (including disabled edges and mixed AUX/Sub Mix paths) are rejected transactionally. A Sub Mix cannot send to AUX. MAIN and Monitor have no sends.

Any enabled Sub Mix send suppresses the source's direct MAIN contribution. An AUX with at least one enabled Sub Mix send hides its own output ports; disabling all such sends restores them. Sub Mix outputs remain exposed. Routes and per-send panners are saved by stable ID in the existing sends array; optional version-1 Sub Mix bus fields preserve old project compatibility. Removing a bus removes incoming routes. Reconnect affected ports and save existing projects when AUX output visibility changes.

SEL adds a Sub Mix tab for all strips except MAIN/Monitor. It reuses `AuxSettingsWindow::setSubMixMode(bool)` to show the shared send list, per-send panner and channel controls, with no Pre/Post or bus management buttons and permanently post-fader sampling. Sub Mix SEL has MAIN, FX and Sub Mix tabs without AUX. There are no strip dropdown selectors. The shared send controls use the existing `aux` theme namespace; the green fader thumb/track/grip, Sub Mix scrollbar and adjustable divider have registered mixer tokens. IN/AUX/Sub Mix use independently scrollable regions with default widths 6:2:2 and two draggable dividers.

## Optional Digital Mixer view entry

Digital Mixer exports `int wjn_digital_mixer_show_view(INode* node, int view)` through dynamic lookup. Views are 0=调音台, 1=通道详情, 2=通道总览. It opens only the requested view and returns 1 on success, 0 for an invalid/null node or unsupported view. Closing one view keeps the shared controller until `destroyWindow` or node shutdown. The required Node v2 ABI is unchanged; `createWindow` still opens view 0.

Patchbay exports `int wjn_patchbay_show_digital_mixer_view(int view)` for the host shortcut menu: 1=opened, 0=no loaded mixer, -1=invalid view or opening failure. The existing `wjn_patchbay_show_digital_mixer()` remains view 0. Older mixer modules without the optional export retain view-0 support through `createWindow`; unavailable detail/overview entries report failure.
# Optional i18n v1 extension (S1)

Node v2 required exports and `INode` layout remain unchanged. Include
`WinJACKNexus/Common/I18nApi.h` (also included by NodeApi.h); old modules need
not implement the optional declarations. Hosts resolve
`wjn_get_node_i18n_api_version()` and
`wjn_get_node_i18n_descriptor_json(uint32_t index)` dynamically; version is 1,
invalid index returns NULL. Lifecycle exports, when used, must include all
three of `wjn_i18n_attach_host`, `wjn_i18n_language_changed` and
`wjn_i18n_detach_host`.

The exact signatures, UTF-8 buffer/status rules, path limits, English
completeness, GUI-thread rules and lease/unload order are frozen in
[I18n API v1](../../Docs/I18n-API.md). `I18nManager::registerLibrary` is the
host call site; absence of the extension preserves original metadata/UI.
The buildable ExampleNode exports it and has its own `locate` directory,
with intentionally missing translated entries to demonstrate same-module
English fallback. Its output is `examples/node/wjn_example_node.dll`, so
its language root cannot collide with the application's locate package.
Offline `i18n_contract_tests` verify the actual DLL service and unload
without starting a JACK client. Example-only `wjn_example_i18n_events` and
`wjn_example_i18n_query` are test probes, not public Node API requirements.

The S3 patchbay host consumes this extension in both its node-library scan
and its actual instance loader, including project-loaded DLLs outside the
scan directory. It shares a domain lease by canonical DLL path and node
index, reads the validated displayNameKey, and preserves raw Node v2
metadata for identity. Registration precedes createNode/init; destruction
clears UI and destroys the node before releasing the lease and unloading
the library. `patchbay_i18n_tests` exercises the actual nonaudio ExampleNode
loader, late creation, language changes without instance recreation, and
multiple lease consumers. This does not promise translations for old or
untranslated community modules, and adds no required Node v2 export.
# S5 built-in Native Fx language integration (implementation pending verification)

Built-in Native Fx node descriptors use `nativefx.<Meta.id>` and the module-local `locate/` directory. Their node and mixer placements expose the same optional i18n v1 domain; retain independent host leases until their instances and windows have been destroyed. Releasing one placement must keep live windows in the other placement localized. SDK names, JACK client and port identities and settings JSON remain untranslated.

Native Fx standalone titles refresh their translated effect name together with the current user client name. ExampleNativeFx demonstrates the same behavior with its existing `example.nativefx/name` key; no host or a detached host uses local English. `Develop/NativeFx/NativeFxI18nTests.cpp` exercises real DLLs and Windows node windows without calling INode::init or opening JACK clients. These additions have not yet passed S5 Release or real-window verification.
