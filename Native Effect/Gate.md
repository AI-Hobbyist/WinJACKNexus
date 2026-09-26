# Gate 门限效果器

**状态：已完成**（实现、Release 编译和测试均通过）  
分类：`Native Fx`  
实现方式：WinJACKNexus 原生节点模块，不依赖 VST。

## 接口与控制

- 固定八路输入和八路输出，顺序为 `L / R / C / LFE / Ls / Rs / Lb / Rb`。
- JACK 端口命名为 `in_L` … `in_Rb` 和 `out_L` … `out_Rb`，每个声道独立处理，不混音。
- 顶部声道栏按参考图风格绘制，增加 `Lb`、`Rb` 以满足八声道要求。选择 `All` 时，后续参数操作同时写入八个声道；选择单个声道时只修改该声道。
- Gate 效果控制界面由独立顶层窗口显示。节点侧栏只提供 JACK 客户端名称输入和应用按钮。
- 在线改客户端名称时会重新打开 JACK 客户端，并尝试恢复每个声道原有的输入、输出连接；接线盘会重新读取节点当前客户端 ID。
- 主面板按参考图的 754 × 509 坐标布局自绘，包括曲线、六个可拖动旋钮、旁路、预设和侧链滤波控件；声道栏位于主面板上方。曲线不再对曲线下方区域填充。Attack 和 Release 使用对数刻度。预览见 [Gate-preview.png](Gate-preview.png)。

## 参数

| 参数 | 默认值 | 范围 | 作用 |
| --- | ---: | ---: | --- |
| Threshold | −32.0 dB | −60～0 dB | 开门阈值 |
| Ratio | 4.0:1 | 1～20:1 | 阈值以下的向下扩展比率 |
| Attack | 10 ms | 0.1～200 ms | 打开速度 |
| Release | 150 ms | 1～2000 ms | 关闭速度 |
| Floor | −60.0 dB | −90～0 dB | 最大衰减量 |
| Hold | 3.0 dB | 0～24 dB | 关闭阈值相对打开阈值的滞后量，避免阈值附近频繁开合 |
| Sidechain HP/LP | 80 Hz / 8 kHz | 20～2000 Hz / 500～20000 Hz | 仅过滤检测信号，不过滤主音频 |

预设为 `Vocal Gate`、`Tight Gate` 和 `Gentle Gate`。修改预设参数后界面显示 `Custom`。参数以八个声道对象保存到节点 JSON，恢复时严格校验类型、范围和声道数。图表是静态传输特性的示意图；实际输出包含检测包络、滞后和 Attack/Release 时间响应。

## 代码位置

- 模块 DSP：[Dsp.hpp](../src/native_fx/effects/gate/include/Dsp.hpp)、[Dsp.cpp](../src/native_fx/effects/gate/src/Dsp.cpp)
- UI 自绘和交互：[Ui.hpp](../src/native_fx/effects/gate/include/WinJACKNexus/NativeFx/gate/Ui.hpp)、[Ui.cpp](../src/native_fx/effects/gate/src/Ui.cpp)
- 节点、JACK 八进八出及设置持久化：[Library.cpp](../src/native_fx/src/Library.cpp)
- 自动检查：[GateTests.cpp](../tests/native_fx/GateTests.cpp)

## 完成验证

- Visual Studio 18 + Qt 6.10.3 的历史 Release 构建：`WinJACKNexus` 和 `wjn_gate_tests` 均成功；当前独立 DLL 输出为 `build/bin/nodes/native_fx/gate/mod_gate.dll`。
- 测试程序检查 DSP 衰减、开门、旁路、3 dB 滞后、`All`/单声道交互、旋钮拖动、图表区域无填充、独立效果窗口、侧栏只有客户端名称编辑、节点 API 导出、八声道设置往返与非法设置拒绝。
- JACK 集成测试在可访问服务器的环境中通过：确认八个输入及八个输出端口，临时 1 kHz 正弦音频链路高于阈值时放行、低于阈值时衰减；在线改名后确认连接恢复并继续放行。最终输出：`Gate DSP, UI, settings and JACK audio path passed`。

编译和测试均按前台 PowerShell 管道执行，并通过 `2>&1 | Tee-Object -FilePath "build.log"` 记录输出和立即检查 `$LASTEXITCODE`。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/gate`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/gate/mod_gate.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_gate_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。

## 调音台内部宿主桥接（M5，2026-09-26）

本模块通过 `NativeFxApi.h` 的可选八声道参数快照和原子峰值分析接口接入调音台。GUI 与音频实例独立；调音台在 JACK 块间安全点应用快照，保留 DSP 历史。克隆复制参数而不共享处理历史，模块加载不创建新的 JACK 客户端。Debug `ALL_BUILD` 及 24 个 CTest 套件通过；证据见 `build/m5-all-build.log`、`build/m5-verification-tests.log`。

## 公共数值交互（2026-09-26）

Gate 六个旋钮的数字读数接入 `UI::Controls::editNumericValue`，点击可输入精确参数并用 Enter 应用；双击旋钮通过 `resetNumericValue` 恢复模块参数默认值。All 模式把绝对目标值写入八个声道，单声道模式仅修改所选声道。使用现有 `build` 树完成 Debug `ALL_BUILD`，`native_gate_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 全部通过；验证范围为模块加载与 UI 行为。

旋钮绘制已接入公共 `Controls::drawKnob` 和宿主调色板。新增渲染测试验证宿主强调色覆盖可到达旋钮笔画；迁移后的 Debug `ALL_BUILD` 与 `native_gate_tests` 通过。

门限图中的上、下传输曲线使用公共 `Controls::drawCurvePath`，笔画不携带填充。现有 `build` 树的 Debug `ALL_BUILD`、`native_gate_tests`、`ui_unit_tests` 通过。
