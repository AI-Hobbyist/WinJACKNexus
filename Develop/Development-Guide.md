# 模块开发建议

1. **先选宿主边界。** 需要任意 JACK 端口布局或自定义右栏时选
   [Node API](Node/Node-API.md)。需要固定 8 声道效果、独立 GUI 或混音
   链处理时选 [Native Fx API](NativeFx/NativeFx-API.md)，按元数据设置
   `WJN_FX_NODE`、`WJN_FX_MIXER` 或两者。
2. **保持实例独立。** Native Fx 的 mixer 实例仅处理宿主传入的八路音频；
   Node 适配器自行持有 JACK 客户端和固定端口。每路声道保存自己的参数。
   `All` 操作将编辑后的绝对值写入八路，未编辑参数保持原状态。
3. **按窗口职责组织控件。** 普通 Node 可用右栏设置控件和可选独立
   GUI。Native Fx 的右栏由宿主管理客户端名称；效果参数和声道选择放
   在独立 GUI。混音链编辑器由宿主嵌入，并使用宿主的声道选择器。
   应用内容控件优先采用 [UI API](../Docs/UI-API.md)。
4. **保证导出与生命周期。** Native Fx Node 放置实现边界 v1 四个导出、
   `wjn_get_node_client_id` 和 `wjn_show_node_gui`。布局校验应查看实际
   注册的端口，而非只返回常量。重命名活跃 JACK 客户端时保存并恢复
   连接；如模块不支持安全重连，应拒绝改名并保持原连接。当前 Delay、
   Limiter、Reverb 在 JACK 客户端打开时会拒绝改名。返回给宿主的字符串
   在约定生命周期内保持有效。
5. **在现有构建树验证。** 使用 Visual Studio 18 的 CMake，在仓库
   `build` 中编译。测试 DLL 加载、位置位、边界导出、编辑器生命周期、
   设置/参数快照和宿主加载。固定端口的实际运行校验需要可用 JACK；
   缺少 JACK 时应记录为未验证，不能把导出存在等同于端口运行通过。

从 [Node 示例](Node/ExampleNode.cpp) 或
[Native Fx 示例](NativeFx/ExampleNativeFx.cpp) 开始，随后对照各自 API
契约补全产品功能。Native Fx 示例编译到 `build/bin/examples/native_fx`，
不会自动出现在应用效果列表；需要在应用内试用时，将 DLL 放入
`build/bin/nodes/native_fx/example_gain`。示例是开发起点，不替代模块级测试。
