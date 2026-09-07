# WinJACKNexus - 接线盘(Interactive Patchbay)- 开发计划

> 版本:v1.11
> 日期:2026-09-06
> 依据:«WinJACKNexus 开发规范» v3.1(补充:基础库计划 v1.0)
> 参考实现:«Ref/Carla»(Carla Plugin Host,Patchbay 处理模式 + JACK Multi-Client)

---

## 0. 计划要点(用户需求摘要)

| 需求 | 结论 |
| :--- | :--- |
| 参考 `Ref/Carla` | 以其 Patchbay 模式与 JACK Multi-Client 引擎为蓝本 |
| 实现方式 | **纯 Qt6 + C++20 重写**(不沿用 Carla 的 Python/Qt4-5 前端与 Bridge 体系) |
| 精简 | **走带(Transport)**、**钢琴键盘(Notes/Virtual Keyboard)** |
| 占位 | **VST2 / VST3 加载逻辑**(详见《VST Host 开发计划》,本计划仅留插件节点占位与桥接接口) |
| 正常加载 | **Internal** —— 内置 DSP 模块直接加载；SF 系列将作为专用 Internal 模块后续实现(见 §6.2) |
| 引擎 | **仅保留 JACK Multi-Client**(`ENGINE_PROCESS_MODE_MULTIPLE_CLIENTS` + JACK 驱动;不实现 ALSA/PulseAudio/文件/Dummy 等其他后端) |
| 其余 | Carla 中与接线盘相关的功能**原封不动保留**(节点/端口/连线/分组/搜索/缩放等语义) |
| 主题 | **全部样式集中到一个 `style.css` + 可选扩展贴图包**——换 CSS + 贴图即可完全改主题(覆盖全部自带模块与节点详情窗);无扩展贴图时**回退为自绘组件外观**(现代扁平化);字体用当前系统字体(见 §7) |
| 可复用 | 分门别类下沉到 `WinJACKNexus` 基础库(Common / UI / Audio / Node 各找各家,见 §5) |
| 连接保持 | **默认保持连接**:客户端改名/端口改名/增减端口情况下连接依然保持;加载存档后自动连接(见 §4.4) |
| 存档 | 扩展名 **`.wjnproj`**、内容为**标准 JSON**(RFC 8259,UTF-8)(接线盘场景:连接 + 布局 + 插件 + **Mixer/Switch 等模块存档占位**);存档内已有连接信息时启动自动连接;**启动后自动加载上次保存的存档**(见 §4.5) |
| 部署 | patchbay 为**主模块**:与 `WinJACKNexus.exe` **同级**(`mod_patchbay.dll` + `patchbay_core.dll` + `patchbay_plugins.dll`);Carla 模块与后续自带模块统一放 `nodes/<类型>/`(按类型分文件夹,见 §8) |

---

## 1. 参考实现分析(Ref/Carla)

### 1.1 与接线盘强相关的组件

| Carla 组件 | 作用 | 本计划处理 |
| :--- | :--- | :--- |
| `source/backend/CarlaBackend.h` | Patchbay 模式端口模型:`PATCHBAY_PORT_IS_INPUT` / `PORT_TYPE_AUDIO/MIDI/CV/OSC`、端口组 hints(`MAIN_INPUT/OUTPUT`、`STEREO`、`MID_SIDE`)、`ENGINE_CALLBACK_PATCHBAY_CLIENT/PORT/CONNECTION_ADDED/REMOVED/RENAMED/DATA_CHANGED` | 语义保留(译为 `WinJACKNexus::Patchbay::Core` 模型) |
| `source/backend/engine/CarlaEngineJack.cpp` | JACK 引擎:多客户端、端口扫描、`CarlaJackPortHints`(audio/midi/cv/osc 识别)、连接管理、metadata(UUID/icon/position/plugin-id) | 引擎语义保留 → **仅 JACK MultiClient**,重写于 `Audio::JackGraph` + Patchbay Core |
| `source/backend/engine/CarlaEngine.cpp` | `ENGINE_PROCESS_MODE_MULTIPLE_CLIENTS` 逻辑、`Audio Input/Output` 端口映射 | 语义保留(多客户端全局图),实现改为观察 JACK 全局 |
| `source/frontend/carla-jack-multi` | 启动脚本:`audioDriver="JACK"` + `ENGINE_PROCESS_MODE_MULTIPLE_CLIENTS` | 本计划的**唯一引擎模式** |
| `source/frontend/patchcanvas/` | Qt 画布:`PatchScene`、`CanvasBox`、`CanvasPort`、`CanvasLine/BezierLine`、`CanvasIcon`、发光/淡入淡出动画、`theme.py`、分组 `split/join`、位置持久化 | 语义保留,Qt6 重写(扁平样式) |
| `source/frontend/carla-patchbay` | 启动脚本:强制 `ENGINE_PROCESS_MODE_PATCHBAY` | 不单独成应用——接线盘作为 **WinJACKNexus 主模块**(与主程序同级) |
| `source/frontend/notes-ui`、走带控件(Transport) | 钢琴键盘、走带/同步 | **精简去除** |
| `backend/plugin/CarlaPluginInternal.cpp`、`CarlaPluginNative.cpp`、`native-plugins/` | Internal 插件:gain/bigmeter/bypass/cv/lfo/midi-*/notes/quadpanner/xycontroller 等内置 | 保留语义 → `patchbay_plugins.dll` 的 `InternalLoader`(直接编译,无外部依赖) |
| `backend/plugin/CarlaPluginLADSPADSSI.cpp`、`CarlaPluginLV2.cpp`、`CarlaPluginJSFX.cpp` | LADSPA / DSSI / LV2 / JSFX 加载 | **去除**：不引入外部插件格式运行时 |
| `CarlaPluginFluidSynth.cpp`、`CarlaPluginSFZero.cpp` | SF2/SF3/SFZ 采样器加载 | **去除外部格式支持**；后续按需求实现专用 Internal 采样器模块 |
| VST2/VST3 插件加载(`CarlaPluginVST2/VST3.cpp`、bridge) | VST 插件发现/加载/编辑器 | **占位**(仅占位菜单与 `VstSlotBridge` 接口,详 §6.3) |
| `source/utils`、`frontend/carla_skin.py` | 工具函数、皮肤 | 可复用逻辑下沉基础库(§5) |

### 1.2 保留 / 精简 / 占位 / 下沉 对照表

| 类别 | 内容 | 去向 |
| :--- | :--- | :--- |
| **保留** | 节点(Group)卡片与图标;端口(pins:Audio 绿 / MIDI 橙 / CV·OSC 预留);连线(Bézier、平滑动画);分组 split/join;搜索/过滤;平移/缩放;框选;右键菜单(connect/disconnect/rename);位置与布局持久化;客户端增删改名事件刷新 | Patchbay Core + Qt6 UI |
| **保留(引擎)** | JACK 全局图观察 :全部客户端、端口、连接实时刷新;音频 + MIDI 连接/断开 | `Audio::JackGraph` |
| **精简** | 走带区域(JACK Transport 同步、播放/停止、位置显示) | 移除 |
| **精简** | 钢琴键盘(notes-ui / 虚拟键盘面板) | 移除 |
| **保留(插件)** | Internal:插件发现、加载、实例化、端口绑定、参数/状态 | `patchbay_plugins.dll`(`IPluginInstance` + `InternalLoader`) |
| **占位** | VST2/VST3:插件添加按钮/编辑器/HWND 嵌入 | 占位 UI + `VstSlotBridge` 接口(实现见 VST 计划) |
| **下沉** | 日志/路径/版本/配置持久化/字符串工具 → `Common`;扁平主题/QSS/自绘控件/图标/字体 → `UI`;JACK 快照/连接/元数据 → `Audio`;节点接口 → `Node` | 基础库(§5) |
| **去除** | 其他 Engine(ALSA/Pulse/文件/Dummy/SDL/RtAudio)、OSC 远程控制、FB(fallback)、Bridge 体系、LADSPA/DSSI/LV2/JSFX/SF2/SF3/SFZ 外部格式运行时 | 不实现 |

---

## 2. 目标、范围与交付物

### 2.1 目标
- 以纯 Qt6 + C++20 重写 Carla 的接线盘(Patchbay)体验,作为 WinJACKNexus 的 **总配线盘节点**;
- 展示**所有 JACK 客户端**(含外部客户端与系统端口),节点化连线、断开、分组与搜索;
- 现代扁平化界面 + 系统字体;仅 JACK Multi-Client 引擎;VST 加载留占位。

### 2.2 交付物(部署于 `nodes/internal/patchbay/`)

| 产物 | 说明 |
| :--- | :--- |
| **与主程序同级 `mod_patchbay.dll`** | **主 DLL**:实现 `WinJACKNexus::Node::INode`,承载 Qt6 画布主窗口与交互 |
| **与主程序同级 `patchbay_core.dll`** | **依赖库**:`WinJACKNexus::Patchbay::Core`(节点/端口/连接模型、JACK 快照、搜索过滤;无 Qt,可单元测试) |
| **与主程序同级 `patchbay_plugins.dll`** | **依赖库**:`WinJACKNexus::Patchbay::Plugins`——Internal 加载器与 `IPluginInstance`(无 Qt,可单元测试) |
| `theme/style.css`(+ 可选 `assets/` 扩展贴图包) | **全部主题样式的唯一载体**;无贴图包时组件回退自绘外观 |

> **部署定位**:patchbay 是**主模块**(全局配线中枢),与 `WinJACKNexus.exe` **同级**部署(主 DLL + 依赖库同目录);源码位于 `src/patchbay/`(与 common/ui/audio/node 并列),构建产物输出至主程序目录。其他节点(Carla 模块 + 后续自带模块)统一放 `nodes/` 下**按类型分文件夹**(adapter/mixer/meter/switch/midi/vst/carla…),由主进程递归扫描。

---

## 3. 目录结构

```
src/patchbay/                         # 接线盘主模块源码(产物输出至主程序目录,与 WinJACKNexus.exe 同级)
├─ CMakeLists.txt                 # 顶层(三个 target)
├─ core/                          # → patchbay_core.dll
│  ├─ CMakeLists.txt
│  ├─ include/WinJACKNexus/Patchbay/Core/
│  │  ├─ Export.hpp               # WINJACK_PATCHBAY_API
│  │  ├─ Types.hpp                # PortKind(Audio/MIDI/CV/OSC)、Direction、端口组 hints
│  │  ├─ ClientInfo.hpp           # 客户端名称/图标/位置上/存活
│  │  ├─ PortInfo.hpp
│  │  ├─ ConnectionInfo.hpp
│  │  ├─ Snapshot.hpp             # JackGraph → 可序列化快照(增删改事件)
│  │  ├─ GraphModel.hpp           # 模型容器(增删客户端/端口/连接,查询,split/join 分组)
│  │  └─ SearchModel.hpp          # 名称过滤/搜索(节点+端口)
│  └─ src/*.cpp
├─ mod/                           # → mod_patchbay.dll
│  ├─ CMakeLists.txt
│  ├─ include/mod_patchbay/       # 内部头
│  ├─ src/
│  │  ├─ PatchbayNode.cpp         # INode 实现(wjn_* / createWindow)
│  │  ├─ Library.cpp              # INode 实现(wjn_* / createWindow + 主题工具栏)
│  │  └─ PatchbayCanvas.cpp       # Qt6 QGraphicsView:节点/端口/贝塞尔连线自绘
│  └─ resources/
│     ├─ patchbay.qrc             # 主题资源
│     └─ theme/
│        ├─ style.css             # 全部主题样式(唯一样式载体,QSS 兼容)
│        └─ assets/               # 可选扩展贴图包(纹理/节点卡/端口图标/旋钮…;缺失→自绘外观)
├─ plugins/                       # → patchbay_plugins.dll
│  ├─ CMakeLists.txt
│  ├─ include/WinJACKNexus/Patchbay/Plugins/
│  │  ├─ Export.hpp                # WINJACK_PATCHBAY_PLUGINS_API
│  │  ├─ IPluginInstance.hpp       # descriptor/initialize/process/ports/parameters/state
│  │  ├─ PluginDescriptor.hpp      # 类型(Internal；VST2/VST3 仅占位)、名称、作者、版本
│  │  ├─ Loader.hpp                # 按类型/扩展/清单探测并实例化
│  │  └─ InternalLoader.hpp        # native-plugins(gain/meter/bypass；后续可扩展 SF 专用模块)
│  └─ src/*.cpp
└─ tests/
   ├─ core/                       # 模型/快照单元测试(ctest)
   └─ plugins/                    # 各 Loader 实例化/端口/处理单元测试(ctest)
nodes/<类型>/                       # 其他节点(Carla 模块 + 后续自带模块)按类型分文件夹:
│  ├─ adapter/  mixer/  meter/  switch/  midi/  vst/  carla/ ...(见规范 §3.1)```

---

## 4. 功能设计(保留语义,Qt6 重写)

### 4.1 核心模型(`patchbay_core.dll`,无 Qt)

| 概念 | 设计 |
| :--- | :--- |
| `ClientInfo` | 一个 JACK 客户端 = 一个节点:短名/全名、图标 hint、`uri`(UUID,如 jack metadata)、位置(x,y)、分组 split 状态、存活 |
| `PortInfo` | 端口 = 引脚:`PortKind{Audio,MIDI,CV,OSC}`(CV/OSC 预留——Windows JACK 无属性时默认 Audio/MIDI)、`Direction{Input,Output}`、组 hints(`MainInput/Output`、`Stereo`、`MidSide`)、别名 |
| `ConnectionInfo` | 源端口 → 目标端口;实时安全 |
| `Snapshot` | `Audio::JackGraph` 全量快照:客户端集合、端口集合、连接集合 + 增量事件(`clientAdded/Removed/Renamed/DataChanged`、`portAdded/Removed`、`connectionAdded/Removed`),驱动 UI 刷新 |
| `GraphModel` | 快照容器 + 查询(`byName`/`byPort`)+ **分组 split/join**(端口组折叠为节点内子行)、分组名 → 客户端 id 映射 |
| `SearchModel` | 大小写不敏感过滤(节点名/端口名/别名),供工具栏搜索框 |

> 上限参考 Carla:`MAX_PATCHBAY_PLUGINS = 255` 个节点;画布需为此规模保持流畅(见 §10)。

### 4.2 画布交互(参照 `patchcanvas/*.py` 语义,Qt6 重写)

| 交互 | 实现 |
| :--- | :--- |
| 节点卡片 | `QGraphicsObject`(自绘,`PaintableWidget` 风格拆分可复用但此处用 QGraphics)+ 主题色块、圆形端口引脚 |
| 连线 | `QGraphicsPath` Bézier;连接/断开带过渡动画(`AnimatedWidget` 节奏,可停用(LOD));未连接端口在拖线时显示可连接目标 |
| 拖拽/框选/平移/缩放 | `QGraphicsView`(RubberBandDrag、ScrollHandDrag、Ctrl+滚轮缩放、自适应缩放 `fitInView`) |
| 分组 | 双击节点标题或右键 → `Split/Join Group`(同客户端端口分组显示) |
| 右键菜单 | 端口:连接/断开/查看别名;节点:重命名(别名显示)、居中、隐藏 |
| 搜索 | 顶部 QLineEdit → `SearchModel` 过滤,命中节点高亮 |
| 布局持久化 | 节点位置、缩放比、分组状态存 `%LOCALAPPDATA%\WinJACKNexus\patchbay.json`(Common 配置读写) |

### 4.3 与 JACK 的桥接(仅 Multi-Client)

- 接线盘自身以唯一 JACK 客户端 `WinJACKNexus-Patchbay` 打开(`Audio::JackClient`),仅用于**观察全局图**(不注册音频端口,不参与处理);
- 通过 `Audio::JackGraph`:`walkPorts()`(audio/midi)、`isConnected()`、`connect/disconnect()`、`onGraphChanged()`(`jack_set_graph_order_callback` + 定时刷新),增量更新 `Snapshot`;
- **多客户端语义**:所有节点(Adapter、Mixer、电平表、VST 节点、外部客户端、DAW 桥)均为 JACK 图中独立客户端/端口组——接线盘只负责观察与连线,不拥有处理链(区别于 Carla 的内部 rack engine);
- 元数据(UUID/图标/位置):增量阶段接入 `jack_get_property/set_property`(需在 `Audio::JackApi` 增加 metadata 函数,见 §5)。

### 4.4 连接保持与自动重连(默认开启)

> **默认保持连接**:接线盘正常运行时**不主动断开**用户建立的连接;客户端改名、端口改名、端口增减后依然保持连接;加载存档(含连接信息)后启动时**自动连接**。

- **连接标识与匹配策略(四级回退)**:

| 层级 | 匹配键 | 说明 |
| :--- | :--- | :--- |
| 1 | **端口/客户端 UUID**(`jack_port_uuid` / `jack_uuid` + JACK metadata,`Audio::JackApi` 增量提供) | 改名/增删端口后仍**精确匹配**,连接自动恢复 |
| 2 | 客户端短名 + 端口名 + 端口类型 | UUID 不可用/失效时回退 |
| 3 | 客户端短名 + 端口组 hints(`MainInput/Output`、`Stereo`…)+ 序号 | 端口增删后的模糊匹配 |
| 4 | 无法匹配 | 保存并显示为**「缺失连接」**(虚线占位),保留记录供用户手动重连/清理 |

- **重连时机**:启动加载存档后;`graph_order`/客户端/端口事件后重新评估(UI 线程执行,`jack_connect` 幂等——已连接返回 `-EEXIST` 忽略);端口未就绪时按 100ms/500ms 退避重试;
- **事件驱动**:`Snapshot` 增量事件(page)带 UUID,匹配成功后自动 `Audio::JackGraph::connect()`;**断开连接同样是显式行为**(用户操作/插件卸载),不因窗口隐藏、刷新、重启而丢失。

### 4.5 项目存档(.wjnproj)

| 项 | 说明 |
| :--- | :--- |
| 扩展名 | **`.wjnproj`**(WinJACKNexus 接线盘项目/场景文件) |
| 位置 | 用户选择;默认 `%LOCALAPPDATA%\WinJACKNexus\projects\` |
| 内容 | `connections[]`:src/dst 的 UUID + 名称 + 端口类型 + 组 hints;`layouts[]`:节点位置/缩放/分组 split 状态;`plugins[]`:已加载插件(类型/路径/参数/状态,B3 后启用);**`mixers[]` / `switches[]`(本版占位,见下)** |
| **模块存档(占位)** | **Mixer、Switch 等模块的状态也并入本工程文件**——预留顶级字段 `mixers[]`(调音台:通道增益/Mute/Solo/Pan、视图状态)与 `switches[]`(信号切换:路由选择/crossfade 状态);本版仅保留 **占位(空数组/最小结构)**,待 Mixer/Switch 各自开发完成后通过 `INode` 状态接口(增量:`saveState/loadState` 或 C-API `wjn_get_node_state/wjn_set_node_state`)读写填充;不破坏旧存档兼容(`version` 递进) |
| **格式** | **标准 JSON(RFC 8259,UTF-8 文本)**——严格 JSON 语法,可用任意标准 JSON 工具(编辑器/`jq` 等)查看与校验;**不接受自定义/非标准序列化** |
| 序列化 | `nlohmann::json`(单头库,vendoring 至 `third_party/`)或 `WinJACKNexus::Common::Json` 封装;Qt 侧亦可 `QJsonDocument` 互读;反序列化失败给出明确错误且不破坏文件 |
| API | `PatchbayCore::Project::save(path)` / `load(path)` / `restoreConnections()`(按 §4.4 匹配策略自动连接) |
| **启动行为** | **启动后自动加载上次保存的存档**:每次保存成功时将路径写入“最近项目”记忆(`Common` QSettings/配置:`lastProjectPath`,默认 `%LOCALAPPDATA%\WinJACKNexus\last.wjnproj`);启动时自动 `load` + `restoreConnections`;加载失败(损坏/缺失)→ 提示并回退为空场景,不阻塞启动;设置中可关闭自动加载 |
| 保存时机 | 菜单/`Ctrl+S`、关闭前询问、可选自动保存;连接变化可标记脏状态;每次保存成功后更新“最近项目”记忆 |

示例(标准 JSON):

```json
{
  "version": 1,
  "app": "WinJACKNexus",
  "connections": [
    {
      "src": { "uuid": "7f5c...", "client": "system", "port": "capture_1", "type": "audio", "group": "main_input" },
      "dst": { "uuid": "9a2b...", "client": "mod_mixer", "port": "ch1_in", "type": "audio", "group": null }
    }
  ],
  "layouts": [
    { "client": "mod_mixer", "x": 120.0, "y": 240.0, "split": "joined" }
  ],
  "plugins": [
    { "kind": "internal", "identifier": "internal:gain", "parameters": [] }
  ],
  "mixers": [],
  "switches": []
}
```

---

## 5. 基础库复用(分门别类,各找各家)

| 基础库 | 下沉内容(从 Carla 参考) | 本计划使用 |
| :--- | :--- | :--- |
| `WinJACKNexus::Common` | `CarlaString/CarlaUtils`(字符串/文件名/路径工具)、设置持久化、日志、版本、`RealtimeObject`(状态快照) | 配置读写 `patchbay.json`;日志;版本协商 |
| `WinJACKNexus::UI` | `carla_skin.py` 皮肤机制 → `ThemeManager`+`ThemePalette`;单个 **`style.css`** 加载与变量替换;`PaintableWidget/AnimatedWidget`(自绘回退);`IconManager`(图标/贴图);**新增:`ThemeAssets` 工具**(贴图包加载/`url(assets/...)` 解析,缺失回退自绘)与 **`SystemFont`**(`QFontDatabase::systemFont(GeneralFont)`,不捆绑字体) | 全局 style.css 主题;可选贴图包;自绘回退;节点/引脚/连线绘制;SVG 图标;系统字体 |
| `WinJACKNexus::Audio` | `CarlaEngineJack`(JackBridge)→ 已有 `JackClient/JackPort/JackGraph/JackServerGuard`;**增量**:`JackGraph` 增加 metadata 封装(`jack_get_property/set_property`)与端口 hint 判定(Audio/MIDI/CV) | JACK 多客户端观察、连线、图变更通知 |
| `WinJACKNexus::Node` | —(接口层已有 `INode`) | `mod_patchbay.dll` 导出 `wjn_get_node_*` 系列 |
| Patchbay 私有 | `patchcanvas` 画布语义(CanvasBox/Port/Line/动画)→ 保留在 **`patchbay_core.dll` + `mod_patchbay.dll` 内部**(不抽象到基础库——属于模块业务) | 节点卡片/引脚/连线 items |

> 原则:「能进基础库的进基础库」仅限跨模块可复用逻辑;画布之**业务**语义(节点=JACK 客户端)留在 patchbay 内部。

---

## 6. 精简与占位

### 6.1 精简(去除)
- **走带**:不提供 Transport 控件、不连接 JACK Transport、不实现 `ENGINE_TRANSPORT_MODE_JACK`/Ableton Link 同步、无播放/停止/位置显示;
- **钢琴键盘**:不提供 notes-ui 键盘、无虚拟 MIDI 键盘面板。

### 6.2 插件类型正常加载(Internal)

- **加载范围**:Internal ✅；LADSPA、DSSI、LV2、JSFX、SF2/SF3、SFZ 不支持；VST2/VST3 保持占位(§6.3)。
- **加载核心 `patchbay_plugins.dll`**(`WinJACKNexus::Patchbay::Plugins`,无 Qt 依赖):

| 类型 | 加载器 | 参考(Carla) | 说明 / 依赖 |
| :--- | :--- | :--- | :--- |
| **Internal** | `InternalLoader` | `CarlaPluginInternal/Native.cpp` + `native-plugins/` | 内置原生插件直接编译进 `patchbay_plugins.dll`,零外部依赖；现有 Gain、Bypass、Peak Meter，后续可增添 SF 专用模块 |

- **统一接口 `IPluginInstance`**:`descriptor()`(类型/名称/作者/版本)、`initialize(sampleRate, bufferSize)`、`process(audio in/out)`、`parameters()`(数量/名称/取值);
- **节点呈现**:每个已加载 Internal 插件 = 接线盘 1 个插件节点(端口 = 插件端口),经 `PatchbayCore` 描述；「添加插件」仅列出可用 Internal 条目，VST2/VST3 保持置灰;
- **线程模型**:Internal DSP 在主进程内运行，`process` 在 JACK 回调线程直接调用(遵循无锁规范,prepare 阶段完成分配)。

### 6.3 VST2/VST3 占位(本版仅留槽位)

- `mod_patchbay.dll` 中「添加插件」菜单 **VST2/VST3 项置灰** + 提示「VST 支持将在《WinJACKNexus - VST Host - 开发计划》中提供」;
- 定义桥接口(仅声明):
  ```cpp
  namespace WinJACKNexus::Patchbay {
      // 占位:由 VST Host(沙盒进程)实现;接线盘据此渲染插件节点
      class VstSlotBridge {  // 本版仅接口,无实现
      public:
          virtual ~VstSlotBridge() = default;
          virtual bool loadPlugin(const char* path) = 0;
          virtual void unloadPlugin() = 0;
          virtual void* editorWindow() const = 0;   // HWND(沙盒进程)
      };
  }
  ```
- **不引入** Carla 的 VST2/VST3 加载与 bridge 代码;§6.2 各 Loader 亦不处理 VST 格式。

---

## 7. 主题系统(一个 style.css + 扩展贴图,全模块生效)

### 7.1 主题载体:单个 `style.css`

- **全部主题样式集中在一个 `style.css`**(QSS 兼容 CSS 语法,UTF-8):颜色、字体、圆角、间距、边框、图标颜色、端口配色、控件状态(hover/disabled/checked)等**全部**写入该文件,**不允许代码内硬编码样式/颜色**;
- 主题切换 = 替换 `style.css` + 可选贴图包 → **无需修改任何 C++ 代码**;
- 经 `UI::ThemeManager` 加载:`setTheme(name)`(内置 `flat-dark`/`flat-light`)或 `loadTheme(path)`(外部 `style.css`);变量替换引擎 `${var}` 与 `ThemePalette` 联动,运行时热切换。

### 7.2 扩展贴图(可选):一个 CSS + 多个贴图 = 完全改主题

| 项 | 说明 |
| :--- | :--- |
| 贴图包 | 主题目录 `assets/` 下的一组贴图/纹理:背景纹理、节点卡片贴图(9-slice)、端口图标、按钮/推子/旋钮贴图、连线纹理、窗口标题条等;`style.css` 内以 `url(assets/xxx.png)` 引用 |
| **完全改主题** | 换一套 `style.css` + `assets/` 即可**完全变更外观**(拟物/金属/木质/自定义风格皆可),代码零改动 |
| 多主题并存 | 每套主题 = 目录 `theme/<name>/style.css` + `assets/`,`ThemeManager` 列出并热切换 |
| **自绘回退** | **未提供扩展贴图时,组件按自绘外观渲染**——`PaintableWidget`/画布节点等以扁平纯色 + 1px 描边 + 圆角(默认主题)自绘;**贴图可选、自绘兜底**,不因缺图破相 |

### 7.3 覆盖范围(所有自带模块与节点详情窗)

- **全模块生效**:主程序窗口、托盘、接线盘画布(节点/端口/连线)、**调音台详情窗**、**电平表详情窗**、插件添加对话框、各节点(Adapter/Mixer/Meter/Switch/MIDI 等)设置与详情窗口——全部由全局 `style.css` 驱动(`ThemeManager::applyToApp()` + 模块窗口 `applyToWidget()`);
- **约束**:模块/窗口不得硬编码颜色、字体、图片路径;自定义绘制(自绘控件/画布)一律从 `ThemePalette`(与 style.css 变量同源)取色,贴图经 `IconManager`/资源接口读取;QSS 属性选择器(规范 §5.2)必须支持;
- **默认外观**:无扩展贴图时 = 自绘组件外观(扁平风格,即 §7.5 默认主题)。

### 7.4 字体

- **当前系统字体**:`QFontDatabase::systemFont(QFontDatabase::GeneralFont)`(Windows 通常 Segoe UI / 微软雅黑);字号 10~11pt;不捆绑字体文件、不硬编码字体名;`style.css` 内统一 `font-family: inherit`。

### 7.5 内置主题与端口配色

| 项 | 默认值 |
| :--- | :--- |
| `flat-dark`(默认) | 背景 #1E1E1E、节点卡 #2D2D30、文字 #E0E0E0、强调 #4FC3F7 |
| `flat-light` | #F5F5F5 / #FFFFFF / #37474F |
| 端口配色 | Audio=绿 #66BB6A;MIDI=橙 #FFA726;CV/OSC 预留色(均可被 style.css 覆盖) |

---

## 8. 构建与部署

### 8.1 CMake(根 `CMakeLists.txt` 追加)
```cmake
add_subdirectory(src/patchbay)
```

### 8.2 目标定义
- `patchbay_core`:`add_library(patchbay_core SHARED ...)` + `wjn_configure_shared_target(... WINJACK_PATCHBAY_CORE_BUILD)`;链接 `WinJACKNexus::Common`、`WinJACKNexus::Audio`;
- `mod_patchbay`:`add_library(mod_patchbay SHARED ...)` + `wjn_configure_shared_target(... WINJACK_PATCHBAY_BUILD)`;链接 `WinJACKNexus::UI`、`WinJACKNexus::Node`、`patchbay_core`;`AUTOMOC ON`、`AUTORCC ON`(qrc);
- 输出目录:**主程序目录(与 `WinJACKNexus.exe` 同级)**——`CMAKE_RUNTIME_OUTPUT_DIRECTORY` 指向主程序目录,产出 `mod_patchbay.dll` / `patchbay_core.dll` / `patchbay_plugins.dll`;其余节点 DLL 输出至 `nodes/<类型>/` 对应文件夹。

### 8.3 目标平台
- Windows x64 / MSVC 2022 / C++20;仅 Qt6 6.10.3 `msvc2022_64`(与全工程一致)。

---

## 9. 里程碑与任务分解

| 里程碑 | 内容 | 预计 | 验收 |
| :--- | :--- | :--- | :--- |
| **B1 骨架** | `src/patchbay/` 目录、CMake、`patchbay_core.dll` + `patchbay_plugins.dll` + `mod_patchbay.dll` 空库编译输出至主程序目录 | 0.5 天 | 三个 DLL 生成于主程序同级目录 |
| **B2 Core 模型** | Types/ClientInfo/PortInfo/ConnectionInfo/Snapshot/GraphModel/SearchModel + JACK 快照(JackGraph)+ 单测 | 3~5 天 | `ctest` 通过;快照事件正确 |
| **B3 插件运行时** | `patchbay_plugins.dll`:Internal 加载器 + `IPluginInstance` + JACK 端口绑定 + 「添加插件」对话框 | 3~5 天 | Internal 示例插件显示为节点,端口可连线 |
| **B4 画布 UI（已完成）** | `PatchbayCanvas`(Qt6 QGraphics 重写节点/端口/连线)+ **单一 `style.css` 主题资源 + 自绘回退** + 系统字体 | 5~8 天 | 演示 Snapshot 节点与连线可渲染;深浅主题切换生效 |
| **B5 交互（已完成）** | 拖拽/缩放/框选、连线/断开、分组 split/join、右键菜单、搜索、本地位置持久化 | 4~6 天 | 手动验证流畅(255 节点规模) |
| **B6 连接持久化与自动重连（已完成）** | `.wjnproj` 标准 JSON 存档(save/load/restoreConnections)+ **启动自动加载上次存档(lastProjectPath)** + 可用端口重连重试 + 缺失连接保留 | 3~5 天 | 可保存/加载工程;重启自动加载并恢复可用连接;缺失端口等待重试 |
| **B7 INode 接入（已完成）** | `mod_patchbay.dll` 实现 `INode`(createWindow → 画布)+ 托盘/菜单注册 | 2~3 天 | LoadLibrary 导出符号可解析;窗口可打开 |
| **B8 集成验证（已完成）** | `jackd` 下观察 system/外部客户端,连线/断开实时刷新;工程存档往返验证 | 1~2 天 | 编译通过交付,用户手动验证 |

**实施状态（2026-09-03）**：B1–B8 已完成。已建立 `src/patchbay/` 的 Core、Plugins、Mod 三层 CMake 骨架，注册 `patchbay_core`、`patchbay_plugins` 与 `mod_patchbay` 共享库目标，并确认 Debug 构建零警告、零错误，三个 DLL 均输出至 `build/bin/`。B2 已实现无 Qt 的 `Types`、客户端/端口/连接信息、JACK 全局图快照、增量事件、图查询、split 状态保持与大小写不敏感搜索，并通过 `wjn_patchbay_core_tests` 验证真实 JACK 客户端/端口/连线采集及模型行为。B3 已完成统一 `IPluginInstance`/Descriptor/Loader 契约和可运行的 Internal Gain、Bypass、Peak Meter 实例，并通过 `wjn_patchbay_plugins_tests` 验证实例化、参数与音频处理。LADSPA、DSSI、LV2、JSFX、SF2/SF3、SFZ 已从插件 API 与计划范围移除；SF 系列后续会作为专用 Internal 模块按需求实现。B4 已完成：`PatchbayCanvas` 由 Core `Snapshot` 渲染可选节点卡片、Audio/MIDI/CV/OSC 端口及类型着色的贝塞尔连线，深浅主题通过唯一资源 `style.css` 驱动。B5 已完成：画布提供节点拖拽、框选、中键平移、滚轮缩放、适配/重置视图、端口长按拖动建立兼容的 Audio/MIDI 连接、端口点击建连、右键断连/节点断开全部/重命名、可视 Split/Join、客户端和端口名称搜索，并将节点位置和 Split 状态持久化为当前用户的 Patchbay 本地设置。拖线时显示 Bézier 预览线，可连接端口以实线提示；节点移动期间会实时重算已有连线，因此线缆保持端口锚定。连接成功后会延迟到当前 Qt 鼠标事件结束才重建场景，避免事件处理中的节点被 `scene()->clear()` 提前销毁而卡死或闪退。节点只来自当前 JACK 图，已移除演示拓扑、占位节点、复制粘贴及节点库拖放入口；system 固定在线并默认 Split。模块窗口已重构为 ComfyUI 风格工作区：左侧节点库只列出可加入的 JACK 客户端，窗口持有的只观察 JACK 客户端每秒采集全局图并刷新，离线客户端仍可识别并在重新出现后自动恢复在线。Audio 端口为绿色，MIDI 端口为红色，输入和输出端口按相同行配对显示并带类型色标签。实时刷新会在节点拖动期间暂停，避免长按拖动时由场景重建导致的坐标跳变。右侧可收缩参数面板展示所选节点并编辑别名和 Split 状态，标准菜单栏提供工程、视图面板与画布命令。B7 的模块发现与托盘接入已完成：主程序成功加载 `mod_patchbay.dll` 且 Node API 版本匹配、`init()` 成功时，托盘“接线盘”项恢复可用并可打开基础模块窗口。

**B6 完成补充（2026-09-03）**：文件菜单现提供工程打开、保存和另存为；`.wjnproj` 使用 `QJsonDocument` 生成标准 UTF-8 JSON，并用 `QSaveFile` 原子写入。工程保存 clients、ports、connections 和 `layouts[]`，后者包含普通节点及 Split 输入/输出半节点的独立位置。加载会校验版本和必填标识，恢复画布与布局，并把路径写入 `lastProjectPath`；下次打开接线盘时自动加载。每秒图刷新会通过观察用 JACK client 对项目中当前存在的源/目标端口检查并调用 `jack_connect` 恢复未连接项；暂时缺失端口的连接记录不会删除，待客户端或端口再次出现后继续重试。

**接线盘完成补充（2026-09-03）**：接线盘已完成 B1–B8。节点来源仅限当前 JACK 图，不再创建或保留占位节点；`system` 客户端在观察图中固定为在线。文件菜单新增自动保存间隔设置，默认每 60 秒保存一次已有路径且已修改的工程，间隔可设为 0–3600 秒，其中 0 表示关闭自动保存。手动断开会真实调用 `jack_disconnect` 并移除恢复记录，不会在后续自动重连中恢复。

> 遵循项目既有工作流:每里程碑**编译通过即可交付**,图形/交互行为由用户手动验证;不反复跑自动化验证。

### 9.1 最终验收标准(与 Carla Patchbay 对齐)

> **核心判定:接线盘可以统一管理当前已有的全部 JACK 客户端——和 Carla 一样。**

| # | 验收项 | 判定 |
| :--- | :--- | :--- |
| A1 | **客户端全覆盖**:启动后,当前 JACK 图中**所有**客户端自动出现在画布——`system` 物理端口、外部 JACK 应用、DAW 桥插件、WinJACKNexus 自身节点(Adapter/Mixer/Meter/Switch/VST 节点/插件节点)一个不漏 | 与 Carla `Patchbay` 同视图覆盖 |
| A2 | **端口即引脚**:客户端端口按 Audio/MIDI 分类显示为引脚(组 hints `MainInput/Output`、`Stereo`… 生效),可拖线连接/断开、右键菜单操作 | 与 Carla 一致 |
| A3 | **实时拓扑**:客户端/端口增删、改名、图标变化 → 画布实时刷新(节点出现/消失/重命名),无需重启 | 与 Carla `ENGINE_CALLBACK_PATCHBAY_*` 行为一致 |
| A4 | **管理操作**:连线/断开、分组 split/join、搜索过滤、平移/缩放/框选、节点重命名(别名)与位置持久化 | 与 Carla patchcanvas 语义一致 |
| A5 | **连接保持**:外部客户端重启/改名/增减端口后连接自动恢复(§4.4) | 优于 Carla(默认保持) |
| A6 | **工程存档**:`.wjnproj` 保存/加载/启动自动加载,连接一键恢复(§4.5) | 与 Carla 场景存续一致 |

**验收用例(手动,用户执行)**:
1. 启动 `jackd` + 至少一个**外部 JACK 客户端**(如 `jack_simple_client`/任意 JACK 应用)+ WinJACKNexus 节点;
2. 打开接线盘 → A1 覆盖全部客户端;`system:capture_*` 可连线到外部客户端输入,反之亦然(A2);
3. 中途杀掉/重启外部客户端 → 画布节点消失/恢复,A5 自动重连;
4. 外部客户端改名或增删端口 → A3 实时刷新且连接保持;
5. 保存 `.wjnproj` → 重启 → 启动自动加载,A6 连接与布局恢复;
6. 加载 Internal 示例插件 → 显示为节点并可连线。

> **全部 A1–A6 通过 = 最终验收完成**;任一不通过则回到对应里程碑修复。

---

## 10. 风险与对策

| 风险 | 对策 |
| :--- | :--- |
| Carla 画布为 Python/Qt4-5 实现,行为语义迁移 | 逐项对照 `patchcanvas/*.py`(Scene/Box/Port/Line/动画/主题)列行为清单(B3 前完成) |
| 大图性能(255+ 节点) | 暂停 LOD 动画、隐藏时暂停刷新(UI 库节流)、`QGraphicsView` 视口优化、必要时项缓存 |
| JACK 图变化高频 | `jack_set_graph_order_callback` + 合并定时刷新(≥30ms)而非每事件全量重建 |
| metadata API 缺失 | `Audio::JackApi` 增量补充 `jack_get_property/set_property/jack_uuid` 系列(动态加载函数表) |
| 中文字符/系统字体差异 | 不硬编码字体名,统一 `QFontDatabase::systemFont`;QSS 使用 font-family: inherit |
| 与现有 `Patchbay` 模块命名冲突(规范 §4 的 mod_patchbay 计划) | 本计划即该模块的开发计划,后续以此为准(规范引用保持一致) |
| 后续 SF 专用 Internal 模块的依赖与许可 | 选型时单独评估并记录许可；不将外部插件格式运行时引入 `patchbay_plugins.dll` |
| 客户端/端口 UUID 不稳定或第三方客户端无 UUID | 四级回退匹配;无法匹配时保留「缺失连接」记录并提示,不覆盖用户显式断开 |
| 重连时 JACK 端口短暂未就绪 | 100ms/500ms 退避重试;`jack_connect` 幂等(`-EEXIST` 忽略);重试仅限 UI 线程 |
| **扩展贴图缺失/损坏** | `ThemeAssets` 缺失回退自绘外观;贴图加载失败记录日志并继续(不崩溃);提供默认 assets 完整性检查 |
| 贴图 DPI/缩放 | 贴图提供 @2x(`devicePixelRatio`);无则 Qt 缩放;9-slice 用 QSS `border-image` 分片,保证任意尺寸 |
| VST 占位导致用户误解 | 占位项明确禁用文案,并在 README/计划书标注依赖 VST 计划 |

---

## 11. 后续衔接

1. **VST 加载**:《WinJACKNexus - VST Host - 开发计划》详写 VST2/VST3 加载、编辑器嵌入(HWND)与节点渲染,届时替换本计划 §6.3 占位;
2. **Audio Adapter / Mixer / Meterbridge / Switch**:以其各自的 `INode` 实现注册 JACK 端口,接线盘自动显示为节点(无需改动 patchbay);Mixer/Switch 后续分别实现 `saveState/loadState`,其场景数据写入 `.wjnproj` 的 `mixers[]` / `switches[]`(已占位);
3. SF 系列能力仅通过后续专用 Internal 模块提供；Carla 的 OSC/远程控制仍不在本计划范围。

---

## 12. 扩展阶段计划:子图(Sub Graph)(追加,2026-09-06)

> 说明:本扩展**追加**于现有接线盘计划,当前 B1–B8 进度保持现状不变;子图功能**复用当前主 Patchbay 实现**(Canvas/Scene/Core 模型/存档/连线交互),仅增加“子图上下文”与端口提升机制。

### 12.1 概念

- **子图 = 一个子 Patchbay 节点(Sub Graph 节点)**:主图中是一个节点卡;**双击 → 在新标签页打开子图 Patchbay**(主模块窗口采用标签页/多文档视图,或按设置使用独立窗口);
- **可无限嵌套**:子图内部可再放置 Sub Graph 节点,双击继续打开下一层子图(树形层级,无深度限制)。

### 12.2 子图内节点列表(唯一白名单)

| 节点 | 说明 |
| :--- | :--- |
| **Sub Graph Audio In** | 子图音频输入端(对应子图节点卡的输入引脚);**名称可自定义** |
| **Sub Graph Audio Out** | 子图音频输出端;**名称可自定义** |
| **Sub Graph MIDI In** | 子图 MIDI 输入端;**名称可自定义** |
| **Sub Graph MIDI Out** | 子图 MIDI 输出端;**名称可自定义** |
| **Sub Graph**(子节点) | **默认 2 进 2 出 + 1 个 MIDI 输入**,端口**可自由增减**(音/音频 in/out、MIDI in/out 可加可删);用于无限嵌套 |

- **接口名称可自定义**:Sub Graph 的 Audio/MIDI In/Out 接口节点支持**改名**(右键/参数栏重命名),端口显示名与别名随自定义名同步(真实 JACK 名保持规范);
- 子图节点列表**仅提供上述 5 类节点**;其余分类隐藏(含 Windows Native adapter、jackclient,见 12.3)。

### 12.3 分类隐藏策略

- **子图节点列表自动隐藏「Windows Native adapter」与「jackclient」两个分类**(设备/外部客户端类,主图专属)——避免子图误接系统设备;子图只通过 Sub Graph 接口与外部世界互通;
- **主图默认隐藏 Sub Graph 分类下的 `sub*` 接口**:主接线盘中 Sub Graph 节点卡**不渲染**其 audio/midi 进出引脚(与 VST Chain 端口同策略;端口在 JACK 图中真实存在,经别名可连);在子图内部(该子图的标签页)则**始终显示并可管理**。

### 12.4 实现(复用主 Patchbay)

- 以“**场景上下文(Scene Context)**”复用现有实现:主图 / 各子图共享 `PatchbayCore` 模型、`PatchbayCanvas`、连线/分组/搜索/存档逻辑;差异仅为:
  1. 节点列表(子图=白名单 §12.2);
  2. 显示策略(主图隐藏 sub\* 引脚);
  3. 标签页管理(QTabWidget:主图 + 各子图;双击 Sub Graph 节点打开/聚焦对应标签);
- **端口模型**:每个 Sub Graph 节点 = 一个 JACK 客户端端口组(默认 `in_1..2`、`out_1..2`、`midi_in_1`;增删后同步 `jack_port_register/unregister`,客户端名规则同主节点);接口端口**显示名/别名随自定义名**(用户可改),真实 JACK 名保持规范;
- **嵌套(端口提升/透传)**:内层 Sub Graph 的接口端口经外层子图汇聚为同一客户端命名空间(`sub_*` 前缀),保证从主图看整棵子树就是一个端口组;层级信息记录在 `PatchbayCore::TreeModel`(父子引用)。

### 12.5 存档(.
wjnproj)

- 顶级新增 **`subgraphs[]`**(树形):节点(含嵌套引用)、各层连接、布局、端口增减记录;主图 `connections[]` 引用 sub 端口(带层级路径);加载时递归恢复各标签页与连接。

### 12.6 里程碑(扩展阶段)

| 里程碑 | 内容 | 预计 | 验收 |
| :--- | :--- | :--- | :--- |
| **S1 子图基础（已完成）** | 场景上下文抽象 + 标签页视图(双击 Sub Graph 节点打开子图标签)| 3~5 天 | 主图加 Sub Graph 节点并双击打开子图标签页 |
| **S2 接口节点（已完成）** | Sub Graph Audio/MIDI In/Out 节点 + 端口注册 | 3~5 天 | 子图内接口节点可与内部节点连线 |
| **S3 嵌套（已完成）** | Sub Graph 节点(默认 2 进 2 出 + 1 MIDI,端口可增删)+ 无限嵌套/端口提升 | 5~7 天 | 多层嵌套;端口从外部可见可连 |
| **S4 隐藏策略（已完成）** | 子图列表隐藏 adapter/jackclient 分类;主图隐藏 sub\* 引脚 | 2~3 天 | 子图列表无设备分类;主图 Sub Graph 卡不显示接口 |
| **S5 存档** | `subgraphs[]` 树形存档/恢复 + 自动加载 | 3~4 天 | 工程保存/恢复子图树与连接 |

**S1 实施状态（2026-09-06）**：已完成 `PatchbaySceneContext` 场景上下文、主图与子图复用独立 `PatchbayCanvas` 的 `QTabWidget` 标签页视图。主图节点库提供 `Sub Graph` 节点，支持双击创建、拖放创建；双击画布中的 Sub Graph 节点会打开或聚焦对应子图标签页，子图标签页只展示 Sub Graph 节点分类，且支持继续打开子图上下文。S1 不涉及 S2 的接口节点/端口注册、S3 的端口提升与深层连接、S5 的 `subgraphs[]` 工程存档。

**S2 实施状态（2026-09-06）**：已完成子图 `Sub Graph Audio In`、`Sub Graph Audio Out`、`Sub Graph MIDI In`、`Sub Graph MIDI Out` 四类接口节点。子图节点库仅在子图上下文提供这四类接口项；接口节点支持双击创建和拖放创建，画布使用稳定上下文 ID 映射接口端点。每个子图注册为单一 JACK client：在其他 JACK graph 中呈现为一个大节点，只公开 `audio_in_1`、`audio_out_1`、`midi_in_1`、`midi_out_1` 等纯净端口名；子图画布内的 `In` 到 `Out` 连线维护该 client 的实时 Audio/MIDI 路由表，不再公开内部 bridge 端口。子图内部 `In` 作为逻辑输出、`Out` 作为逻辑输入，外部主图投影为 `In = JACK input` 并显示在节点左侧、`Out = JACK output` 并显示在节点右侧。相同类型接口允许重复创建，默认以 `... In 1`、`... In 2`、`... In 3` 等连续编号，JACK 注册名也基于相同序号保持唯一。接口节点支持右键改名，名称作为端口显示别名同步刷新；接口端口随模块窗口生命周期注销。新增 Patchbay Core 回归测试覆盖四类端口注册、类型/方向识别和真实 JACK 连接；Debug 编译零警告零错误，CTest `8/8` 通过。S2 不涉及 S3 的端口提升与可变端口集合、S4 的主图隐藏策略或 S5 的 `subgraphs[]` 工程存档。

**S3 实施状态（2026-09-07）**：已完成无限嵌套与端口提升。新建子图首次打开时自动创建两个 Audio In、两个 Audio Out 和一个 MIDI In；接口仍可在子图内自由增减。每个最外层子图仅注册一个真实 JACK client，嵌套子图接口作为该根子图路由图中的虚拟端点，实时 Audio/MIDI 回调沿逻辑连接穿透多层嵌套并转发到可达的真实输出端口，因此外部 JACK 客户端只看到最外层子图。接口创建与重命名使用用户指定名称作为 JACK 端口名；关闭标签仅隐藏并缓存画布，重新打开会保留嵌套节点、逻辑连接和布局。

**S4 实施状态（2026-09-07）**：已完成子图上下文的节点库白名单与主图隐藏策略。子图上下文仅列出 Sub Graph 及四类 Sub Graph Interface，不显示 `jackclient`、Windows Native adapter 或其他模块分类；主图仅显示 Sub Graph 节点卡，不渲染其提升接口引脚。最外层子图的接口仍由 JACK 图向外部客户端公开，并可在子图标签页内管理。

> 每里程碑编译通过即可交付,行为由用户手动验证。

### 12.7 验收要点

1. 主图添加 Sub Graph 节点(默认 2 进 2 出 + 1 MIDI);双击 → 新标签页打开子图 Patchbay;
2. 子图节点列表仅显示 Sub Graph 五类节点(adapter/jackclient 分类自动隐藏);
3. 主图 Sub Graph 节点卡默认隐藏 sub\* 接口;
4. 子图内接口增减、**自定义改名**(Audio/MIDI In/Out 名称可改,显示名/别名同步)实时生效;内层 Sub Graph 可再嵌套(无限);
5. 存档往返:嵌套树、端口、连接全部恢复。

---

*本计划书随开发反馈迭代,修改后递增版本号。*
