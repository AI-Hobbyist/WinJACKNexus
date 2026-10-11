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

## 全局资源预算（2026-10-11）

全局设置新增独立 SF2/SFZ 加载器卡片，复用公共整数控件和默认主题。
仅在 `nodes` 中存在可加载、Node SDK v3 的 `sf2_loader` 单节点 DLL 时显示；
探测不创建节点、不初始化 JACK。四语言文本来自该模块的独立语言包。

资源预算以 SF2 等效大小表示，默认 64 MiB，可设为 1–4096 MiB。
SF2 仍按文件长度的 8 倍预留；SF2/SFZ 的共享采样预算为该值的 8 倍，
包含候选及尚未退役的资源，不代表整个进程 RSS。默认保持原来的 512 MiB 采样预算。
保存在全局 `WinJACKNexus/Application` 的 `sf2LoaderResourceBudgetMiB`，不写入工程。
设置在下一次提交加载请求时取快照；即使资源路径相同，预算变化也会重新准备。
音频回调不读取配置或新增锁；调整配置本身不终止已加载音色。

Release 构建、真实 SF2/SFZ 核心回归、实际 DLL GUI 回归通过。
结构合法且大于 64 MiB 的 SF2：默认拒绝，设置为 128 MiB 后相同资源加载并播放；
降低预算后准备失败的保留模式维持原音色可播放。范围约束及全局配置保存通过。
实际生产全局设置在 Windows 100% 可见窗口中验证：模块存在/缺失/不兼容、
默认值、键盘编辑及重新创建对话框恢复，四语言标签完整且系统字体无方块。
全局设置测试保守显示次数低于 8/100，加载器 GUI 回归低于 48/100。
证据：`build/Synth-budget-build.log`、`build/Synth-budget-core-build.log`、
`build/Synth-budget-settings-build.log`、`build/Synth-budget-core-tests.log`、
`build/Synth-budget-settings-tests.log`、`build/Synth-budget-loader-tests.log`、
`build/Synth-budget-real-window/`、`build/Synth-budget-loader-real-window/`。

人工安装及验收：已跳过（用户指令），跳过不代表通过；真实 JACK/硬件连接未验证。
增量包更新主程序、SF2/SFZ 节点 DLL 及该模块四语言资源/清单，保留配置和工程。
