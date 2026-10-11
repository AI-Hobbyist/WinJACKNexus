# 单节点 DLL 迁移

## 拆分验证

Adapter 的 WASAPI In、WASAPI Out、System MIDI In、System MIDI Out 已分别编译为
`mod_wasapi_in.dll`、`mod_wasapi_out.dll`、`mod_system_midi_in.dll`、
`mod_system_midi_out.dll`，位于 `nodes/adapter/<节点 ID>/`。

Meter Probe 的 Mono、Stereo、5.1、7.1、Custom 已分别编译为
`mod_meter_mono.dll`、`mod_meter_stereo.dll`、`mod_meter_5_1.dll`、
`mod_meter_7_1.dll`、`mod_meter_custom.dll`，位于 `nodes/meter/<节点 ID>/`。

拆分完成并独立提交后，旧多合一构建目标及加载支持已移除。各独立 DLL 仅导出一个节点类型；
名称、分类、配置格式、默认端口、音频处理和原有主题命名空间保持原约定。

Release 编译及九个真实 DLL 的离线创建、Unicode 配置恢复、销毁后重建、
独立主题租约与卸载检查通过。九个设置界面均在 Windows 后端的可见原生窗口中
以 100% 缩放验证，使用系统字体并捕获实际窗口；未初始化 JACK 或音频/MIDI 设备。
证据位于 `build/Single-node-split-{build,tests}.log` 和
`build/Single-node-split-real-window/`。

人工审校、设备接入及安装验收：**已跳过（用户指令）**。未验证真实 JACK 连接及硬件工作流。

## SDK v3 与废弃处理验证

Node SDK 已删除数量接口以及名称、分类、图标、工厂、主题和国际化描述入口的类型索引。
Node ABI 为 v3；Node 主题和国际化入口版本为 v2，描述数据与国际化服务格式保持 v1。
自维护节点、调用方、C/C++ 示例及 API 文档均已迁移。Native Fx 独立接口不变。

宿主在调用新签名前检查版本，拒绝旧 ABI 或仍导出数量接口的 DLL。
已有多合一文件不删除，忽略加载；首次启动以一个提示列出全部完整路径，注明“已废弃”。
正常应用配置持久保存提示状态，后续启动不重复显示。旧单节点 v2 模块也需按 v3 重编。
旧工程的多合一节点引用需用户改为对应独立 DLL 并保存；不自动改写工程或用户配置。

Release 编译、C 静态签名校验、全部内置独立模块离线创建、九个设置实窗、中文首次提示
及重新读取配置后不重复提示均通过。GUI 使用 Windows 平台、100% 缩放与系统字体，
真实窗口截图验证中文、日文、韩文正常显示；该批次共十个顶层显示，累计显示低于 100。
证据：`build/Single-node-sdk-build.log`、`build/Single-node-sdk-tests.log`、
`build/Single-node-sdk-real-window/`。三个 MIDI 节点复用 S6 的离线与实窗回归。

人工审校、设备接入及安装验收：**已跳过（用户指令）**；跳过不代表通过。
真实 JACK 音频及工程连接未验证。最终推送后在现有 build 目录完全重编 Release，
完整替换包保留用户配置、工程、自定义主题和预设；不包含废弃多合一 DLL。
