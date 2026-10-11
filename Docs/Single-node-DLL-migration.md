# 单节点 DLL 迁移

## 拆分验证

Adapter 的 WASAPI In、WASAPI Out、System MIDI In、System MIDI Out 已分别编译为
`mod_wasapi_in.dll`、`mod_wasapi_out.dll`、`mod_system_midi_in.dll`、
`mod_system_midi_out.dll`，位于 `nodes/adapter/<节点 ID>/`。

Meter Probe 的 Mono、Stereo、5.1、7.1、Custom 已分别编译为
`mod_meter_mono.dll`、`mod_meter_stereo.dll`、`mod_meter_5_1.dll`、
`mod_meter_7_1.dll`、`mod_meter_custom.dll`，位于 `nodes/meter/<节点 ID>/`。

拆分阶段暂留旧构建目标，供验证结束后单独移除。各独立 DLL 仅导出一个节点类型；
名称、分类、配置格式、默认端口、音频处理和原有主题命名空间保持原约定。

Release 编译及九个真实 DLL 的离线创建、Unicode 配置恢复、销毁后重建、
独立主题租约与卸载检查通过。九个设置界面均在 Windows 后端的可见原生窗口中
以 100% 缩放验证，使用系统字体并捕获实际窗口；未初始化 JACK 或音频/MIDI 设备。
证据位于 `build/Single-node-split-{build,tests}.log` 和
`build/Single-node-split-real-window/`。

人工审校、设备接入及安装验收：**已跳过（用户指令）**。未验证真实 JACK 连接及硬件工作流。

## 后续检查点

- 移除旧多合一构建及加载支持。
- Node SDK 删除数量与类型索引接口，迁移所有自维护模块和调用方并更新文档。
- 首次启动提示已忽略的旧多合一 DLL 完整路径，原因为“已废弃”；后续不重复提示。
- 最终推送后完全重编 Release，再生成保留用户配置及工程文件的完整替换包。
