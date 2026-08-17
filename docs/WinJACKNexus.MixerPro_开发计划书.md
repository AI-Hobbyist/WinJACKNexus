# WinJACKNexus.MixerPro 架构与开发计划

> 项目：**WinJACKNexus.MixerPro**
> 定位：**轻量、解耦的虚拟音频混音器**
> 技术栈：**JUCE、C++20、JACK2 原生 API、juce::dsp**
> 文档状态：2026-08-17

## 0. 当前边界与清理记录

WinJACKNexus.MixerPro 是 `ref/WinJACKNexus.MixerPro` 下的独立参考工程，后续用于提取可合并到 WinJACKNexus 的音频引擎、控制模型和基础 UI 结构。当前只整理参考工程，不进行合并、提交或分支操作。

本次清理已经完成：

- 删除 `ref/WinJACKNexus.MixerPro` 中独立的 Meter Bridge 窗口、卡片视图、滚动容器和 Meter History 窗口。
- 删除主控制台通过右键打开 Meter Bridge 的入口，以及对应的窗口生命周期管理。
- 保留主控制台通道条内的基础峰值电平表和 `IN/OUT` 原型切换。
- 保留 `Source/DSP/LevelMeterProbe.*`，因为它属于后续音频引擎的基础电平探针，不是独立电平表桥 UI。
- 不修改 `modules/WinJACKNexus.MeterBridge`。该模块属于 WinJACKNexus 主工程，当前不与 WinJACKNexus.MixerPro 合并。

后续计划中的“电平表”均指通道处理链中的探针、峰值/RMS 数据和嵌入式通道条显示，不再指向已删除的独立电平表桥或历史窗口。

## 1. 项目目标

WinJACKNexus.MixerPro 面向低延迟、可预测的虚拟音频路由和轻量 DSP。它明确不是 VST 宿主、DAW 或第三方效果器机架，而是一个由 JACK2 连接物理或虚拟音频 I/O、提供基础混音处理并输出到目标端口的确定性控制台。MixerPro 不支持加载、扫描、管理或运行 VST/VST3/AU 插件；需要插件处理时，必须配合独立的 `WinJACKNexus.SingleVSTHost` 或 `WinJACKNexus.ChainVSTHost`，并通过 JACK 连接。

核心原则：

- **插件处理外置**：插件加载、插件参数、插件状态和插件链由独立的 SingleVSTHost/ChainVSTHost 负责，MixerPro 只通过 JACK 交换音频；首个架构只实现增益、EQ、滤波、声像、发送、测量和求和。
- **实时音频优先**：音频回调不得分配内存、加锁、阻塞 I/O、调用 UI、解析配置或调整容器大小。
- **隔离 JACK2**：JACK 生命周期、端口发现、回调和路由名称处理全部位于 `AudioBackend` 边界之后。
- **仅支持 JACK2**：MixerPro 的生产音频后端固定为 JACK2，不提供其他音频后端、后端选择或后端切换。
- **控制与数据分离**：UI 只修改参数意图，DSP 使用准备好的参数状态；UI 刷新频率不决定音频处理频率。
- **显式通道布局**：Mono、Stereo、2.1、5.1、7.1 使用明确的布局和总线映射，不把通道条写死成单声道或立体声。
- **结构变更快照化**：新增通道、删除通道、改变布局和重建路由都在非音频线程准备，通过快照交换提交。
- **监看保持轻量**：基础峰值/RMS 探针属于音频图的一部分；独立电平表桥不属于当前 WinJACKNexus.MixerPro 参考工程。

## 2. 系统架构

### 2.1 推荐目录

```text
Source/
  App/
    WinJACKNexus.MixerProApplication.h/.cpp
    MainWindow.h/.cpp
  Audio/
    AudioEngine.h/.cpp
    AudioBackend.h
    AudioSettings.h/.cpp
    JackAudioBackend.h/.cpp
    RealtimeTypes.h
  Mixer/
    MixerGraph.h/.cpp
    ChannelStrip.h/.cpp
    ChannelLayout.h/.cpp
    InputChannel.h/.cpp
    SubmixChannel.h/.cpp
    MasterChannel.h/.cpp
    AuxChannel.h/.cpp
    AuxSendMatrix.h/.cpp
    SoloMuteResolver.h/.cpp
  DSP/
    GainStage.h/.cpp
    Eq3Band.h/.cpp
    ParametricEq.h/.cpp
    LowCutFilter.h/.cpp
    Panner.h/.cpp
    SpatialPanner.h/.cpp
    LevelMeterProbe.h/.cpp
  Control/
    ParameterId.h
    ParameterStore.h/.cpp
    ControlSnapshot.h
    CommandQueue.h
    ProjectState.h/.cpp
    MixerProjectFile.h/.cpp
  UI/
    Components/
      ChannelStripComponent.h/.cpp
      MeterComponent.h/.cpp
      SpatialPannerComponent.h/.cpp
      ParametricEqWindow.h/.cpp
      AuxSendComponent.h/.cpp
    Theme/
      SkinManager.h/.cpp
      SkinPackage.h/.cpp
      SkinStyleTokens.h
      SkinAssetCache.h/.cpp
    Views/
      MixerConsoleView.h/.cpp
      KioskConsoleView.h/.cpp
```

WinJACKNexus.MixerPro 当前不包含独立 `FloatingMeterBridge` 或 `MeterHistoryWindow`。如果未来需要独立监看窗口，应作为 WinJACKNexus 的独立产品模块设计，并通过明确的数据接口消费共享电平帧，而不是重新把它嵌入 WinJACKNexus.MixerPro 原型。

### 2.2 信号流

```text
JACK 输入
  |
  v
输入通道条
  |
  +--> 前推子 Aux 发送 --> Aux 求和总线
  |
  v
输入推子/声像
  |
  +--> 后推子 Aux 发送 --> Aux 求和总线
  |
  v
直接输出目标
  |
  +--> 主混音总线
  |
  +--> 子混音通道 --> 主混音总线或下游子混音
  |
  v
Master 通道条
  |
  v
JACK 输出
```

输入通道的标准处理顺序：

```text
输入缓冲
  -> 输入增益
  -> Mute/Solo 解析
  -> 80 Hz 低切
  -> 三段快速 EQ
  -> 可选参数 EQ
  -> 前推子电平探针
  -> 前推子 Aux 发送
  -> 推子
  -> 标准声像或空间声像
  -> 后推子电平探针
  -> 后推子 Aux 发送
  -> 直接输出目标
```

Aux 和子混音通道采用相同的基本处理链：先对输入贡献求和，再执行输入增益、低切、EQ、推子、声像和电平探针，最后送往主混音、下游子混音或 JACK 输出。

Master 通道顺序：

```text
主混音求和
  -> Master DSP
  -> Master 推子
  -> 最终输出电平探针
  -> JACK 输出
```

### 2.3 音频线程与 UI 线程

音频线程只拥有当前已经准备好的处理快照。UI 不得原地修改快照。

```cpp
struct EngineSnapshot
{
    double sampleRate;
    int blockSize;
    std::vector<InputChannelRuntime> inputs;
    std::vector<AuxChannelRuntime> auxes;
    std::vector<SubmixChannelRuntime> submixes;
    MasterChannelRuntime master;
    RoutingMatrix routing;
    SoloMuteState soloMuteState;
};
```

工作规则：

- UI 在线程安全的 `ParameterStore` 中写入参数修改。
- 准备线程构建下一份快照，分配所有缓冲区，准备 DSP，并验证布局和路由。
- 音频线程在块边界交换 `std::shared_ptr<const EngineSnapshot>` 或等效的 RCU 句柄。
- 推子、声像、发送和 EQ 增益使用无锁参数槽与 `juce::SmoothedValue<float>` 平滑。
- 通道数量、Aux 数量、布局和路由目标变化必须通过快照重建提交。
- 电平数据只发布原始数值。UI 读取最新完整帧，允许丢弃过期帧，不对音频线程施加反压。

音频回调禁止：

- 分配内存、扩容容器或创建对象。
- 获取互斥锁、等待条件变量或执行阻塞调用。
- 写日志到磁盘或控制台。
- 枚举设备、重建 JACK 端口映射或解析项目文件。
- 调用组件、刷新窗口、创建弹窗或触发 UI 回调。

### 2.4 外部插件应用协作边界

MixerPro 不提供插件扫描、插件加载、插件插槽、插件参数编辑或插件状态保存。需要插件处理时，使用独立应用完成插件处理：

- `WinJACKNexus.SingleVSTHost`：加载和运行一个插件。
- `WinJACKNexus.ChainVSTHost`：加载和运行一条插件链。

典型 JACK 信号路径为：

```text
MixerPro 通道或总线
  -> JACK
  -> SingleVSTHost / ChainVSTHost
  -> JACK
  -> MixerPro 输入、Aux 或主输出
```

独立插件应用拥有插件扫描、插件文件路径、插件参数、插件预设、插件状态和插件链配置；MixerPro 只拥有自己的通道、混音、JACK 连接和电平数据。两类应用之间通过 JACK 端口连接，不在 MixerPro 内复制插件宿主功能。

## 3. JACK2 后端

### 3.1 固定的 JACK2 后端接口

```cpp
struct JackAudioSettings
{
    double requestedSampleRate = 48000.0;
    int requestedBlockSize = 128;
    bool followExternalClock = true;
};

class AudioBackend
{
public:
    virtual ~AudioBackend() = default;
    virtual BackendInfo getBackendInfo() const = 0;
    virtual std::vector<DeviceInfo> enumerateDevices() = 0;
    virtual void open(const BackendOpenConfig&) = 0;
    virtual void close() = 0;
    virtual void start(AudioProcessCallback*) = 0;
    virtual void stop() = 0;
    virtual BackendPortMap getPortMap() const = 0;
    virtual void refreshPortMapAsync() = 0;
};
```

MixerPro 只实例化 Common 的 JACK 后端。`AudioBackend` 是 Common 内部的统一边界，不代表 MixerPro 支持多个用户可选后端；MixerPro 不提供后端选择器或后端切换命令。

`AudioEngine` 只接收统一的 `AudioBufferView`，不暴露 JACK 缓冲区所有权、端口句柄或 JACK 回调细节。

### 3.2 采样率与块大小

- UI 只显示 JACK 请求值和当前实际生效值。
- JACK2 的采样率和块大小由 JACK 服务端控制，WinJACKNexus.MixerPro 默认跟随外部时钟和实际值。
- 应用设置不得在音频回调中处理。
- 用户不能切换音频后端；修改采样率或块大小时，由控制/JACK 线程停止或重配 JACK 连接，重新准备 `juce::dsp::ProcessSpec`，再提交新的引擎快照。
- 采样率变化必须重新计算滤波器、EQ、声像和电平积分窗口。
- 块大小变化应显示预期延迟，且不能影响实时回调的无分配约束。

### 3.3 JACK 端口身份

JACK 的 `client:port` 名称可能因服务端重启、设备重连、桥接器重启或用户编辑图而改变，不能作为唯一持久化键。

Common 的端口身份结构可以保留通用的 `backend` 字段，但在 MixerPro 中始终固定为 JACK，不对用户暴露后端类型选择。

```cpp
struct BackendPortIdentity
{
    BackendKind backend;
    juce::String stableId;
    juce::String canonicalName;
    juce::String displayName;
    juce::StringArray aliases;
    PortDirection direction;
    int channelIndex;
};

struct BackendPortBinding
{
    BackendKind backend;
    juce::String preferredStableId;
    juce::String preferredCanonicalName;
    juce::StringArray fallbackAliases;
    PortDirection requiredDirection;
    int expectedChannelIndex;
};
```

匹配顺序：

1. 稳定 ID 或 JACK 元数据。
2. 当前规范名称。
3. 用户别名和历史名称。
4. 方向与通道索引。
5. 最近一次布局提示。

端口暂时消失时保留通道和路由状态，仅将绑定标为未解析；匹配端口重新出现后，只有在启用自动重连时才自动恢复。手动重新绑定必须把旧名称加入别名列表。

JACK 图回调只投递轻量通知。完整端口映射、名称解析和路由重建在非实时线程完成。

## 4. 通道模型与路由

### 4.1 通道布局

| 模式 | 通道数 | 标准扬声器顺序 |
| --- | ---: | --- |
| Mono | 1 | C |
| Stereo | 2 | L、R |
| 2.1 | 3 | L、R、LFE |
| 5.1 | 6 | L、R、C、LFE、Ls、Rs |
| 7.1 | 8 | L、R、C、LFE、Ls、Rs、Lrs、Rrs |

```cpp
enum class ChannelMode
{
    mono,
    stereo,
    twoPointOne,
    fivePointOne,
    sevenPointOne
};

struct ChannelLayout
{
    ChannelMode mode;
    std::array<SpeakerRole, 8> speakers;
    int channelCount;
};
```

每个处理阶段都必须声明支持的布局或确定性的降级方式。标准 Pan 只处理 Mono/Stereo；环绕布局使用 `SpatialPanner`。

### 4.2 共享通道状态

```cpp
struct ChannelStripState
{
    ChannelId id;
    juce::String name;
    ChannelLayout layout;
    bool mute;
    bool solo;
    float inputGainDb;
    float faderDb;
    Eq3BandState quickEq;
    LowCutState lowCut80Hz;
    ParametricEqState parametricEq;
    MeterState meter;
};

enum class OutputTargetKind
{
    mainMix,
    submix,
    backendOutput
};

struct OutputTarget
{
    OutputTargetKind kind = OutputTargetKind::mainMix;
    SubmixId submixId;
    BackendOutputBinding backendOutput;
};
```

输入通道额外持有 JACK 输入绑定、输出目标、声像状态和 Aux 发送列表。Aux 和子混音通道持有输出目标与自己的声像状态。Master 是每个项目唯一且不可删除的通道，持有最终输出绑定和过载锁存状态。

### 4.3 容量限制

```cpp
struct ChannelCapacitySettings
{
    int maxInputChannels = 128;
    int maxAuxChannels = 32;
    int maxSubmixChannels = 64;
    int maxVisibleMeterChannels = 256;
    bool allowLimitChangesWhileEngineRunning = true;
    bool warnBeforeLargeAuxMatrix = true;
};
```

这些是项目软限制，不是编译期固定上限：

- 提高限制只影响校验和新增通道按钮，不会自动重建音频图。
- 降低限制不能自动删除现有通道；只阻止继续新增超限通道。
- 新增通道仍必须通过后端端口、内存和快照准备检查。
- Aux 矩阵规模约为 `输入通道数 * Aux 通道数`，应用前应显示资源提示。
- 内部可以保留防止整数溢出和异常分配的安全上限，但不能把它当成产品层通道限制。

### 4.4 输入、Aux、子混音与 Master

输入通道提供：

- Mono、Stereo、2.1、5.1、7.1 布局。
- `-60 dB` 至 `+24 dB` 的输入增益，默认 `0 dB`。
- Mute、Solo、80 Hz 低切、三段快速 EQ、参数 EQ、推子和声像。
- 按现有 Aux 动态生成的发送控制。
- 主混音、子混音或后端输出目标。
- 通道级峰值/RMS 探针和嵌入式电平表。

Aux 是一等总线。创建 Aux 后，为每个输入生成对应的发送状态；删除 Aux 时通过新的快照使相关发送失效。Aux 可以送主混音、子混音或独立后端输出。

子混音用于鼓组、对白、游戏采集、监听、环绕 Stem 和广播节目分组。输入、Aux 或其他子混音可以送入子混音；子混音可以送入主混音、下游子混音或后端输出。

所有子混音直连图必须无环。快照构建器需要拒绝直接环和间接环，例如 `Submix A -> Submix B -> Submix A`。直接送后端的通道默认不再同时送主混音，除非未来明确加入复制输出功能。

### 4.5 Aux 发送矩阵

逻辑矩阵：

```text
InputChannel[N] x AuxChannel[M] -> AuxSendState[N][M]
```

```cpp
struct AuxSendState
{
    AuxId targetAux;
    bool enabled;
    bool preFader;
    float sendLevelDb;
    PanState pan;
    SpatialPanState spatialPan;
};
```

规则：

- 关闭发送即输出静音。
- 前推子发送取自输入增益、Mute/Solo、低切和 EQ 之后，通道推子和主声像之前。
- 后推子发送取自推子和主声像之后。
- 发送声像独立于通道声像。
- 发送增益必须平滑，避免 zipper noise。
- 运行时使用预计算的源缓冲、目标缓冲、布局转换和 tap 描述，不在回调中按字符串或映射查找路由。

## 5. DSP 与电平数据

### 5.1 推荐处理器

- 增益：`juce::dsp::Gain<float>` 与 `juce::SmoothedValue<float>`。
- 低切：`ProcessorDuplicator` 加 IIR 系数，默认 80 Hz。
- 快速 EQ：低架、1 kHz 中频峰值和高架三个双二阶段。
- 参数 EQ：准备阶段分配固定的最大频段数，未启用频段旁路。
- 声像：立体声使用等功率 Pan，环绕使用空间分配。
- 电平探针：在输入、前推子、后推子和最终输出等边界采集原始指标。

```cpp
juce::dsp::ProcessSpec spec
{
    sampleRate,
    static_cast<juce::uint32>(maximumBlockSize),
    static_cast<juce::uint32>(layout.channelCount)
};
```

### 5.2 电平探针与 MeterFrame

基础电平数据属于引擎处理链，而不是窗口层的临时模拟值。当前 WinJACKNexus.MixerPro 保留 `LevelMeterProbe` 作为后续生产实现的占位边界。

```cpp
struct MeterFrame
{
    ChannelId channelId;
    std::array<float, 8> peakDb;
    std::array<float, 8> rmsDb;
    std::array<float, 8> peakHoldDb;
    bool overload;
    uint64_t audioFrameCounter;
};
```

后续实现顺序：

1. 在输入、Aux、子混音和 Master 的输入/输出边界加入探针。
2. 在 Compressor 和 Gate 后加入独立的 reduction 探针，减小值从上向下绘制。
3. 通过 SPSC 环形缓冲或原子最新帧发布数据，音频线程只写原始数值。
4. UI 读取最新帧并做显示插值，不能反向阻塞音频线程。
5. 测试峰值、RMS、Peak Hold、过载锁存、重置和不同布局的通道顺序。

### 5.3 声像

立体声标准 Pan 默认使用等功率曲线，中心衰减可配置为 `-3 dB`、`-4.5 dB` 或 `-6 dB`。单声道输入根据 Pan 位置分配到左右声道；立体声首版采用 balance 模式。

5.1 和 7.1 使用空间声像。状态预留高度和扩散参数：

```cpp
struct SpatialPanState
{
    float x;
    float y;
    float z;
    float divergence;
};
```

普通全频信号默认不进入 LFE。LFE 必须由低频管理策略或明确的专用发送产生。

## 6. 项目文件 `.mixer`

WinJACKNexus.MixerPro 项目文件使用 UTF-8 JSON 和 `.mixer` 扩展名，要求可读、可比较并支持版本迁移。

根对象至少包含：

```json
{
  "format": "WinJACKNexus.MixerProProject",
  "formatVersion": 1,
  "application": {
    "name": "WinJACKNexus.MixerPro",
    "version": "0.1.0"
  },
  "project": {
    "name": "Untitled",
    "masterLayout": "stereo",
    "audioSettings": {},
    "limits": {},
    "jack": {},
    "channels": {},
    "ui": {}
  }
}
```

默认空项目必须包含：零个输入、零个 Aux、零个子混音，以及一个不可删除的 Stereo Master。Master 默认 `0 dB`、未静音、快速 EQ 置平、80 Hz 低切关闭，并启用峰值/RMS 电平测量。Master 输出绑定可以为空，或在明确启用自动绑定时指向 JACK 默认立体声输出。

文件应保存：

- 输入、Aux、子混音和 Master 的稳定 ID、名称、布局与处理状态。
- 输入增益、推子、Mute、Solo、低切、快速 EQ、参数 EQ、声像和空间声像。
- Aux 列表、发送开关、发送增益、独立发送声像和 Pre/Post 状态。
- 子混音输出目标以及经过校验的路由引用。
- JACK 后端绑定的稳定 ID、规范名称、显示名称、别名、方向和通道索引。
- JACK 连接参数、请求采样率、块大小、外部时钟策略和重连策略。
- 输入/Aux/子混音容量软限制。
- 主窗口布局、当前窗口模式、皮肤引用和必要的显示状态。

文件不应保存：

- 实时电平、Peak Hold 历史、RMS 历史或过载锁存，除非未来单独加入诊断快照格式。
- 原始音频缓冲和后端拥有的端口句柄。
- 插件实例、插件参数、插件预设、插件状态、插件扫描结果、插件扫描路径和插件链配置；这些内容由 SingleVSTHost 或 ChainVSTHost 独立保存。
- SDK、编译器或构建工具的绝对路径。

加载流程：

```text
读取 JSON
  -> 校验根格式和版本
  -> 迁移旧版本
  -> 解析稳定对象 ID
  -> 校验后端绑定，但不要求端口当前在线
  -> 校验输出目标并拒绝子混音环
  -> 构建 ControlModel
  -> 在线程外准备 EngineSnapshot
  -> 原子提交快照
  -> 恢复窗口状态
```

保存必须写入同目录临时文件，完成刷新后在平台允许时原子替换目标文件。自动保存使用独立的临时后缀，不覆盖规范 `.mixer` 文件语义。

## 7. 皮肤包 `.mixerskin`

皮肤只改变 UI 外观，不改变音频、路由、DSP 或后端状态。皮肤包可以是解压目录或 zip 兼容文件，清单为 UTF-8 的 `manifest.json`，资源位于 `assets/`。

```text
MyDarkPro.mixerskin/
  manifest.json
  assets/
    fader_cap.png
    knob_background.png
    meter_bg.svg
    logo.png
```

清单至少包含 `format`、`formatVersion`、`packageId`、`name`、`author`、`style` 和 `assets`。支持 PNG 和 SVG；包不得执行代码。

`SkinManager` 负责：

- 加载内置默认皮肤。
- 校验并加载外部皮肤包。
- 提供颜色、字体、尺寸、间距、圆角、边框、阴影和电平颜色 token。
- 提供可选的旋钮、推子帽、按钮、开关、背景、图标和 Logo 资源。
- 在运行时切换皮肤、失效缓存并重绘 UI。
- 对缺失或损坏的资源逐控件回退到默认矢量绘制。

皮肤加载和位图读取不得发生在音频线程。所有控制组件必须从 `SkinManager` 取得视觉属性，不应散落硬编码产品颜色、字体和资源路径。

## 8. UI 结构

主控制台是高密度的操作界面，不是宣传页。通道条应保持稳定宽度和统一顺序：

1. 通道名称和布局标识。
2. 输入增益。
3. 低切和快速 EQ。
4. 参数 EQ 入口。
5. Aux 发送。
6. 直接输出目标。
7. 标准声像或空间声像。
8. 嵌入式峰值/RMS 电平表。
9. Mute/Solo。
10. 推子。

组件建议：

- `ChannelStripComponent`：固定宽度的通道条。
- `RotaryControl`：增益、EQ 和发送旋钮。
- `ToggleButton`：Mute、Solo、低切和发送开关。
- `SegmentedControl`：Pre-Fader/Post-Fader。
- `MeterComponent`：通道条内的峰值、RMS、Peak Hold 和过载显示。
- `SpatialPannerComponent`：环绕 XY 声像。
- `EqCurveComponent`：参数 EQ 频响预览。
- `OutputTargetSelector`：主混音、子混音或后端输出选择。

组件只持有显示状态和用户操作，不拥有权威音频状态。所有修改通过 ControlModel/ParameterStore 进入引擎。

当前不实现独立 Meter Bridge 和历史曲线窗口。主控制台可以显示紧凑的通道电平表；如果未来需要广播式多通道监看，应在 WinJACKNexus 的独立模块中定义共享电平帧接口后再实现。

## 9. UI 模式

WinJACKNexus.MixerPro 当前保留以下运行模式目标：

| 模式 | 用途 | 行为 |
| --- | --- | --- |
| 主窗口 | 日常混音操作 | 显示完整控制台 |
| 系统托盘后台 | 隐藏界面继续路由 | 引擎继续运行，托盘提供恢复、静音和退出 |
| Kiosk 全屏 | 第二显示器控制面 | 无边框全屏，选择目标显示器，可锁定布局 |

独立电平表窗口已从 WinJACKNexus.MixerPro 参考实现中移除，因此不再作为本工程的窗口状态。窗口模式切换不得改变 JACK 连接；Kiosk 模式要保存目标显示器和缩放比例，并提供可靠的退出序列。

托盘菜单至少应提供：显示主控制台、全部静音、重置过载、JACK 设置和退出。托盘图标需要反映运行、静音、JACK 断开和过载状态。

## 10. 线程模型

```text
消息/UI 线程
  - JUCE 组件
  - 参数编辑
  - EQ、动态和空间声像编辑窗口
  - 托盘和窗口模式
  - 项目加载/保存命令

音频回调线程
  - 后端回调
  - DSP 处理
  - 求和与路由
  - 无锁电平帧发布

引擎准备线程
  - 快照重建
  - 处理器准备
  - 缓冲池分配
  - 路由校验

持久化线程
  - 项目和设置保存
  - 临时文件写入
  - 原子替换
```

UI 默认以 60 FPS 更新控制和电平显示；隐藏窗口时应降低或停止刷新。电平数据采用拉取模型，单个电平组件变化时只重绘受影响区域。

## 11. 开发路线图

### 里程碑 0：仓库与构建基础

目标：建立可重复的 C++20 JUCE 应用骨架。

交付内容：

- CMake 工程和 `Source/` 模块结构。
- JUCE `juce_audio_basics`、`juce_audio_devices`、`juce_dsp`、`juce_gui_basics` 和 `juce_gui_extra` 配置。
- C++20、警告和平台编译选项。
- 不访问音频设备即可启动和退出的基础窗口。
- Debug/Release 构建记录。

验收：干净检出后可以配置、构建和启动；产品源文件不放在 `third_party/`。

### 里程碑 1：引擎骨架与 JACK 接入

目标：在增加 UI 复杂度前建立实时安全的音频边界。

交付内容：

- Common `AudioBackend` 接口与固定的 JACK2 适配器。
- JACK 请求/实际采样率与块大小模型。
- `AudioEngine` 的准备、启动、停止和不可变快照交换。
- 默认空项目与不可删除的 Stereo Master。
- 固定的立体声输入到 Master 直通路径。
- 动态 JACK 名称发现、重命名、断开和重连绑定。

验收：静音输入保持静音；回调只调用实时 API；采样率和块大小改变在回调外完成；稳定处理阶段无分配和加锁。

### 里程碑 2：核心混音图

目标：实现输入、Aux、子混音和 Master 的确定性求和。

交付内容：

- Mono、Stereo、2.1、5.1、7.1 布局。
- 输入、Aux、子混音和 Master 运行时结构。
- 主总线、子混音总线和输出目标路由。
- Mute/Solo 解析。
- 输入增益和推子平滑。
- 通道新增、删除、布局变化的快照构建器。

验收：多输入可稳定求和；输入和 Aux 可送子混音；子混音直接环和间接环被拒绝；Mute/Solo、布局转换和结构变更有测试。

### 里程碑 3：内置 DSP

目标：完成轻量的基础处理。

交付内容：

- 80 Hz 低切。
- 高/中/低三段快速 EQ。
- 固定最大频段的参数 EQ。
- EQ 系数更新管线。
- 标准立体声 Pan。
- 5.1/7.1 空间声像。
- 参数 EQ 浮动编辑窗口，但不新增电平表桥窗口。

验收：增益、滤波、EQ 和 Pan 更新平滑；旁路和频段启停不在回调中分配；频响计算不读取音频所有权状态。

### 里程碑 4：Aux 发送矩阵

目标：让 Aux 数量动态可控，并在每个输入通道上提供发送设置。

交付内容：

- 动态 `AuxSendMatrix`。
- Aux 新增时自动创建输入发送。
- 发送开关、增益、独立声像和 Pre/Post。
- 前推子和后推子 tap 缓冲。
- Aux 求和与输出目标路由。

验收：新增/删除 Aux 更新所有通道和快照；前推子不跟随输入推子变化；后推子跟随推子和声像；发送声像独立；Stereo 和 5.1 矩阵有测试。

### 里程碑 5：引擎电平测量

目标：提供基础、可测试且不拖慢音频线程的电平数据。

交付内容：

- Peak、RMS、Peak Hold 和过载锁存。
- 输入、前推子、后推子、Aux、子混音和 Master 探针。
- 无锁电平帧发布。
- 嵌入式通道条电平组件。
- 电平离线测试。

验收：回调不分配、不阻塞；UI 丢帧不影响音频；确定性测试信号得到容差内的峰值/RMS；过载保持到显式重置。

### 里程碑 6：主控制台

目标：提供可用的输入、Aux、子混音和 Master 控制台。

交付内容：

- `MixerConsoleView` 和通道条组件。
- 增益、Mute、Solo、低切、快速 EQ、推子、Pan/空间 Pan。
- 动态 Aux 发送和输出目标选择。
- 项目设置中的通道软限制。
- JACK 设置中的实际采样率、实际块大小和延迟。
- 皮肤选择和 `.mixer` 基础保存/加载。
- 嵌入式峰值/RMS 电平表。

验收：通道数量变化无需重启；限制降低不删除现有通道；控制通过模型进入引擎；所有视觉属性通过皮肤 token；保存文件可恢复路由、DSP、绑定和窗口状态。

### 里程碑 7：参数 EQ 与频响

目标：完成高级 EQ 编辑。

交付内容：

- 右键进入参数 EQ 编辑器。
- 多频段控制和可拖拽频响节点。
- 频段启用、类型、频率、Q 和增益。
- 可撤销编辑和持久化。

验收：打开和关闭 EQ 不中断音频；多个通道编辑窗口不串状态；频响与当前参数一致。

### 里程碑 8：托盘与 Kiosk 模式

目标：支持主窗口之外的运行方式，但不引入独立电平表桥。

交付内容：

- 系统托盘后台处理。
- 托盘菜单和状态图标。
- 选定显示器的 Kiosk 全屏控制台。
- 窗口和布局持久化。
- 主窗口与 Kiosk 之间的运行时皮肤切换。

验收：主窗口、托盘和 Kiosk 切换时音频不中断；后台仍可静音和恢复；Kiosk 可退出；皮肤切换不重启引擎。

### 里程碑 9：性能、可靠性与发布

目标：在真实路由负载下稳定运行。

交付内容：

- 代表性通道数量下的 CPU 基准。
- 实时分配/加锁审计。
- JACK 断开、重连、重命名和客户端重建恢复。
- MixerPro 与 `WinJACKNexus.SingleVSTHost`、`WinJACKNexus.ChainVSTHost` 的 JACK 往返连接测试，确认插件由独立应用加载。
- 采样率、块大小和项目恢复测试。
- Aux 矩阵重建压力测试。
- `.mixer` 崩溃安全保存、版本校验和迁移测试。
- `.mixerskin` 清单、资源、缺失文件和回退测试。
- 目标平台发布包。

验收：无已知稳定处理阶段分配；后端错误不会导致崩溃；项目恢复能够重建布局、路由、EQ、窗口和 JACK 别名；MixerPro 不加载插件，但可以通过 JACK 与 SingleVSTHost/ChainVSTHost 完成处理信号往返；调试关闭和发布构建通过基本内存/资源检查。

## 12. 初始性能目标

目标需要在真实硬件上复测和调整：

| 场景 | 采样率 | 块大小 | 目标 |
| --- | ---: | ---: | --- |
| 16 个立体声输入、4 个 Aux、快速 EQ、电平表 | 48 kHz | 128 | 现代桌面 CPU 上保持低个位数 CPU 占用 |
| 32 个立体声输入、8 个 Aux、快速 EQ、电平表 | 48 kHz | 128 | 无掉音，UI 稳定在 60 FPS |
| 8 个 5.1 输入、4 个环绕 Aux | 48 kHz | 256 | 无实时分配，路由重建可预测 |
| Kiosk 控制台运行 | 48 kHz | 128 | UI 不影响音频优先级 |

## 13. 首个公开预览的完成定义

WinJACKNexus.MixerPro 达到首个公开预览标准时，应满足：

- JACK2 后端可在目标 JACK 配置下使用。
- 新空项目包含不可删除的 Stereo Master、推子、EQ 和基础峰值/RMS 电平表。
- 输入、Aux、子混音和 Master 支持规定的控制。
- 音频设置显示 JACK 实际采样率和块大小，并能处理重连。
- Mono、Stereo、2.1、5.1、7.1 在模型中可表示并可预测路由。
- Aux 发送动态、可独立 Pan，并支持 Pre/Post。
- 输入/Aux 可直接送子混音，且子混音路由拒绝环路。
- 80 Hz 低切、快速 EQ、参数 EQ 和空间 Pan 已实现。
- 嵌入式峰值/RMS、Peak Hold 和过载指示可用。
- `.mixerskin` 可通过 token 和资源重绘控制件。
- 主窗口、托盘和 Kiosk 模式切换不停止音频。
- `.mixer` 可恢复足够的工程状态以继续工作。
- MixerPro 不支持插件加载；需要插件处理时，必须配合 `WinJACKNexus.SingleVSTHost` 或 `WinJACKNexus.ChainVSTHost` 独立应用，并通过 JACK 完成信号往返；MixerPro 项目不保存插件实例、插件参数、扫描路径或插件链状态。
- 音频回调通过分配、加锁和阻塞调用审计。
- 不把已删除的独立 Meter Bridge 或 Meter History 原型作为 WinJACKNexus.MixerPro 的交付依赖。

## 14. 生产移交顺序

1. 建立稳定通道 ID、`ChannelLayout`、参数状态和窗口/控制器所有权。
2. 实现快照式混音图、路由、EQ、动态处理和空间 Pan。
3. 加入输入/输出/削减量探针和无锁电平帧发布，替换所有代表性电平数据。
4. 持久化路由、DSP、电平 tap 和窗口状态到 `.mixer`。
5. 将原型控制绑定到可撤销的参数命令，并同步同一通道 ID 的可见控制。
6. 保持独立电平表桥不进入 WinJACKNexus.MixerPro；如需合并监看能力，先在 WinJACKNexus 主工程中定义模块边界和共享帧协议。
7. 验证 MixerPro 与 `WinJACKNexus.SingleVSTHost`、`WinJACKNexus.ChainVSTHost` 通过 JACK 协作；MixerPro 不保存插件实例、插件参数、扫描路径或插件链状态。
8. 完成 Debug CRT、AddressSanitizer 和正常窗口销毁检查后再进入发布加固。

## 7. WinJACKNexus.MixerPro 合并实施阶段

### M3：建立 MixerPro 模块并改名

1. 新增 `modules/WinJACKNexus.MixerPro` 和顶层 `add_subdirectory`。
2. 将 WinJACKNexus.MixerPro 应用入口、窗口和 `MixerConsoleView` 迁入 MixerPro。
3. 将 `WinJACKNexus.MixerProCore` 的调用改为 Common API，删除或停用独立 `WinJACKNexus.MixerProCore` target。
4. 将产品标识、target、窗口标题、资源和测试名称统一为 `MixerPro` / `WinJACKNexus.MixerPro`。
5. 产品运行路径固定接入 Common 的 JACK2 backend；`NullAudioBackend` 只供开发/测试构建使用，不提供后端选择或切换。

**验收**：MixerPro 可独立启动；没有 `WinJACKNexus.MixerPro` target、命名空间或产品标题残留；引擎 smoke tests 迁入统一测试目标并通过。

## 8. WinJACKNexus.MixerPro 模块落地边界

### 8.1 目标目录与应用迁移

MixerPro 的应用层目标目录为：

```text
modules/WinJACKNexus.MixerPro/
  CMakeLists.txt
  Source/
    Main.cpp
    App/
      MixerApplication.h/.cpp
      MixerMainWindow.h/.cpp
    Model/
      MixerProject.h/.cpp
      MixerViewState.h
    UI/
      MixerConsoleView.h/.cpp
      MixerMainComponent.h/.cpp
    Engine/
      MixerSession.h/.cpp
```

迁移 `ref/WinJACKNexus.MixerPro` 时，以下内容属于 MixerPro：应用入口、主窗口、混音器页面、通道条组合、快捷操作、界面状态和项目工作流。MixerPro 的生产运行固定使用 Common 的 JACK2 后端；`NullAudioBackend` 只保留为开发/测试替身，不作为用户可选后端。

MixerPro 只依赖 Common，不保留 `WinJACKNexus.MixerProCore` 独立 target，也不在 MixerPro 内复制 Common 已提供的 `AudioEngine`、DSP、JACK、MIDI 或电平计量实现。target、命名空间、窗口标题、资源和测试名称统一使用 `WinJACKNexus.MixerPro` / `MixerPro`。

### 8.2 应用级测试与独立运行

- MixerPro 单测覆盖 `MixerGraph` 路由、通道布局、增益/声像、空后端 smoke test、项目状态和 `.mixer` 配置。
- MixerPro UI 验收覆盖无数据、正常电平、过载 Peak hold、窄窗口、横向滚动、历史和 CSV 操作，以及 `Common + MixerPro` 主题覆盖。
- MixerPro 必须能够单独启动和关闭，不要求 Adapter 或 MeterBridge 同时运行。
- 与 Adapter、MeterBridge 并行运行时，MixerPro 的 JACK client/port 命名、线程生命周期和资源释放必须符合 Common 的跨 APP 约定。
