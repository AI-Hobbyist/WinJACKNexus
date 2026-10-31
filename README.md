# WinJACKNexus

[简体中文](README.md) · [日本語](README.ja.md) · [English](README.en.md) · [한국어](README.ko.md)

面向 Windows x64 的模块化音频与 MIDI 路由工具。通过图形化接线盘连接 JACK 端口，将设备、调音台、原生效果、VST 宿主和子图组合成自己的处理链。

## 当前提供

| 功能 | 内容 |
| --- | --- |
| 接线盘 | 可视化音频/MIDI 连线、工程保存与恢复、子图 |
| 设备节点 | WASAPI 输入/输出、系统 MIDI 输入/输出 |
| 调音台 | 数字调音台，以及 8/16/24 通道 Classic Mixer |
| Native Fx | Gate、31 段图示 EQ、De-Esser、Compressor、Limiter、Delay、参数 EQ、Reverb |
| 电平桥 | 多通道电平显示、历史曲线与 CSV 记录 |
| VST 宿主 | VST3；本地配置也支持 VST2，需自行准备相应 SDK |
| MIDI Tools | 通道拆分、16×16 矩阵、SF2/SFZ 音源加载 |
| Control Deck（控制面板） | 信号开关、音效按钮、单声道峰值表、多声道增益旋钮 |
| 外观和语言 | 主题 Token、自定义主题；简体中文、日语、英语、韩语 |

## Control Deck

以下节点都支持主图和子图，每个版本为独立 DLL；节点自身不带 GUI，在快捷栏打开“控制面板”统一操作。

| 控件 | 版本与行为 |
| --- | --- |
| 信号开关 | 单声道、立体声、5.1、7.1、MIDI；新建默认启用，控制信号通断 |
| 音效按钮 | 仅立体声输出；点击或全局快捷键触发一次，再次触发从头播放 |
| 峰值电平表 | 仅单声道输入，无输出端口，刻度 −72～+12 dBFS；右键切换水平/垂直 |
| 增益旋钮 | 单声道、立体声、5.1、7.1；一个旋钮同时控制该节点全部声道，单位 dB，范围可调整 |

- 在编辑器中拖动排列。Ctrl 多选或框选后，通过右键菜单设置尺寸、颜色、分组、自由/网格对齐；分组卡片可改名并整体拖动。信号开关可建立互斥关系，也可独立操作。
- 控件名称与图中节点名称同步；默认继承主题颜色，也可单独指定颜色。
- 锁定布局后，编辑器背景隐藏，保留已经排列好的控件及位置，置顶显示在普通应用窗口前。快捷栏下拉菜单提供锁定、解锁、鼠标穿透及打开编辑器入口。
- “控件透明度”只影响控件。启用鼠标穿透时，使用 Alt+左键操作；旋钮可通过该操作打开数值输入。
- 音效按钮右键加载文件、设置全局快捷键。其他应用前台时也能触发；快捷键冲突时保留原设置。支持 BASS/BASSmix 及已安装兼容解码扩展支持的格式；已验证 WAV、AIFF、MP3、OGG、MOD。额外 x64 BASS 解码 DLL 放在 `control_deck/bass_plugins/`，然后重启应用。
- 工程文件以 Base64 保存 Control Deck 的布局、窗口大小/位置、分组、关系、颜色、尺寸、控件透明度、穿透状态以及节点设置。音效保存文件路径和快捷键，音频文件本身仍在外部；重新打开工程不会自动播放。

所有控件节点统一归类为 `Control Deck`，节点目录为 `nodes/control_deck/`。主模块 `control_deck.dll` 放在程序同级目录，主面板语言资源已并入程序的 `locate/`。旧工程的 `nodes/signal_switch/` 路径会自动映射至已安装的新目录，节点 DLL 名和保存身份保持不变。峰值表从待测信号分支接入，不串联在信号链中。

## 调音台窗口

数字调音台的「通道总览」和「通道详情」是独立的顶层窗口，各自带有任务栏缩略图：可以单独切到前台，最小化主调音台时也不会跟着最小化。

在「通道详情」中切换通道（包括「监听」）时，「通道总览」的高亮和主调音台的滚动位置会同步跟随；在总览中选择通道会打开该通道的详情，并同样让主调音台同步。

## 下载与运行

需要 Windows x64、JACK2 和 Microsoft Visual C++ 运行库。解压后保留目录结构，运行 `WinJACKNexus.exe`。连接实际设备前先检查 JACK 配置与接线。

整合包发布在 [GitHub Releases](https://github.com/AI-Hobbyist/WinJACKNexus/releases)。下载 `WJN_Release.7z`，解压后按顺序运行 `1_安装JACK.exe`、`2_安装VAC.exe`，再解压 `3_解压WinJACKNexus.7z` 并运行 `WinJACKNexus.exe`。

启动时会先确认 JACK 服务器是否可用。没有可用服务器时，若启用了「JACK 服务器设置」中的自动启动，会先用该设置尝试启动；仍未就绪就显示模态的「JACK 服务器设置」窗口，服务器就绪后该窗口自动关闭并继续启动。直接关闭这个窗口会退出应用。

「全局设置 → Windows 进程优先级」提供低、低于正常、正常、高于正常、高和实时六档。修改会立即应用到本程序，并用于此后启动的子进程（jackd、VST 宿主进程和 VST 扫描器）；已经在运行的子进程保持原有优先级。实时优先级可能影响整个系统的响应，并且需要额外的 Windows 权限；Windows 拒绝该优先级时会恢复原值并提示失败。

GitHub 的 **Actions → Build full Release package → Run workflow** 可手动全量编译 Release。成功后在该次运行的 Artifacts 下载 `WinJACKNexus-Release-x64-VST3`，内含完整应用 ZIP、SHA256 校验文件、Qt 运行时、全部当前模块，以及 `prerequisites/` 下的 JACK2 和 VC++ x64/x86 安装程序。先安装这些运行依赖。该 Action 明确关闭 VST2，保留 VST3；首次云端构建结果以 Actions 为准。

增量覆盖包需要已有的匹配版本。请在方便时自行关闭应用，备份后覆盖并重启；不要在音频运行中替换 DLL。完整包和增量包不能混为一谈。

## 完成状态与预告

Control Deck 的开关、音效、峰值表、四种增益旋钮、布局操作和工程保存恢复已实现，并通过 Release 构建、离线处理及 Windows 实窗自动检查。实测包括其他进程前台时的音效快捷键和四语显示；真实设备/JACK 连线、安装及人工验收仍未验证。

启动前的 JACK 服务器检查、进程优先级设置，以及「通道总览」「通道详情」的独立任务栏与联动行为均已实现。

**预告：** DAW Bridge、Carla 集成及尚未交付的主会话音频设备管理流程仍在规划或开发中，不属于当前已完成功能。未完成事项不会因文档列出而视为可用。

## 主题与许可

主题制作见 [自定义主题](Docs/Theme-Customization.md)。开发接口见 [开发指南](Develop/Development-Guide.md)。

WinJACKNexus 自有源码采用 [AGPL-3.0](LICENSE)。Qt、JACK、VST SDK、FluidSynth、sfizz、BASS/BASSmix 等依赖保留各自许可；具体文本随发行包提供。BASS 的使用条件见随包 `licenses/bass.txt` 与 `licenses/bassmix.txt`。
