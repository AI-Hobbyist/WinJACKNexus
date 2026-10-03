# S5 Native Fx 与社区接入完成记录

2026-10-03：S5 实施与技术验证完成，按用户最新测试范围验收。四语言译文仍是待 S6 人工审校的草稿，S6/S7 尚未完成。

八个内置 Native Fx DLL 分别提供独立语言域、manifest、完整 English 与中文/日语/韩语包；275 个模块 key 与五个共享客户端名称 key 已接入，411 处 Native Fx 显示来源与核对表一致。独立节点窗口、调音台窗口、实际 FX 宿主和社区示例使用可选 i18n v1 服务，不改变模块 ID、端口、工程字段、预设索引或声道参数。

沿用已有 Release 构建及当前二进制的实窗记录。每个模块已有四语言热切换验证，覆盖实际 DLL、多实例、双宿主租约、English fallback、卸载/重开、未提交数值与选区、预设和操作菜单；实际八模块 FX 宿主及社区示例也已有记录。公共文字省略、viewport 映射和已显示 tooltip 热刷新已验证。系统字体为 Microsoft YaHei UI，平台为 windows，截图通过原生窗口归属检查；未使用 offscreen 或 JACK 连接集成测试。

最终资源/部署/证据核对通过：275 模块 key、5 共享 key、411 显示来源、18 个受限批次、800 张截图，源代码、DLL、语言资源及截图哈希一致。见 [最终核对](S5-verification.json)、[核对日志](S5-final-evidence-check.log)、[视觉抽查](S5-visual-review.md)。必要 Release 编译历史见 `S5-build-final-catalog-deployment.log`、`Qt-size-correction-build.log` 与 `S5-build-native-capture-ownership.log`。社区 detach 修复后的 S1 合同回归历史通过 201 项检查，见 `S5-S1-regression.log`。

用户先要求不再进行缩放测试，随后要求每个模块只测试一次。遵照 [最新执行约束](S5-validation-policy.json)，沿用每模块已有有效测试，不补跑语言/缩放矩阵。200% 热切换、English 和中文冷启动范围已齐，Japanese 八模块冷启动已通过；Japanese 宿主/社区及 Korean 冷启动缺少当前截图协议的完整验证。十二个原缩放/语言组合仍不完整，具体保留于最终报告的 `missing_bounded_coverage` 与 `unverified_cold_start_coverage`，未将其标为通过。旧协议截图不计入当前视觉覆盖。

Qt 中文两条 Size 已按用户意见改为“大小”；合并资源仅改变这两条译文，其余 1,784 条保留。Gate 名称、Limiter 输出上限/未使用字段、De-Esser 带宽及其他四语言表达仍待用户提供审校意见；本阶段完成不代表译文人工批准或整个国际化交付完成。
