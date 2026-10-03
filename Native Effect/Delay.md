# Delay 延迟

## 完成状态

- Delay 作为 `Native Fx` 节点加载，提供 L、R、C、LFE、Ls、Rs、Lb、Rb 八路独立输入、输出和参数状态。侧栏只提供客户端名称设置；效果器控制区位于独立顶层窗口。
- 顶部通道栏采用参考图的深蓝按钮样式，去掉了右侧说明文字。All 与单通道互斥。All 编辑以当前显示值计算目标，只覆盖被编辑的参数，不改变其他参数已有的通道差异。
- 主窗口按参考图绘制标题、启用开关、预设、时间模式、音符值、点音符、时间视图、反馈与干湿旋钮、滤波器及混合模式。时间旋钮和视图只显示一组，为当前所选声道；没有立体声宽度和链接按钮。
- 自由时间模式按毫秒设置延迟。同步模式以旋钮设置 BPM（30–300），按 `60000 / BPM × 音符拍数 × 点音符倍率` 计算延迟；四分音符为一拍，点音符倍率为 1.5。时间轴显示回声发生位置，回声衰减视图显示各次重复的幅度。这两个视图只改变显示方式。
- 内置人声回声、短 slap、节奏回声、氛围长回声和磁带回声五个预设。预设应用于所选声道，或在 All 模式应用于全部声道。
- 每声道独立延迟缓冲和反馈滤波状态。反馈上限为 95%；高通与低通处理反馈路径；正常模式使用单延迟拍，叠加模式另混入半延迟时间的弱拍。节点参数可序列化并恢复。

## 验证

- 使用现有 `build` 目录的 Visual Studio 18 配置完成 Debug `ALL_BUILD`，包含启用的 VST2 和 VST3 目标。
- `native_delay_tests` 验证脉冲延迟、同步与点音符换算、旁通、All 目标值写入、通道独立编辑、节点发现与加载、独立窗口、参数保存和非法参数拒绝。按 Native Fx 规则未运行 JACK 客户端或端口连接集成测试。
- 界面预览：`build/bin/Delay-preview.png`。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/delay`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/delay/mod_delay.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_delay_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。

## 调音台内部宿主桥接（M5，2026-09-26）

- 本模块在原生 FX v1 ABI 上提供可选的八声道参数快照：格式版本、模块 ID、有效声道掩码及逐声道真实参数。GUI 实例和音频实例分别创建；控制线程导出快照，调音台在 JACK 块间安全点导入，保留音频实例的 DSP 历史。克隆复制参数并重新准备处理器，不共享历史。
- 音频实例以原子峰值提供每声道输入/输出分析；FX 窗口显示调音台实时电平。效果链按 CH/AUX/MAIN 各自顺序执行，支持整体和单模块旁通、增删、排序、克隆及预设。内部加载只调用 mixer API，不创建效果器 JACK 客户端。
- 模块算法及原有独立节点入口保持原实现。参数导入路径使用固定大小内存和对应 DSP 的无分配 `setParameters`；Delay、Reverb 的缓冲分配只在 `prepare` 阶段进行。涉及文件：`src/native_fx/include/WinJACKNexus/NativeFx/NativeFxApi.h`、`src/native_fx/src/NativeFxApi.cpp`、`src/ui/src/FxSettingsWindow.cpp`、`src/mixer/EffectGraph.cpp`。
- Debug `ALL_BUILD`（含 VST2/VST3）及 24 个 CTest 套件通过；其中 `native_fx_api_tests` 验证八模块加载、快照掩码、分析和克隆，`ui_unit_tests` 验证 GUI 桥接，`digital_mixer_node_port_tests` 验证实际样本、旁通及端口不变性。证据：`build/m5-all-build.log`、`build/m5-verification-tests.log`。原生 FX 模块验收限于加载与 UI，不要求效果器 JACK 连接集成。

## 公共数值交互（2026-09-26）

时间、反馈、湿/干信号四个旋钮的读数可点击输入精确值，按 Enter 写入；双击旋钮恢复对应参数默认值。同步模式的时间读数仍以毫秒输入，并按当前音符与附点设置换算为 BPM。All 模式将同一目标值写入八个通道。现有 `build` 树的 Debug `ALL_BUILD`、`native_delay_tests`、`ui_unit_tests` 通过；测试覆盖精确输入、复位及同步换算。

首个预设现为 `Default`，选择它会恢复真实的 `DelayParameters` 默认值；编辑后显示 `Custom`，并保留原有命名预设。旋钮绘制已调用公共 `Controls::drawKnob`。更新后 Debug `ALL_BUILD`、`native_delay_tests`、`native_fx_api_tests`、`ui_unit_tests` 均通过。

## 主题命名空间与绘制接入（2026-09-27，S12 后）

本效果单独导出 Native Fx / Node v1 双入口，共用稳定 ID `delay`、描述符及模块作用域；越界索引返回 null，宿主按 ID 去重并分别释放引用。描述符从冻结注册表复制类型/默认值/继承，声明旋钮、开关和 primary tap、repeated tap、baseline 三条图表语义。

实际窗口的主表面、文字、旋钮活动环/外环、开关、时间轴背景与主次网格、首个与后续 tap 和两种视图下的基线按模块 token 绘制。无外部主题与发布默认 `.wjnskin` 的实窗截图逐像素相同；改色截图保存于 `tests/preview/PostS12/Delay/`，修改文件名为 `-modified.png`。截图观感：**人工验收跳过/未人工验证**。

现有 `build` 的 `mod_delay` Debug 单模块编译、完整 `ALL_BUILD` Debug 退出 0；`theme_delay_native_fx`、先前五个模块 UI 测试、`native_delay_tests`、`native_fx_api_tests`、`theme_default_template` 9/9 通过。验证仅含模块加载与 UI，未运行 JACK 客户端或端口连接集成。证据见 `build/PostS12-Delay-*.log`。

## i18n S5 完成记录（2026-10-03）

本模块 S5 显示接入、所需 Release 构建和实窗验证已完成，按用户最新单次模块验证范围验收。独立 locate 包含 38 个 key 的完整 English、中文、日语和韩语；节点及调音台独立窗口、预设/操作显示接入可选 i18n v1，不改变稳定身份、工程字段或声道参数。

沿用实际 DLL 四语言热切换记录 [20261003T140124696Z-scale-2-limiter_delay-hot](../Docs/i18n/S5-bounded-records/20261003T140124696Z-scale-2-limiter_delay-hot.json)，覆盖多实例、双宿主、卸载/重新打开、English fallback、数字草稿及选区保持；组合批次检查数不能当作本模块独立检查数。平台为 windows，默认系统字体，使用原生窗口归属核对后的可见实窗截图，未运行 JACK 连接集成测试。

最终资源及二进制一致性见 [S5 核对](../Docs/i18n/S5-verification.json)，范围、构建日志和未验证场景见 [完成记录](../Docs/i18n/S5-completion.md)。依用户要求不追加缩放或重复模块测试；未补齐的冷启动/缩放矩阵不标为通过。译文仍待 S6 用户人工审校。
