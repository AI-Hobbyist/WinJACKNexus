# S4 — sf2_loader 合成核心完成记录

2026-10-11。核心实施、Release 构建、离线合成与生命周期用例、公共侧栏 100% Windows 实窗验证通过。独立业务窗口留到 S5；本记录不宣称 S5/S6 或最终完整包已经完成。

## 实施边界

- `MidiToolsSynth` 在 common 内实现，与 QWidget/JACK 无耦合；sf2_loader 独立 DLL 提供固定 `MIDI_IN`、`OUT_L`、`OUT_R`，公共改名侧栏和原有 Node v2/I18n v1 导出。
- 资源有稳定 ID、路径、导入顺序、状态和音色索引；SF2 查原生 Bank/Preset，SFZ 显式保存 MSB/LSB/Program 映射。首选资源优先，其后按导入顺序。无匹配的新音符被忽略并计数，默认第 10 通道查 drum bank 128。
- 每通道最多 64 个音符归属和 4 个音色代际；Note Off/延音释放送回旧拥有者。超额清理最旧代际并计数。资源移除渐退 5 ms，不删除原文件；采样率改变时停止旧率声部，候选未准备期间清零。
- 引擎内处理 CC7/CC11，后级单独处理通道 Gain/Mute/Solo 与 Master；Mute 优先，静音仍推进合成和处理释放。CC121 保留 Bank/音量/声像及 sound/effect controller，复位表情/踏板等，不改 UI 设置；两个引擎的控制状态归一。
- JSON version 1 完整校验后恢复：资源顺序、首选、16 通道映射/混音、Master、实际客户端名称。拒绝非法版本/字段/数值/重复 ID 或路径；合法缺失文件保留引用并显示 missing，损坏文件单独 failed。

## 有界线程与资源

- 加载线程独占文件读取、引擎创建、样本准备和索引。活动引擎只由音频线程派发/渲染；候选通过容量 2 的 SPSC 发布，退役容量 8，由加载线程销毁。电平/诊断容量 4，控制线程单一消费。
- 最多 32 资源、4 活动资源库、每周期 4096 事件/8192 帧、每个 SysEx 4096 字节；子块至多 256 帧。超限清音、优先保留释放消息，诊断可读取；不无限积压 pending Note On。
- 准备预算 512 MiB，含候选和未退役资源；SF2 按文件长度的 8 倍保守预留。sfizz 的 `sfizz_get_num_bytes()` 实际来自全局 `Buffer<float>::counter()`，首实例快照保守包含其他活动实例，其后按创建/加载增量补足预留。它是缓冲计数，**不是整个进程 RSS**；不据此宣称进程总内存不超过 512 MiB。
- FluidSynth 关闭 dynamic-sample-loading、API 互斥、驱动、reverb/chorus，预分配 1024 总复音；sfizz 每资源 16 独立实例，各 64 复音、256 帧块，加载线程设置有限 preload 并关闭 freewheeling。遵循 S1 审计：文件流读取在上游加载线程，回调不等待 freewheeling；MIDI 事件延迟为 0，在事件边界分段后提交，避免 CC120 非零 delay 问题。
- 适配回调只访问固定数组/已准备索引/有界队列，不调用 Qt、i18n、文件 API、同步日志、mutex 或引擎销毁。全局采样率通过原子交换，准备完成率单独记录，避免变率后误报完成。加载取消检查版本，队列满时加载线程等待/处理退役，音频线程不等待。
- 时序界限沿用真实引擎探针：FluidSynth 起音误差 2～65 帧，CC120 最多保留 63 帧内部缓冲；sfizz 起音 1～2 帧。未声称 FluidSynth 采样精确。

## 自动验证

- 前台 PowerShell Release 定向构建，日志 `build/MidiTools-S4-{configure,build,build-repair}.log`。Qt 6.10.3，现有 build，VST 配置未变。
- `SynthTests`：真实 SF2/SFZ、16 路隔离、映射/无匹配、旧资源延音释放、独立增益/CC7/CC11/CC121、Mute/Solo/Master、静音后的释放、缺失/损坏/缺样本/Unicode、预算拒绝、取消/快速提交、实际队列满后恢复、资源移除、4 代际淘汰、超额事件/帧恢复、44100 Hz 重建。最后一次准备计数 7,730,880 字节；损坏 SF2/缺样本的引擎报错是预期夹具结果。日志 `build/MidiTools-S4-synth.log`。
- 加载器真实 DLL 不 init JACK，完整状态/改名后资源和混音保持/错误原子拒绝/卸载；3 端口完整快照、冲突实际名称、自连接、回滚和部分重连失败模型。日志 `build/MidiTools-S4-sidebar.log`。
- 实窗平台 `windows`、visible=1、DPR=1、系统字体 Microsoft YaHei UI，复用 1 个窗口，预算 1/100。等待稳定后 QScreen 捕获真实窗口，检查四语言与韩文错误截图，CJK 无方块字。截图 `S4-real-window/synth-sidebar-*.png`。
- 本任务新代码按更新后的全局可读性规则展开格式；S1～S3 任务文件的格式调整已重新编译，基础加载/真实引擎/分流/矩阵回归通过。实窗回归额外显示分流 1 次、矩阵 3 次，批次总数不超过 100；代表截图再次检查。日志 `build/MidiTools-S4-{foundation,engines,split-regression,matrix-regression}.log`。

## 明确未验证项目

人工审校、人工界面验收、人工安装验收均已跳过（用户指令）；未确认实际部署或用户演奏体验。真实 JACK 客户端/连线/在线改名重连未执行，按计划使用离线模型，不把截图或模型当作真实 JACK 通过证据。任意商业音色及全部 SFZ opcode 不在覆盖范围。
