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