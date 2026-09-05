# WinJACKNexus - Audio Adapter - 开发计划

> 版本:v1.4
> 日期:2026-09-04
> 依据:«WinJACKNexus 开发规范» v3.1;«WinJACKNexus - 基础库 - 开发计划»;«WinJACKNexus - 接线盘 - 开发计划»
> 现状:Common / UI / Audio / Node 基础库已完成;接线盘 B1–B8 已完成(右侧参数栏、工程存档已就绪);Adapter A1–A6 已完成

---

## 0. 计划要点(用户需求摘要)

| 需求 | 结论 |
| :--- | :--- |
| 模块形态 | `nodes/adapter/mod_adapter.dll`,**共 4 个节点**:WASAPI In、WASAPI Out、System MIDI In、System MIDI Out |
| WASAPI In | 录制(捕捉)设备 → `jackclient:capture_1..N`(out) |
| WASAPI Out | `jackclient:playback_1..N`(in) → 系统回放设备 |
| 通道数 | **动态 1~16**(直接覆盖单声道→9.1.6 全景环绕);**默认取该设备 WDM/WASAPI mix 格式通道数** |
| 设置位置 | **Patchbay 右侧参数栏**(节点属性面板:客户端名 / 设备下拉 / 通道数) |
| WASAPI 自定 GUI | 节点自定义 GUI 按钮 / 右键菜单 → **对应通道数的独立电平表窗口**(纵向;−70~+12 dB;**峰值保持**;绿/黄/红三段,**超过 0dB 一律红色**) |
| System MIDI In | MIDI 输入设备 → `jackclient:midi_out` |
| System MIDI Out | `jackclient:midi_in` → MIDI 输出设备 |
| MIDI 设置 | Patchbay 右侧参数栏:客户端名 / 设备下拉 |
| MIDI 自定 GUI | 自定 GUI 按钮 / 右键菜单 → **各通道力度表(0~127)**;纵向表 |
| 设备互斥 | 模块内 **WASAPI 输入设备、WASAPI 输出设备、MIDI 输入设备、MIDI 输出设备各自唯一**——已被某节点选中的设备从其余节点下拉中移除;提供**刷新设备列表** |
| 可复用 | 模块节点可复用(4 节点类型固定,支持多实例);**可复用控件/自绘组件下沉 `WinJACKNexus::UI`**(§5) |
| **自动重采样** | WASAPI 设备采样率 ≠ JACK 后端采样率 → **自动重采样**;相等 → **直通**(见 §3.5) |

---

## 1. 现状分析(已有基础库,直接复用)

| 已有组件 | 提供 | 本模块使用 |
| :--- | :--- | :--- |
| `Audio::WasapiDeviceEnumerator` | `enumerate(direction, visitor)` / `defaultDevice()`;设备 id/name/isDefault/isInput/isOutput | WASAPI 设备下拉数据源(共享模式 Capture/Render) |
| `Audio::WasapiStream` | `open(direction, options{deviceId, requestedSampleRate, requestedChannels})` + `start(render/capture callback)` + `format()`(实际 mix 格式) | WASAPI In/Out 音频流;`requestedChannels=0` 时按设备 mix 通道数 |
| `Audio::MidiDeviceEnumerator` | `enumerate(direction, visitor)` / `firstDevice()` | MIDI 设备下拉数据源 |
| `Audio::MidiStream` | `open(direction, options{deviceId})` + `popEvent()`(SPSC 输入事件)/ `sendShortMessage()`(输出短消息) | MIDI In 事件读取 / MIDI Out 短消息发送 |
| `Audio::MidiEvent` / `MidiMessageParser` | 通道号/消息类型解析 | 力度表统计(note-on/off、CC、通道) |
| `Audio::JackClient` / `JackPort` / `JackGraph` | 打开客户端(name/exactName/autoStart)、注册端口(registerPort)、process 回调、connect/disconnect | 每个节点 = 独立 JACK 客户端 + 端口 |
| `Common::LockFreeSpscQueue` / `AudioRingBuffer` / `RealtimeObject` | 无锁管道 | 电平数据 JACK/设备线程 → UI;设备/参数快照 |
| `UI::PaintableWidget` / `AnimatedWidget` / `ThemeManager` | 自绘基类、渲染节流、`style.css` 主题 | 电平表 / 力度表自绘控件 |
| `UI::MeterWidget` / `PeakMeterWidget` / `MeterArrayWidget` / `MidiVelocityWidget` | 自绘、峰值保持、动态通道和力度表 | Adapter 电平表 / MIDI 力度表 |
| `Node::INode` / NodeApi | 单节点接口(wjn_create_node) | **需增量**:多节点(4 个子节点)与设置面板接口(§3.0) |

> 缺口(本计划新增):① Node API 多节点 + 设置面板扩展;② UI 设备下拉控件;③ **自动重采样**(设备 mix SR vs JACK SR,不等则重采样、相等直通——`Audio::Resampler` 由可选提升为必需,见 §3.5)。

---

## 2. 目标、范围与交付物

### 2.1 目标
- 提供 **4 个可复用节点**(WASAPI In / WASAPI Out / MIDI In / MIDI Out),把系统 WASAPI(共享)与系统 MIDI 设备接入 JACK 图;
- 节点在 Patchbay 右侧参数栏直接配置,自定 GUI 打开电平表/力度表窗口;
- 动态通道数 1~16;设备互斥、可刷新。

### 2.2 交付物

| 产物 | 说明 |
| :--- | :--- |
| `nodes/adapter/mod_adapter.dll` | **Adapter 模块 DLL**:4 个节点实现 + 设置面板 + 电平/力度窗口 |
| `src/adapter/` | 模块源码(产物输出至 `nodes/adapter/`) |
| UI 增量(`ui.dll`) | `PeakMeterWidget` / `MeterArrayWidget` / `MidiVelocityWidget` / `DeviceComboBox`(§5) |
| Node API 增量 | `wjn_get_node_count` / `wjn_get_node_info` / `wjn_create_node(index)` / 设置面板扩展(§3.0) |

---

## 3. 架构设计

### 3.0 Node API 增量(多节点 + 设置面板)

```cpp
// NodeApi.h 增量(API 版本协商返回 "2")
WINJACK_NODE_API std::uint32_t wjn_get_node_count(void);                 // 4
WINJACK_NODE_API const char*    wjn_get_node_name(std::uint32_t index);
WINJACK_NODE_API const char*    wjn_get_node_category(std::uint32_t index);
WINJACK_NODE_API INode*         wjn_create_node(std::uint32_t index);    // index 0..3
// 设置面板(供 Patchbay 右侧参数栏嵌入;HWND 嵌套,与 createWindow 同模型)
WINJACK_NODE_API void*          wjn_create_settings_view(INode* node, void* parentHwnd);
WINJACK_NODE_API void           wjn_destroy_settings_view(INode* node, void* viewHwnd);
```

- 4 个节点由同一 DLL 提供(共用一个实例化上下文),各自独立 JACK 客户端;
- Patchbay 参数栏经 `UI::NativeWindowUtils` 嵌入设置视图(Qt 属性页);设置变更经 SPSC/回调实时生效。

### 3.1 节点定义(JACK 客户端与端口)

| 节点 | JACK 客户端(默认名) | 端口 | 方向 | 设置项 |
| :--- | :--- | :--- | :--- | :--- |
| **WASAPI In** | `WASAPI-In` | `capture_1..N` | 输出(out) | 客户端名、设备(捕获)、通道数(1~16,默认=WDM 通道) |
| **WASAPI Out** | `WASAPI-Out` | `playback_1..N` | 输入(in) | 客户端名、设备(回放)、通道数(同上) |
| **System MIDI In** | `MIDI-In` | `midi_out` | 输出(out) | 客户端名、设备(MIDI 输入) |
| **System MIDI Out** | `MIDI-Out` | `midi_in` | 输入(in) | 客户端名、设备(MIDI 输出) |

> 端口命名 = `<客户端名>:capture_1..N / playback_1..N / midi_in / midi_out`,与接线盘端口模型自动对接。

### 3.2 数据流(音频线程零锁)

**WASAPI In(设备 → JACK)**
```
WasapiStream::start(captureCallback)
  → 交错 float 缓冲(设备 mix 格式)
  → [自动重采样:设备 SR ≠ JACK SR?重采样 : 直通](§3.5)
  → 按通道数拆分/补静音(超出设备声道→0;少于→取前 N)
  → 写入 jack_port buffers(capture_1..N,out)
同时:电平计算(Peak/RMS)→ Common::Spsc<LevelFrame> → UI 电平表
```

**WASAPI Out(JACK → 设备)**
```
jack process 回调:读 playback_1..N(in) → 交织
  → [自动重采样:JACK SR ≠ 设备 SR?重采样 : 直通](§3.5)
  → 写入设备共享环(render callback 消费)
同时:电平计算 → Spsc → UI 电平表(显示输入信号)
```

**System MIDI In**
```
midiIn 回调(MidiStream 内部线程) → popEvent()(SPSC)
  → jack process 内 jack_midi_event_write 到 midi_out 端口
同时:按 MidiEvent{channel,...} 统计各通道最近力度 → Spsc → UI 力度表
```

**System MIDI Out**
```
jack process:读 midi_in 端口事件 → MidiStream::sendShortMessage(短消息)
同时:统计各通道力度 → Spsc → UI 力度表
```

### 3.3 动态通道数(1~16)

- 设置 `channels` ∈ [1,16],映射 WDM 通道布局(单声道/立体声/5.1/7.1/9.1.6 全景);
- **默认 = 所选设备 mix 格式通道数**(`WasapiStream::format().channels`;共享模式典型 2ch);
- 变更通道数:重建 JACK 端口(1~16 个 out/in 音频端口)+ 重建采样/电平缓冲;**实时安全**:端口重建在 UI 线程 `jack_unregister/register` 序列完成,process 回调经 `RealtimeObject` 原子切换(旧端口映射在回调内延迟释放)。

### 3.5 自动重采样(设备 SR ≠ JACK SR 时)

- **判定**:比较 `Audio::WasapiStream::format()`(共享模式 mix 格式,通常固定 48000/44100)与 JACK 后端采样率(`JackClient::sampleRate()` / `jack_get_sample_rate`);
  - **相等 → 直通**:直接拷贝/指针传递,**零额外延迟、零 CPU 开销**;
  - **不等 → 自动重采样**:启用 `Audio::Resampler`(由基础库可选降级改为**必需实现**),实时安全分数步长(默认高质量线性/样条;量大后可换 `libsamplerate`,接口预留);
- **重采样位置**:
  - WASAPI In:capture 回调内(设备帧 → 重采样 → JACK 帧数),写入 JACK out 端口(**JACK 回调侧按需拉取**,双缓冲/环形缓冲衔接);
  - WASAPI Out:JACK process 回调读出并重采样后写入设备共享环,保持 JACK 时钟为基准;
- **通道数不变**(仅采样率变换);系数随设备 SR 变化自动重建(reset 时无爆音处理);
- **SR 变化事件**:JACK `setSampleRateCallback` 与设备 format 变化时自动重建重采样器并提示(状态可在参数栏显示,如“重采样 44100→48000”)。

### 3.4 设备唯一性(互斥)与刷新

- **4 个设备域**:WASAPI-捕获、WASAPI-回放、MIDI-输入、MIDI-输出;**每个域内设备唯一**;
- 模块内维护“已占用设备表”:某节点选中后,其余节点(同域)下拉中**移除该设备**;取消选择即释放;
- **刷新**:「刷新」按钮 → 重枚举(WasapiDeviceEnumerator / MidiDeviceEnumerator);热插拔:WASAPI 走 `IMMNotificationClient` 增量(现有枚举为静态 → 模块层定时重枚举兜底,≥1s),MIDI 提供手动刷新(winMM 无通知);
- 被占用设备在重枚举后保持占用标记;设备消失/恢复自动提示与重建。

---

## 4. 自定 GUI(节点按钮/右键菜单)

### 4.1 WASAPI 电平表窗口(In/Out 共用)

| 项 | 规范 |
| :--- | :--- |
| 窗口 | 每节点 1 个独立窗口(QDialog,无边框可选);标题含客户端名与设备名 |
| 表头 | **纵向表**,数量 = 当前通道数;通道标签 `1..N`(或 `L/R/C/LFE...` 按布局) |
| 刻度 | **−70 ~ +12 dB**,对数刻度(刻度线:−70/−60/−40/−24/−18/−12/−6/−3/0/+3/+6/+12) |
| 峰值保持 | 峰值衰减保持(可配置 1~3 s 释放);顶部小三角标记 |
| 分段配色 | **绿**(≤ −18 dB 附近起)→ **黄**(−18 ~ 0)→ **红**(>0);**超过 0 dB 一律红色**(整段填充红) |
| 刷新 | 60/120 fps(AnimatedWidget);窗口隐藏自动暂停渲染(规范 §5.3) |
| 数据源 | `Common::Spsc<PerChannelLevel{peakDb, rmsDb, clip}>`;JACK/设备线程写,UI 读 |

### 4.2 MIDI 力度表窗口(In/Out 共用)

| 项 | 规范 |
| :--- | :--- |
| 窗口 | 每节点 1 个独立窗口;标题含客户端名与设备名 |
| 表头 | **纵向表 × 16 通道**(通道 1~16);刻度 **0~127** |
| 显示 | 每通道最近 Note-On 力度(含 CC7?仅力度:Note-On velocity;Note-Off 后衰减保持 500ms) |
| 配色 | 与主题一致(扁平,绿色低→黄色中→红色高可配,或单色随力度) |
| 刷新/暂停 | 同上 |
| 数据源 | `Common::Spsc<ChannelVelocity[16]>`;适配 `MidiEvent` 通道解析 |

---

## 5. 可复用组件下沉 `WinJACKNexus::UI`

| 新增 UI 组件 | 说明 |
| :--- | :--- |
| `PeakMeterWidget : MeterWidget` | **纵向单通道电平表**:−70~+12 dB 对数刻度、峰值保持、三段色、>0 红色;驱动 `setLevel(peakDb, holdDb)`;自绘 + QSS 主题(style.css 变量,无贴图=自绘外观) |
| `MeterArrayWidget` | 多通道电平表组合(通道数动态 1~16,横向排布纵向表;含通道标签) |
| `MidiVelocityMeter` | 16 通道力度表(0~127 纵向条,支持 1/16 通道子集) |
| `DeviceComboBox` | 设备下拉:枚举数据源接口(`wasapi`/`midi`)、刷新按钮、**已选设备移除(互斥)**、默认设备标记;信号 `deviceChanged(index/id)` |
| 复用组件 | `ThemeManager/style.css`、`PaintableWidget/AnimatedWidget`(节流)、`NativeWindowUtils`(设置面板 HWND 嵌入) |

> 遵循主题规范:全部颜色/字体/尺寸取自 `style.css`/`ThemePalette`,不硬编码;无扩展贴图时自绘回退。

---

## 6. 配置与工程存档

- **本地设置**(QSettings/Common 配置,`%LOCALAPPDATA%\WinJACKNexus\adapter.ini`):各节点 客户端名、设备 id、通道数;重启恢复;
- **`.wjnproj` 衔接(占位)**:顶级字段 `adapters[]`(客户端名/设备 id/通道数/节点位置)——本版存本地配置,工程文件字段随 `version` 递增接入(与 `mixers[]`/`switches[]` 同模式);
- 客户端名冲突:`JackUseExactName` 失败 → 自动追加序号(与接线盘一致)。

---

## 7. 构建与部署

```cmake
# 根 CMakeLists.txt
add_subdirectory(src/adapter)
# → nodes/adapter/mod_adapter.dll(输出目录 nodes/adapter/)
```

- `mod_adapter`:`add_library(mod_adapter SHARED ...)` + `wjn_configure_shared_target(... WINJACK_ADAPTER_BUILD)`;链接 `WinJACKNexus::UI`、`WinJACKNexus::Audio`、`WinJACKNexus::Common`、`WinJACKNexus::Node`;`AUTOMOC ON`、`AUTORCC ON`;
- 平台:Windows x64 / MSVC 2022 / C++20 / Qt6 6.10.3。

---

## 8. 里程碑与任务分解

| 里程碑 | 内容 | 预计 | 验收 |
| :--- | :--- | :--- | :--- |
| **A1 骨架** | `src/adapter/` + CMake + Node API v2(多节点/设置面板)+ 空 DLL | **已完成(2026-09-04)** | `mod_adapter.dll` 产出;`wjn_get_node_count=4`;Node API 单测通过 |
| **A2 WASAPI In** | 枚举默认设备/按 mix 通道数 1~16、**自动重采样(不等重采样/相等直通)**、capture 回调→JACK out 端口、电平 Spsc | **已完成(2026-09-04)** | 编译通过;Node API 单测通过;设备声音与电平由用户在 JACK 环境手动验证 |
| **A3 WASAPI Out** | JACK in→render 回调交织、通道映射、**自动重采样(反向)** | **已完成(2026-09-04)** | 编译通过;Node API 单测通过;系统回放与采样率适配由用户在 JACK 环境手动验证 |
| **A4 MIDI In/Out** | MidiStream 事件→JACK midi;JACK midi→sendShortMessage;力度统计 | **已完成(2026-09-04)** | 编译通过;Node API 单测通过;双向 MIDI 传输与力度数据由用户在 JACK 环境手动验证 |
| **A5 设置面板** | Patchbay 右侧参数栏嵌入(客户端名/设备/通道数);设备互斥+刷新;配置即时重建 WASAPI/MIDI 流;客户端名变更后重新绑定 JACK client | **已完成(2026-09-04)** | `mod_adapter` / `mod_patchbay` Debug 编译通过;Node API 回归通过;参数栏修改、设备互斥和刷新由用户在 JACK 环境手动验证 |
| **A6 电平/力度窗口** | UI 控件(PeakMeterWidget/MeterArrayWidget/MidiVelocityWidget)+ 自定 GUI 按钮/右键菜单 | **已完成(2026-09-04)** | `mod_adapter` / `mod_patchbay` Debug 编译通过;弹出对应通道数电平表和 16 通道力度表;配色/峰值保持由用户手动验证 |
| **A7 集成验证** | jackd 下 4 节点出现在接线盘,连线/工程保存恢复;配置持久化 | 1~2 天 | 编译通过交付,用户手动验证 |

> 遵循项目既有工作流:每里程碑**编译通过即可交付**,图形/设备行为由用户手动验证。

---

## 9. 风险与对策

| 风险 | 对策 |
| :--- | :--- |
| 共享模式采样率/通道被系统固定 | 显示实际 mix 格式;**自动重采样**适配 SR(不等重采样/相等直通);请求通道>设备实际→补零,<实际→取前 N |
| 重采样 CPU 开销/质量 | 默认高质量线性/样条分数步长;JACK 回调内开销可控(测量);可选 libsamplerate 换高质后端;直通路径零开销 |
| SR 变化/重采样器重建 | `setSampleRateCallback` + format 变化自动重建;reset 时淡入淡出防爆音 |
| 通道数变更导致 JACK 端口重建 | UI 线程重建 + `RealtimeObject` 双缓冲切换;process 内延迟释放旧端口 |
| 设备热插拔/消失 | 定时重枚举(≥1s)+ `IMMNotificationClient`(增量);异常时节点置“设备缺失”并保留端口 |
| MIDI 长消息(SysEx) | 现有 MidiStream 短消息范围;力度表仅需短消息;SysEx 输出留增量(可选) |
| 电平表 60~120fps 开销 | 隐藏暂停渲染(规范 §5.3);按通道数控制刷新;SPSC 无锁 |
| 设备互斥跨实例 | 模块内全局占用表(4 域);重启后按本地配置恢复占用 |

---

## 10. 验收标准(最终)

1. 接线盘中出现 **4 个节点**(WASAPI In/Out、MIDI In/Out),均暴露标准 JACK 端口,可全局连线;
2. WASAPI In:选“录制设备” → 系统音频进入 JACK `capture_*`;WASAPI Out:JACK `playback_*` → 系统回放;
3. 通道数 1~16 可设(默认=WDM 通道数);端口数与通道数一致;**采样率自动适配:设备 SR==JACK SR 无重采样(直通),不等时自动重采样且音高正确**;
4. MIDI 输入设备 → `midi_out`;`midi_in` → MIDI 输出设备,双向互通;
5. Patchbay 右侧参数栏可配置(客户端名/设备/通道数/MIDI 设备),改动即时生效;
6. 自定 GUI/右键菜单:WASAPI → N 通道纵向电平表(−70~+12dB,峰值保持,绿/黄/红,>0 红);MIDI → 16 通道力度表(0~127);
7. 设备互斥:同域设备已被选则从其他节点下拉移除;刷新可用。

---

*本计划书随开发反馈迭代,修改后递增版本号。*
