# Node module API (v2)

Node modules are Windows DLLs loaded by the patchbay. The exported interface is
declared in [`NodeApi.h`](../src/node/include/WinJACKNexus/Node/NodeApi.h), and
an implementation example is in
[`ExampleNode.cpp`](../src/node/examples/ExampleNode.cpp). The host looks up
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

The patchbay also probes optional host-specific exports such as
`wjn_prepare_node_for_canvas`, `wjn_show_node_gui` and
`wjn_get_node_client_id`. Modules that do not provide them must still work
through the required v2 discovery and lifetime exports.

## Native Fx nodes

A Native Fx DLL can expose both this Node API and the
[`Native Fx API`](NativeFx-API.md). Its node adapter owns the JACK client and
eight input/output channels ordered `L`, `R`, `C`, `LFE`, `Ls`, `Rs`, `Lb`,
`Rb`. Its standalone editor keeps the channel selector; the mixer placement
uses a separate, host-owned processing instance without a JACK client.

Build and test in the repository's existing `build` tree. Test module
discovery, settings round-trip and editor lifetime through the exported API.
