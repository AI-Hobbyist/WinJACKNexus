# Reverb 混响

状态：已完成（2026-09-25）

## 节点与界面

- Native Fx 模块导出 `Reverb` 节点，保留八路输入和八路输出：L、R、C、LFE、Ls、Rs、Lb、Rb。
- 控制界面是独立的 770 × 645 窗口。顶部通道条采用参考图的深蓝按钮样式，不显示按钮后方的说明文字。All 模式把本次编辑的目标值写给全部八路；单通道模式只更改当前声道。
- 主面板按参考图排列电源、预设、场景、空间图示、十四个旋钮和底部混合区。Room、Hall、Plate、Church、Studio、Spring 用不同简单几何体表示；Custom 为圆柱体，其底面积随 Size 改变，高度随 Decay 改变。
- 内置 Small Room、Large Hall、Bright Plate、Cathedral、Studio Booth、Vintage Spring 与 Custom 七组预设。每个声道的参数可以独立保存并从 JSON 恢复。

## 音频处理

- 每个声道各自使用预延迟、四条反馈延迟线、两级扩散和频段阻尼生成混响尾音。场景类型改变延迟线对应的空间尺度。
- 早期反射、衰减时间、空间大小、预延迟、扩散、高低频阻尼、Wet、Dry 和输出电平均接入处理路径。L/R、Ls/Rs、Lb/Rb 声道对的宽度控制对湿声进行 Mid/Side 调整；C 和 LFE 保持独立声道处理。
- 关闭效果器时原样输出输入信号。处理时不在音频回调内分配内存。

## 验证

- 在现有 `build` 目录，以 Visual Studio 18 2026 的 CMake 完成 `ALL_BUILD`，且 `WJN_ENABLE_VST2=ON`。
- `ctest -R '^native_'`：8/8 通过。混响测试覆盖模块加载、独立窗口、八声道 JSON 往返、通道编辑、预设场景、Custom 图形变化、混响尾音及旁通。
- 预览图：`build/bin/Reverb-preview.png`。

## 原生 FX API 迁移验证（2026-09-25）

- 效果元数据、逻辑适配、界面和 DSP 已归入 `src/native_fx/effects/reverb`；效果算法不再编入 `common.dll`，并独立输出 `build/bin/nodes/native_fx/reverb/mod_reverb.dll`。
- 使用现有 `build` 树完成 Debug `ALL_BUILD`；`native_reverb_tests`、`native_fx_api_tests` 和 `ui_unit_tests` 通过。验证范围为模块加载与界面行为，未运行 JACK 客户端或端口连接集成测试。
