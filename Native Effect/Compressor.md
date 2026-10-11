# Compressor 压缩器

## 当前阶段：DSP、节点加载与界面完成

- 作为 `Native Fx` 模块的第 4 个节点加载，提供 L、R、C、LFE、Ls、Rs、Lb、Rb 八路输入和输出。
- 控制界面为独立顶层窗口。顶部声道栏采用参考图的深蓝色按钮风格，All 与单声道互斥选择，交互遵循 Graphic EQ 与 De-Esser。
- 移除了声道栏右侧的说明文案，并使声道顶栏与主效果器面板左右对齐。半透明橙色填充随输入强度移动，覆盖当前传输曲线以下区域，不再用输入/输出点表示动态电平。
- All 图表遵循 Graphic EQ 的多声道处理方式：只把活动声道纳入计算，并以线性功率平均合并输入强度和各声道传输曲线；静音声道不会扩展填充。单声道模式只读取所选声道。
- 主界面按参考图绘制标题、开关、预设、压缩曲线、六个旋钮、检测模式、侧链滤波与自动增益控件。参数可逐声道编辑并序列化；All 对八路分别应用设置。
- 每个输入声道有独立的压缩器状态。只有有输入及输出缓冲并检测到有效信号的声道进入活动掩码；静音声道不贡献电平或增益衰减，缺失输入缓冲时输出静音，缺失输出缓冲时重置该声道状态。
- DSP 支持阈值、压缩比、软拐点、Attack/Release、峰值或 RMS 检测、侧链高通/低通、手动补偿、自动补偿和旁通。强度图分别读取各声道未经压缩的输入电平和压缩后的实际输出电平；侧链滤波只影响压缩检测，不会把侧链电平误显示成输入音量。
- 每采样点的 dB 到线性增益换算使用 `exp2` 代替通用 `pow(10, dB/20)`；8 声道、48 kHz、启用压缩和侧链滤波时，基准耗时 Debug 从 696 ms 降至 496 ms，Release 从 140 ms 降至 105 ms（各声道处理 10 秒信号的中位耗时）。
- Compressor 参考窗口宽 770 px，声道栏与主面板均宽 748 px，并从 x=10 对齐绘制，保持窄而均匀的左右边距。
- 模块侧栏包含客户端名称编辑，可更新 Compressor 客户端名并在名称改变时恢复原有端口连接。

## 验证

- Debug 配置 `ALL_BUILD` 通过；本轮 Common、UI 和 Native Compressor 模块测试通过。模块测试从 Native Fx 路径加载模块并验证处理曲线与独立窗口，无需 JACK 端口连接测试。
- UI 预览图：`build/bin/Compressor-calculated-fill.png`。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/compressor`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/compressor/mod_compressor.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_compressor_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。
## 调音台内部宿主桥接（M5，2026-09-26）

本模块通过 `NativeFxApi.h` 的可选八声道参数快照和原子峰值分析接口接入调音台。GUI 与音频实例独立；调音台在 JACK 块间安全点应用快照，保留 DSP 历史。克隆复制参数而不共享处理历史，模块加载不创建新的 JACK 客户端。Debug `ALL_BUILD` 及 24 个 CTest 套件通过；证据见 `build/m5-all-build.log`、`build/m5-verification-tests.log`。

## 公共数值交互（2026-09-26）

六个旋钮的读数可点击输入精确值，按 Enter 写入；双击旋钮恢复对应的 `CompressorParameters` 默认值。All 模式将显示目标值作为绝对值写入八个通道。现有 `build` 树的 Debug `ALL_BUILD`、`native_compressor_tests` 与 `ui_unit_tests` 通过，新增测试覆盖上述交互。

旋钮绘制已接入公共 `Controls::drawKnob` 和宿主调色板；迁移后的 Debug `ALL_BUILD`、`native_compressor_tests`、`ui_unit_tests` 通过。

GR 表现使用公共 `Controls::drawMeter` 的 `fillOriginDb=0`，从 0 dB 向负衰减值填充；刻度也由公共控件绘制。现有 `build` 树的 Debug `ALL_BUILD`、`native_compressor_tests`、`ui_unit_tests` 通过，公共控件测试覆盖衰减方向和独立刻度。

传输响应曲线与实时电平填充改用公共 `Controls::drawCurvePath`；效果器保留压缩算法和路径坐标计算。Debug `ALL_BUILD`、`native_compressor_tests`、`ui_unit_tests` 通过。

## 主题命名空间与绘制接入（2026-09-27，S12 后）

本效果单独导出 Native Fx / Node v1 主题描述符，共用稳定 ID `compressor` 和对应模块作用域。描述符按冻结注册表提供 token 类型、默认值和继承，声明旋钮、开关、transfer 曲线、独立 level-fill 填充及 reduction-zone-single 电平分段。双入口按稳定 ID 去重，租约分别释放；越界索引返回 null。

实际窗口的面板渐变两端、文字、旋钮活动环/外环、开关、图表背景/主次网格、响应曲线与活动电平填充、GR 表活动/非活动分段已接入模块 token。无外部主题与发布默认 `.wjnskin` 的实窗截图逐像素相同；改色截图见 `tests/preview/PostS12/Compressor/`，修改文件名为 `-modified.png`。截图观感：**人工验收跳过/未人工验证**。

现有 `build` 的 `mod_compressor` Debug 单模块编译与完整 `ALL_BUILD` Debug 退出 0；`theme_compressor_native_fx`、前三个已迁移模块 UI 测试、`native_compressor_tests`、`native_fx_api_tests`、`theme_default_template` 7/7 通过。仅做模块加载及 UI 验证，未运行 JACK 客户端或端口连接集成。证据见 `build/PostS12-Compressor-*.log`。

## i18n S5 完成记录（2026-10-03）

本模块 S5 显示接入、所需 Release 构建和实窗验证已完成，按用户最新单次模块验证范围验收。独立 locate 包含 26 个 key 的完整 English、中文、日语和韩语；节点及调音台独立窗口、预设/操作显示接入可选 i18n v1，不改变稳定身份、工程字段或声道参数。

沿用实际 DLL 四语言热切换记录 [20261003T141857552Z-scale-2-compressor_reverb-hot](../Docs/i18n/S5-bounded-records/20261003T141857552Z-scale-2-compressor_reverb-hot.json)，覆盖多实例、双宿主、卸载/重新打开、English fallback、数字草稿及选区保持；组合批次检查数不能当作本模块独立检查数。平台为 windows，默认系统字体，使用原生窗口归属核对后的可见实窗截图，未运行 JACK 连接集成测试。

最终资源及二进制一致性见 [S5 核对](../Docs/i18n/S5-verification.json)，范围、构建日志和未验证场景见 [完成记录](../Docs/i18n/S5-completion.md)。依用户要求不追加缩放或重复模块测试；未补齐的冷启动/缩放矩阵不标为通过。译文仍待 S6 用户人工审校。

## 公开仓库前 S2 Node v4（2026-10-11）

本模块 Node 导出升级 v4，声明当前已实现的 JACK_NODE 和自带 GUI；
显式 VIRT 创建返回 null。S4 双模式迁移尚未完成，不提前声明 VIRT 能力。
Mixer placement、DSP、八声道参数和独立窗口行为保持。

Release 所需模块编译及离线模块加载/metadata 校验通过。独立节点窗口和
mixer 窗口完成 windows 平台、100% 缩放、系统字体实窗验证，测试退出 0；
截图为 Docs/Node/PrePublic-S2-native-fx-real-window/node-5.png 和
mixer-5.png。阶段显示计数、命令和日志见
[S2 证据](../Docs/Node/PrePublic-S2-results.md)。不运行 JACK 连接集成测试；
人工界面、听音、硬件及安装验收已跳过（用户指令），不将跳过标为通过。

## 公开仓库前 S4 双模式完成（2026-10-11）

本效果 JACK_NODE/VIRT_NODE 实现及所需 Release 构建、离线模块加载、逻辑八通道布局和 100% Windows 实窗验证完成。两种实例模式固定且复用同一处理核心；VIRT 不初始化 JACK。Node boundary v2 区分实际 JACK 布局和 VIRT 逻辑布局，mixer placement 与参数/独立窗口语义保持。

本模块实际 DLL 的 VIRT、未初始化 JACK 节点和 mixer 三个窗口均通过；截图为 [VIRT 窗口](../Docs/Node/PrePublic-S4-native-real-window/virtual-node-5.png)、node-5.png、mixer-5.png。整个批次 31/100 次显示，并非本模块独立计数。命令、日志及范围见 [S4 证据](../Docs/Node/PrePublic-S4-results.md)。不运行 JACK 连接集成或新增效果算法测试；听音、硬件、人工和安装验收已跳过（用户指令），未验证影响见阶段证据。
