# SF2/SFZ Loader（SF2 加载器）完成记录

状态：完成（S4 核心、S5 GUI、S6 集成交付）。双引擎，显式 Bank/Program 映射、
16 通道及 Master 混音，旧声部归属，有界准备/发布/退役与资源持久化。
公共旋钮、−72～+12 dB 电平表、按钮、卡片及分隔线；四语言/独立主题 CSS。
Release、真实 SF2/SFZ 离线合成/生命周期/预算、实际 DLL 状态恢复、改名模型、
100% Windows 实窗交互/背景边界/关闭重开验证通过。
详见 [S4](S4-sf2-loader-core.md)、[S5](S5-sf2-loader-gui.md) 和 [S6](S6-delivery.md)。
人工验收 SKIPPED（用户指令），真实 JACK 音频/连接未验证。音库需由用户自行导入。

## 信息提示优化（2026-10-11）

底部资源信息卡的长说明改为公共按钮 `i` 的鼠标悬浮提示。保留完整资源路径、
准备状态、音色数量、首选资源信息及 SFZ Bank MSB/LSB/Program 说明；随资源选择、
加载状态和界面语言更新。保留映射字段和首选操作，复用公共控件及现有主题。

Release 构建及实际 DLL/离线合成 GUI 回归通过。Windows 平台、100% 缩放、
系统字体的真实窗口及实际鼠标悬浮 Tooltip 验证通过，覆盖中文、英文、日文和韩文，
该批次累计窗口/编辑器/Tooltip 显示预算低于 48/100。截图检查说明完整且中文无方块。
证据：`build/Synth-info-tooltip-build.log`、`build/Synth-info-tooltip-tests.log`、
`build/Synth-info-tooltip-real-window/`。

人工验收：已跳过（用户指令），跳过不代表通过；真实 JACK/硬件连接未验证。
增量替换包仅更新 `nodes/midi_tools/sf2_loader/mod_sf2_loader.dll`，保留用户配置和工程。
