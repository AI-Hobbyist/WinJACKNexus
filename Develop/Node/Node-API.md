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

The S3 patchbay host checks the same contract for library/search/add menus,
drag/drop, direct creation and project restoration. The main graph never
dispatches the VIRT table. A root subgraph owns one JACK boundary client and
all of its descendants share a preallocated virtual execution schedule;
nested graphs and VIRT modules register no JACK clients or ports. Audio
inputs sum incoming routes, missing inputs are zero, and MIDI events retain
frame offsets with stable ordering and bounded drop-newest storage.
Zero-latency cycles and incompatible port routes are rejected before replacing
the running schedule. Interface IDs and instance IDs remain stable across
display renames and saved projects. Closing an editor does not stop DSP.
Deletion stops the affected schedule before destroying its instances.

Project format 2 retains format 1 reading. Instance mode is inferred from the
saved graph context, never from untrusted saved mode flags. Unloadable modules
retain their parameter and pending connection records with diagnostics;
loading does not silently overwrite the source project. Root clock changes
stop/prepare its VIRT instances outside processing, and processing stays
silent until buffers and clock are ready.

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

## 双模式模块适配（公开仓库前 S4，2026-10-11）

`DualModeModuleV4.inc` 以 WJN_NODE_MODULE_TYPE / WJN_NODE_MODULE_FEATURES 导出完整 v4 metadata、固定模式 factory 和 VIRT v1 函数表。实现类构造时接受模式，并提供 virtualPortCount/virtualPort/virtualPrepare/virtualProcess/virtualReset/virtualStop；表不承担 JACK 初始化或实例销毁。JACK-only 模块继续用 JackOnlyModuleV4.inc，不能以完整空实现提前宣称 VIRT。

MIDI Split/Matrix 和八个 Native Fx、ExampleNativeFx 已迁移双模式。VIRT 的逻辑端口及事件数据借用遵循现有 v1 契约；宿主缓冲外不保存事件指针，超容量丢弃新事件并累计 dropped。Split 的显示名称翻译不更改 midi_spilit 稳定标识。Native Fx placement 的布局行为与边界 v2 见 NativeFx-API.md；其他尚未迁移节点保持 JACK-only。

## VST 双模式（公开仓库前 S5，2026-10-11）

VST Host 及继承其 metadata 的扫描插件条目已声明 JACK/VIRT 双能力。模式、内部块协议 v1、有界 MIDI、根时钟、异步延迟和失败清理见 [VST 处理契约](../Vst/Virtual-Processing.md)。Node v4 外部函数表未改变；Host 与 x86/x64 VSTProcess 必须同步替换。

## 最终迁移清单（公开仓库前 S6，2026-10-11）

能力与 GUI 的实际模块清单见 [S6 清单](../../Docs/Node/PrePublic-S6-inventory.md)。主图、子图各加载入口和工程恢复均按所属图选择固定模式；旧 v3/非法 ABI/缺失或不允许放置节点只保留工程记录与诊断，不创建处理实例。保存旧工程须由用户触发，恢复不静默覆写。Host、Node v4 模块、NativeFx boundary v2 和 VSTProcess IPC v1 必须按增量包清单同步替换。完整验证与未验证影响见 [S6 记录](../../Docs/Node/PrePublic-S6-results.md)。

## Classic Mixer node

`mod_classic_mixer.dll` is one reusable `Classic Mixer` type with independent JACK/VIRT instances, fixed 8/16/24 input-channel layouts and GUI feature. Its `wjn_prepare_node_for_canvas` chooses the layout with shared controls before preparation. The sidebar exports only the shared node-name editor; `createWindow`/`wjn_show_node_gui` opens its independent console. VIRT exposes individual channels (`channels=1`), input IDs `2*stripIndex+1` (+1 for stereo R), output IDs 101..108 ordered Master L/R, AUX1 L/R, AUX2 L/R, Monitor L/R. Names change without changing these IDs. Settings schema version 1 embeds preset records with `payloadBase64`; the existing host wraps settings JSON in project `settingsBase64`. Theme/i18n use optional unindexed v2 Node descriptors, v1 payloads and domain `classicMixer`. GUI/module call-site checks live in Develop/Node/ClassicMixer*Tests.cpp.

Classic Mixer additionally exports optional `uint64_t wjn_classic_mixer_state_revision(INode*)` (GUI/control thread). The value increases on committed state changes; null returns zero. Patchbay observes it to refresh virtual node names and port labels without serializing the embedded preset library every frame. It does not change the Node v4 ABI or the existing Digital Mixer refresh path.
