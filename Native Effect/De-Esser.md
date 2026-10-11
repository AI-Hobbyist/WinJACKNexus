# De-Esser 去齿音效果器

状态：主体与频谱扩展完成；默认监听及监听路由更新已通过 Release 完整构建（2026-09-24；本轮未运行测试）。

## 模块与声道

- 节点名称：`De-Esser`；分类：`Native Fx`，不是 VST 插件。
- 八路输入、八路输出，顺序为 `L / R / C / LFE / Ls / Rs / Lb / Rb`；各声道由独立处理器处理。
- 顶部通道条采用参考图的深色内嵌按钮与蓝色选中描边；`All` 编辑全部八路，单声道按钮只编辑对应声道。
- 主控制界面为 768×650 的独立顶层窗口；侧栏只提供客户端名称编辑，不嵌入效果器控制面板。预览见 [De-Esser-preview.png](De-Esser-preview.png)。

## 控制与处理

- 面板按参考图绘制标题、启用开关、预设、频谱、中心频率提示、六个旋钮、处理模式、RMS/峰值检测和监听选项。预设标签与选择框垂直对齐；频段曲线只绘制描边，曲线下方不填充。
- 中心频率范围为 1–12 kHz，带宽为 250 Hz–12 kHz，阈值为 −60–0 dB，起始时间为 0–100 ms，释放时间为 5–1000 ms，输出增益为 −24–12 dB。拖动旋钮会修改所选声道参数；频谱、对数刻度、中心频点/竖线、去齿音曲线峰值与提示框共用频率坐标映射。
- 三种预设通过下拉框及前后按钮选择；省略号菜单可恢复默认值。设置以八声道 JSON 保存并恢复。
- DSP 位于本效果的 `src/native_fx/effects/deesser/src/Dsp.cpp`，使用带通检测、包络跟踪和增益控制，提供宽带、分频和动态 EQ 模式。RMS 与峰值检测使用不同包络量。默认监听为“关”，正常输出去齿音处理结果；“齿音”监听检测频带，“原始”直接监听未处理输入。
- 频谱图同时显示输入参考（青色）和处理后输出（紫色），音频回调将同一帧的输入/输出样本对写入预分配 SPSC 环形缓冲区；UI 每 33 ms 取样、Hann 加窗并进行 1024 点 FFT。单声道选择显示该声道的两条频谱；`All` 仅纳入输入 RMS 高于 −100 dBFS 的活跃声道，并使用相同声道集合分别计算输入与处理后平均功率，避免静音声道稀释曲线。采样率变化时重置分析历史；FFT 与绘制都在 UI 线程完成。

## 验证

- 在仓库现有 `build` 目录运行 Release `ALL_BUILD`，退出码为 0；构建缓存保持 `WJN_ENABLE_VST2=ON`，完整构建覆盖 VST2 与 VST3 目标。
- 先前版本的 `wjn_deesser_tests` 通过：模块加载与导出、独立窗口打开、八声道设置往返、`All`/单声道操作、旋钮拖动及预览渲染。
- 先前版本的 `wjn_graphic_eq_tests` 与 `wjn_ui_tests` 通过，确认共享模块导出和 UI 无回归；本轮频谱扩展未运行测试。
- 按仓库验收规则，未执行 JACK 客户端或端口连接集成测试。

代码位置：[Dsp.cpp](../src/native_fx/effects/deesser/src/Dsp.cpp)、[Ui.cpp](../src/native_fx/effects/deesser/src/Ui.cpp)、[Library.cpp](../src/native_fx/src/Library.cpp)、[DeEsserTests.cpp](../tests/native_fx/DeEsserTests.cpp)。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/deesser`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/deesser/mod_deesser.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_deesser_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。
## 调音台内部宿主桥接（M5，2026-09-26）

本模块通过 `NativeFxApi.h` 的可选八声道参数快照和原子峰值分析接口接入调音台。GUI 与音频实例独立；调音台在 JACK 块间安全点应用快照，保留 DSP 历史。克隆复制参数而不共享处理历史，模块加载不创建新的 JACK 客户端。Debug `ALL_BUILD` 及 24 个 CTest 套件通过；证据见 `build/m5-all-build.log`、`build/m5-verification-tests.log`。

## 公共数值交互（2026-09-26）

六个旋钮的读数可点击输入精确值，频率读数按界面单位 kHz 输入；按 Enter 写入。双击旋钮恢复对应的 `DeEsserParameters` 默认值。All 模式写入相同的绝对目标值。现有 `build` 树的 Debug `ALL_BUILD`、`native_deesser_tests` 通过；`ui_unit_tests` 首次在历史图表悬停断言上失败，复跑通过。

旋钮绘制已接入公共 `Controls::drawKnob` 和宿主调色板；迁移后的 Debug `ALL_BUILD`、`native_deesser_tests`、`ui_unit_tests` 通过。

钟形检测曲线和输入、处理后频谱的路径描边改用公共 `Controls::drawCurvePath`，保持 `Qt::NoBrush`。Debug `ALL_BUILD`、`native_deesser_tests`、`ui_unit_tests` 通过。

## 主题命名空间与绘制接入（2026-09-27，S12 后）

本效果单独导出 Native Fx / Node v1 主题描述符。两入口共用稳定 ID `deesser` 和 `--wjn-theme-native-fx-deesser-`；越界索引返回 null，宿主按 ID 合并注册并分别释放引用。描述符复制冻结注册表中本模块 token 的类型、默认值与继承，声明旋钮、开关及 response-bell、input-spectrum、output-spectrum 三条独立图表系列。

实际窗口的表面/主次文字、旋钮活动环与外环、开关、图表背景/网格/中心线、钟形响应、输入与输出频谱按模块 token 绘制。无外部主题与发布默认 `.wjnskin` 的实窗截图逐像素相同；修改 token 后重绘，截图保存在 `tests/preview/PostS12/DeEsser/`，改色文件名为 `-modified.png`。截图观感：**人工验收跳过/未人工验证**。

现有 `build` 的 `mod_deesser` Debug 单模块编译及完整 `ALL_BUILD` Debug 退出 0；`theme_deesser_native_fx`、`native_deesser_tests`、`native_fx_api_tests`、`theme_default_template` 4/4 通过。验证只含模块加载与 UI，不运行 JACK 客户端/端口连接集成。证据见 `build/PostS12-DeEsser-*.log`。

## i18n S5 完成记录（2026-10-03）

本模块 S5 显示接入、所需 Release 构建和实窗验证已完成，按用户最新单次模块验证范围验收。独立 locate 包含 31 个 key 的完整 English、中文、日语和韩语；节点及调音台独立窗口、预设/操作显示接入可选 i18n v1，不改变稳定身份、工程字段或声道参数。

沿用实际 DLL 四语言热切换记录 [20261003T141614352Z-scale-2-gate_deesser-hot](../Docs/i18n/S5-bounded-records/20261003T141614352Z-scale-2-gate_deesser-hot.json)，覆盖多实例、双宿主、卸载/重新打开、English fallback、数字草稿及选区保持；组合批次检查数不能当作本模块独立检查数。平台为 windows，默认系统字体，使用原生窗口归属核对后的可见实窗截图，未运行 JACK 连接集成测试。

最终资源及二进制一致性见 [S5 核对](../Docs/i18n/S5-verification.json)，范围、构建日志和未验证场景见 [完成记录](../Docs/i18n/S5-completion.md)。依用户要求不追加缩放或重复模块测试；未补齐的冷启动/缩放矩阵不标为通过。译文仍待 S6 用户人工审校。

## 公开仓库前 S2 Node v4（2026-10-11）

本模块 Node 导出升级 v4，声明当前已实现的 JACK_NODE 和自带 GUI；
显式 VIRT 创建返回 null。S4 双模式迁移尚未完成，不提前声明 VIRT 能力。
Mixer placement、DSP、八声道参数和独立窗口行为保持。

Release 所需模块编译及离线模块加载/metadata 校验通过。独立节点窗口和
mixer 窗口完成 windows 平台、100% 缩放、系统字体实窗验证，测试退出 0；
截图为 Docs/Node/PrePublic-S2-native-fx-real-window/node-4.png 和
mixer-4.png。阶段显示计数、命令和日志见
[S2 证据](../Docs/Node/PrePublic-S2-results.md)。不运行 JACK 连接集成测试；
人工界面、听音、硬件及安装验收已跳过（用户指令），不将跳过标为通过。

## 公开仓库前 S4 双模式完成（2026-10-11）

本效果 JACK_NODE/VIRT_NODE 实现及所需 Release 构建、离线模块加载、逻辑八通道布局和 100% Windows 实窗验证完成。两种实例模式固定且复用同一处理核心；VIRT 不初始化 JACK。Node boundary v2 区分实际 JACK 布局和 VIRT 逻辑布局，mixer placement 与参数/独立窗口语义保持。

本模块实际 DLL 的 VIRT、未初始化 JACK 节点和 mixer 三个窗口均通过；截图为 [VIRT 窗口](../Docs/Node/PrePublic-S4-native-real-window/virtual-node-4.png)、node-4.png、mixer-4.png。整个批次 31/100 次显示，并非本模块独立计数。命令、日志及范围见 [S4 证据](../Docs/Node/PrePublic-S4-results.md)。不运行 JACK 连接集成或新增效果算法测试；听音、硬件、人工和安装验收已跳过（用户指令），未验证影响见阶段证据。
