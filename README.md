# WinJACKNexus

> 专为 **VST 机架玩家** 打造的模块化音频套件 —— 基于 **JACK Audio Connection Kit** 音频后端,对标经典 **kX 驱动** 的图形化插线与极低延迟路由体验。
> **完全开源免费 · 原生支持 Windows 64 位**

---

## 简介

WinJACKNexus 是一款面向 Windows 的模块化音频路由与处理套件。它以 **JACK Audio Connection Kit** 为核心音频后端,把 Linux 专业音频世界成熟的 **“应用即 JACK 客户端,端口即插线”** 工作流带到 Windows,为 VST 机架玩家提供：

- **kX 式图形化插线** —— 所有模块(适配器、调音台、电平桥、VST 机架、DAW 桥)一律暴露标准 Native JACK Audio/MIDI 端口,由 `Interactive Patchbay` 全局接线盘可视化连线、断开与拓扑管理;
- **极低延迟路由** —— 继承 JACK 实时调度模型,模块间走无锁零拷贝音频图;
- **沙盒级稳定性** —— “1 个核心主进程 + N 个沙盒 VST 宿主进程”,第三方 VST 崩溃不波及主套件与系统音频流;
- **渐进式模块化** —— 节点 DLL 热插拔(`nodes/` 下按类型分文件夹:adapter/mixer/meter/switch/midi/vst/carla…),菜单动态注册,无需重编译主程序即可扩充套件。

---

## 灵感来源:kX Project

[**kX Project**](https://www.kxproject.com/) 是著名的免费开源声卡驱动项目(WDM 音频驱动,面向 Creative EMU10K1/EMU10K2 芯片组声卡),其标志性体验是 **完全 DIY 的 DSP 混音器**。

> **背景:经典声卡停产与驱动断供**。Creative(创新)自 Vista 起逐步停止对 EMU10Kx 系列声卡的驱动支持,后续更是随产品线停产(如 Audigy / X-Fi 老卡)而彻底断更,大量老玩家手里性能优秀的“创新卡”沦为无驱之砖。kX 项目正是**为此而生**——以社区开发的 WDM 驱动延续这些声卡的生命,并凭借“kX DSP 混音器 + FXBus 图形化接线”成为一代声卡玩家/网络歌手/翻唱调音圈的共同记忆。如今这类体验随老声卡退场而逐渐消失,这也是 WinJACKNexus 想用**纯软件方式**把它复刻到现代 Windows 的原因之一。

- 图形化 **FXBus 路由器**(FXBus / FXBus2 / FXBus32),在界面里用“线”把各种 DSP 模块(效果器、EQ、混响等)串接起来;
- 模块化的信号处理链,直观的 **接线索托** 心智模型;
- 低延迟、可玩性极高,在声卡玩家/网络歌手/调音圈子里长盛不衰。

WinJACKNexus 借鉴的正是这一 **“模块 + 插线”** 的心智模型与体验,但把舞台从**专属硬件 DSP** 搬到**通用软件的 JACK 音频图**:

| | kX Project | WinJACKNexus |
| :--- | :--- | :--- |
| 路由载体 | 声卡硬件 DSP(FXBus) | JACK 服务器音频图(软件) |
| 硬件要求 | 专属 Creative EMU10Kx 芯片声卡 | 任意 WASAPI/系统 MIDI 设备(Windows x64) |
| 插线方式 | kX DSP 混音器图形界面 | Interactive Patchbay 全局可视化接线盘 |
| 模块化 | FXBus 插件链 | 可热插拔模块 DLL + 沙盒 VST 宿主 |
| 适用人群 | 创新声卡用户 | **VST 机架玩家 / 音频路由玩家** |

---

## 为什么是 JACK?(Linux 与 Windows 生态现状)

[**JACK Audio Connection Kit**](https://jackaudio.org/) 自 2002 年起就是 Linux 专业音频的 **事实标准**(由 Paul Davis 等发起,服务器 GPL-2.0 / 客户端库 LGPL-2.1)。它与 ALSA、ASIO、CoreAudio 等“纯硬件 I/O API”最大的不同,在于它提供**应用之间的音频/MIDI 实时互连**(端口 + 连接图),而不是只让你的程序出声。

### Linux 生态(成熟)

- **双实现**:jack1(C,维护模式)与 jack2 / jackdmp(C++,多处理器优化、无锁图访问、无爆音动态连线);
- **默认集成**:PipeWire 自 Fedora 34 起作为 JACK 的默认兼容实现成为发行版标配,生态统一;
- **低延迟基础设施**:实时内核 / `PREEMPT_RT`、`rtprio` 调度、ALSA 后端,数十年来持续打磨;
- **工具与宿主林立**:Ardour、Carla、LMMS、Zrythm、MuseScore 等原生 JACK 应用,以及 QjackCtl、Cadence/Catia(Routing,即本仓库 `Ref/Cadence` 参考实现)、Patchage 等可视化接线工具。

### Windows 生态(薄弱的“插件式”存在)

- JACK2 官方提供 **win32 / win64 安装包**(1.9.22,Win7+,64 位包兼容 32 位客户端),通过 PortAudio(dsound/wmme/ASIO)接系统驱动;
- **ASIO 是 Windows 专业音频的事实标准**,但 ASIO 只是“更干净的硬件 HAL”——**它不连接应用之间**,音频路由要靠 JackRouter 这样的桥接驱动(ASIO 应用 → ASIO JackRouter → JACK 服务器)绕一圈;
- 原生 JACK 应用数量少(Ardour、Bitwig、Carla、LMMS、MuseScore、Zrythm 等),绝大多数 DAW/插件生态走 **WASAPI/ASIO/VST**;
- 系统音频层(默认 WASAPI 共享)与低延迟专业层(ASIO)长期割裂;**没有** PipeWire 式的统一层;图形化接线工具(如 QjackCtl、Cadence)需要用户另行配置维护。

### 对比小结

| 维度 | Linux | Windows |
| :--- | :--- | :--- |
| JACK 地位 | 事实标准,发行版默认生态 | 存在,但属于“插件式”小众路径 |
| 互联能力 | 原生、应用间实时连线 | 依赖 JackRouter ASIO 桥接 |
| 硬件接入 | ALSA 直接接管 | WASAPI / ASIO(割裂) |
| 工具链 | QjackCtl / Cadence / Patchage 等丰富 | 需手动折腾,无统一套件 |
| 专业音频默认层 | JACK / PipeWire | **ASIO**(不互联应用) + WASAPI(日常) |

### 项目意图(由此而来)

> **Windows 缺的不是 JACK,而是“开箱即用、图形化、模块化、为 VST 机架玩家打造”的 JACK 套件。**

WinJACKNexus 的定位:

1. **以 JACK 为核心**:所有音频/MIDI 路由、混合、连线都在 JACK 图中完成,拒绝“插件式”二次桥接;
2. **放弃 ASIO 依赖**:系统音频设备用 **WASAPI(共享模式)+ 系统 MIDI** 直接接入 JACK(`Audio Adapter`),不需要 ASIO 驱动、不链接 JackRouter —— 对用户零配置门槛;
3. **对标 kX 的体验**:把“模块化插线”做成可视化接线盘 + 可热插拔模块 DLL,同时用沙盒进程隔离 VST 崩溃风险;
4. **面向 Windows 玩家**:一切为 VST 机架/音频路由玩家服务 —— 单效果器宿主、链式机架、MIDI 拆分/矩阵、DAW 桥接等一应俱全。

> **已有 DAW 想接入 JACK?** 如果你已经有惯用的 DAW、或任何 ASIO 应用,想在 JACK 图里看到它,可以在该 DAW/应用的音频设备设置中直接选择 **JackRouter**(ASIO 驱动,随 [JACK 官方 Windows 安装包](https://jackaudio.org/downloads/) 提供)——DAW 便会出现在 JACK 服务器中,由 Patchbay 全局接线。**注意**:这仅是可选的**外部桥接方式**;WinJACKNexus 自身不依赖、不实现 ASIO,系统音频/MIDI 设备统一经 `Audio Adapter`(WASAPI 共享模式 + 系统 MIDI)接入 JACK。

---

## 核心模块一览

| 模块 | 说明 |
| :--- | :--- |
| **Audio Adapter**(物理/虚拟音频适配器) | 将 WASAPI(共享)系统音频设备与系统 MIDI 接入 JACK,导出 `system:capture_*` / `system:playback_*` 端口 |
| **Interactive Patchbay**(模块化虚拟插线板) | 全局接线盘中枢,对系统中所有模块的 JACK 端口进行可视化连线/断开/拓扑管理 |
| **Pure Mixer**(调音台) | 通道 Gain/Mute/Solo,全部通道抽象为标准 JACK 端口 |
| **Meterbridge**(多通道电平桥监控) | Peak/RMS/dBTP/LUFS/LRA 监控、历史图表与 CSV 记录,SPSC 无锁管道直通音频线程 |
| **Single VST Host**(单效果器宿主) | 沙盒进程托管单个 VST2/VST3 插件,机架 In/Out 暴露为 JACK 端口 |
| **VST 效果器链** | 由 Interactive Patchbay 的子图组合单 VST 节点,支持拖拽编排 |
| **MIDI Splitter**(全通道 MIDI 拆分器) | Omni → 16 Ch,一路输入拆分 16 通道输出 |
| **16x16 MIDI Matrix**(矩阵 MIDI 路由) | 16×16 通道级 MIDI 路由矩阵 |
| **Signal Switch**(信号路由切换开关) | 多路切换,平滑 Crossfade 零爆音切换 |

> 全部模块(包括 VST Host 机架与 DAW 桥)严格暴露标准 Native JACK Audio/MIDI Port,可被 Patchbay 全局接线。

---

## 架构要点

- **1 个核心主进程 + N 个沙盒 VST 宿主进程**(kX Project 式沙盒与高稳定主内核);
- 主进程内功能节点(`nodes/<类型>/*.dll`,按类型分文件夹,接线盘 `mod_patchbay.dll` 为主模块与主程序同级)通过 Native JACK API 通信,**零 IPC**;
- 节点 DLL 扫描注入(`nodes/` 递归扫描)、主菜单/托盘菜单 **动态注册 + 热插拔**;
- 音频线程与 UI 线程之间**严禁互斥锁**,统一使用 `WinJACKNexus::Common` 无锁 SPSC 队列 / `RealtimeObject`;
- `WinJACKNexus::UI::ThemeManager` 全局 QSS 主题(暗黑机架风 / 明亮调音台风,支持变量替换、实时换肤);
- 窗口隐藏/最小化自动暂停渲染(后台 CPU ≈ 0)。

详细架构与命名空间规范见 [`Docs/WinJACKNexus 开发规范.md`](Docs/WinJACKNexus%20开发规范.md)。

---

## 技术栈

| 层 | 技术 |
| :--- | :--- |
| 语言/标准 | C++20(MSVC 2022) |
| 前端 | Qt 6.10.3(Widgets/Gui/Core,`msvc2022_64`) |
| 音频后端 | Native JACK C API(libjack64,运行时动态加载) |
| 系统接入 | WASAPI 共享模式(MMDevice)+ WinMM MIDI |
| DSP/宿主 | 原生 C++/Win32 VST2/VST3 宿主 |
| 插件 SDK | VST 3 SDK 3.8.x / VST 2 SDK |
| 构建 | CMake ≥ 3.25 |
| 平台 | **Windows 10/11 x64(仅 64 位)** |

---

## 系统要求

- **操作系统:Windows 10 / 11(x64)**,仅支持 **64 位**,不支持 32 位或 ARM;
- **JACK2 运行时**:`jackd.exe` / `libjack64.dll`(仓库 `third_party/JACK2` 已内置,或使用 [JACK 官方 Windows 安装包](https://jackaudio.org/downloads/));
- 音频设备:WASAPI 兼容(任意现代 Windows 声卡);MIDI 设备可选。

---

## Windows 下手动启动 JACK2

安装 JACK2 后,可以直接在 PowerShell 或命令提示符中运行 `jackd.exe`。下面使用 `C:\Program Files\JACK2\jackd.exe` 作为常见安装路径示例;如果本机安装位置不同,请替换为实际的 `jackd.exe` 路径。

> **重要**:下列命令中的 `采样率`、`每周期帧数`、`ASIO 驱动名`、实时优先级等均为示例或占位信息,不能直接照抄。必须根据实际设备支持的采样率、缓冲粒度、驱动名称,以及本机权限和系统负载进行调整。尖括号中的内容需要替换后再执行。

### 无 ASIO 声卡:使用 dummy backend

```text
"C:\Program Files\JACK2\jackd.exe" -R -P95 -S -d dummy -r <采样率> -p <每周期帧数>
```

`dummy` 只创建虚拟 JACK 音频端口和时钟,不连接任何物理声卡,适合先验证 JACK 音频图和客户端连接。它不是物理声卡的无 ASIO 驱动,也不会提供真实的音频输入或输出。

如果目标是使用真实但没有 ASIO 驱动的声卡,仍应使用 `portaudio` 并从 `-l` 输出中选择对应的非 ASIO 设备全名(例如 `DirectSound::...`),而不是使用 `dummy`。

### ASIO 声卡:使用 PortAudio backend

先列出 JACK2 通过 PortAudio 能看到的设备,并从输出的 `Name` 字段复制完整设备名:

```text
"C:\Program Files\JACK2\jackd.exe" -R -S -d portaudio -l
```

然后启动 JACK2:

```text
"C:\Program Files\JACK2\jackd.exe" -R -P95 -S -d portaudio -d "ASIO::<ASIO 驱动名>" -r <采样率> -p <每周期帧数>
```

这里的第一个 `-d portaudio` 选择 JACK2 的 PortAudio 主后端;后面的 `-d "ASIO::..."` 是传给 PortAudio 的设备参数,两者不是重复选择 JACK2 后端。`ASIO::<ASIO 驱动名>` 必须与 `-l` 输出的完整名称完全一致,不能凭猜测填写。

### 启动参数说明

| 参数 | 含义 |
| :--- | :--- |
| `-R` | 启用实时调度;能否获得预期优先级取决于本机权限、系统配置和负载 |
| `-P95` | 将实时优先级示例设置为 95;应按本机 JACK2、驱动和系统情况调整 |
| `-S` | 启用 JACK2 同步模式 |
| `-d dummy` | 使用虚拟 dummy 主后端,不连接物理音频设备 |
| `-d portaudio` | 使用 Windows PortAudio 主后端 |
| `-d "ASIO::..."` | 选择 PortAudio 设备;完整名称由主机 API 名和设备名组成 |
| `-r` | JACK 采样率,例如 `44100` 或 `48000`;必须是设备和驱动支持的值 |
| `-p` | 每周期帧数,也就是 JACK buffer size,不是毫秒;必须符合设备或 ASIO 驱动支持的缓冲粒度 |
| `-l` | 列出 PortAudio 设备并退出,用于获取准确的设备全名 |

部分 ASIO 驱动只接受特定的缓冲粒度。JACK2 PortAudio backend 支持在 ASIO 场景下使用 `-p 0` 请求驱动报告的 preferred buffer size,但最终仍应以本机驱动和 JACK2 启动日志为准。手动使用 ASIO backend 只是 JACK2 的可选启动方式,不改变 WinJACKNexus 自身不依赖 ASIO 的项目定位。

---

## 节点完成记录

> 状态图例:`📋 未开始` → `🔨 开发中` → `✅ 已完成`
> 每个节点(模块)对应一份计划书,命名约定:`Docs/WinJACKNexus - <模块名> - 开发计划.md`

| 节点 | 形态 | 状态 | 计划书 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| 基础库 `WinJACKNexus::Common` | `common.dll` | ✅ 已完成 | [基础库计划](Docs/WinJACKNexus%20-%20基础库%20-%20开发计划.md) | 无锁队列/环形缓冲/实时对象/日志 |
| 基础库 `WinJACKNexus::UI` | `ui.dll` | ✅ 已完成 | [基础库计划](Docs/WinJACKNexus%20-%20基础库%20-%20开发计划.md) | ThemeManager/窗口嵌入/自绘控件 |
| 基础库 `WinJACKNexus::Audio` | `audio.dll` | ✅ 已完成 | [基础库计划](Docs/WinJACKNexus%20-%20基础库%20-%20开发计划.md) | JACK 封装/WASAPI/系统 MIDI |
| 基础库 `WinJACKNexus::Node` | `node.dll` | ✅ 已完成(接口桩) | [基础库计划](Docs/WinJACKNexus%20-%20基础库%20-%20开发计划.md) | INode + C-API + 示例节点 |
| Audio Adapter | `nodes/adapter/mod_adapter.dll` | ✅ 已完成(A1–A6) | [Audio Adapter 计划](Docs/WinJACKNexus%20-%20Audio%20Adapter%20-%20开发计划.md) | WASAPI In/Out、MIDI In/Out、设置面板、设备互斥与刷新、电平/力度窗口 |
| Interactive Patchbay | `mod_patchbay.dll`(主程序同级) | ✅ 已完成(B1–B8) | [接线盘计划](Docs/WinJACKNexus%20-%20接线盘%20-%20开发计划.md) | 实时 JACK 图、拖线连通/断开、工程保存恢复、自动保存、无限画布 |
| Pure Mixer | `nodes/mixer/mod_mixer.dll` | 📋 未开始 | — | P3 |
| Meterbridge | `mod_meterbrigde.dll` + `nodes/meter/mod_meterbridge_probe.dll` | ✅ 已完成 | [电平表桥计划](Docs/电平表桥开发计划书.md) | 唯一桥窗口、Mono/Stereo/5.1/7.1/Custom Probe、BS.1770-5 计量、历史图表、CSV、工程恢复 |
| Signal Switch | `nodes/switch/mod_switch.dll` | 📋 未开始 | — | P4 |
| MIDI Splitter | 节点(`mod_patchbay` 内/独立) | 📋 未开始 | — | 与 Patchbay 同期 |
| 16x16 MIDI Matrix | 节点(同上) | 📋 未开始 | — | 与 Patchbay 同期 |
| Single VST Host | `nodes/vst/mod_vst_host.dll` + `VSTProcess32/64.exe` | ✅ 已完成 | [VST Support 计划](Docs/WinJACKNexus%20-%20VST%20Support%20-%20开发计划.md) | VST2/VST3 沙盒、分类栏、存档与旁通/重启 |
| VST 效果器链 | Patchbay 子图 | ✅ 已完成 | [VST Support 计划](Docs/WinJACKNexus%20-%20VST%20Support%20-%20开发计划.md) | 子图内复用单 VST 节点 |
| DAW2JackMIDI VST3 | `*.vst3` | 📋 未开始 | — | P5 |
| DAW2JackAudio VST3 | `*.vst3`(Mono/Stereo/5.1/7.1) | 📋 未开始 | — | P5 |
| 主程序外壳 | `WinJACKNexus.exe` | ✅ 已完成 | — | 节点模块加载与主窗口 |

### 开发路线

- **P0 基础库(当前)**:`Common` / `UI` / `Audio` 完整实现 + `Node` 接口桩 → 编译产物 `common.dll` / `ui.dll` / `audio.dll` / `node.dll`;
- **P2 主程序 + Adapter + Patchbay**:主进程外壳、系统音频/ MIDI 接入、全局接线盘;
- **P3 Mixer**:简单/专业调音台;
- **P4 Meterbridge**:已完成；**Signal Switch + MIDI 工具**(Splitter / Matrix)待开发;
- **P5 沙盒 VST Host + DAW 桥**(VST3,含 5.1/7.1 多声道)。

---

## 文档

- [`Docs/WinJACKNexus 开发规范.md`](Docs/WinJACKNexus%20开发规范.md) —— 架构规范 v3.1(强制遵循)
- [`Docs/WinJACKNexus - 基础库 - 开发计划.md`](Docs/WinJACKNexus%20-%20基础库%20-%20开发计划.md) —— 当前阶段计划书
- [`Docs/WinJACKNexus - 接线盘 - 开发计划.md`](Docs/WinJACKNexus%20-%20接线盘%20-%20开发计划.md) —— Interactive Patchbay B1–B8 完成记录
- [`Docs/电平表桥开发计划书.md`](Docs/电平表桥开发计划书.md) —— Meterbridge 完成记录

---

## 项目许可

WinJACKNexus 的第一方代码依据 GNU Affero General Public License v3.0 发布,详见 [LICENSE](LICENSE)。第三方依赖仍按各自的许可条款使用。

---

## 第三方依赖与许可

| 依赖 | 版本 | 许可 |
| :--- | :--- | :--- |
| JACK2 (jackd / libjack) | 1.9.22 | 服务器 GPL-2.0,客户端库 LGPL-2.1 |
| Qt 6 | 6.10.3 | LGPLv3 / 商业(本仓库动态链接使用开源版 Qt) |
| VST 3 SDK | 3.8.x | GPLv3+例外 / 商业 |
| VST 2 SDK | — | 专有许可(仅保留,未使用前不作为依赖) |

本项目使用上述第三方库对应的开源许可条款;涉及商用需自行评估商业许可。

---

*WinJACKNexus —— 把 kX 的插线乐趣与 JACK 的专业互联,带到每一台 Windows 音频工作站。*
