# Limiter 限幅器

## 当前阶段：Limiter 处理、节点加载与 UI

- Limiter 作为第 5 个 `Native Fx` 节点加载，按 L、R、C、LFE、Ls、Rs、Lb、Rb 顺序提供八路输入和输出。
- 界面是独立顶层窗口，主面板为 750 × 500 px 风格布局，顶部为参考图风格的通道按钮。按仓库规定提供 All 和八个独立声道；已去掉参考通道图右侧的文字注释。
- 主面板绘制标题、开关、预设、电平柱、传输曲线、阈值与输出上限等四个旋钮、模式、过采样及自动增益。通道选择和控件编辑可交互；每个声道保存独立参数，All 将同一编辑批量应用到八个声道。
- 参数按八声道 JSON 序列化。每个声道使用独立的 LimiterProcessor，Threshold 和模式控制软拐点压缩比，Ceiling 限制最终峰值，Release 控制增益恢复；启用状态关闭时直通。处理器输出前保留 Ceiling 硬上限。
- 输入与输出表头显示 10 ms 攻击、180 ms 衰减的 RMS 电平，GR 显示实时增益衰减。响应曲线下的填充范围到当前输入 RMS 电平；All 模式按活动声道功率平均，单声道模式只取所选声道。
- Lookahead、过采样和自动增益目前仍是界面参数，尚未参与 DSP。
- 侧栏仅提供客户端名称编辑，效果控制都在独立窗口。

## 验证

- 现有 `build` 目录的 Release `ALL_BUILD` 通过。
- `native_limiter_tests` 之前验证过模块导出、节点创建、八声道设置读写、独立窗口渲染及 All/单声道编辑；本次新增 DSP 与实时表头后未运行测试。
- UI 预览：`build/bin/Limiter-preview.png`。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/limiter`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/limiter/mod_limiter.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_limiter_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。