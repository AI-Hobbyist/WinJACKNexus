# WinJACKNexus - VST Support - 开发计划

> 版本:v1.8
> 日期:2026-09-08
> 依据:«WinJACKNexus 开发规范» v3.1;«VST_Prompt.md»(Docs/VST_Prompt.md);«WinJACKNexus - 接线盘 - 开发计划»
> 内部分类名:**VST Support**
> v1.8:VST 支持 V1–V8 已完成;VST2 默认启用,真实插件/JACK 全链路由用户手动验证
> v1.7:VST 链节点 `mod_chain_host` **已被子图(Sub Graph)取代**——直接重定向到接线盘计划 §12(见 §3.4);VST 插件节点在子图内复用,不建链 DLL
> 现状:基础库(Common/UI/Audio/Node)与接线盘 B1–B8 已完成;V1 扫描器、V2 VSTProcess 骨架、V3 VST2 宿主、V4 单 VST 节点集成、V5 崩溃旁通/重启、V7 分类栏/存档与 V8 集成验证均已完成(真实插件/JACK 连线待手动验证)
> 本版决策:VSTProcess 为兼顾 32 位插件改用原生 C++ + VST2/VST3 SDK,不使用 Qt 后端;VST 节点 DLL 与设置界面继续使用 Qt

---

## 0. 计划要点(依据 VST_Prompt)

| 需求 | 结论 |
| :--- | :--- |
| 插件格式 | **VST2 + VST3 均支持**;VST2 编译开关 **`WJN_ENABLE_VST2` 默认 ON**——启用前需确认自身许可(§8) |
| 位宽识别 | **扫描时识别插件 32/64 位**(PE Machine:`0x14C`=x86,`0x8664`=x64),写入 `.vstinfo/.vst3info` 的 **`bits`** 字段;节点据此分发 **VSTProcess32/VSTProcess64**(§3.1/§3.2) |
| 单 VST 节点 | `nodes/vst/mod_vst_host.dll`(单插件) |
| VST 链节点 | ~~`nodes/vst/mod_chain_host.dll`~~ **已被子图取代**(§3.4 重定向)→ 链式 VST 编排由《接线盘》§12 子图(Sub Graph)承担;原 chain 端口/白名单/改名/主图隐藏策略由子图的 sub 接口承接 |
| 专用节点分类栏 | Patchbay 侧边根据 `nodes/VSTPlugins/` **文件夹分类**显示;识别 **`.vstinfo` / `.vst3info`**(JSON,一个插件一个文件;参考结构 VST2|VST3 → Instruments|Effects → 子类文件夹) |
| 插件扫描器 | 独立 **`Vst Scanner.exe`**(Qt6):多个 VST 路径;扫描后写入 `nodes/VSTPlugins` 对应目录;**Waves Shell** → info 附加 `internal_plugin` 字段 |
| 插件处理器 | 独立 **`VSTProcess32.exe` / `VSTProcess64.exe`**(原生 C++ + VST2/VST3 SDK + IPC,不依赖 Qt):`--path` 加载,额外 `--internal_plugin`(Waves Shell) |
| 节点界面 | `mod_vst_host.dll` / `mod_chain_host.dll` 的节点逻辑、设置面板和编辑器容器继续使用 Qt;通过 IPC 驱动原生 VSTProcess |
| 设置 | `mod_vst_host` 侧边栏:插件路径、预设同步、**端口显示方式**(默认/自定义总线·标签名/标准声道名/jack 默认)、**打开自带 GUI**(GUI 上有预设栏,保存/加载 `.fxp/.fxb` 预设);菜单与侧边栏添加 **重载 VST**(非 VST 节点隐藏) |
| 存档 | 插件路径、插件参数、当前预设、客户端名(入 `.wjnproj`) |
| 崩溃隔离 | 独立沙盒进程运行 VST;插件崩溃 → **对应节点旁通**,不崩整个 WinJACKNexus |
| 端口 fallback | 自定义总线/标签名 → **jack 默认**;端口数量取决于插件能力(音频 + MIDI) |
| 客户端名 | 普通插件=无扩展名文件名;Shell=内部插件名;过长:完整名→中间省略→缩写→`vst_xxxxx` 随机;重名:`名字_自增序号` |
| SDK | 均在 `third_party/`:`vst3sdk`(3.8.x)、`vst2sdk`(含 VST2_License_Agreement.pdf) |

---

## 1. 现状与 SDK 分析

### 1.1 第三方 SDK(third_party)

| SDK | 内容 | 本计划用途 |
| :--- | :--- | :--- |
| `third_party/vst3sdk`(3.8.x) | `pluginterfaces/`、`public.sdk/`、`cmake/`(SMTG CMake、`smtg_add_vst3plugin` 等) | ① Vst Scanner 用其 module info/parser 与 hosting API 探测 VST3;② VSTProcess 使用原生 VST3 Hosting API |
| `third_party/vst2sdk` | `pluginterfaces/vst2.x/aeffect.h` 等 + `VST2_License_Agreement.pdf` | VST2 宿主自实现(AEffect C ABI);**编译开关 `WJN_ENABLE_VST2` 默认 ON**;许可:`VST2_License_Agreement.pdf` 仅允许授权开发者使用、不可再分发头文件 —— 启用和分发前需自行确认授权(**风险 §8**) |
| `Ref/Carla` | `backend/plugin/CarlaPluginVST2/VST3.cpp`(参考) | 仅作行为参考(Fallback 命名、Bypass 语义),不直接移植 |

### 1.2 已有基础库复用

| 组件 | 复用 |
| :--- | :--- |
| `WinJACKNexus::Audio` `JackClient/JackPort/JackGraph` | 沙盒进程(VSTProcess)自身作为 **JACK 客户端**注册端口(独立进程直连 JACK 图,崩溃→端口消失→主节点旁通) |
| `WinJACKNexus::Node`(NodeApi) | `mod_vst_host.dll`/`mod_chain_host.dll` 实现 `INode`;**需增量**:设置面板(`wjn_create_settings_view`)与多节点 |
| `WinJACKNexus::UI` | `ThemeManager/style.css`、`PaintableWidget/AnimatedWidget`、`NativeWindowUtils`(**跨进程 HWND 编辑器嵌入**)、`DeviceComboBox`(VST 路径/预设选择)、参数栏组件 |
| `WinJACKNexus::Common` | Logger、路径、`RealtimeObject`(状态)、SPSC(电平/状态);**WM_COPYDATA IPC 基础预留**(改用命名管道/TCP,见 §3.2) |
| Patchbay | 节点分类栏扩展(读 `nodes/VSTPlugins` info 树)、右键菜单「重载 VST」、节点旁通/重启 UI、参数栏嵌入 |

> 缺口(本计划新增):① **VST2 最小宿主**(自实现 AEffect,`WJN_ENABLE_VST2=ON` 时编译);② VSTProcess 沙盒进程(原生 C++ + VST2/VST3 SDK + IPC + JACK 客户端);③ Vst Scanner(**含 32/64 位识别**);④ info 文件规范(`bits` 字段)与分类栏;⑤ 端口显示方式与客户端名规则;⑥ 旁通管线(进程级)。其中 V1 扫描器已完成,其余仍按里程碑推进。

---

## 2. 目标、范围与交付物

### 2.1 目标
在 WinJACKNexus 中提供 VST2/VST3 支持:扫描/分类展示、单插件节点与链节点、沙盒进程加载(崩溃旁通)、设置/预设/存档、自定义端口与客户端名规则。

### 2.2 交付物

| 产物 | 部署/形态 | 说明 |
| :--- | :--- | :--- |
| `Vst Scanner.exe` | 独立工具(主程序目录) | Qt6;多路径扫描;写 `nodes/VSTPlugins/**/*.vstinfo|*.vst3info` |
| `VSTProcess64.exe` | 独立沙盒进程(主程序目录) | 64 位插件处理:原生 C++ + VST2/VST3 SDK + IPC + JACK 客户端,不依赖 Qt |
| `VSTProcess32.exe` | 独立沙盒进程(主程序目录) | 32 位插件处理:原生 C++ + VST2/VST3 SDK,加载 `C:\Windows\libjack.dll` 32 位进 JACK,不依赖 Qt |
| `nodes/vst/mod_vst_host.dll` | 节点 DLL | 单 VST 节点(INode):Qt 设置面板、位宽分发与 VSTProcess 重载;编辑 GUI/预设 IPC 后续扩展 |
| ~~`nodes/vst/mod_chain_host.dll`~~ | — | **已被子图取代(不实施)**:链式 VST 组合由接线盘子图承担(接线盘 §12);子图内 VST 插件仍走 `mod_vst_host.dll` 单节点 + 原生 VSTProcess 沙盒 |
| `nodes/VSTPlugins/` | 数据目录 | info 参考结构:`VST2|VST3/Instruments|Effects/<子类>/*.vstinfo|*.vst3info` |
| 基础库增量 | `ui.dll`/`node.dll` | 编辑 GUI 容器、`VstPathWidget`、Node 设置面板 API v2 |

---

## 3. 架构设计

### 3.1 插件扫描器 `Vst Scanner.exe`

- **Qt6 应用**(可无窗口/命令行 + 手动触发),设置页维护**多个 VST 路径**;
- **扫描**:`*.dll`(VST2,尝试 `AEffect` magic `VstP`)、`*.vst3`(VST3,经 VST3 SDK module info/parser 与原生 hosting probe)逐个加载头部校验并读取描述(名称/厂商/类别/io 音频/MIDI、bus 与标签名);
- **位宽识别**:读取 PE 头 `Machine` 字段(`0x14C`=x86 → `bits: 32`,`0x8664`=x64 → `bits: 64`;VST3 bundle 对其内部实际二进制检测)写入 info;32 位(老 VST2 常见)→ `VSTProcess32`,64 位 → `VSTProcess64`;所有插件(含 VST3)均检测位宽;

- **分类写入 `nodes/VSTPlugins`**:
  ```
  nodes/VSTPlugins/
  ├─ VST2/
  │  ├─ Instruments/            # *.vstinfo
  │  └─ Effects/
  │     ├─ EQ/ Dynamic/ ···      # 子类文件夹(按插件 subcategory)
  │     └─ *.vstinfo
  └─ VST3/                       # *.vst3info(同上结构)
  ```
- **info 文件(JSON,一个插件一个)** 示例:
  ```json
  {
    "name": "Example EQ",
    "vendor": "Acme",
    "version": "1.2.3",
    "type": "vst3",
    "bits": 64,
    "path": "C:\\VST\\ExampleEQ.vst3\\Contents\\x86_64-win\\ExampleEQ.vst3",
    "category": "effect",
    "subcategory": "EQ",
    "isInstrument": false,
    "audioIns": 2, "audioOuts": 2, "midiIn": false, "midiOut": false,
    "buses": [ { "name": "Stereo", "channels": 2, "type": "audio", "isInput": true } ],
    "internal_plugin": "WavesShellVST 1"     // 仅 Waves Shell 等:额外字段
  }
  ```
- **Waves Shell**:识别 Shell DLL → 枚举内部插件,每个内部插件生成一份 info(带 `internal_plugin` 字段);
- 增量/重扫:文件时间戳缓存,手动或启动时刷新;扫描日志写 `%LOCALAPPDATA%`。

### 3.2 插件处理器 `VSTProcess32.exe / VSTProcess64.exe`(原生 C++ 沙盒)

- **启动参数**:`--path <插件路径> --pipe <IPC管道> [--internal_plugin <内部名>] [--client <客户端名>]`;**`--pipe` 必填**，无控制通道时不创建 JACK 客户端，避免短生命周期客户端扰动实时图;**位宽分发**:64 位插件 → `VSTProcess64.exe`,32 位插件(老 VST2 常见)→ `VSTProcess32.exe`(扫描 info 的 `bits`/PE 探测决定);
- **进程结构**(原生 C++/Win32 + STL,不链接 Qt):
  - 使用 VST3 SDK Hosting API 加载 VST3;**VST2 用自实现加载器**(§3.5);两种格式都由原生宿主直接管理实例、参数、状态和处理;
  - 插件失败/崩溃 → 进程退出(exit code 非 0)→ **主进程检测 → 节点旁通**;
  - 自身作为 **JACK 客户端**(原生 Win32 动态加载:64 位优先 `libjack64.dll`,32 位 `libjack.dll`):按 VST3 audio/event input bus 与 VST2 MIDI 能力注册端口(端口名按 §3.6)。VST2 `effProcessEvents`、VST3 `inputEvents` 与 MIDI-CC 参数映射已在 JACK process 回调中接入; MIDI 输出仍留待后续里程碑;
  - 编辑器:加载成功后按需创建插件编辑器(VST2 `effEditOpen` 或 VST3 `IPlugView` → HWND),HWND 经 IPC 交 Qt 节点主进程嵌入(§3.4);
  - **IPC(主进程 ↔ VSTProcess)**:Windows **命名管道**(`\\.\pipe\WinJACKNexus.VST.<pid>`)或本地 TCP(备选,回退自动);
    - V2 已实现:启动加载、`ping`、`status`、`quit` 命名管道命令;后续扩展:unload、setParameter/getParameter、saveState/loadState(`.fxp/.fxb` 与插件私有流)、openEditor/closeEditor(返回 HWND)、preset load/save、reload、keepAlive(50ms)与崩溃检测;
    - 协议:由原生 Win32 命名管道/TCP + C++ 实现长度前缀二进制(msgpack-lite 风格,或 JSON 头 + 二进制块);Qt 仅保留在节点侧,不进入 VSTProcess。

### 3.3 单 VST 节点 `nodes/vst/mod_vst_host.dll`

- 实现 `INode`(设置面板 API v2);**一个节点 = 一个 VST 插件实例**(经由 VSTProcess 沙盒渲染);**创建节点时按 info `bits`(32/64)→ 选择 `VSTProcess32/VSTProcess64`**;手动指定路径的插件先做 PE 位宽探测再分发;
- **节点侧保持 Qt**:节点 DLL、设置面板、参数栏和原生编辑器 HWND 容器继续使用 Qt;VST 音频/事件处理与插件生命周期放在无 Qt 的 VSTProcess 中,两侧通过 IPC 通信;
- 侧边栏设置:
  - **V4 已完成**:**插件路径**(浏览/输入)、可选客户端名、**Auto/32/64 位**进程选择和「**Apply and reload**」;Auto 对 DLL 读取 PE Machine,`.vst3` 默认分发至 64 位进程;
  - **V4 已完成**:以唯一 `WinJACKNexus.VST.<pid>.<counter>` 管道启动 `VSTProcess32/64`;重载先通过该管道发送 `quit`,仅在超时后终止子进程;
  - 后续:预设同步、端口显示方式、自带 GUI/`NativeWindowUtils` 编辑器嵌入与 `.fxp/.fxb` 预设栏,依赖 VSTProcess 的参数、状态和编辑器 IPC。
- 崩溃/旁通:VSTProcess 退出 → 节点状态 **Bypass**(输入直通输出,端口保持注册,由主进程接管),节点右键「重启插件」拉起新 VSTProcess 恢复。

### 3.4 VST 链节点 —— 已被子图取代(重定向)

> **结论:`mod_chain_host.dll` 不再实施。** 链式 VST 编排由「子图(Sub Graph)」功能承担——详见《WinJACKNexus - 接线盘 - 开发计划》**§12 扩展阶段:子图(Sub Graph)**。子图已覆盖并扩展原链节点全部需求:

| 原链节点需求 | 子图方案(接线盘 §12) |
| :--- | :--- |
| VST 特化子 Patchbay(独立窗口/标签页) | 子图 = 子 Patchbay,双击 Sub Graph 节点在新标签页打开 |
| chain in/out/midi(自由增删、可改名) | Sub Graph Audio/MIDI In/Out 接口——自由增删、可改名 |
| 主 Patchbay 隐藏 chain 端口 | 主图默认隐藏 Sub Graph `sub*` 接口 |
| 子 Patchbay 白名单(chain/VST 插件) | 子图节点列表仅 Sub Graph 五类节点(adapter/jackclient 分类隐藏;VST 插件经分类栏加入) |
| 插件经 `mod_vst_host.dll` 加载 | 子图内 VST 插件仍由 `mod_vst_host.dll`(单 VST 节点)+ 原生 VSTProcess 沙盒加载 |
| 链内崩溃→槽旁通 | 子图内单个 VST 节点崩溃→该节点旁通(沿用 V5 机制) |

> **重定向**:后续实现与验收均以接线盘计划 §12(S1–S5)为准;本计划不再维护链节点交付物。

### 3.5 VST2 最小宿主(原生 C++ 自实现)

- 头:`third_party/vst2sdk/pluginterfaces/vst2.x/aeffect.h`;
- 加载:`LoadLibrary` + `GetProcAddress(entry)` → `AEffect*`;`audioMasterCallback` 实现(kVstParameterChanged、kWantMidi、kVstUserEditor 等最小集);
- 处理:`processReplacing`(float 交错`;不支持则 `process`);
- 预设:`getChunk/setChunk`(fxp/fxb)与参数枚举(dump);
- 编辑器(可选增量):`dispatcher(effEditOpen, hwnd)`——VST2 编辑器优先**后续增量**,首个版本音频/参数/预设为主;
- 端口/总线:由 `numInputs/numOutputs/numPrograms/numParams` 描述(→ info 与端口规则)。

### 3.6 端口命名(显示方式 + Fallback)

| 显示方式 | 规则 |
| :--- | :--- |
| **默认** | 插件总线名(`bus_1_in`…;VST3 `AudioBus` 名;VST2 `in_1/out_1`) |
| **自定义总线·标签名** | **插件内部获取的总线/标签名**(VST3 `AudioBus::getName()`/总线标签、VST2 总线/标签名;**非用户可编辑**,仅展示插件内部命名)——插件未提供该名称时 → **fallback jack 默认** |
| **标准声道名** | 按通道布局 `L/R/C/LFE/SL/SR/…`(5.1/7.1/9.1.6 全景逐通道命名,超出用 `ch_<n>`) |
| **jack 默认** | `in_1..N` / `out_1..N` / `midi_in` / `midi_out` |

- **Fallback 链:自定义总线·标签名(插件内部获取)→ jack 默认**(即插件未提供自定义总线/标签名时,统一回退 jack 默认规则);
- 端口数量由插件能力决定(音频 in/out + MIDI in/out),端口名经 IPC 与 JACK `jack_port_set_alias` 同步(set 显示别名,真实名保持 JACK 规范,保证 ABI/图稳定,别名供接线盘显示)。

### 3.7 客户端名规则(Sandbox 客户端)

```
1. 普通插件:插件文件名(去扩展名);Waves Shell / internal_plugin:内部插件名
2. 过长(>127, jack_client_name_size):完整名 → 中间省略("Axxx…xxxZ",保首尾) → 缩写(各词首字母+数字) → vst_<随机6位>
3. 重名:上述名字_自增序号(1,2,3…)
```
- 由 `mod_vst_host`/`mod_chain_host` 生成并传给 VSTProcess `--client`;主进程与子进程共用同一实现(common 工具函数)。

---

## 4. 节点分类栏(专用)VST 插件栏

- Patchbay 侧边**新增「VST 插件」分类栏**:读取 `nodes/VSTPlugins/` 树(文件夹=分类:VST2|VST3 → Instruments|Effects → 子类),`*.vstinfo/*.vst3info` 为可拖入的插件条目;
- 点击/拖入 → 创建对应 `mod_vst_host.dll` 节点(或链节点的槽);
- 信息展示:名称/厂商/版本/类别(来自 info);无 info 的 VST 文件不显示;
- 重新扫描:分类栏提供「重新扫描」(调用 `Vst Scanner.exe`)。

---

## 5. 存档(wjnproj)

- `.wjnproj` 增加顶级字段 **`vsts[]`**(单 VST 节点与链节点统一):
  ```json
  {
    "node": "mod_vst_host_1",
    "type": "vst3",
    "path": "C:\\VST\\ExampleEQ.vst3",
    "internalPlugin": null,
    "preset": "my-preset.fxb",
    "clientName": "ExampleEQ",
    "clientNameRenamed": true,
    "portDisplayMode": "default",
    "parameters": [ { "index": 0, "value": 0.5 } ],
    "state": "<base64 插件私有状态>",
    "chained": false
  }
  ```
- 恢复流程:启动/加载工程 → 按 `path/internalPlugin` 拉起 VSTProcess → IPC 恢复参数/状态/预设 → 注册端口 → 自动重连(`chain` 同上,含子链布局);
- 与 `mixers[]`/`switches[]`/`adapters[]` 同模式,`version` 递进。

---

## 6. 构建与部署

```cmake
# 根 CMakeLists.txt
option(WJN_ENABLE_VST2 "Build VST2 host support (licensed use only)" OFF)   # 需要时:-DWJN_ENABLE_VST2=ON
add_subdirectory(src/vst)           # mod_vst_host + mod_chain_host
add_subdirectory(tools/vst_scanner) # Vst Scanner.exe
add_subdirectory(tools/vst_process) # VSTProcess32/64.exe
```

- **VST2 编译开关**:`WJN_ENABLE_VST2=ON` 时 VSTProcess 才包含 VST2 自实现宿主(链接 `vst2sdk` 头);`OFF` 时仍支持 VST3 与扫描(扫描永远做位宽识别);改为临时 ON 后需全量重编 VSTProcess/节点。

- `VSTProcess64/32`:原生 C++/Win32 + **vst3sdk Hosting API**(`pluginterfaces`/`public.sdk`) + vst2sdk(仅 `pluginterfaces/vst2.x`);JACK 通过本地动态加载器连接,不链接 x64 `WinJACKNexus::Audio`,因此 `VSTProcess32` 可作为独立 `-A Win32` CMake 工程构建;**不链接 Qt**;
- `mod_vst_host`/`mod_chain_host`:节点 DLL 和设置/编辑器界面继续使用 Qt6,通过原生 IPC 调用 VSTProcess;
- 平台:x64 主(64 位插件 + 32 位 VST2 走 VSTProcess32);Qt 节点使用 Qt6 6.10.3 msvc2022_64;
- 输出:三个 exe 至主程序目录;节点 DLL 至 `nodes/vst/`;`nodes/VSTPlugins/` 由扫描器生成(数据,入 .gitignore 可选)。

---

## 7. 里程碑与任务分解

| 里程碑 | 内容 | 状态 | 预计 | 验收 |
| :--- | :--- | :--- | :--- | :--- |
| **V1 扫描器** | `Vst Scanner.exe`(Qt6):多路径、VST2/VST3 描述读取、**PE 位宽识别(32/64)→ info `bits`**、info 写入与分类、Waves internal_plugin;Probe64/Probe32 隔离探测;VST2 Shell、非法 DLL、Synth 分类和旧缓存迁移测试 | ✅ 已完成 | 3~5 天 | 扫描示例插件生成正确 `.vstinfo/.vst3info`(**含 `bits`**)与目录树;真实 DSK VSTi 可识别为 Instrument/MIDI in |
| **V2 VSTProcess 骨架** | VSTProcess64/32(原生 C++ + VST3 SDK Hosting + IPC 命名管道 + JACK 客户端,不使用 Qt)+ 崩溃退出码 | ✅ 已完成(编译验证) | 4~6 天 | x64/x86 目标均编译成功;CLI 与 VST3 加载失败退出码已验证;真实 VST3 + JACK 的端口出现/进程退出端口移除由用户手动验证 |
| **V3 VST2 宿主**(`WJN_ENABLE_VST2=ON` 时编译) | 原生 C++ 自实现 AEffect 加载/`processReplacing`/参数与 chunk 状态 API;VSTProcess32 验证 | ✅ 已完成(编译与 fixture 验证) | 4~6 天 | 启用开关后 x64/x86 目标均编译成功;VST2 fixture 完成 AEffect→JACK 生命周期;Shell `internal_plugin` 名称可解析;真实 VST2 的 fxp/fxb 文件协议与节点 IPC 留待 V4 |
| **V4 节点集成** | `mod_vst_host.dll`(Qt `INode`、标准设置导出、插件路径/客户端名、PE 位宽自动分发、命名管道启动与优雅重载);VST2 Shell/VST3 多 class 选择与持久化;端口显示模式与标签 fallback | ✅ 已完成(编译与 DLL 契约验证) | 4~6 天 | Debug 生成 `nodes/vst/mod_vst_host.dll` 与 `VSTProcess64.exe`;动态加载、Node API 导出、节点创建、无插件初始化和 JSON 设置往返测试通过。Shell/VST3 class 使用内部插件名作为默认客户端名;端口默认优先 VST3 bus/VST2 pin 标签,缺失时回退 JACK 默认。真实插件/JACK 由用户手动验证 |
| **V5 旁通/重启** | 崩溃检测、Bypass 直通、节点「重启 VSTProcess」 | ✅ 已完成(编译与契约验证) | 2~3 天 | `QProcess::finished` 检测 sandbox 异常退出后，启动同客户端名、已知端口数和插件标签的 bypass `VSTProcess`，逐通道输入直通输出；设置面板「Restart plugin」退出旁通并重启真实插件。Debug 节点契约、VSTProcess64 构建和 bypass CLI 验证通过；真实 JACK 图端口连续性与崩溃恢复由用户手动验证 |
| **V6 链节点** | ~~`mod_chain_host.dll`~~ **已被子图取代**——由接线盘 §12 子图(S1–S5)承担;子图内 VST 插件复用 `mod_vst_host.dll`(不建链 DLL) | ❌ 取消(重定向) | — | 见接线盘 §12 验收 |
| **V7 分类栏与存档** | Patchbay 读取 `nodes/VSTPlugins` info 分类，提供扫描器入口；拖入/双击创建 VST 节点；四种端口显示模式；客户端名长度保护；`.wjnproj vsts[]` 存档/恢复（兼容旧 `modules[]`） | ✅ 已完成（编译验证） | 3~4 天 | `mod_patchbay`、`mod_vst_host`、`VSTProcess64` 已构建；分类与真实插件/JACK 连线待手动验证 |
| **V8 集成验证** | 全链路:扫描→拖入→原生 VSTProcess 沙盒→连线→预设→存档→崩溃旁通 | ✅ 已完成（编译验证） | — | 编译通过交付,真实插件/JACK 由用户手动验证 |

> 遵循项目既有工作流:每里程碑**编译通过即可交付**,行为由用户手动验证。

---

## 8. 风险与对策

| 风险 | 对策 |
| :--- | :--- |
| **VST2 SDK 许可**(`VST2_License_Agreement.pdf`) | `WJN_ENABLE_VST2` 默认 ON;启用和分发前必须确认自身已获授权,不得再分发 SDK 头;未授权时应关闭该开关并仅使用 VST3 |
| VSTProcess 需兼顾 32 位 ABI | 处理器进程不依赖 Qt,直接使用原生 C++、Win32、VST2 SDK 和 VST3 SDK;节点 UI 保持 Qt,仅通过 IPC 交互 |
| 插件崩溃/挂死 | VSTProcess 独立进程 + keepAlive 超时检测(500ms)+ 节点 Bypass;挂死可强杀重启;内存分配上限守护(可选 Job Object) |
| 跨进程编辑器 HWND | 仅本机同会话;`NativeWindowUtils` 嵌入 + 进程退出时自动 detach;UIPI:同权限进程正常,异常时回退为独立窗口 |
| 32/64 位 JACK 客户端 | VSTProcess32 链 `C:\Windows\libjack.dll`(已装);端口名/图一致 |
| 端口名无自定义 | fallback jack 默认;真实名保持 JACK 规范,别名仅展示 |
| 重载/换插件导致端口抖动 | UI 线程重建端口 + RealtimeObject 双缓冲;保持客户端名优先 |
| `nodes/VSTPlugins` 体积/更新 | 缓存 mtime;扫描结果可版本管理(info 小文件);大数据不提交 |

---

## 9. 验收标准(最终)

1. `Vst Scanner.exe` 扫描多路径并在 `nodes/VSTPlugins/` 生成参考结构的 info 文件(Waves Shell 含 `internal_plugin`;**每个 info 含 `bits`(32/64)**);
2. Patchbay 出现「VST 插件」分类栏,按文件夹分类展示;拖入创建节点;
3. `mod_vst_host.dll` 节点(节点界面保持 Qt):插件路径设置、预设同步、端口显示方式(4 种 + fallback)、自带 GUI(预设栏保存/加载 `.fxp/.fxb`)、「重载 VST」(仅 VST 节点可见),全部生效;
4. ~~`mod_chain_host.dll`~~ **已被子图取代**:链式能力验收转接线盘 §12(子图接口增删/改名、主图隐藏 sub*、白名单、无限嵌套);子图内 VST 插件正常加载/旁通;
5. 原生 C++ VSTProcess 不依赖 Qt;VST2 与 VST3 均可在 64 位加载;32 位 VST2 经 VSTProcess32 加载;
6. 沙盒崩溃:杀 VSTProcess → 节点旁通(端口保持/直通),主程序不崩,「重启插件」恢复;
7. 客户端名符合规则(去扩展名/内部名、过长 fallback、重名自增);端口数量由插件能力决定;
8. `.wjnproj` 保存/恢复插件路径、参数、当前预设、客户端名。

---

*本计划书随开发反馈迭代,修改后递增版本号。v1.8 记录 V8 完成：扫描、VST2/VST3 沙盒、分类栏、单节点、子图复用、旁通/重启和工程存档均已完成编译验证；真实插件与 JACK 图全链路由用户手动验证。*
