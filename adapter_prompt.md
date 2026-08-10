你是一位精通 C++20、Modern Audio Architecture（WASAPI/WinMM/libjack）与 JUCE 7/8 框架的首席音频系统架构师兼 UI/UX 设计师。

请为我撰写一份极其详尽、工业级标准的《WinJACK Nexus - Adapter 模块核心开发与落地执行计划书.md》。这份计划书需要全面总结我们之前关于 Windows WDM 与 JACK 接口解耦、实时无锁架构、线程安全及动态节点管理的讨论成果。

---

### 一、 项目命名与工程架构规范
1. **套件总名称**：**WinJACK Nexus**
2. **模块与文件夹目录规范**：
   - 本次开发的第一个核心独立模块为：`WinJACKNexus.Adapter`（独立的单实例 GUI 应用程序）。
   - 跨模块通用共享库文件夹为：`WinJACKNexus.Common`（包含无锁环形缓冲区封装、自定义 JSON 序列化工具、JUCE 与 libjack 的 Bridge 抽象类、16进制 UI 主题样式库等）。
3. **纯粹 Backend 限制**：除了 Windows 侧接入 WDM/WASAPI/WinMM 外，系统内部所有音频与 MIDI 接口【仅支持原生 JACK Backend】（依赖 `libjack` C API）。
4. **独立 Client 架构**：每个被添加的 Windows 设备（物理/虚拟、音频/MIDI）都独立注册为一个标准的 JACK Client（一设备一 Client），绝不合并为单一 Client。
5. **Client 命名规则**：
   - **默认命名**：采用严格规律的语义化默认名，例如 `WDM_AudioIn_01`、`WDM_AudioOut_01`、`WDM_MidiIn_01`、`WDM_MidiOut_01`。
   - **自定义命名**：支持用户在 GUI 上随时重命名 Client 名字，并在 JACK 图谱中同步实时更新别名/节点名。
6. **极简初始预置**：
   - 程序首次启动或新建空白配置时，**默认仅预置一个 Physical Out 物理扬声器/耳机设备**（自动绑定系统默认渲染设备）。
   - 绝不自动扫描并堆砌全量设备，剩下的所有物理/虚拟通道均由用户在 GUI 上按需手动添加。
7. **配置存档系统**：
   - 支持完整的导出与导入功能，配置文件自定义扩展名为 `.adapter`（如 `Studio_Live.adapter`）。
   - 文件内部底层结构为标准的、人类可读且易于解析的 **JSON 格式**。包含所有 Client 的映射关系、自定义名称、通道配置、设备 GUID 及采样率重采样选项。
8. **单实例保护（Single-Instance）**：使用 Windows 命名互斥量（Named Mutex `WinJACK_Nexus_Adapter_Lock`）确保全局仅能运行一个 GUI 实例。若重复启动，自动唤醒并置顶已存在的窗口。

9. **参考依赖**：
    - 所需依赖均在 `third_party` 文件夹，当前模块只需要用到 `JUCE` 和 `JACK2`
    - JACK2开发lib是 `third_party\JACK2\lib\libjack64.lib`，头文件在 `third_party\JACK2\include\jack`，实际运行不需要 `libjack64.dll`
---

### 二、 视觉语言与视觉标准（16进制颜色规范）
项目界面采用工业级暗黑数字机架（Dark Rack Unit）风格，所有 UI 元素的颜色绘制必须严格遵循以下 16 进制颜色代码（Hex Color Codes）：

- **主背景色（Dark Canvas）**：`#121316`（极深灰黑，降低视觉疲劳）
- **面板/卡片背景（Rack Panel）**：`#1A1C23`（深蓝灰机架面板）
- **边框与分割线（Borders & Dividers）**：`#2A2D3A`（暗金属质感边框）
- **主文本/高亮字（Primary Text）**：`#E6E8EE`（冷白）
- **次要文本/标注（Secondary Text）**：`#8A8F9E`（灰蓝）
- **Tab 激活状态（Active Tab Indicator）**：`#3B82F6`（科技蓝）
- **音频 LED 状态灯颜色**：
  - 未连接/熄灭（Off）：`#22252D`（暗灰）
  - 已连接/静音待机（Dim Green）：`#064E3B`（深绿）
  - 正常音频信号/动态发光（Active Green）：`#10B981`（高亮翡翠绿）
  - 信号预警（Warning）：`#F59E0B`（高亮琥珀黄）
  - 严重过载削波/挂留（Clipping Peak Hold）：`#EF4444`（高亮警示红，触发后悬停 1.5 秒淡出）
- **MIDI LED 状态灯颜色**：
  - 未连接/熄灭（Off）：`#22252D`
  - 已连接待机（Dim Blue）：`#1E3A8A`（深蓝）
  - MIDI 活动脉冲（Activity Pulse）：`#06B6D4`（高亮电光青，随 MIDI 节奏实施 80ms 指数衰减闪烁）

---

### 三、 界面结构设计（Tab 布局规范）
顶层采用 **大类型分 Tab 页** 架构，每个 Tab 页内部划分为 **In（输入/源）** 与 **Out（输出/去向）** 两个区域：

1. **Tab 1: Physical Audio（物理音频）**
   - **In 区域**：显示物理麦克风、线路输入（WASAPI Capture $\rightarrow$ 映射为 JACK Output Port）。
   - **Out 区域**：显示物理扬声器、耳机（WASAPI Render $\leftarrow$ 映射为 JACK Input Port）。
2. **Tab 2: Virtual / Playback Audio（虚拟/系统音频）**
   - **In 区域**：显示应用/游戏/系统 Playback 抓取（WASAPI Loopback $\rightarrow$ 映射为 JACK Output Port）。
   - **Out 区域**：显示通信/录音注入至虚拟缆线（Virtual Injector $\leftarrow$ 映射为 JACK Input Port）。
3. **Tab 3: System MIDI（系统 MIDI）**
   - **In 区域**：显示物理 MIDI 键盘、虚拟 LoopMIDI 接收（WinMM/WinRT MIDI $\rightarrow$ 映射为 JACK MIDI Output Port）。
   - **Out 区域**：显示发送至外部硬件音源/虚拟端口（WinMM/WinRT MIDI $\leftarrow$ 映射为 JACK MIDI Input Port）。

设备选择菜单采用不经过正则猜测的 4 层嵌套级联菜单：`驱动 (WASAPI/MME/KS) -> 类型 (Playback/Record) -> 设备 -> 声道数 (默认自动根据驱动获取)`。

---

### 四、 计划书推进阶段与子里程碑（Milestones）

请将计划书严格划分为以下两大阶段及其子里程碑：

#### 阶段一：原型设计与模拟数据（Prototype & Mock Engine）
*目标：在完全不挂接真实 WASAPI/JACK 底层的情况下，完成全套 GUI 交互、数据结构建模、状态机渲染与 JSON 序列化闭环。*

- **子里程碑 1.1：全局 GUI 骨架与 16 进制主题构建**
  - 在 `WinJACKNexus.Common` 中完成基于上述 16 进制颜色的 Component 样式库与 LookAndFeel 定义。
  - 在 `WinJACKNexus.Adapter` 中实现顶层 3 个 Tab 页（Physical / Virtual / MIDI）的切换逻辑及 In/Out 区域分割。
  - 实现主窗口的单实例锁逻辑（Named Mutex）与系统托盘（System Tray）最小化挂载。
- **子里程碑 1.2：级联菜单与卡片列表 UI 组件化**
  - 编写 4 层级联菜单选择器（`驱动 -> 类型 -> 设备 -> 声道数`，默认自动根据驱动获取通道数）。
  - 编写设备卡片组件（Device Item Card），包含：自定义 Client 名称编辑框（支持失焦/回车保存）、端口模式标签、采样率显示、删除/暂停按钮。
- **子里程碑 1.3：模拟数据源与 LED 矢量绘制渲染器**
  - 构建 Mock Audio/MIDI Engine，使用定时器模拟产生正弦波、随机峰值及 MIDI Activity 事件。
  - 编写基于 16 进制颜色规范的矢量 LED 绘制类：
    - 音频 LED 实现 Peak Hold（1.5 秒红灯挂留）与平滑淡出算法。
    - MIDI LED 实现基于 Envelope Decay（80ms 指数衰减）的随节奏脉冲闪烁算法。
- **子里程碑 1.4：JSON (.adapter) 存档逻辑实现**
  - 设计 `.adapter` JSON Data Schema。
  - 实现默认仅加载 1 个 Physical Out 设备的初始化逻辑。
  - 实现配置文件的“导出保存”、“导入恢复”与“新建配置”完整数据流测试。

#### 阶段二：真实逻辑接入与底层引擎（Real Engine Integration）
*目标：剥离 Mock 数据，将核心逻辑挂接至真正的 WASAPI、WinMM 和 libjack，确保实时安全性。*

- **子里程碑 2.1：libjack 节点封装与动态 Client 管理**
  - 在 `WinJACKNexus.Common` 中编写纯 C++ / libjack 封装类，支持按需动态创建与销毁独立的 `jack_client_t`。
  - 实现 Client 名称的动态重命名，并处理重名冲突自动自增后缀（如 `_01`）。
- **子里程碑 2.2：WASAPI 抓取/渲染与无锁 FIFO 桥接**
  - 使用 `juce::AudioDeviceManager` 实现 WASAPI Loopback（抓取系统/游戏声）、Capture（麦克风）和 Render（扬声器）的异步流提取。
  - 引入 `juce::AbstractFifo` + `juce::AudioBuffer<float>` 作为 WASAPI 线程与 JACK `process` 回调之间的**无锁单生产者单消费者（SPSC）环形缓冲区**。
  - 实现 WASAPI Loopback 的静音陷阱处理：在系统无声音播放时自动填充 `0` 数据（Silence Padding），维持固定帧率推给 JACK。
- **子里程碑 2.3：透明重采样引擎（In-process Resampling）**
  - 引入 `juce::LagrangeInterpolator` 重采样器。
  - 自动感知 WDM 设备的采样率与 `jack_get_sample_rate()` 的差异，动态计算 Ratio 并透明重采样，消除变调与爆音。
- **子里程碑 2.4：System MIDI (WinMM) 到 JACK MIDI 桥接**
  - 实现 WinMM/WinRT MIDI 事件的线程安全监听。
  - 将 Windows MIDI 消息解析转换并塞入 `jack_midi_event_t` 缓冲区，实现零延迟 MIDI 转发与 LED 脉冲联动。
- **子里程碑 2.5：全系统联调与稳定性极限测试**
  - 联调硬件 ASIO + `jackd` + WinJACKNexus.Adapter，测试长时间运行下的 Memory Leak（内存泄漏）与 Xrun（断音）。
  - 验证游戏反作弊环境下的兼容性（无未签名驱动依赖）。

---

请根据上述所有要求，输出这份格式严谨、包含详细 CMake 目录结构（含 Common 和 Adapter 划分）、C++ 数据结构定义、UI 布局伪代码、`.adapter` JSON 样例及开发时间表评估的完整计划书！