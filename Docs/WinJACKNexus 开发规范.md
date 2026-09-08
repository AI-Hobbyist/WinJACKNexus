# WinJACKNexus 架构改造与模块化演进规范 (v3.1)

> **v3.1 变更(2026-09-02)**:接口命名空间 `WinJACKNexus::Plugin` 更名为 **`WinJACKNexus::Node`**(`IModulePlugin` → `INode`);节点 DLL 目录由 `modules/` 调整为**主程序目录下**的 `nodes/internal/`(自带节点)与 `nodes/custom/`(第三方节点);接口桩库 `plugin.dll` → `node.dll`。
>
> **v3.0 变更(2026-09-02)**:引入 **“万物皆为节点”** 拓扑模型——接线盘是所有 JACK 客户端的总配线盘;调音台/电平表为“节点 + 独立窗口”;VST/VST3 采用独立后台进程,插件崩溃仅使对应节点旁通;主进程增加基础托盘占位菜单(接线盘 / 调音台 / 电平表桥 / 信号切换开关);DAW 桥接插件维持现状。

## 1. 核心架构哲学：kX Project 式沙盒、节点化拓扑与高稳定主内核

为兼顾**极低延迟**、**极致稳定性**与**灵活的拓展性**，WinJACKNexus 采用 **“1 个核心主进程（DLL 插件容器） + N 个沙盒 VST 宿主进程”** 的架构[cite: 3]，并以 **“万物皆为节点”** 为全局拓扑模型：

- **主进程 (`WinJACKNexus.exe`)**：托盘守护进程 + 模块/窗口管理器[cite: 3]。负责加载高度稳定的内置功能模块（DLL），处理主菜单、托盘菜单与窗口调度[cite: 3]。
  - **基础托盘实现(占位菜单)**：主进程启动即显示系统托盘图标，右键菜单包含 **接线盘 / 调音台 / 电平表桥 / 信号切换开关** 四个占位入口；随着模块 DLL 动态注入，占位项逐步替换/扩展为真实菜单项（见 §3.0）。
- **万物皆为节点(Node-based Topology)**：**接线盘(Patchbay)是所有 JACK 客户端的总配线盘**——系统内一切可路由单元（自家模块、VST 宿主节点、DAW 桥插件、外部 JACK 客户端与系统端口）都以**节点**形式出现在接线盘画布上，端口即节点引脚，连线即 JACK 连接。节点按呈现方式分两类：
  - **独立窗口节点**：调音台节点、电平表节点——画布上是单个节点，双击打开对应**独立窗口**。电平表遵循 **“1 模块 = 1 节点 = 1 独立详情窗口”**：添加几个电平表模块，就有几个电平表节点与对应详情窗口。
  - **纯画布节点**：MIDI Splitter、16×16 MIDI Matrix、Signal Switch、VST/VST3 宿主、音频适配器以及一切外部 JACK 客户端，直接在接线盘上连线路由。
- **内置节点 DLL (`nodes/<类型>/*.dll`)**：包含 Adapter、Mixer、Meterbridge 等（**接线盘为主模块，与主程序同级**），运行在主进程空间内，**零 IPC 开销**，通过 Native JACK API 进行音频/MIDI 连线[cite: 3]。
- **节点按类型分文件夹（`nodes/adapter|mixer|meter|switch|midi|vst|carla/`）**：Carla 模块与后续自带模块统一放 `nodes/` 下按功能类型分文件夹；第三方开发者节点放置于对应类型文件夹（或新建类型文件夹），与内置节点走同一 `WinJACKNexus::Node` 接口与扫描机制，无需修改主程序即可加入（见 §3.1）。
- **沙盒 VST 宿主 (`VSTProcess32.exe` / `VSTProcess64.exe`)**：基于原生 C++/Win32 与 VST SDK 的独立**后台进程**，专用于托管不稳定或第三方的 VST3/VST 插件[cite: 3]。每个 VST/VST3 插件对应接线盘上一个 **VST 节点**；即使插件崩溃，也仅影响对应节点——该节点自动进入 **旁通(Bypass)** 状态（直通/静音由节点策略决定），主套件与系统音频流 **100% 不会崩溃**，节点可一键重启恢复[cite: 3]。
- **全局路由拓扑规范(Patchbay Core)**：
  - **模块连线全覆盖**：系统内**所有功能模块**（包括 Adapter、调音台、电平表、Signal Switch、VST Host 节点以及 DAW2Jack 桥接插件）**必须严格暴露标准 Native JACK Audio/MIDI Port**[cite: 3]。
  - **Patchbay 总配线盘**：`mod_patchbay.dll` 作为全部 JACK 客户端的可视化配线中枢，对所有节点暴露的 JACK 端口进行无死角可视化连线、断开与拓扑管理（含外部 JACK 客户端）[cite: 3]。
- **架构范式**：全套桌面模块统一采用 **Qt 前端 + Native C++ 音频引擎** 的混合架构[cite: 3]。

---

## 2. 公共基础库与命名空间规范 (Common & Namespaces)

为了实现模块解耦与代码复用，引入 `common` 静态/动态基础库。全套项目代码严格遵循统一的命名空间层级：

### 2.1 命名空间划分
- `WinJACKNexus::Common`：跨模块复用的基础工具类、数据结构与无锁环形缓冲区。
- `WinJACKNexus::UI`：Qt 前端公共组件、主题/QSS 管理器、自绘控件基类。
- `WinJACKNexus::Audio`：Native JACK 封装与 Audio Thread 安全容器。
- `WinJACKNexus::Node`：DLL 节点抽象接口 (`INode`) 与 C-API 导出规范。

### 2.2 Common 基础库核心职责
- **无锁 SPSC Queue**：提供统一的实时无锁数据传输容器（基于 `farbot::RealtimeObject` / `boost::lockfree::spsc_queue`）[cite: 3]。
- **主题管理器 (`ThemeManager`)**：
  - 支持动态加载与实时切换外部 `.qss` 样式表。
  - 提供全局主题 QSS 变量替换引擎（如自定义暗黑机架风、明亮调音台风）。
  - 支持图标与 UI 颜色的全局 Theme Palette 绑定。
- **窗口句柄与 UI 绑定工具 (`NativeWindowUtils`)**：提供 Windows `HWND` 句柄在 Qt `QWidget` 与第三方 VST 编辑器容器间的通用嵌入与 DPI 缩放适配逻辑[cite: 3]。

---

## 3. 循序渐进开发与菜单动态加载机制 (Dynamic Menu Registration)

主进程外壳支持 **“按需加载，热插拔菜单”**[cite: 3]。主界面顶部菜单栏（`QMenuBar`）和托盘右键菜单（`QSystemTrayIcon`）不硬编码任何具体子窗口入口，而是暴露统一的模块注册接口[cite: 3]。

### 3.0 基础托盘实现（占位菜单）

- 主进程启动即创建 `QSystemTrayIcon`，右键菜单包含四个**占位入口**：
  - **接线盘** → 打开 `mod_patchbay.dll` 总配线盘窗口（核心视图）；
  - **调音台** → 打开调音台节点详情窗口；
  - **电平表桥** → 打开电平表节点集合窗口/各电平表详情窗口；
  - **信号切换开关** → 打开 Signal Switch 控制窗口。
- 占位项在对应节点 DLL 尚未注册时显示为“敬请期待/禁用”；节点注册后由动态注入机制替换为真实菜单项。

### 3.1 节点部署目录结构（主程序目录）

```
WinJACKNexus/
├─ WinJACKNexus.exe              # 主程序
├─ WinJACKNexus-VSTHost.exe      # 沙盒 VST 宿主（后续）
├─ mod_patchbay.dll              # 接线盘主模块（主 DLL，与主程序同级）
├─ patchbay_core.dll             # 接线盘依赖库（同目录）
├─ patchbay_plugins.dll          # 插件运行时：Carla 模块（Internal/LADSPA/DSSI/LV2/JSFX/Sound Kits）
├─ common.dll / ui.dll / audio.dll / node.dll   # 基础库与 Qt 运行时
└─ nodes/                        # 节点（Carla 模块 + 后续自带模块），按类型分文件夹
   ├─ adapter/                   # mod_adapter.dll（Audio Adapter）
   ├─ mixer/                     # mod_mixer_basic.dll / mod_mixer_pro.dll（调音台）
   ├─ meter/                     # mod_meterbridge.dll（电平表桥）
   ├─ switch/                    # mod_switch.dll（信号切换）
   ├─ midi/                      # MIDI Splitter / 16×16 Matrix
   ├─ vst/                       # VST Host 沙盒相关（后续）
   └─ carla/                     # 来自 Carla 的模块：插件加载器与插件库（lilv/ysfx/sfzero/FluidSynth 依赖）
```

- **patchbay 主模块**：接线盘是全局配线中枢，**与主程序同级**部署（主 DLL + 依赖库同目录）；
- **nodes/<类型>/**：节点按功能类型分文件夹存放（adapter / mixer / meter / switch / midi / vst / carla …），主程序启动时**递归扫描 `nodes/`** 全部子文件夹加载节点 DLL；
- 第三方开发者节点：放置于 `nodes/` 下对应类型文件夹（或新建类型文件夹），遵循 `WinJACKNexus::Node` C-API 规范即可被加载（无需修改主程序）。

### 模块注册与菜单联动流程：
1. **节点扫描**：主进程启动时**递归扫描**主程序目录下 `nodes/`（按类型分文件夹：adapter / mixer / meter / switch / midi / vst / carla …）中的动态库（或在运行时动态加载新 DLL）[cite: 3]。
2. **导出接口**：每个节点 DLL 导出标准的 C 函数接口（定义在 `WinJACKNexus::Node::INode`）：
   - `getNodeName()`：获取节点名称（如 `"Interactive Patchbay"`）[cite: 3]。
   - `getCategory()`：获取菜单分类（如 `"视图(V)"`、`"路由(R)"` 或 `"工具(T)"`）[cite: 3]。
   - `createWindow()`：实例化该节点对应的 `QMainWindow` / `QDialog`[cite: 3]。
3. **动态注入**：主进程解析 DLL 后，自动在主菜单对应的分类下追加该节点的打开/隐藏菜单项（`QAction`），绑定快捷键并注入全局 QSS 主题[cite: 3]。
4. **渐进式发布**：开发者或用户添加新节点 DLL（按类型放入 `nodes/<类型>/` 文件夹），系统再次启动时会自动更新菜单项，无需重新编译主程序外壳[cite: 3]。

---

## 4. 全模块架构选型与分层部署矩阵

> **架构提示**：项目全部采用 **Qt + Native C++** 混合架构。**万物皆为节点**：所有模块均必须注册 JACK 端口并以节点形式出现在总配线盘上，支持由 Patchbay 全局路由[cite: 3]。

| 模块名称 | 部署形式 | 前端 (Frontend / UI) | 后端/逻辑 (Backend & Logic) | 音频引擎 (Audio Engine) | 选型与改造理由 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **主总控与托盘守护<br>(Tray & Daemon)** | **主程序 (`.exe`)** | **Qt6** (`QSystemTrayIcon` 托盘占位菜单：接线盘/调音台/电平表桥/信号切换开关 + `QMenuBar` + QSS 主题控制) | **C++20** (DLL 动态扫描注入、Scene 预设解析、全局热键) | **Native JACK C API** (全局连接拓扑守护) | 混合架构。负责全局 UI 状态调度、托盘占位菜单、全局主题样式分发与 DLL 模块动态加载[cite: 3]。 |
| **音频适配器<br>(Audio Adapter)** | **核心节点 DLL (`nodes/adapter/mod_adapter.dll`)** | **Qt6** (标准设置窗口 / 设备与 Buffer 列表) | **C++20** (配置持久化、采样率与 Buffer 状态绑定) | **Native JACK C API** (导出 System Input/Output 端口) | 混合架构。**常驻画布节点**，暴露物理/虚拟设备端口，作为系统设备节点出现在总配线盘上供 Patchbay 和其他节点接线[cite: 3]。 |
| **总配线盘<br>(Interactive Patchbay)** | **主模块 DLL（与主程序同级 `mod_patchbay.dll` + `patchbay_core.dll` + `patchbay_plugins.dll`）** | **Qt6** (`QGraphicsView` 画布，QSS 自定义节点/连线风格；**所有 JACK 客户端节点**的总配线盘) | **C++20** (聚合画布拓扑计算，内置 MIDI Splitter / Matrix 节点逻辑，节点⇆独立窗口映射) | **JACK Graph API** (`jack_connect` / `jack_disconnect`) | 混合架构。**全局配线中枢**。显示所有 JACK 客户端（含外部客户端/系统端口），对节点端口进行图形化画线与路由调度；调音台/电平表节点双击打开独立窗口[cite: 3]。 |
| **调音台<br>(Mixer)** | **功能节点 DLL (`nodes/mixer/mod_mixer_basic.dll` / `mod_mixer_pro.dll`)** | **Qt6** (`QPainter` 自绘通道条、推子与 Pan，支持 QSS 换肤) | **C++20** (通道 Gain/Mute/Solo 状态机，SPSC 逻辑控制) | **Native JACK Audio** (导出通道 In/Out 端口) | 混合架构。**调音台节点 + 独立窗口**：画布上为 1 个节点，双击打开调音台详情窗口；所有通道抽象为标准 JACK 端口，可在配线盘任意插入[cite: 3]。 |
| **电平表桥<br>(Meterbridge)** | **功能节点 DLL (`nodes/meter/mod_meterbridge.dll`)** | **Qt6** (`QPainter` 60-120fps 高刷渲染，QSS 色彩表配置) | **WinJACKNexus::Common** (SPSC 环形缓冲区提取电平数据) | **Native JACK Audio RingBuffer** (导出 Monitor Input 端口) | 混合架构。**电平表节点 + 独立窗口**：1 节点 = 1 个独立详情电平表窗口；添加 N 个电平表节点即有 N 个详情窗口，可监听任意信号线 Peak/RMS[cite: 3]。 |
| **信号路由切换开关<br>(Signal Switch)** | **功能节点 DLL (`nodes/switch/mod_switch.dll`)** | **Qt6** (状态切换 Toggle，QSS 按钮动画) | **C++20** (指针交换 + 平滑 Crossfade 状态机) | **Native C++ Audio Switch Engine** (导出多路切换 In/Out 端口) | 混合架构。**纯画布节点**：暴露多路输入输出端口，通过平滑 Crossfade 在配线盘通道间实现零爆音平滑切换[cite: 3]。 |
| **VST/VST3 宿主节点<br>(Single VST Host)** | **沙盒进程 (`.exe` 于主程序目录) + 画布 VST 节点** | **Qt6** (原生 `HWND` 嵌入容器) | **C++20** (IPC 通信处理、插件预设/参数映射、**旁通/重启状态机**) | **Native VST2/VST3 SDK** (导出机架 In/Out/MIDI 端口) | 混合架构。**独立后台进程**加载插件；插件崩溃 → 对应 **VST 节点自动旁通**（端口保持注册），节点可一键重启；机架输入输出完全暴露给配线盘图形化接线[cite: 3]。 |
### 4.1 节点化拓扑模型（Node Model）

> **“万物皆为节点”**——配线盘画布即拓扑视图，所有 JACK 客户端（内部模块、外部客户端、系统端口）均以节点呈现。

| 节点类型 | 画布表现 | 独立窗口 | 生命周期 / 异常策略 |
| :--- | :--- | :--- | :--- |
| **适配器节点** (Adapter) | 常驻节点：`system:capture_*` / `system:playback_*` | 右键 → 设置（设备 / Buffer） | 设备热插拔 → 端口重建 |
| **调音台节点** (Mixer) | 1 节点，通道 In/Out 引脚 | **双击打开调音台详情窗口** | 参数经 SPSC 无锁下发 |
| **电平表节点** (Meter) | 1 模块 = 1 节点（Monitor In） | **1 节点 = 1 独立详情电平表窗口**（添加 N 个模块 → N 个窗口） | SPSC 电平数据；窗口隐藏暂停渲染 |
| **信号切换节点** (Signal Switch) | 纯画布节点（多路 In/Out） | —（可选小浮窗） | Crossfade 平滑切换 |
| **MIDI 节点** (Splitter / 16×16 Matrix) | 纯画布节点（MIDI In/Out） | — | 纯 MIDI 处理，无窗口 |
| **VST/VST3 节点** | 画布节点（机架 In/Out/MIDI） | 双击打开插件编辑器（HWND 嵌入，沙盒进程内） | **独立后台进程**；插件崩溃 → **节点旁通**，端口保持注册，可一键重启 |
| **外部 JACK 客户端** | 实时出现在总配线盘，可接线 | — | 客户端退出 → 节点消失 |

- **节点 = JACK 客户端 + 元数据**：每个节点绑定一个（或一组）JACK 端口与描述信息（name / category / type）；配线盘监听 `jack_set_graph_order_callback` 等拓扑事件实时增删节点。
- **独立窗口规则**：仅**调音台**与**电平表**两类节点拥有独立详情窗口；窗口随节点创建/销毁，隐藏时按 §5.3 暂停渲染。
- **VST 节点旁通**：VST/VST3 插件运行在独立后台进程；进程异常退出时节点状态置为 **Bypass**（输入直通输出或按节点配置静音），端口保持注册，点击节点即可重启插件进程恢复。
---

## 5. 线程隔离、主题渲染与数据同步规范

1. **实时性与无锁隔离**：音频线程（JACK Audio Callback / VSTProcess 处理回调）与 Qt UI 渲染线程之间**严禁使用任何互斥锁（Mutex）**[cite: 3]。统一使用 `WinJACKNexus::Common` 内提供的无锁 SPSC 环形缓冲区传输电平与控制信号[cite: 3]。
2. **QSS 主题与样式响应**：
   - 所有自定义 Qt 控件必须支持 Qt 样式表 (QSS) 属性选择器。
   - 主主题切换时，由 `WinJACKNexus::UI::ThemeManager` 发送 `onThemeChanged` 信号，各 DLL 模块对应的窗口平滑更新样式，无需重启 APP。
   - **主题文件规范**：全部主题样式**集中到单一 `style.css`**(QSS 兼容)，不允许代码硬编码颜色/字体/图片路径；可通过**可选扩展贴图包**(主题目录 `assets/`)实现完全换肤（一个 CSS + 多个贴图），**覆盖主程序、全部自带模块窗口与节点详情窗**；未提供扩展贴图时，控件**默认为自绘组件外观**（`PaintableWidget` 等，扁平化样式）。
3. **窗口按需渲染**：当某个模块窗口（如 Patchbay 或 Meterbridge）最小化或隐藏时，必须自动暂停 Qt UI 线程的 `QTimer` / `paintEvent` 刷新循环，确保后台静默运行时的 CPU 占用接近于 0[cite: 3]。