# WinJACKNexus M1 实施记录

> 记录日期：2026-08-11
>
> 范围：Common 音频设置、实时 block 契约、AudioEngine 和 JACK 音频后端最小闭环。

## 已完成

- 在 `WinJACKNexus.Common` 建立 `AudioDeviceSettings`、`EffectiveAudioSettings` 和 `AudioProcessContext`。
- 建立 `AudioBackend` 接口和 `JackAudioBackend` 实现。
- 建立固定容量 `JackClient`：最多 32 个音频端口，JACK process 回调只使用预注册端口指针数组，不进行容器扩容或文件 I/O。
- 建立 `JackAudioInput` 和 `JackAudioOutput`：分别负责真实 JACK 音频输入接收与输出写入，输出无提交 block 时按 block 清零。
- 建立 `JackMidiInput` 和 `JackMidiOutput`：支持真实 JACK MIDI 端口、frame offset、固定容量事件队列和原子丢事件计数。
- 支持 JACK 音频 input/output 端口注册、采样率/缓冲区回调、xrun 计数、激活/停用和关闭。
- `AudioEngine` 在运行时执行输入到输出的无分配回环；停止或无输入时清零输出 block。
- 建立固定容量 `SpscRingBuffer` 和 `MidiEventQueue`，支持实时线程间的音频/事件传递，不在 push/pop 中分配。
- 建立 `MeterFrame`、`LevelMeterProbe`、`HistorySample` 和 `HistoryProvider`，统一跨应用的基础计量/历史数据契约。
- 建立 `LoudnessPresetLibrary`，内置常用响度目标并支持非实时加载/保存自定义预设。
- 建立后台 `CsvLogWriter`，将 CSV 文件 I/O 与实时音频/MIDI 路径隔离，并提供丢弃/失败计数。
- 迁入 `MeterEngine`：Peak、RMS、True Peak、Momentary/Short-term/Integrated LUFS 和 LRA。
- 迁入 `SilenceDetector`：阈值、持续时间、重新 armed 和单次触发逻辑。
- Common 注册 `WinJACKNexus.Common.AudioEngineTests`，覆盖停止态清零和运行态回环。
- Common 注册 Meter、Silence、MIDI/FIFO 回归测试。
- Common 注册 M1 集成边界测试，覆盖 LevelMeter、预设、CSV writer，以及 JACK 服务可用/不可用两种生命周期路径。
- 顶层启用 `CTest`，使 Common 测试可从主工程根目录发现。

## 实时线程边界

- JACK process 回调不创建 `std::vector`、不分配内存、不加锁、不访问文件、不调用 UI。
- JACK MIDI process 回调只使用 JACK port buffer、固定大小 `MidiEvent` 和 SPSC 队列；超容量事件通过原子计数报告。
- 端口和 client 生命周期只在控制线程执行。
- 输出 block 在 `AudioEngine::process` 开始处清零，避免无数据时保留旧内容。
- 非法负 frame、block size 和 channel count 会在实时入口被安全忽略或钳制。
- 当前 AudioEngine 是最小 passthrough 骨架，具体 Mixer DSP 和应用 UI 仍留在后续阶段。

## 验证

- 主工程 `scripts/configure.cmd`：通过。
- 主工程 `scripts/build.cmd`：通过。
- `WinJACKNexus.Common.AudioEngineTests`：通过。
- `WinJACKNexus.Common.MeterEngineTests`：通过。
- `WinJACKNexus.Common.SilenceDetectorTests`：通过。
- `WinJACKNexus.Common.MidiAndFifoTests`：通过。
- `WinJACKNexus.Common.M1IntegrationBoundaryTests`：通过；当前环境 JACK 服务可用，已执行真实 client/port 打开、状态读取和关闭路径。
- 根目录 CTest：通过，当前包含以上五项 Common 测试。
- JACK 服务不可用路径通过错误信息分支测试；真实音频/MIDI 外部端口回环仍需连接外部 JACK 端口后手工验收。

## 保留边界

- `third_party/` 保持未追踪且不新增忽略规则。
- LCD 字体仍按本机资源忽略，不进入 commit/push。
- `ref/` 工程继续作为参考实现，不删除、不改名。

## M1 边界说明

- `JackAudioBackend`/`JackClient` 保留通用双向 backend；`JackAudioInput`、`JackAudioOutput` 提供独立真实端口角色，避免应用直接操作 JACK C API。
- `JackMidiInput`、`JackMidiOutput` 已提供真实 JACK MIDI 端口和事件读写；外部设备回环、SysEx 分片和端口连接拓扑属于手工集成验收范围。
- `CsvLogWriter`、预设文件和历史数据只在非实时线程使用，不进入 JACK process 回调。