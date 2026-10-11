# Reverb 混响

状态：已完成（2026-09-25）

## 节点与界面

- Native Fx 模块导出 `Reverb` 节点，保留八路输入和八路输出：L、R、C、LFE、Ls、Rs、Lb、Rb。
- 控制界面是独立的 770 × 645 窗口。顶部通道条采用参考图的深蓝按钮样式，不显示按钮后方的说明文字。All 模式把本次编辑的目标值写给全部八路；单通道模式只更改当前声道。
- 主面板按参考图排列电源、预设、场景、空间图示、十四个旋钮和底部混合区。Room、Hall、Plate、Church、Studio、Spring 用不同简单几何体表示；Custom 为圆柱体，其底面积随 Size 改变，高度随 Decay 改变。
- 内置 Default、Small Room、Bright Plate、Cathedral、Studio Booth、Vintage Spring 与 Custom 七组预设。Default 使用原 Large Hall 的 Hall 场景参数；Room／Hall 场景按钮分别选择 Small Room／Default。每个声道的参数可以独立保存并从 JSON 恢复。

## 音频处理

- 每个声道各自使用预延迟、四条反馈延迟线、两级扩散和频段阻尼生成混响尾音。场景类型改变延迟线对应的空间尺度。
- 早期反射、衰减时间、空间大小、预延迟、扩散、高低频阻尼、Wet、Dry 和输出电平均接入处理路径。L/R、Ls/Rs、Lb/Rb 声道对的宽度控制对湿声进行 Mid/Side 调整；C 和 LFE 保持独立声道处理。
- 关闭效果器时原样输出输入信号。处理时不在音频回调内分配内存。

## 验证

- 在现有 `build` 目录，以 Visual Studio 18 2026 的 CMake 完成 `ALL_BUILD`，且 `WJN_ENABLE_VST2=ON`。
- `ctest -R '^native_'`：8/8 通过。混响测试覆盖模块加载、独立窗口、八声道 JSON 往返、通道编辑、预设场景、Custom 图形变化、混响尾音及旁通。

## Default 预设对齐（2026-09-26）

- Reverb 初始参数、首项 Default、Hall 场景保持相同；载入参数或切换声道后依据完整参数显示预设名，All 模式声道参数不同时显示 Custom。Room 和 Hall 场景按钮及场景图片导航均按实际场景选择对应预设。All 模式的连续参数显示八声道算术平均值，编辑时由该显示值算出一个绝对目标并写给八路；未编辑参数仍保持各声道差异。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`（含 VST2、VST3）；`native_reverb_tests`、`native_parametric_eq_tests`、`native_fx_api_tests`、`ui_unit_tests` 均通过。验证范围为模块加载和 UI 行为。

## 公共旋钮与精确输入（2026-09-26）

- 十四个旋钮统一调用 `Controls::drawKnob`，并通过 `Controls::paletteFor` 接收宿主主题。单击各旋钮数字显示调用公共数值编辑器，按 Enter 应用；双击旋钮调用公共重置入口恢复该参数默认值。All 模式向八路广播同一绝对目标。
- 现有 `build` 树的 Debug `ALL_BUILD` 以及 `native_reverb_tests`、`native_fx_api_tests`、`ui_unit_tests` 通过；Reverb 测试覆盖数值输入和双击恢复。
- 预览图：`build/bin/Reverb-preview.png`。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/reverb`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/reverb/mod_reverb.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_reverb_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。

## 调音台内部宿主桥接（M5，2026-09-26）

- 本模块在原生 FX v1 ABI 上提供可选的八声道参数快照：格式版本、模块 ID、有效声道掩码及逐声道真实参数。GUI 实例和音频实例分别创建；控制线程导出快照，调音台在 JACK 块间安全点导入，保留音频实例的 DSP 历史。克隆复制参数并重新准备处理器，不共享历史。
- 音频实例以原子峰值提供每声道输入/输出分析；FX 窗口显示调音台实时电平。效果链按 CH/AUX/MAIN 各自顺序执行，支持整体和单模块旁通、增删、排序、克隆及预设。内部加载只调用 mixer API，不创建效果器 JACK 客户端。
- 模块算法及原有独立节点入口保持原实现。参数导入路径使用固定大小内存和对应 DSP 的无分配 `setParameters`；Delay、Reverb 的缓冲分配只在 `prepare` 阶段进行。涉及文件：`src/native_fx/include/WinJACKNexus/NativeFx/NativeFxApi.h`、`src/native_fx/src/NativeFxApi.cpp`、`src/ui/src/FxSettingsWindow.cpp`、`src/mixer/EffectGraph.cpp`。
- Debug `ALL_BUILD`（含 VST2/VST3）及 24 个 CTest 套件通过；其中 `native_fx_api_tests` 验证八模块加载、快照掩码、分析和克隆，`ui_unit_tests` 验证 GUI 桥接，`digital_mixer_node_port_tests` 验证实际样本、旁通及端口不变性。证据：`build/m5-all-build.log`、`build/m5-verification-tests.log`。原生 FX 模块验收限于加载与 UI，不要求效果器 JACK 连接集成。

## 主题命名空间与绘制接入（2026-09-27，S12 后）

本效果单独导出 Native Fx / Node v1 双入口，共用稳定 ID `reverb` 和模块作用域；越界索引返回 null，宿主按 ID 去重并分别释放引用。描述符从冻结注册表复制 token 类型/默认值/继承，声明紫色及青色旋钮、开关和场景图示的选中青色、形体高光与辉光。Reverb 没有 dB 曲线或电平表，描述符不声明这两类语义。

实际窗口的表面、主/次/标题文字、旋钮活动环与外环、开关、场景选中边框、形体高光及辉光已接入模块 token。无外部主题与发布默认 `.wjnskin` 的实际 Windows 弹窗截图逐像素相同；改色截图保存在 `tests/preview/PostS12/Reverb/`，修改文件名为 `-modified.png`。截图观感：**人工验收跳过/未人工验证**。

现有 `build` 的 `mod_reverb` Debug 单模块编译及完整 `ALL_BUILD` Debug 退出 0；`theme_reverb_native_fx` 实窗测试通过。最终组合回归及启动探针另见主题系统计划书最终记录。仅验证模块加载与 UI，未运行 JACK 客户端或端口连接集成。

## i18n S5 完成记录（2026-10-03）

本模块 S5 显示接入、所需 Release 构建和实窗验证已完成，按用户最新单次模块验证范围验收。独立 locate 包含 42 个 key 的完整 English、中文、日语和韩语；节点及调音台独立窗口、预设/操作显示接入可选 i18n v1，不改变稳定身份、工程字段或声道参数。

沿用实际 DLL 四语言热切换记录 [20261003T141857552Z-scale-2-compressor_reverb-hot](../Docs/i18n/S5-bounded-records/20261003T141857552Z-scale-2-compressor_reverb-hot.json)，覆盖多实例、双宿主、卸载/重新打开、English fallback、数字草稿及选区保持；组合批次检查数不能当作本模块独立检查数。平台为 windows，默认系统字体，使用原生窗口归属核对后的可见实窗截图，未运行 JACK 连接集成测试。

最终资源及二进制一致性见 [S5 核对](../Docs/i18n/S5-verification.json)，范围、构建日志和未验证场景见 [完成记录](../Docs/i18n/S5-completion.md)。依用户要求不追加缩放或重复模块测试；未补齐的冷启动/缩放矩阵不标为通过。译文仍待 S6 用户人工审校。

## 公开仓库前 S2 Node v4（2026-10-11）

本模块 Node 导出升级 v4，声明当前已实现的 JACK_NODE 和自带 GUI；
显式 VIRT 创建返回 null。S4 双模式迁移尚未完成，不提前声明 VIRT 能力。
Mixer placement、DSP、八声道参数和独立窗口行为保持。

Release 所需模块编译及离线模块加载/metadata 校验通过。独立节点窗口和
mixer 窗口完成 windows 平台、100% 缩放、系统字体实窗验证，测试退出 0；
截图为 Docs/Node/PrePublic-S2-native-fx-real-window/node-9.png 和
mixer-9.png。阶段显示计数、命令和日志见
[S2 证据](../Docs/Node/PrePublic-S2-results.md)。不运行 JACK 连接集成测试；
人工界面、听音、硬件及安装验收已跳过（用户指令），不将跳过标为通过。
