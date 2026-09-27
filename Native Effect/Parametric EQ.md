# Parametric EQ 参量均衡器

## 完成状态

- 节点名称为 `Parametric EQ`，分类为 `Native Fx`。提供 L、R、C、LFE、Ls、Rs、Lb、Rb 八路独立输入、输出及参数状态；侧栏只保留 JACK 客户端名称。控制界面在独立顶层窗口，通道选择位于窗口上方，去掉参考图右侧的说明文字。
- 主界面按参考图的分区绘制标题、总开关、预设选择与导航、频响图、显示选项、分析设置、四组频段卡片及底部 HPF、LPF、输出增益。图表背景显示输入和处理后的频谱曲线，不绘制柱状频谱。图内图例区分输入频谱、处理后频谱、综合响应及四条频段曲线。无音频帧时只显示 EQ 频响曲线。
- 四个频段分别提供频率、Q、增益、启用和低架／峰值／高架／低通／高通类型。曲线选择按钮、当前类型预览及下拉项使用同一套自绘滤波器图标。频点可在图中拖动；旋钮可垂直拖动；双击频段旋钮可恢复该值。HPF、LPF 提供截止频率、启用和可从下拉菜单选择的 12／24／48 dB/Oct 斜率；总输出增益可调。底部三张卡片均位于窗口内。
- `All` 只显示八路参数的平均值；编辑时先以该显示值计算目标，再将目标绝对值写入八路对应参数。未编辑的参数仍保留各声道差异。单声道编辑不改变其他声道。
- 内置 Default、Vocal Clear、Bass Boost、Warmth、Presence 五个预设。Default 的四个频段增益均为 0 dB，HPF／LPF 关闭，输出增益为 0 dB；原有 Vocal Clear 的频段增减益保留为单独预设。预设作用于当前声道或 All 的八路。参数和客户端名称支持 JSON 保存、恢复，并校验范围与类型。
- DSP 位于本效果的 `src/native_fx/effects/parametric_eq/src/Dsp.cpp`，每路独立使用 biquad 处理四个频段及可选 HPF、LPF。输入／处理后采样复用插件内的 Graphic EQ 固定帧频谱队列；频谱在 UI 线程计算，可从下拉菜单选择 1024／2048／4096 点 FFT，All 模式按活跃声道功率聚合。平滑和增益显示范围也从下拉菜单选择。

## 验证

- 使用仓库现有 `build` 目录、Visual Studio 18 的 CMake 和 Debug 配置完成 `ALL_BUILD`，包含现有 VST2 和 VST3 目标。
- `native_parametric_eq_tests` 通过：节点发现与加载、独立窗口、参数保存和非法值拒绝、All 与单声道写入、预设通道隔离、频响与有限输出。`native_graphic_eq_tests` 回归通过。
- 未运行 JACK 客户端或端口连接集成测试，符合 Native Fx 验收要求。
- 预览图：`build/bin/ParametricEq-preview.png`，已检查与参考图的整体布局和色彩。
- 双曲线频谱预览：`build/bin/ParametricEq-spectrum-preview.png`，由模拟的单声道输入与衰减后输出采样生成。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/parametric_eq`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/parametric_eq/mod_parametric_eq.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_parametric_eq_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。

## Default 预设与状态显示（2026-09-26）

- DSP 初始参数和第一个 `Default` 预设统一为中性响应；底部重置和菜单重置均选择 Default。原初始增减益归入 Vocal Clear。界面载入参数或切换声道后，依据所选声道的完整参数判定预设；All 模式中声道参数不同则显示 Custom。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`（包含 VST2 和 VST3）；`native_parametric_eq_tests`、`native_fx_api_tests`、`ui_unit_tests` 均通过。验证范围仍为模块加载与 UI 行为。

## 公共旋钮与精确输入（2026-09-26）

- 四个频段及底部 HPF、LPF、输出增益共十五个旋钮统一调用 `Controls::drawKnob` 和宿主主题调色板。单击对应数值显示使用公共数值编辑器，按 Enter 写入；双击旋钮通过公共重置入口恢复该参数默认值。选择单声道时只写该声道；All 写同一绝对目标。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_parametric_eq_tests`、`native_reverb_tests`、`native_fx_api_tests`、`ui_unit_tests` 均通过。参量均衡器测试覆盖数值输入及双击重置。

## 调音台内部宿主桥接（M5，2026-09-26）

- 本模块在原生 FX v1 ABI 上提供可选的八声道参数快照：格式版本、模块 ID、有效声道掩码及逐声道真实参数。GUI 实例和音频实例分别创建；控制线程导出快照，调音台在 JACK 块间安全点导入，保留音频实例的 DSP 历史。克隆复制参数并重新准备处理器，不共享历史。
- 音频实例以原子峰值提供每声道输入/输出分析；FX 窗口显示调音台实时电平。效果链按 CH/AUX/MAIN 各自顺序执行，支持整体和单模块旁通、增删、排序、克隆及预设。内部加载只调用 mixer API，不创建效果器 JACK 客户端。
- 模块算法及原有独立节点入口保持原实现。参数导入路径使用固定大小内存和对应 DSP 的无分配 `setParameters`；Delay、Reverb 的缓冲分配只在 `prepare` 阶段进行。涉及文件：`src/native_fx/include/WinJACKNexus/NativeFx/NativeFxApi.h`、`src/native_fx/src/NativeFxApi.cpp`、`src/ui/src/FxSettingsWindow.cpp`、`src/mixer/EffectGraph.cpp`。
- Debug `ALL_BUILD`（含 VST2/VST3）及 24 个 CTest 套件通过；其中 `native_fx_api_tests` 验证八模块加载、快照掩码、分析和克隆，`ui_unit_tests` 验证 GUI 桥接，`digital_mixer_node_port_tests` 验证实际样本、旁通及端口不变性。证据：`build/m5-all-build.log`、`build/m5-verification-tests.log`。原生 FX 模块验收限于加载与 UI，不要求效果器 JACK 连接集成。

## 公共曲线绘制（2026-09-26）

- 输入频谱、各频段响应及综合响应统一使用 UI 公共 `Controls::drawCurvePath` 绘制；频段填充与曲线描边分开处理。
- 在现有 `build` 树完成 Debug `ALL_BUILD`，`native_parametric_eq_tests` 和 `ui_unit_tests` 通过。

## 主题命名空间与绘制接入（2026-09-27，S12 后）

本效果单独导出 Native Fx / Node v1 双入口，共用稳定 ID `parametric-eq` 与模块作用域；越界索引返回 null，宿主按 ID 去重并分别释放引用。描述符按冻结注册表复制 token 类型/默认值/继承，声明四条分频段响应及独立填充、输入/处理后频谱、总响应、四组分频段旋钮、滤波旋钮和开关。

实际窗口的背景、文字、主开关和普通开关、图表背景/网格、各分频段曲线及填充、图例、输入/处理后频谱、总响应、分频段及滤波旋钮活动环/外环已读取模块 token。主开关原有 `#1682d8` 强调色与普通开关 `#087afa` 不同，采用独立默认 token 保持原外观。无外部主题与发布默认 `.wjnskin` 的实窗截图逐像素相同；改色截图保存在 `tests/preview/PostS12/ParametricEq/`，修改文件名为 `-modified.png`。截图观感：**人工验收跳过/未人工验证**。

现有 `build` 的 `mod_parametric_eq` Debug 单模块编译及完整 `ALL_BUILD` Debug 退出 0；`theme_parametric_eq_native_fx`、先前六个模块 UI 测试、`native_parametric_eq_tests`、`native_fx_api_tests`、`theme_default_template` 10/10 通过。首次新测试编译因 include 目录名使用连字符而失败，修正后通过；首次默认像素比较发现主开关 738 像素不同，定位默认色后修复，历史失败不追认。只验证模块加载与 UI，未运行 JACK 客户端/端口连接集成。证据见 `build/PostS12-ParametricEq-*.log`。
