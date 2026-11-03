# 广播系统阶段记录

## S0 — 协议与挂接点（2026-11-03）

状态：协议与调查完成，待本阶段提交/推送检查点。

用户已授权实施整个 S0—S5，并新增 UDP 上行、HTTP 参数收听、浏览器原生播放和 WAV/Opus 双格式要求。当前有效协议为 [Protocol-v1.md](Protocol-v1.md)，替代旧 WSS 媒体草稿。

调查结论：

- Node v4 JACK-only 适配可复用 `JackOnlyModuleV4.inc`，不添加 VIRT 能力。
- `Controls::DragTreeWidget` 提供层级展示和共享树样式；禁用拖出，移动通过共享 ActionButton/ChoiceField 和领域事务实现，不创建私有树控件或扩展共享 ABI。
- `ConsoleKnob`、单柱竖向 `FxLevelMeterWidget`、共享开关/容器及 `bindRefreshTimer` 可复用。
- `ApplicationSettings` 使用 QSettings UserScope `WinJACKNexus/Application`；新增模块自己的 `broadcast/` 子组，无需修改主窗口全局设置 API。凭据通过 CredReadW/CredWriteW 保存，项目不含 Token。
- `Control Deck` 的恢复通过 live-ID 注册表区分复制/重建；广播使用同边界的模块内稳定 ID 注册，无需新增 Node 克隆 ABI。跨进程发布冲突由服务端拒绝。
- 发送端承担双格式编码、必要的带限重采样和 Opus 分声道变体；服务端只完成有界重组、容器封装和字节转发。

验证：

- RED：`Develop/Broadcast/ContractTests.py` 因缺少新协议夹具失败，`build/bc-s0-udp-red.log`。
- GREEN：新 UDP 64 字节头、HMAC、立体声样本、四布局、WAV/Opus MIME 和协议项校验通过，`build/bc-s0-green.log`。
- 原图 SHA-256 与源图一致，计划和素材将随 S0 显式纳入版本控制，保留既有全局忽略规则。
- 这是规范/夹具检查，不是 Rust 网络、JACK 或浏览器功能 PASS。生产实现和实际 live URL 兼容验证在后续阶段。

已知环境事项：新的 PowerShell PATH 中 python 指向 Windows Store alias；使用现有 `D:/ProgramData/miniconda3/python.exe`，无需安装另一个解释器。

## 后续阶段

| 阶段 | 状态 | 下一个最小步骤 |
| --- | --- | --- |
| S1 Rust relay | 未开始 | 真实进程控制鉴权/UDP/HTTP 的首条 RED 测试，逐条实现 |
| S2 CLI/通用播放 | 未开始 | S1 检查点后执行；两种格式及浏览器原生控件证据 |
| S3 JACK-only node | 未开始 | S2 检查点后执行；模块/离线音频/发布会话 |
| S4 独立 GUI | 未开始 | S3 检查点后执行；Windows 实窗、系统字体及显示次数预算 |
| S5 联调/打包 | 未开始 | S4 检查点后执行；双格式真实进程链路及增量替换包 |

当前没有发布包。人工审校/听音/安装验收已跳过（用户指令），不等于通过。现有 JACK/WinJACKNexus 实例保持运行；没有硬件连线验证。
