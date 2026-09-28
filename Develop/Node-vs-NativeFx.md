# Node 与 Native Fx 的边界

| 能力 | Node API v2 | Native Fx API v1 |
| --- | --- | --- |
| 接线盘右栏 | 可导出任意 Qt 设置控件或 HWND 设置视图，也可不提供 | 宿主创建客户端名称控件；模块的其它参数留在独立 GUI |
| 独立 GUI | 可选，`createWindow` 可返回空 | Node 放置必须提供 `wjn_show_node_gui` 与有效的独立窗口 |
| JACK 端口 | 模块自行注册数量、方向、类型和名称 | Node 放置固定为 8 路音频输入、8 路音频输出，顺序为 `L, R, C, LFE, Ls, Rs, Lb, Rb` |
| UI API | 可链接 `WinJACKNexus::UI` | 可链接 `WinJACKNexus::UI` |
| 调音台效果链 | Node 实例不进入效果链 | `WJN_FX_MIXER` 放置作为宿主拥有的处理实例进入效果链，没有 JACK 客户端 |
| 接线盘 Node | 普通 Node DLL 直接提供 | `WJN_FX_NODE` 放置通过 Node API 适配器提供 |
| 双放置 | 不适用 | 同一 DLL 可声明两个位置位，宿主分别实例化 |

一个 Native Fx DLL 可以同时导出 Node API 和 Native Fx API。这里的“Node
不可进入效果链”指 **Node 实例**：效果链只通过 Native Fx 的
`wjn_fx_create_mixer` 创建独立实例，不复用其 JACK Node。

Native Fx 的 Node 放置须通过[边界 v1](NativeFx/NativeFx-API.md#required-node-boundary-v1)
校验：版本、名称桥、独立 GUI 入口，以及初始化后的实际 JACK 端口布局。
混音链内的编辑器是嵌入式控件，不能代替独立 GUI。普通 Node 保留自行
决定端口和右栏内容的能力。
