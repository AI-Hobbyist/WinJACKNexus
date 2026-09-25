# Parametric EQ 参量均衡器

## 完成状态

- 节点名称为 `Parametric EQ`，分类为 `Native Fx`。提供 L、R、C、LFE、Ls、Rs、Lb、Rb 八路独立输入、输出及参数状态；侧栏只保留 JACK 客户端名称。控制界面在独立顶层窗口，通道选择位于窗口上方，去掉参考图右侧的说明文字。
- 主界面按参考图的分区绘制标题、总开关、预设选择与导航、频响图、显示选项、分析设置、四组频段卡片及底部 HPF、LPF、输出增益。图表背景显示输入和处理后的频谱曲线，不绘制柱状频谱。图内图例区分输入频谱、处理后频谱、综合响应及四条频段曲线。无音频帧时只显示 EQ 频响曲线。
- 四个频段分别提供频率、Q、增益、启用和低架／峰值／高架／低通／高通类型。曲线选择按钮、当前类型预览及下拉项使用同一套自绘滤波器图标。频点可在图中拖动；旋钮可垂直拖动；双击频段旋钮可恢复该值。HPF、LPF 提供截止频率、启用和可从下拉菜单选择的 12／24／48 dB/Oct 斜率；总输出增益可调。底部三张卡片均位于窗口内。
- `All` 只显示八路参数的平均值；编辑时先以该显示值计算目标，再将目标绝对值写入八路对应参数。未编辑的参数仍保留各声道差异。单声道编辑不改变其他声道。
- 内置 Vocal Clear、Flat、Bass Boost、Warmth、Presence 五个预设。预设作用于当前声道或 All 的八路。参数和客户端名称支持 JSON 保存、恢复，并校验范围与类型。
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
