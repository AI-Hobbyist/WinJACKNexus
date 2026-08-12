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
7. 确认 `third_party/` 依赖仍保持独立，不把参考工程或第三方源码误并入产品模块。

## 3. 验收

- 迁移后的模块无旧 target、旧命名空间和旧产品标题残留。
- 完整 Debug 构建和 CTest 通过。
- Common、Adapter、Mixer、MeterBridge 的模块边界和依赖方向符合合并计划书。
- 真实 JACK 测试在具备 JACK 服务时完成；无 JACK 服务时，非实时测试仍可运行并给出明确状态。
- 文档、测试说明、安装产物和资源许可记录已更新。

## 4. 依赖

- M1 Common 音频与 JACK 能力合并完成。
- M2 Common 自绘控件、主题、字体和本地化能力合并完成。
- M3 Mixer 模块建立并改名完成。
- M4 MeterBridge 独立 APP 建立完成。
- M5 Mixer 真实 JACK 流接入完成。
