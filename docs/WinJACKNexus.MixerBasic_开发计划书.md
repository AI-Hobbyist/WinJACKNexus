# WinJACKNexus.MixerBasic 开发计划

> 产品名称：**WinJACKNexus.MixerBasic**
> 客户端名称：**MixerBasic**
> 定位：**面向初学者的基础音频混音器**
> 技术栈：**JUCE、C++20、JACK2 原生 API**
> 文档状态：2026-08-17

## 0. 产品定位

WinJACKNexus.MixerBasic 是 WinJACKNexus 的简化版混音客户端。它面向第一次接触 JACK、虚拟音频路由和混音器的用户，优先提供容易理解、容易操作、容易排错的基础工作流。

MixerBasic 只解决三件事：

1. 接收 JACK 输入音频。
2. 调整每路输入的音量、声像和静音状态。
3. 将混音结果发送到主输出或基础 Aux 输出。

MixerBasic 不追求功能数量。用户打开程序后，应能在较少的概念和按钮下完成“输入、调音、输出”的完整流程。

插件处理不在 MixerBasic 内完成。MixerBasic 不扫描、加载、管理或运行 VST/VST3/AU 插件；需要插件处理时，使用独立的 `WinJACKNexus.SingleVSTHost`（单插件）或 `WinJACKNexus.ChainVSTHost`（插件链），通过 JACK 与 MixerBasic 连接。插件宿主负责插件加载、插件状态和插件处理，MixerBasic 只负责音频路由、混音和电平显示。

## 1. 范围与明确排除项

### 1.1 首个版本包含

- 单声道 Mono 输入和输出。
- 立体声 Stereo 输入和输出。
- 输入增益、通道推子、声像、静音和独奏。
- 主混音和最多 8 个基础 Aux 发送总线。
- 输入通道、Aux 和 Master 的 Peak/RMS 电平显示。
- JACK 音频输入、音频输出和端口重新连接。
- 生产音频后端仅支持 JACK2，不提供其他后端或用户可选的后端切换。
- 内部 NullAudioBackend 测试替身，供开发和自动化测试使用，不作为用户运行模式。
- 简单项目保存和加载。

### 1.2 首个版本不包含

- 任何 EQ：包括低切、高切、三段 EQ、参数 EQ 和 EQ 曲线编辑器。
- 任何 DYN：包括 Compressor、Gate、Limiter、Expander 和动态处理器。
- 任何 SubMix：不提供子混音层级、子混音嵌套或子混音路由。
- 2.1、5.1、7.1 或其他环绕声布局。
- 不支持加载、扫描或管理 VST/VST3/AU 插件，也不包含插件插槽和第三方效果器机架。
- 独立 Meter Bridge、历史曲线窗口和 CSV 电平记录。
- MIDI 控制、控制面板脚本和复杂自动化系统。
- 需要用户理解复杂路由图的高级工作流。

“不包含”是产品边界，不是暂时隐藏的按钮。首个版本的代码、界面、项目格式和测试都不应依赖这些功能。

## 2. 面向初学者的使用目标

### 2.1 第一次启动

第一次启动时，MixerBasic 应提供一个可理解的空项目：

- 一个不可删除的 Stereo Master。
- 没有隐藏的 EQ、DYN 或 SubMix 状态。
- 主界面明确显示当前 JACK 状态、输入数量和输出状态。
- 没有 JACK 服务时显示可读的错误说明，不提供切换到其他音频后端的选项。
- 用户可以通过“添加输入”开始创建第一个通道。

### 2.2 用户应该能看懂的词

界面优先使用以下词汇：

| 界面名称 | 含义 |
| --- | --- |
| 输入 | JACK 提供给 MixerBasic 的音频来源 |
| 音量 | 当前通道的输入增益或推子音量 |
| 声像 | 单声道声音在左右声道之间的位置 |
| 发送 | 将当前通道的一部分声音复制到 Aux |
| 主输出 | 所有通道最终汇总的位置 |
| 静音 | 暂停该通道的声音 |
| 独奏 | 只试听选中的通道或通道组 |
| 电平 | 当前声音的大小 |

实现时避免在首屏直接展示 `tap`、`snapshot`、`layout transform` 等内部术语。需要显示技术状态时，使用简短说明和明确错误原因。

### 2.3 最短操作路径

用户完成第一次声音输出最多需要以下步骤：

1. 启动 MixerBasic。
2. 选择或确认 JACK 输入端口。
3. 添加一个 Mono 或 Stereo 输入通道。
4. 选择输入端口。
5. 推高通道推子和 Master 推子。
6. 观察电平表并确认主输出端口。

## 3. 支持的音频模型

### 3.1 通道布局

MixerBasic 只支持两种布局：

| 模式 | 通道数 | 说明 |
| --- | ---: | --- |
| Mono | 1 | 单通道输入或单通道输出 |
| Stereo | 2 | 左、右两个通道 |

所有输入通道、Aux 和 Master 都必须明确声明 Mono 或 Stereo。不能通过隐藏的通道数量猜测布局，也不能在首个版本中接受 2.1、5.1 或 7.1。

### 3.2 Mono 与 Stereo 转换

为了让初学者不必理解复杂的格式转换，首个版本使用固定规则：

- Mono 输入送到 Stereo 目标时，默认居中复制到左、右声道。
- Stereo 输入送到 Mono 目标时，按固定规则求和为单声道。
- Stereo 输入使用声像控制时，首个版本将声像解释为左右平衡控制。
- Mono 输入使用声像控制时，首个版本使用等功率左右分配。
- 不提供用户可编辑的声像曲线和布局转换矩阵。

所有转换都应在非实时线程准备好处理描述，音频回调只执行已经准备好的通道操作。

### 3.3 音频信号流

```text
JACK 输入
  |
  v
输入通道
  | \
  |  +--> 基础 Aux 发送（可选）--> Aux 总线
  |
  +--> 音量与声像
  |
  v
主混音
  |
  v
Master
  |
  v
JACK 主输出
```

MixerBasic 不提供输入到 SubMix、Aux 到 SubMix 或总线级联。Aux 是并行发送总线，不是 SubMix；首个版本中 Aux 只能输出到主混音或明确绑定的 JACK 输出。

## 4. 基础功能

### 4.1 输入通道

每个输入通道提供：

- 名称和 Mono/Stereo 标识。
- 一个 JACK 输入端口绑定。
- 输入增益，建议范围为 `-60 dB` 至 `+24 dB`，默认 `0 dB`。
- 通道推子，默认 `0 dB`。
- 单声道声像或立体声平衡控制。
- 静音和独奏。
- 主输出开关。
- 0 到 8 个 Aux 发送控制，每个发送可切换 Pre-Fader 或 Post-Fader。
- Peak、RMS 和 Peak Hold 电平显示。

输入通道不包含 EQ、DYN、插件插槽、子混音目标或复杂的输出矩阵。

### 4.2 Master

每个项目必须拥有一个不可删除的 Master：

- 默认布局为 Stereo。
- 提供 Master 推子和 Master 静音。
- 提供最终 Peak、RMS、Peak Hold 和过载指示。
- 绑定 0 到 2 个 JACK 主输出端口。
- 不提供 Aux 发送。
- 不包含 EQ、DYN 或插件处理。

空项目的默认 Master 推子为 `0 dB`，未静音，输出端口可以为空，避免首次启动时误连到错误设备。

### 4.3 基础 Aux

Aux 用于最简单的并行发送场景，例如把一部分输入发送到独立的监听或效果输出。MixerBasic 首个版本遵循以下限制：

- 最多 8 个 Aux。
- 每个 Aux 只有 Mono 或 Stereo 布局。
- 每个输入对每个 Aux 提供启用开关、发送音量和 Pre-Fader/Post-Fader 切换。
- 发送默认为 Post-Fader；Pre-Fader 发送取自输入增益和 Mute/Solo 之后、通道推子和声像之前。
- Post-Fader 发送取自通道推子和声像之后。
- Aux 可以直接送入主混音，或绑定到一个明确的 JACK 输出。
- Aux 不得发送到其他 Aux。
- Aux 不得作为输入通道的层级父级。
- Aux 提供基础电平显示，但不提供 EQ、DYN 或独立插件链。

删除 Aux 时，相关发送应在控制模型和下一份音频处理状态中同时失效，不能留下无效的用户界面控件。

### 4.4 Mute 与 Solo

首个版本使用容易解释的行为：

- Mute 关闭当前通道的主输出和 Aux 发送。
- Solo 启用后，只保留被 Solo 的输入通道和相关 Master/Aux 监看路径。
- 没有 PFL、AFL、Solo-in-place 等高级模式。
- 当没有任何通道 Solo 时，所有未静音通道正常输出。
- Master 的静音始终优先于通道状态。

具体 Solo 逻辑应在测试中固定下来，不能由 UI 控件的显示状态推断音频结果。

## 5. 电平测量

电平测量只用于帮助用户判断“有没有声音”和“声音是否过大”，不提供广播级监看功能。

### 5.1 显示内容

- Peak：当前短时间内的最高电平。
- RMS：一段时间内的平均能量。
- Peak Hold：短时间保留的峰值标记。
- Overload：超过安全范围后的过载提示。

通道条、Aux 和 Master 均使用同一套基础 MeterFrame 数据结构。UI 只读取最新数据，允许丢帧，不得阻塞音频线程。

### 5.2 电平颜色

首个版本只使用简单的三段颜色：

- 绿色：正常电平。
- 黄色：接近上限。
- 红色：可能过载。

颜色和阈值可以由主题 token 提供，但不提供复杂的用户级电平标尺编辑。

### 5.3 过载重置

主界面提供一个“重置过载”操作，清除 Master 和各通道的过载锁存。重置动作只改变显示状态，不改变增益、推子或路由。

## 6. JACK 后端

### 6.1 后端边界

MixerBasic 的唯一生产音频后端是 JACK2。它只通过 Common 提供的后端接口使用 JACK，不在应用层直接管理 JACK 客户端、端口句柄或实时回调细节，也不向用户提供其他后端选择或后端切换。

`AudioBackend` 是 Common 内部的统一接口，不代表 MixerBasic 支持多个用户可选后端。MixerBasic 的产品实现固定使用 Common 的 JACK 后端。

```cpp
struct BasicAudioSettings
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
    virtual void open(const BackendOpenConfig&) = 0;
    virtual void close() = 0;
    virtual void start(AudioProcessCallback*) = 0;
    virtual void stop() = 0;
    virtual BackendPortMap getPortMap() const = 0;
    virtual void refreshPortMapAsync() = 0;
};
```

### 6.2 端口连接

- 输入和输出端口显示名称、方向和 Mono/Stereo 通道数。
- 端口名称变化时，优先使用稳定 ID 和历史别名重新匹配。
- 端口暂时消失时保留通道和推子状态，只显示未连接。
- JACK 图变化通过轻量通知交给非实时线程处理。
- 端口枚举、名称匹配和连接重建不得发生在音频回调中。

### 6.3 NullAudioBackend

在开发和自动化测试中，NullAudioBackend 可以提供无声输入和可观察的输出缓冲，用于：

- 学习 MixerBasic 的界面。
- 测试通道新增、删除和项目保存。
- 测试 Mute、Solo、推子、声像和电平显示。
- 验证窗口关闭和后端停止流程。

NullAudioBackend 只属于开发/测试设施，不是 MixerBasic 的产品后端，也不在用户界面中提供选择。正式运行必须使用 JACK2。

需要插件处理时，MixerBasic 通过 JACK 将信号连接到 `WinJACKNexus.SingleVSTHost` 或 `WinJACKNexus.ChainVSTHost`，再接收插件处理后的返回信号。MixerBasic 不负责插件扫描、加载、参数编辑或插件状态保存。

## 7. 线程与实时安全

MixerBasic 虽然功能较少，仍必须遵守实时音频的基本规则。

### 7.1 消息/UI 线程

- 创建和销毁组件。
- 处理用户点击、拖动和文本编辑。
- 修改参数和通道状态。
- 加载、保存和恢复项目。
- 显示后端错误和端口连接状态。

### 7.2 音频线程

- 读取已经准备好的通道状态。
- 执行 Mono/Stereo 转换、增益、静音、声像、Aux 求和和 Master 输出。
- 发布 Peak、RMS 和过载原始数值。
- 不分配内存、不获取互斥锁、不写日志、不调用 UI。

### 7.3 准备线程

- 创建或销毁输入、Aux 和输出绑定的运行时缓冲。
- 验证 Mono/Stereo 布局。
- 准备通道转换和 Aux 路由描述。
- 准备音频处理状态后，在块边界提交新状态。

用户修改推子和声像时可以使用无锁参数槽或平滑值；通道数量和端口路由变化必须使用准备好的状态交换。

## 8. 项目文件

首个版本继续使用 `.mixer` 扩展名，以便与 WinJACKNexus 的基础项目工作流保持一致。文件只保存 MixerBasic 支持的状态，不保存实时数据。

根对象至少包含：

```json
{
  "format": "WinJACKNexus.MixerBasicProject",
  "formatVersion": 1,
  "application": {
    "name": "WinJACKNexus.MixerBasic",
    "clientName": "MixerBasic",
    "version": "0.1.0"
  },
  "project": {
    "name": "Untitled",
    "masterLayout": "stereo",
    "audioSettings": {},
    "inputs": [],
    "auxes": [],
    "master": {},
    "ui": {}
  }
}
```

文件应保存：

- 输入通道的稳定 ID、名称、Mono/Stereo 布局和 JACK 绑定。
- 输入增益、推子、声像、Mute、Solo 和主输出开关。
- Aux 列表、Aux 布局、发送开关、发送音量、Pre-Fader/Post-Fader 模式和输出绑定。
- Master 推子、静音、布局和输出绑定。
- 请求的采样率、块大小和跟随外部时钟策略。
- 主窗口大小、通道顺序和必要的界面状态。

文件不应保存：

- Peak、RMS、Peak Hold 历史和过载锁存。
- 原始音频缓冲、实时对象和 JACK 端口句柄。
- EQ、DYN、SubMix 或插件相关字段。
- 插件实例、插件参数、插件状态、插件扫描结果、插件扫描路径和插件链配置。
- SDK、编译器或构建工具的绝对路径。

加载流程：

```text
读取 JSON
  -> 校验格式和版本
  -> 解析输入、Aux、Master
  -> 校验只包含 Mono/Stereo
  -> 校验端口绑定，但不要求端口当前在线
  -> 在线程外准备音频状态
  -> 提交状态
  -> 恢复界面
```

加载文件遇到不支持的 EQ、DYN 或 SubMix 字段时，首个版本应给出清晰提示，而不是默默假装已经加载这些功能。

## 9. 用户界面

### 9.1 主窗口

主窗口按从左到右的固定顺序显示：

1. 输入通道区。
2. Aux 发送区或 Aux 通道区。
3. Master 输出区。
4. 后端状态和连接提示。

首个版本不使用多层浮动窗口。参数尽量直接放在通道条上，只有项目打开、保存和端口选择使用简洁对话框。

### 9.2 输入通道条

输入通道条建议包含：

- 通道名称和 Mono/Stereo 标识。
- 电平表。
- 输入端口选择。
- 输入增益数值。
- 推子和当前 dB 数值。
- 声像或平衡控制。
- Aux 发送按钮和发送量。
- Mute、Solo 和主输出状态。

不显示 EQ、DYN、SubMix 或插件入口，避免用户看到当前不能使用的功能。

### 9.3 首次使用提示

可以提供一个简短的首次使用提示，但不能阻挡熟练用户：

- 第一步：选择输入端口。
- 第二步：确认通道电平。
- 第三步：推高通道和 Master 音量。
- 第四步：选择主输出端口。

错误提示必须说明“发生了什么”和“下一步怎么做”，例如“没有找到 JACK 服务，请启动 JACK，或选择空后端演示模式”。

## 10. 推荐目录

```text
modules/WinJACKNexus.MixerBasic/
  CMakeLists.txt
  Source/
    Main.cpp
    App/
      MixerBasicApplication.h/.cpp
      MixerBasicMainWindow.h/.cpp
    Model/
      MixerBasicProject.h/.cpp
      MixerBasicViewState.h
      BasicChannelState.h
    UI/
      MixerBasicConsoleView.h/.cpp
      BasicChannelStripComponent.h/.cpp
      BasicMeterComponent.h/.cpp
      BasicPortSelectorComponent.h/.cpp
    Engine/
      MixerBasicSession.h/.cpp
      BasicRouting.h/.cpp
```

MixerBasic 只依赖 Common 提供的后端、基础音频类型和通用电平数据接口，不复制 Common 已实现的 JACK、音频引擎或电平测量代码。

## 11. 开发路线图

### 里程碑 0：能启动的空窗口

目标：建立可以配置、编译、启动和关闭的 MixerBasic 应用。

交付内容：

- `WinJACKNexus.MixerBasic` CMake target。
- `MixerBasic` 应用入口和主窗口。
- 一个默认 Stereo Master。
- 基本的错误提示和退出流程。
- JACK 未连接时仍能启动并显示明确错误的界面流程。
- 测试构建使用内部 NullAudioBackend 验证基础音频模型。

验收：干净构建后可以启动；窗口可以正常关闭；没有 EQ、DYN、SubMix 相关 target 或 UI 入口。

### 里程碑 1：单声道和立体声通道

目标：让用户能够创建输入通道并看到基本电平。

交付内容：

- Mono/Stereo 通道模型。
- 输入端口选择。
- 输入增益、推子、静音和基本声像。
- 主混音到 Master 的直接路径。
- Mono/Stereo 转换测试。

验收：Mono 和 Stereo 输入都能在 JACK 和内部测试替身下验证；布局不会被错误解释；静音和推子结果可测试；产品界面不提供后端切换。

### 里程碑 2：真实 JACK 输入输出

目标：完成最基本的真实音频闭环。

交付内容：

- 固定的 Common JACK2 后端接入。
- JACK 输入和输出端口显示。
- 端口绑定、断开和重新连接。
- 验证通过 JACK 与 `WinJACKNexus.SingleVSTHost`、`WinJACKNexus.ChainVSTHost` 进行音频往返；插件由独立应用加载。
- 后端错误状态。
- 请求采样率和块大小显示。

验收：有 JACK 服务时可完成输入到 Master 输出；无 JACK 服务时不崩溃；端口临时消失不会丢失通道设置。

### 里程碑 3：基础 Aux

目标：提供简单、可理解的并行发送。

交付内容：

- 最多 8 个 Mono/Stereo Aux。
- 每个输入的发送开关和发送量。
- 每个发送可切换 Pre-Fader 或 Post-Fader，默认 Post-Fader。
- Aux 到主混音或 JACK 输出的直接路径。
- Aux 的基础电平表。

验收：发送量变化不会改变主通道推子；Pre-Fader 发送不跟随通道推子和声像变化；Post-Fader 发送跟随通道推子和声像变化；关闭发送后 Aux 静音；Aux 不会形成互相发送或层级路由。

### 里程碑 4：项目保存和易用性

目标：让初学者可以保存工作并在下次继续。

交付内容：

- `.mixer` 保存和加载。
- 输入、Aux、Master 和端口绑定恢复。
- 主窗口布局恢复。
- 首次使用提示。
- 清晰的未连接和后端错误状态。

验收：重新打开项目后，Mono/Stereo 布局、音量、声像、Mute/Solo、Aux 和端口绑定都能恢复；不保存实时电平历史。

### 里程碑 5：稳定性与发布

目标：让 MixerBasic 适合日常基础使用。

交付内容：

- 通道增删和状态交换测试。
- Mute、Solo、Mono/Stereo 转换和 Aux 路由测试。
- Pre-Fader 和 Post-Fader 发送分别验证推子、声像和 Mute/Solo 行为。
- 实时分配与加锁审计。
- JACK 断开、重连和客户端重建测试。
- Debug、Release 和基本资源检查。

验收：稳定处理阶段无已知分配和阻塞；后端断开不会导致崩溃；所有首个版本范围内的功能都有自动化测试或明确的手工验收步骤。

## 12. 测试计划

### 12.1 音频模型测试

- Mono 输入到 Mono Master。
- Mono 输入到 Stereo Master，声音保持居中。
- Stereo 输入到 Stereo Master，左右通道顺序正确。
- Stereo 输入到 Mono Aux，求和结果稳定。
- 推子和输入增益的数值范围与平滑行为。
- Mute 后主输出和 Aux 发送均静音。
- Pre-Fader 发送不跟随通道推子和声像，Post-Fader 发送跟随通道推子和声像。
- Solo 状态下非 Solo 通道的输出行为。

### 12.2 路由测试

- 输入只能送主混音或基础 Aux，不允许 SubMix 目标。
- Aux 只能直接送主混音或 JACK 输出。
- Aux 不能发送到其他 Aux。
- 删除 Aux 后所有关联发送都失效。
- JACK 端口消失和恢复不会重置用户参数。
- MixerBasic 可以通过 JACK 将信号送入 `WinJACKNexus.SingleVSTHost` 或 `WinJACKNexus.ChainVSTHost`，并接收处理后的返回信号；MixerBasic 本身不加载插件。

### 12.3 UI 手工验收

- 空项目第一次打开时只有清晰的 Master 和添加输入入口。
- 没有 JACK 服务时错误提示可理解。
- 窄窗口下控件不重叠，通道区可以横向滚动。
- 电平表在无声、正常电平和过载时显示正确。
- Mute、Solo、推子、声像和发送操作有立即可见反馈。
- 界面中没有 EQ、DYN、SubMix 或插件入口。

## 13. 首个可用版本完成定义

WinJACKNexus.MixerBasic 达到首个可用版本时，应满足：

- 客户端名称为 `MixerBasic`，产品 target 为 `WinJACKNexus.MixerBasic`。
- 生产音频后端固定为 JACK2，不支持其他音频后端或后端切换。
- 只支持 Mono 和 Stereo。
- 输入、Aux 和 Master 的布局都不会超出 Mono/Stereo 范围。
- 测试构建可以使用内部 NullAudioBackend 验证基本操作，但用户运行模式只使用 JACK2。
- 可以通过 Common 的 JACK 后端完成真实输入、混音和输出。
- 不支持插件加载；需要插件处理时必须配合 `WinJACKNexus.SingleVSTHost` 或 `WinJACKNexus.ChainVSTHost` 独立应用，通过 JACK 完成信号往返。
- 输入通道支持增益、推子、声像、Mute、Solo 和基础电平表。
- 最多 8 个基础 Aux，支持 Pre-Fader/Post-Fader 切换，默认 Post-Fader，不支持 Aux 互送。
- Master 支持推子、静音、输出绑定和 Peak/RMS 电平表。
- 项目可以保存和恢复基本通道、Aux、Master 及端口状态。
- 音频回调没有分配、加锁、阻塞 I/O 或 UI 调用。
- 产品代码、界面、项目文件和测试中不包含 EQ、DYN 或 SubMix 功能依赖。
- 后端断开、端口消失和窗口关闭不会导致程序崩溃。

## 14. 后续扩展边界

MixerBasic 的后续扩展必须先确认不会破坏初学者工作流。以下内容不属于首个版本：

1. 将 MixerBasic 迁移为 MixerPro 的基础模式或共享基础组件。
2. 在独立模块中增加 EQ、DYN 或 SubMix，而不是把复杂控件直接塞入 Basic 首屏。
3. 复用 Common 的 MeterFrame、JACK 绑定和项目读写基础设施。
4. 在需要广播级监看时使用独立 MeterBridge，而不是扩张 MixerBasic 的主界面。

任何新增功能都必须同步更新产品范围、项目格式、用户提示和测试计划，不能只增加一个隐藏开关。