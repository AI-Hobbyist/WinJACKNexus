# WinJACKNexus M6 收尾与迁移清理计划

> 文档状态：规划中
>
> 本文承载跨 Common、Adapter、Mixer、MeterBridge 的最终收尾工作，不属于任何单一 APP 的开发计划。

## 1. 适用范围

M6 只在所有模块功能完成并通过各自验收后执行。执行前保留 `ref/` 参考工程，避免过早删除仍需对照的实现。

## 2. 收尾任务

1. 所有功能迁移并验收后，才移除或归档对应的 ref 代码。
2. 更新 README、顶层 CMake、模块文档和测试说明。
3. 检查旧 target、旧 include 路径、旧命名空间和产品名残留。
4. 对迁移后的 Common、Adapter、Mixer、MeterBridge 执行完整 Debug 构建和 CTest。
5. 在具备 JACK 服务的环境执行真实 JACK 音频、MIDI 和长时间稳定性手工测试。
6. 确认三个 APP 可以独立启动、关闭和安装，且不会要求其他 APP 同时运行。
7. 确认 Release 安装产物不携带 `libjack64.dll`；JACK2 的头文件和 `.lib` 仅作为开发/编译依赖保留。
8. 确认 `third_party/` 依赖仍保持独立，不把参考工程或第三方源码误并入产品模块。

## 3. 验收

- 迁移后的模块无旧 target、旧命名空间和旧产品标题残留。
- 完整 Debug 构建和 CTest 通过。
- Common、Adapter、Mixer、MeterBridge 的模块边界和依赖方向符合合并计划书。
- 真实 JACK 测试在具备 JACK 服务时完成；无 JACK 服务时，非实时测试仍可运行并给出明确状态。
- 文档、测试说明、安装产物和资源许可记录已更新；安装产物不包含 `libjack64.dll`。

## 4. 依赖

- M1 Common 音频与 JACK 能力合并完成。
- M2 Common 自绘控件、主题、字体和本地化能力合并完成。
- M3 Mixer 模块建立并改名完成。
- M4 MeterBridge 独立 APP 建立完成。
- M5 Mixer 真实 JACK 流接入完成。

## 5. 当前收尾判断（2026-08-15）

M6 目前仍未进入执行阶段。当前状态如下：

> **JACK2 依赖口径**：开发/编译阶段使用 `third_party/JACK2/include` 头文件和 `third_party/JACK2/lib/*.lib`；应用运行和 Release 发布不需要 `libjack64.dll`，真实 JACK 测试使用外部 JACK 服务环境。

- M1 Common 音频/JACK/MIDI 基础和 M2 Common 主题、字体、本地化基础已完成；对应自动测试和构建记录已写入 M1/M2 文档。
- Adapter 已完成真实 WASAPI/JACK/MIDI 桥接的主要代码路径、连续重采样、逐声道 LCD、设备选择修正及 WASAPI shared/exclusive 模式接入，但 `.adapter` 存档、完整三 Tab 工作流、真实外部回环和长时间稳定性验收仍未完成。
- M3 Mixer、M4 MeterBridge 和 M5 Mixer 真实 JACK 流仍未达到本计划的前置条件，因此不能开始删除或归档 `ref/` 内容。
- `ref/`、`third_party/` 和本地构建目录仅作为参考、依赖和验证环境保留；它们不属于本次产品源码提交范围。

进入 M6 前仍需完成：Adapter 自身验收、Mixer/MeterBridge 建立及验收、跨 APP 完整 Debug 构建和 CTest、真实 JACK 音频/MIDI 长时间测试，以及文档/安装产物/许可记录的最终复核。
