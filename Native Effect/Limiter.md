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
## 调音台内部宿主桥接（M5，2026-09-26）

本模块通过 `NativeFxApi.h` 的可选八声道参数快照和原子峰值分析接口接入调音台。GUI 与音频实例独立；调音台在 JACK 块间安全点应用快照，保留 DSP 历史。克隆复制参数而不共享处理历史，模块加载不创建新的 JACK 客户端。Debug `ALL_BUILD` 及 24 个 CTest 套件通过；证据见 `build/m5-all-build.log`、`build/m5-verification-tests.log`。

## 公共数值交互（2026-09-26）

四个旋钮的数字读数现可点击输入精确值，按 Enter 写入；双击旋钮恢复对应的 `LimiterParameters` 默认值。All 模式按显示目标值对八个通道写入同一绝对值。现有 `build` 树的 Debug `ALL_BUILD` 通过，`native_limiter_tests` 与 `ui_unit_tests` 通过；新增测试覆盖精确输入、双击复位及八通道广播。

旋钮绘制已接入公共 `Controls::drawKnob` 和宿主调色板；迁移后的 Debug `ALL_BUILD`、`native_limiter_tests`、`ui_unit_tests` 通过。

输入与输出电平表已接入公共 `Controls::drawMeter` 和逐声道 `MeterBallistics`。显示电平随帧时间平滑变化，峰值线使用全局保持时长，数字读数遵循全局毫秒刷新间隔；All 显示继续按活动声道的功率平均。Debug `ALL_BUILD`、`native_limiter_tests`、`ui_unit_tests` 通过，模块测试验证两条表的峰值线实际渲染。

GR 表改用公共 `Controls::drawMeter` 的 `fillOriginDb=0` 从 0 dB 向负衰减值填充；输入、输出及 GR 刻度使用公共刻度布局。现有 `build` 树的 Debug `ALL_BUILD`、`native_limiter_tests`、`ui_unit_tests` 通过。

传输曲线与其下方实时填充改用公共 `Controls::drawCurvePath`。Debug `ALL_BUILD`、`native_limiter_tests`、`ui_unit_tests` 通过。

## 主题命名空间与绘制接入（2026-09-27，S12 后）

本效果单独导出 Native Fx / Node v1 双入口，共用稳定 ID `limiter` 及模块作用域；越界索引返回 null，宿主按 ID 去重并分别释放租约。描述符从冻结注册表复制默认值、类型与继承，声明旋钮、开关、transfer 曲线、低/中/高三段电平表及独立 GR 分段。每个电平段均可分别设置活动色和非活动色。

实际窗口的表面渐变两端、文字、旋钮外环、开关、图表背景/网格、曲线及其下方填充、两条电平表三段和 GR 分段已接入模块 token。无外部主题与发布默认 `.wjnskin` 的实窗截图逐像素相同；修改 token 后重绘的截图保存在 `tests/preview/PostS12/Limiter/`，改色文件名为 `-modified.png`。截图观感：**人工验收跳过/未人工验证**。

现有 `build` 的 `mod_limiter` Debug 单模块编译、完整 `ALL_BUILD` Debug 退出 0；`theme_limiter_native_fx`、先前四个模块 UI 测试、`native_limiter_tests`、`native_fx_api_tests`、`theme_default_template` 8/8 通过。验证仅含模块加载与 UI，未运行 JACK 客户端或端口连接集成。证据见 `build/PostS12-Limiter-*.log`。

## i18n S5 完成记录（2026-10-03）

本模块 S5 显示接入、所需 Release 构建和实窗验证已完成，按用户最新单次模块验证范围验收。独立 locate 包含 32 个 key 的完整 English、中文、日语和韩语；节点及调音台独立窗口、预设/操作显示接入可选 i18n v1，不改变稳定身份、工程字段或声道参数。

沿用实际 DLL 四语言热切换记录 [20261003T140124696Z-scale-2-limiter_delay-hot](../Docs/i18n/S5-bounded-records/20261003T140124696Z-scale-2-limiter_delay-hot.json)，覆盖多实例、双宿主、卸载/重新打开、English fallback、数字草稿及选区保持；组合批次检查数不能当作本模块独立检查数。平台为 windows，默认系统字体，使用原生窗口归属核对后的可见实窗截图，未运行 JACK 连接集成测试。

最终资源及二进制一致性见 [S5 核对](../Docs/i18n/S5-verification.json)，范围、构建日志和未验证场景见 [完成记录](../Docs/i18n/S5-completion.md)。依用户要求不追加缩放或重复模块测试；未补齐的冷启动/缩放矩阵不标为通过。译文仍待 S6 用户人工审校。

## 公开仓库前 S2 Node v4（2026-10-11）

本模块 Node 导出升级 v4，声明当前已实现的 JACK_NODE 和自带 GUI；
显式 VIRT 创建返回 null。S4 双模式迁移尚未完成，不提前声明 VIRT 能力。
Mixer placement、DSP、八声道参数和独立窗口行为保持。

Release 所需模块编译及离线模块加载/metadata 校验通过。独立节点窗口和
mixer 窗口完成 windows 平台、100% 缩放、系统字体实窗验证，测试退出 0；
截图为 Docs/Node/PrePublic-S2-native-fx-real-window/node-6.png 和
mixer-6.png。阶段显示计数、命令和日志见
[S2 证据](../Docs/Node/PrePublic-S2-results.md)。不运行 JACK 连接集成测试；
人工界面、听音、硬件及安装验收已跳过（用户指令），不将跳过标为通过。

## 公开仓库前 S4 双模式完成（2026-10-11）

本效果 JACK_NODE/VIRT_NODE 实现及所需 Release 构建、离线模块加载、逻辑八通道布局和 100% Windows 实窗验证完成。两种实例模式固定且复用同一处理核心；VIRT 不初始化 JACK。Node boundary v2 区分实际 JACK 布局和 VIRT 逻辑布局，mixer placement 与参数/独立窗口语义保持。

本模块实际 DLL 的 VIRT、未初始化 JACK 节点和 mixer 三个窗口均通过；截图为 [VIRT 窗口](../Docs/Node/PrePublic-S4-native-real-window/virtual-node-6.png)、node-6.png、mixer-6.png。整个批次 31/100 次显示，并非本模块独立计数。命令、日志及范围见 [S4 证据](../Docs/Node/PrePublic-S4-results.md)。不运行 JACK 连接集成或新增效果算法测试；听音、硬件、人工和安装验收已跳过（用户指令），未验证影响见阶段证据。
