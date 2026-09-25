# 31 段图示均衡器（Graphic EQ）

状态：模块原有功能已完成；2026-09-24 DSP 负载优化已通过 `ALL_BUILD`，未针对本次优化运行测试。

## 模块信息

- 节点名称：`Graphic EQ`
- 分类：`Native Fx`。此节点是原生效果器，不是 VST。
- 音频布局：八进八出，通道顺序为 `L / R / C / LFE / Ls / Rs / Lb / Rb`。
- 通道控制：`All` 同时编辑八路参数；选择单个通道时只编辑该通道。

## DSP

算法位于插件的 `src/native_fx/effects/graphic_eq/src/Dsp.cpp`。每个声道各自使用 31 个 peaking biquad 滤波器，中心频率为 20 Hz 至 20 kHz 的标准 1/3 倍频程序列，Q 值为 4.318473。每段增益范围为 −12 至 +12 dB；输出增益范围为 −24 至 +12 dB，并提供总旁通。

实时处理使用稀疏滤波段列表：只有非零增益的频段会参与逐样本 biquad 运算，参数归零后保留残余状态继续衰减，低于 −240 dB 的状态清除后退出列表。平直响应因此不再逐样本运行 31 段滤波器；只启用少数频段时，运算量随启用段数缩减。滤波系数仍在参数更新时计算，不在逐样本路径中计算。

每个声道的启用状态、31 段增益和输出增益均保存在节点设置 JSON 中。预设文件保存和加载 31 段增益。

## UI

自绘界面位于 `src/native_fx/effects/graphic_eq/src/Ui.cpp`，可复用控件位于 `src/ui/src/Controls.cpp`。效果器主界面以无父窗口的 Qt 顶层窗口单独打开；整体窗口为 760×658，顶部 146 像素为声道选择条，下方主效果器面板按 760×512 参考图布局，不放进侧边栏。侧边栏仅显示客户端名称编辑控件。选择条采用 Gate 的标题和按钮排布：All 与八个声道按 80 像素间距排列。主面板包含标题、启用开关、预设导航、31 个矩形推子帽、无下方填充的频响曲线、右侧操作按钮，以及输出增益/显示模式/显示选项三张底部卡片。底部卡片距效果器面板下沿约 12 像素，窗口底部保留约 20 像素空间。

窗口包含 31 段、分频和曲线三种视图；原有视图切换按钮已移入底部“显示模式”卡片，顶部不再保留视图标签。图表固定显示 20 Hz 至 20 kHz，不提供频率范围选择。频段拖动控制、曲线/频率值显示、输出增益、旁通、预设及重置/平直/加载/保存操作均保留。默认预设为平直（所有频段和输出增益为 0 dB）；可选 Vocal Bright、Bass Boost 和 Classical，自定义参数显示 Custom。预设标签与下拉框垂直居中，预设框弹出可选菜单，当前预设文字水平居中；标题文本为启用开关预留了空间，完整显示。

图表使用固定的 20 Hz 至 20 kHz 横轴范围；DSP 使用固定的 31 个标准中心频率。

Graphic EQ 运行时会在频响图背景同时绘制输入参考频谱和 EQ 后频谱。音频回调在处理前后分别采集八个独立声道，通过有界 SPSC 队列把固定大小帧传给 UI；UI 每约 33 ms 读取最新帧，在非实时线程上执行 4096 点 Hann 窗 FFT，并按 20 Hz 至 20 kHz 对数映射为 96 个显示频段。选择单声道时只显示该声道的输入与输出；选择 `All` 时，仅将输入 RMS 高于 −100 dBFS 的声道纳入功率平均，并在图例显示参与路数。输入参考为琥珀色虚线，EQ 后频谱为青色实线及填充，两条曲线分别平滑。没有新音频帧时保留最近的频谱显示。

## 构建与验收

- 使用仓库现有 `build` 目录执行 `ALL_BUILD`，配置为 `RelWithDebInfo`。
- 构建缓存中的 `WJN_ENABLE_VST2=ON`；完整构建包含 VST2 32 位辅助进程和 VST3 主机、扫描器及其测试目标。
- `native_graphic_eq_tests` 通过：加载 Native Fx DLL、检查 Graphic EQ 节点导出信息、验证客户端名称侧栏控件、创建独立 UI、验证标题宽度、打开并选择预设菜单、点击底部显示模式卡片切换分频/曲线视图、验证默认平直响应，以及验证选中声道和 `All` 的输入/输出频谱、活跃声道筛选和渲染预览。
- 预设标签与下拉框现共用同一垂直区域；完整 `ALL_BUILD` 和模块加载/UI 验收通过。
- 按仓库测试记忆，本模块验收只检查模块加载和 UI，不执行 JACK 端口连接集成测试。
- 测试环境将 `${WJN_OUTPUT_DIR}` 加入 `PATH`，以便从配置专用测试目录加载仓库 DLL。
- 视觉预览：`build/bin/RelWithDebInfo/GraphicEq-preview.png`（默认平直）和 `build/bin/RelWithDebInfo/GraphicEq-curve-preview.png`（Vocal Bright，无下方填充），均已人工复核。
- 实时频谱预览：`build/bin/RelWithDebInfo/GraphicEq-spectrum-preview.png`，已人工复核。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/graphic_eq`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/graphic_eq/mod_graphic_eq.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_graphic_eq_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。