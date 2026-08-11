# WinJACKNexus M1 实施记录

> 记录日期：2026-08-11
>
> 范围：Common 音频设置、实时 block 契约、AudioEngine 和 JACK 音频后端最小闭环。

## 已完成

- 在 `WinJACKNexus.Common` 建立 `AudioDeviceSettings`、`EffectiveAudioSettings` 和 `AudioProcessContext`。
- 建立 `AudioBackend` 接口和 `JackAudioBackend` 实现。
- 建立固定容量 `JackClient`：最多 32 个音频端口，JACK process 回调只使用预注册端口指针数组，不进行容器扩容或文件 I/O。
- 支持 JACK 音频 input/output 端口注册、采样率/缓冲区回调、xrun 计数、激活/停用和关闭。
- `AudioEngine` 在运行时执行输入到输出的无分配回环；停止或无输入时清零输出 block。
- Common 注册 `WinJACKNexus.Common.AudioEngineTests`，覆盖停止态清零和运行态回环。
- 顶层启用 `CTest`，使 Common 测试可从主工程根目录发现。

## 实时线程边界

- JACK process 回调不创建 `std::vector`、不分配内存、不加锁、不访问文件、不调用 UI。
- 端口和 client 生命周期只在控制线程执行。
- 输出 block 在 `AudioEngine::process` 开始处清零，避免无数据时保留旧内容。
- 当前 AudioEngine 是最小 passthrough 骨架，DSP、MeterFrame 和 MIDI 能力留在后续 M1/M2 增量。

## 验证

- 主工程 `scripts/configure.cmd`：通过。
- 主工程 `scripts/build.cmd`：通过。
- `WinJACKNexus.Common.AudioEngineTests`：通过。
- 根目录 CTest：通过，当前包含 `WinJACKNexus.Common.AudioEngineTests`。
- 未运行 JACK 服务，因此真实 client/port 集成路径未在本次执行中验证。

## 保留边界

- `third_party/` 保持未追踪且不新增忽略规则。
- LCD 字体仍按本机资源忽略，不进入 commit/push。
- `ref/` 工程继续作为参考实现，不删除、不改名。