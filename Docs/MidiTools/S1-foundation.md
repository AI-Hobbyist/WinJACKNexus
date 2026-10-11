# MIDI Tools S1 基础与引擎门槛

状态：S1 已完成；S2～S6 未开始。

## 冻结边界

- Node v2 / I18n v1 不修改公开签名。三个独立 DLL、每 DLL 一个类型，分类 `Midi Tools`。
- S1 只有发现和实例生命周期基础，`init()` 明确返回失败，避免把尚未接入处理的节点当作可工作客户端。按 S2/S3/S4 顺序启用真实处理；离线 DLL 测试不打开 JACK。
- 三目录分别为 `nodes/midi_tools/{sf2_loader,midi_spilit,midi_matrix}`；各目录包含自己的 DLL 与 `locate`，不共享 manifest。构建暂存根为既有 `build/release/bin/midi_tools`，S6 按 `nodes` 目录打包。
- 客户端改名控制线程接缝：完整固定端口快照后才重建；对端完整名不变，本端（含自连接两端）按短名映射到实际客户端名；失败回滚，保留逐条重连失败信息。节点演奏/业务状态由节点持有，客户端 backend 只替换客户端/端口。宿主沿用 `wjn_get_node_client_id` 读取实际名称和原有工程格式。
- 公共侧栏只复用 `Controls::ClientNameWidget`；S2 起接入。matrix 和音源的独立窗口按阶段加入，S1 不提前实现 GUI。

## 依赖锁定

| 库 | 锁定版本与来源 | SHA256 | 构建/分发 |
| --- | --- | --- | --- |
| FluidSynth | [v2.5.7](https://github.com/FluidSynth/fluidsynth/tree/3ede3f7c8ffc5d16e5d28f416cbfd71f3ad2b814) | `ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a` | 动态库，cpp11 OS 层；所有音频/MIDI 驱动、网络、DLS、libsndfile、LADSPA、OpenMP 关闭；不构建 CLI/上游测试 |
| sfizz | [1.2.3 完整源码包](https://github.com/sfztools/sfizz/releases/tag/1.2.3) | `a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822` | 动态 C API；内部传递依赖静态；不构建 renderer、JACK、插件、demo、benchmark、上游测试；关闭 OpenMP/LTO/libsndfile |
| GCEM | `012ae73c6d0a2cb09ffe86475f5c6fba3926e200`（FluidSynth 自带校验配置） | `28159274c54e9640354852e172d10d88eb159f4e7f2fea42edbcd20105ed3526` | 仅头文件 |

源码与所有依赖生成产物均在既有 `build/_deps` 内。只定向构建 `midi_tools_foundation_tests`、`midi_tools_engine_probe` 与它们所需 DLL/库，Release；不调用 ALL_BUILD，不改 VST 配置。库运行时使用既有 MSVC DLL CRT。分发需提供 FluidSynth LGPL-2.1-or-later 许可/对应可重构建源码信息和动态替换方式；sfizz BSD-2-Clause 与源码包内部传递依赖通知在 S6 汇总，不只附主库 LICENSE。

## 合成配置与预算

- 48 kHz / 256 帧门槛夹具；最大节点帧数 8192（合成内部子块最多 256），准备样本预算 512 MiB，周期 MIDI 事件 4096，单 SysEx 4096 字节，资源 32，通道声部 64，音色代际 4。超限降级在节点阶段实现，不宣称 S1 已完成溢出处理。
- FluidSynth 16 MIDI 通道 / 16 audio groups / 16 stereo audio channels；`fluid_synth_process` 输出交错 L/R 的 32 个 planar 缓冲，调用前清零；禁用 reverb/chorus，不存在共享返回绕过 Mute/Solo。
- FluidSynth 单核、threadsafe-api=0、dynamic-sample-loading=0；加载在非实时线程，活动引擎只被音频线程访问；Bank 使用显式 `program_select`，普通 `(MSB << 7)|LSB`，GM 第 10 通道用显式 drum bank 128。
- sfizz 每通道/代际独立实例；事件 delay 在当前子块内，事件先提交再 render；设定块长并预分配声音/事件缓冲。不得在活动实例调用 load/setSamplesPerBlock/setNumVoices/setPreloadSize/free。
- 自制 `tone.sf2` / `tone.sfz` / `tone.wav` 为数学正弦，无商业样本，CC0 夹具；包含两个旋律 preset 与 drum bank 128。

## 源码实时审计约束

- FluidSynth 关闭 API 互斥和动态采样；单音频线程调用，不创建其驱动。内部缓冲量化必须由探针测量，不能声称采样精确或固定延迟可完全补偿。
- sfizz `MidiState::setSamplesPerBlock` 在控制线程 reserve，`insertEventInVector` 同一 delay 覆盖，实际可用帧数内 delay 不会超过 reserve；节点需要保持该约束。`Synth::renderBlock` 的 freeWheeling 分支会等待后台加载，生产必须禁用 freewheeling。
- sfizz `FilePool::getFilePromise` 为预加载资源发布有界加载队列；I/O 在加载线程；`triggerGarbageCollection` 使用 try-lock，竞争时立即返回；FileDataHolder 只原子引用计数。预加载大小必须受资源预算控制，不以无限预加载掩盖资源问题。
- 这些静态审计与小型探针只冻结适配约束；S4 仍需验证完整资源取消、代际回收、队列溢出和卸载，不以 S1 替代生命周期验收。

## 结果

Release 定向构建、三个真实 DLL 生命周期/语言目录检查、改名事务模型和两个真实引擎离线探针均通过。日志位于既有 build/MidiTools-S1-{configure,build,foundation,engines}.log，可用 Develop/MidiTools/Verify-S1.ps1 重跑。

- FluidSynth 内部 quantum 为 64 帧；首/中/末/跨周期事件 0、1、63、64、127、255、256、257，实际起音误差 2～65 帧（48 kHz 下最大约 1.354 ms）。CC120 在 0、1、127、255、256、257 边界测试，待输出缓冲最多保留 63 帧；不宣称采样精确。
- sfizz 起音误差 1～2 帧，跨周期绝对帧 257 起音为 258。CC120 上游直接立即重置 voice，不遵循非零 delay；节点必须先渲染至事件边界再提交该事件，分段探针在以上六边界全部得到边界后静音。
- 16 路 SF2 隔离、显式 bank/program/drum、旧音释放、缺失 program 拒绝；SFZ 独立实例与踏板释放均通过。sfizz 单夹具 reported bytes 为 633856（部分计数，不代表 RSS）。512 MiB 样本/64 声部/4 代际为适配预算，S4 仍需实际预算与溢出验证，不宣称已完成资源峰值测试。

S1 没有生产业务 GUI 修改，无 GUI 自动验收项；S2/S3/S5 实窗验证必须在全部阶段修改完成后执行。人工审校已跳过（用户指令）；真实 JACK 集成未执行（用户指定全部离线测试）。

### 首次发声崩溃诊断

- SF2 首次发声触发 `0xc0000409`；dump `midi_tools_engine_probe.exe.32164.dmp` 的 ExceptionInformation `[2]` 与 Application 1000 事件一致（stack cookie）。临时 linker map 将偏移 `0x19076` 定位到 `fluid_render_loop_singlethread`。
- FluidSynth 2.5.7 `FLUID_DECLARE_VLA` 的非 VLA/cpp11 分支为 `alloca(_len * sizeof(_type))`，传入加法表达式时只将后半部分乘以元素大小，16 路渲染导致栈数组不足。最小补丁 `alloca((_len) * sizeof(_type))` 已在 Engines.cmake 幂等应用；不改 Node ABI，16 通道探针已验证不再崩溃。
- 自制夹具 INAM 长度需要偶数字节，已修正。初始 64 帧起音断言未计入正弦夹具本身的起音检测 2 帧，需使用引擎内部 quantum + 夹具起音检测作为边界并记录实际值。

- sfizz 1.2.3 构建标准固定为 C++17（隔离在依赖函数作用域）；项目自身保持 C++20，防止二次配置继承 char8_t 导致上游 Opcode.cpp 编译失败。
