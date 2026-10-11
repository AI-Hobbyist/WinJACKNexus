# Node module API (v4)

One Windows DLL advertises exactly one node type. All instances of that type
are created through the same factory. The exported interface is declared in
[`NodeApi.h`](../../src/node/include/WinJACKNexus/Node/NodeApi.h); the buildable
[`ExampleNode.cpp`](ExampleNode.cpp) demonstrates the current contract.

## Discovery and lifetime

| Export | Contract |
| --- | --- |
| `const char *wjn_get_node_api_version(void)` | Returns the string `"4"`. |
| `int32_t wjn_get_node_metadata_v4(WjnNodeMetadataV4 *metadata)` | Creation-free capability query; caller sets `structSize`, module returns 1 on success. |
| `const char *wjn_get_node_name(void)` / `const char *wjn_get_node_category(void)` | Display strings for this DLL's only type. Native effects use category `Native Fx`. |
| `const char *wjn_get_node_icon_glyph(void)` / `const char *wjn_get_node_icon_color(void)` | Optional visual metadata; tolerate absent exports. |
| `INode *wjn_create_node_v4(uint32_t mode)` | Creates an independent instance in exactly `WJN_NODE_MODE_JACK` or `WJN_NODE_MODE_VIRT`; returns null for unsupported/invalid modes or failure. |
| `INode *wjn_create_node(void)` | Historical direct SDK convenience factory for JACK mode; the host uses the explicit v4 factory. |
| `const WjnNodeVirtualApiV1 *wjn_get_node_virtual_api_v1(void)` | Complete versioned processing table for VIRT-capable modules; null for JACK-only modules. |
| `void wjn_destroy_node(INode *node)` | Destroys an instance created by the same DLL. Release its instances and editors before unloading. |

There is no `wjn_get_node_count` export, and none of these entry points takes
an index. Rebuild SDK consumers and modules against v4. Before creation, the host
checks version, metadata, explicit factory, destroy export and the VIRT table.
Old v3/v2, retired count exports, missing metadata, zero/unknown capabilities,
unknown feature bits, nonzero reserved fields and incomplete processing tables
are rejected with diagnostics. There is no capability fallback.
`NodeInfo` and `INode` layouts remain unchanged.

After JACK creation, call `INode::init()` and check its result. Before destruction,
call `INode::shutdown()`. `info()` reports name, category, version, author and
description. `createWindow(HWND parent)` / `destroyWindow(HWND)` remain native
window hooks; a type without an editor returns null. Keep callbacks and borrowed
strings valid for the lifetime documented by the owning module.

## Metadata and virtual processing

[`ProcessingApi.h`](../../src/node/include/WinJACKNexus/Node/ProcessingApi.h)
is a C-compatible ABI. `WjnNodeMetadataV4` is exactly six `uint32_t` fields
(24 bytes): `structSize`, `version == 4`, `capabilities`, `features`, and two
zero reserved fields. `JACK_NODE == 1` and `VIRT_NODE == 2` are independent
support bits; a valid module declares 1, 2 or 3. `features` currently permits
only `WJN_NODE_HAS_GUI == 1`, meaning a custom editor can be opened through
the module's window hook or `wjn_show_node_gui`. Settings editors alone do not
imply a custom GUI. Discovery must not create/init instances or call JACK.
The host uses metadata for placement and GUI actions, including localized
sidebar capability tooltips. A missing GUI feature disables custom GUI actions.

The instance's mode is immutable. Main graphs choose JACK; subgraphs choose
VIRT. Cross-context moves save state, destroy and recreate in the destination
mode. Never call JACK registration from a VIRT factory, prepare or process.
S2 built-ins advertise JACK only until their later migration is implemented.

`WjnNodeVirtualApiV1` has `structSize`, `version == 1` and six required function
pointers: `portCount`, `port`, `prepare`, `process`, `reset`, `stop`. Port IDs
are stable within an instance and independent of display names. Port descriptions
have direction, audio/MIDI type and audio channel count (zero for MIDI). UTF-8
names remain module-owned until instance destruction. The caller sets each
description's `structSize` before querying it.

For VIRT, restore settings, call `prepare` with sample rate, maximum frames and
host context, then call `process` with host-owned buffers. Sample position and
frame count come from the root graph. `INode::init` is not the VIRT preparation
entry. `reset` clears processing history while preserving parameters; `stop`
releases processing resources and must precede shutdown/destruction. The host
serializes lifecycle transitions with processing; prepare/stop never race a
callback. Rate changes stop and prepare between blocks.

Audio ports contain non-interleaved float channel pointers, with no buffers
retained beyond the call. Missing input is silence. Every output is written each
block. MIDI input records borrow payload pointers and preserve frame offsets.
Output event and byte storage is supplied and bounded by the host; modules reset
`eventCount`/`byteCount`, append fitting events, drop newest on overflow and
increment `dropped`. Invalid offsets/payloads are dropped. Do not allocate, lock,
access files, register JACK ports or invoke UI from process. Descriptions and
processing topology changes are applied transactionally between callbacks.

`NodeV4CAbi.c` and `NodeV4Tests.cpp` freeze layout/signatures and test creation-free
inspection, malformed modules, explicit modes, audio/MIDI buffers and offline
lifecycle. Third-party modules need recompilation before use with this host.

## Settings and editors

Optional `wjn_create_settings_widget(node, parent)` and
`wjn_destroy_settings_widget(node, widget)` manage Qt editors. Native HWND
settings views use `wjn_create_settings_view` / `wjn_destroy_settings_view`.
The host probes optional exports dynamically.

`wjn_get_node_settings_json(node)` returns borrowed UTF-8 JSON, copied immediately
by the host. `wjn_set_node_settings_json(node, json)` returns nonzero on success.
Reject malformed or incompatible state without mutating the active settings.
Projects retain `settingsBase64` containing the module JSON; legacy direct
`settings` objects remain readable. The existing project-format index field is
reserved at zero for single-node modules; nonzero values cannot load a v4 DLL.
It is not an SDK type enumeration interface.

Host-specific optional exports such as `wjn_prepare_node_for_canvas`,
`wjn_show_node_gui` and `wjn_get_node_client_id` remain instance-based.

## Optional Node theme descriptor extension (v2)

`uint32_t wjn_get_node_theme_api_version(void)` returns
`WJN_NODE_THEME_API_VERSION == 2`.
`const char *wjn_get_node_theme_namespace_json(void)` returns this DLL's one
borrowed UTF-8 descriptor. Copy it immediately. No type index or enumeration
exists. Descriptor JSON and Theme Token semantics remain
[Theme contract v1](../../Docs/Theme-v1-Contract.md); the entry-point version is
separate from the JSON `themeApiVersion`. Missing optional exports keep a v4
module loadable. Invalid theme data disables only that capability. Stable
`moduleId` still deduplicates Node and Native Fx descriptors with independent
leases; existing theme namespace IDs and colors are preserved.

## Independent built-in DLLs and retired modules

| Former module | Independent DLLs |
| --- | --- |
| `mod_adapter.dll` | `mod_wasapi_in.dll`, `mod_wasapi_out.dll`, `mod_system_midi_in.dll`, `mod_system_midi_out.dll` under `nodes/adapter/<ID>/` |
| `mod_meterbridge_probe.dll` | `mod_meter_mono.dll`, `mod_meter_stereo.dll`, `mod_meter_5_1.dll`, `mod_meter_7_1.dll`, `mod_meter_custom.dll` under `nodes/meter/<ID>/` |

The aggregate targets and indexed Node SDK support are removed. Existing
aggregate DLL files are left intact but ignored during discovery and project
loading. On the first startup after this migration, one notice lists their
complete paths and the reason `已废弃` in Chinese (localized in other languages).
The application stores `nodes/deprecatedMultiNodeNoticeShown` in its normal
application settings; later startups do not repeat the notice. User configuration
and project files are not deleted or rewritten automatically. Replace old
aggregate node references with the corresponding independent DLL, restore their
saved settings and save the project; actual JACK reconnection remains unverified
in the offline checks.

[`SingleNodeTests.cpp`](SingleNodeTests.cpp) and [`SingleNodeCAbi.c`](SingleNodeCAbi.c)
cover real DLL factories/state, absent retired exports, C/C++ no-index signatures,
the nine actual Windows settings windows at 100%, and the persisted first-only
notice. Manual hardware/installation acceptance is SKIPPED by user instruction.

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

Digital Mixer exports `int wjn_digital_mixer_show_view(INode* node, int view)` through dynamic lookup. Views are 0=调音台, 1=通道详情, 2=通道总览. It opens only the requested view and returns 1 on success, 0 for an invalid/null node or unsupported view. Closing one view keeps the shared controller until `destroyWindow` or node shutdown. The required Node v4 ABI is unchanged; `createWindow` still opens view 0.

Patchbay exports `int wjn_patchbay_show_digital_mixer_view(int view)` for the host shortcut menu: 1=opened, 0=no loaded mixer, -1=invalid view or opening failure. The existing `wjn_patchbay_show_digital_mixer()` remains view 0. Older mixer modules without the optional export retain view-0 support through `createWindow`; unavailable detail/overview entries report failure.
# Optional Node i18n descriptor extension (v2)

Node v4 DLLs may export `wjn_get_node_i18n_api_version()` returning
`WJN_NODE_I18N_API_VERSION == 2`, and
`const char *wjn_get_node_i18n_descriptor_json(void)`. No type index exists.
The descriptor JSON schema, catalogs, `WjnI18nHostV1` service table and
attach/change/detach lifecycle remain version 1. Native Fx's indexed i18n
entry remains independent and unchanged. The host validates Node v4 and
extension v2 before calling the unindexed descriptor function.
Lifecycle exports, when used, include all three of `wjn_i18n_attach_host`,
`wjn_i18n_language_changed` and `wjn_i18n_detach_host`.

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
scan directory. It shares a domain lease by canonical DLL path (reserved
project index zero), reads the validated displayNameKey, and preserves raw Node v4
metadata for identity. Registration precedes createNode/init; destruction
clears UI and destroys the node before releasing the lease and unloading
the library. `patchbay_i18n_tests` exercises the actual nonaudio ExampleNode
loader, late creation, language changes without instance recreation, and
multiple lease consumers. This does not promise translations for old or
untranslated community modules, and adds no required Node v4 export.
# S5 built-in Native Fx language integration (implementation pending verification)

Built-in Native Fx node descriptors use `nativefx.<Meta.id>` and the module-local `locate/` directory. Their node and mixer placements expose the same optional i18n v1 domain; retain independent host leases until their instances and windows have been destroyed. Releasing one placement must keep live windows in the other placement localized. SDK names, JACK client and port identities and settings JSON remain untranslated.

Native Fx standalone titles refresh their translated effect name together with the current user client name. ExampleNativeFx demonstrates the same behavior with its existing `example.nativefx/name` key; no host or a detached host uses local English. `Develop/NativeFx/NativeFxI18nTests.cpp` exercises real DLLs and Windows node windows without calling INode::init or opening JACK clients. These additions have not yet passed S5 Release or real-window verification.


### VST 宿主的 Node i18n v2 描述符接入

`mod_vst_host.dll` 实现无索引 Node i18n v2 描述符导出及既有 v1 主机服务，模块 ID 为 `module-winjacknexus-vst-host`，域为 `vst`，固定目录为 DLL 所在目录的 `locate`，catalogVersion 为 1。Node v4 及其稳定插件/工程身份保持。显示状态保留 key 与参数，语言通知不重启 VSTProcess、不提交设置；最终租约释放后，仍存活的设置控件回退调用点 English。实际 DLL 单次 Windows 实窗调用点验证通过 77 项检查，见 `Docs/i18n/S6-vst-verification.json`；真实插件、进程和 JACK 集成不在该验证范围。
