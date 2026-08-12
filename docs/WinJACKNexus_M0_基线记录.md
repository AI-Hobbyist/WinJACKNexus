# WinJACKNexus M0 基线与依赖确认

> 记录日期：2026-08-11
>
> 范围：M0 基线与依赖确认，不包含 M1/M2 的功能迁移。

## 1. 当前工程基线

- 分支：`main`
- 基线提交：`a63cf9e`（`fix: 将 M1.3 LED 修正为正圆并移除扫描线`）
- 顶层工程：CMake 3.22+、C++20、MSVC UTF-8、Ninja Debug。
- 当前目标：`WinJACKNexus.Common` 静态库与 `WinJACKNexus.Adapter` GUI 应用。
- Common 当前仍是骨架实现，包含 Version、主题/LED 和单实例基础代码；M0 不提前迁移 M1 能力。
- 构建入口：`scripts/configure.cmd` 与 `scripts/build.cmd`，构建目录为 `build/`。

## 2. 依赖与运行时

| 依赖 | 已确认内容 | 结论 |
|---|---|---|
| JUCE | `third_party/JUCE` 存在，顶层通过 `add_subdirectory` 集成；许可证为 JUCE 9 双许可证说明 | 可用于主工程构建，迁移时保留许可证要求 |
| JACK2 头文件 | `third_party/JACK2/include/jack/` 存在，包括 `jack.h`、`midiport.h`、`ringbuffer.h` 等 | 可作为 Common JACK/MIDI API 基线 |
| JACK2 MSVC 导入库 | `third_party/JACK2/lib/libjack64.lib` 存在 | Common 当前按该路径链接 |
| JACK2 运行时 | `third_party/JACK2/libjack64.dll`、`libjackserver64.dll` 存在 | 运行时由 JACK2 环境提供；主工程不在 M0 复制 DLL |
| VST SDK | `third_party/vst2sdk`、`third_party/vst3sdk` 存在 | M0 仅保留依赖，不纳入本次迁移 |

当前 `.gitignore` 未忽略 `third_party/`，因此这些依赖保持未追踪但仍可被 CodeGraph 扫描；不要新增忽略规则。

## 3. 参考测试基线

`ref/Jack Meter Bridge/CMakeLists.txt` 提供以下独立测试入口：

- `MeterEngineTests`：Peak、RMS、True Peak、Momentary/Integrated LUFS、LRA 与静音门限。
- `SilenceDetectorTests`：阈值、完整静音持续时间、一次性触发和重新计时。

`ref/PureMixer/CMakeLists.txt` 提供 `PureMixerEngineTests`，覆盖 Null backend、AudioEngine、MixerGraph、Solo/Mute、路由、增益和基础 DSP smoke test。

这些测试属于迁移前参考基线；它们尚未成为主工程 CTest 目标。M0 不复制实现，也不删除 `ref/`。

## 4. LCD 字体确认

| 文件 | 文件大小 | 解析到的字体族 | SHA-256 |
|---|---:|---|---|
| `LCD/zpix.ttf` | 7,179,288 bytes | `Zpix` | `ED39F02845E8C0B8CDBA275432250FB03E8528826F058BC151753BD62B44B744` |
| `LCD/DS-DIGI.TTF` | 24,448 bytes | `DS-Digital` | `87EB14D41EEEAC0BD7FE0C62ECE05134BBF1EE8059B6E3E701D7F4A7799506DC` |

两份字体均可由 Windows `PrivateFontCollection` 解析。字体文件按本机资源处理，默认由 `.gitignore` 排除，不进入 commit 或 push；CodeGraph 仍可在本地工作区观察到它们。字体授权/来源不纳入本次版本提交，Common 的复制、注册和打包逻辑仍留到后续阶段。

## 5. 语言资源 M0 决策

- 默认区域：`zh-CN`。
- 语言文件扩展名：`.lang`。
- 文件格式：UTF-8 JSON。
- 资源边界：语言文件独立于 `.netheme`，不放入主题 ZIP。
- 模块覆盖顺序：当前 APP 模块 > Common > 内置中文默认值。
- 当前阶段只锁定契约；解析、校验、缓存和线程隔离进入 M2。

## 6. 源码与资源许可

- `ref/Jack Meter Bridge/LICENSE`：项目代码为 MIT License，迁移时保留版权和许可文本。
- `third_party/JUCE/LICENSE.md`：JUCE 9 双 AGPLv3/商业许可及依赖说明。
- JACK2 头文件包含其 LGPL/GPL 许可声明；导入库和运行时文件按 JACK2 发行包许可处理。
- 字体许可/来源尚未在仓库中核实，列为阻塞项。

## 7. M0 验收状态

- [x] 主工程 CMake 配置入口与 Debug 构建路径已核对。
- [x] JUCE/JACK2 头文件、导入库和运行时文件已核对。
- [x] ref 的 Meter、SilenceDetector、PureMixer engine 测试入口已记录。
- [x] 两份 LCD 字体的存在性、可解析字体族和哈希已记录。
- [x] `.lang` 基础契约已锁定为 `zh-CN`、UTF-8 JSON、模块覆盖回退链。
- [x] 已识别并记录 JUCE、JACK2、ref 代码的许可证边界。
- [x] ref 工程的 CTest 缓存已按当前工作区路径重新生成，未再引用旧路径。
- [x] `MeterEngineTests`、`SilenceDetectorTests` 和 `PureMixerEngineTests` 均通过。
- [x] 两份 LCD 字体默认排除在 commit/push 之外；授权/来源说明保留为后续本机资源核验项。
- [x] 主工程 Debug 配置与构建通过；主工程当前尚未注册 CTest 目标。