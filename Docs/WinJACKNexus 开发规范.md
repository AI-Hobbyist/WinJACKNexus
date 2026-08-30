# WinJACKNexus 架构改造与模块化演进规范 (v2.0)

## 1. 核心架构哲学：kX Project 式沙盒与高稳定主内核

为兼顾**极低延迟**、**极致稳定性**与**灵活的拓展性**，WinJACKNexus 采用 **“1 个核心主进程（DLL 插件容器） + N 个沙盒 VST 宿主进程”** 的架构[cite: 3]：
- **主进程 (`WinJACKNexus.exe`)**：托盘守护进程 + 模块/窗口管理器[cite: 3]。负责加载高度稳定的内置功能模块（DLL），处理主菜单与窗口调度[cite: 3]。
- **内置功能模块 (`modules/*.dll`)**：包含 Adapter、Patchbay、Mixer、Meterbridge 等，运行在主进程空间内，**零 IPC 开销**，通过 Native JACK API 进行音频/MIDI 连线[cite: 3]。
- **沙盒 VST 宿主 (`WinJACKNexus-VSTHost.exe`)**：基于 JUCE 引擎驱动的独立进程，专用于托管不稳定或第三方的 VST3 插件[cite: 3]。即使插件崩溃，也仅影响该沙盒进程，主套件和系统音频流 **100% 不会崩溃**[cite: 3]。
- **全局路由拓扑规范（Patchbay Core）**：
  - **模块连线全覆盖**：系统内**所有功能模块**（包括 Adapter、Basic/Pro Mixer、Meterbridge、Signal Switch、VST Host 机架以及 DAW2Jack 桥接插件）**必须严格暴露标准 Native JACK Audio/MIDI Port**[cite: 3]。
  - **Patchbay 全局接线盘**：`mod_patchbay.dll` 作为全系统的可视化连线中枢，可以对上述所有模块暴露的 JACK 端口进行无死角的可视化连线、断开与拓扑管理[cite: 3]。
- **架构范式**：除了跑在第三方 DAW 进程内的桥接插件采用纯 JUCE 架构外，其余全套桌面模块**统一采用“Qt 前端 + Native C++/JUCE 音频引擎”的混合架构**[cite: 3]。

---

## 2. 公共基础库与命名空间规范 (Common & Namespaces)

为了实现模块解耦与代码复用，引入 `common` 静态/动态基础库。全套项目代码严格遵循统一的命名空间层级：

### 2.1 命名空间划分
- `WinJACKNexus::Common`：跨模块复用的基础工具类、数据结构与无锁环形缓冲区。
- `WinJACKNexus::UI`：Qt 前端公共组件、主题/QSS 管理器、自绘控件基类。
- `WinJACKNexus::Audio`：Native JACK 封装、JUCE DSP 包装器、Audio Thread 安全容器。
- `WinJACKNexus::Plugin`：DLL 模块抽象接口 (`IModulePlugin`) 与 C-API 导出规范。

### 2.2 Common 基础库核心职责
- **无锁 SPSC Queue**：提供统一的实时无锁数据传输容器（基于 `farbot::RealtimeObject` / `boost::lockfree::spsc_queue`）[cite: 3]。
- **主题管理器 (`ThemeManager`)**：
  - 支持动态加载与实时切换外部 `.qss` 样式表。
  - 提供全局主题 QSS 变量替换引擎（如自定义暗黑机架风、明亮调音台风）。
  - 支持图标与 UI 颜色的全局 Theme Palette 绑定。
- **窗口句柄与 UI 绑定工具 (`NativeWindowUtils`)**：提供 Windows `HWND` 句柄在 Qt `QWidget` 与 JUCE `AudioPluginInstance` 容器间的通用嵌入与 DPI 缩放适配逻辑[cite: 3]。

---

## 3. 循序渐进开发与菜单动态加载机制 (Dynamic Menu Registration)

主进程外壳支持 **“按需加载，热插拔菜单”**[cite: 3]。主界面顶部菜单栏（`QMenuBar`）和托盘右键菜单（`QSystemTrayIcon`）不硬编码任何具体子窗口入口，而是暴露统一的模块注册接口[cite: 3]。

### 模块注册与菜单联动流程：
1. **模块扫描**：主进程启动时扫描 `modules/` 目录下的动态库（或在运行时动态加载新 DLL）[cite: 3]。
2. **导出接口**：每个模块 DLL 导出标准的 C 函数接口（定义在 `WinJACKNexus::Plugin::IModulePlugin`）：
   - `getModuleName()`：获取模块名称（如 `"Interactive Patchbay"`）[cite: 3]。
   - `getCategory()`：获取菜单分类（如 `"视图(V)"`、`"路由(R)"` 或 `"工具(T)"`）[cite: 3]。
   - `createWindow()`：实例化该模块对应的 `QMainWindow` / `QDialog`[cite: 3]。
3. **动态注入**：主进程解析 DLL 后，自动在主菜单对应的分类下追加该模块的打开/隐藏菜单项（`QAction`），绑定快捷键并注入全局 QSS 主题[cite: 3]。
4. **渐进式发布**：开发者或用户添加新 DLL（如 `mod_mixer_pro.dll`），系统再次启动时会自动更新菜单项，无需重新编译主程序外壳[cite: 3]。

---

## 4. 全模块架构选型与分层部署矩阵

> **架构提示**：除 **DAW 桥接插件** 为纯 JUCE 架构外，其余项目全部为 **Qt + Native C++/JUCE 混合架构**[cite: 3]。所有模块均必须注册 JACK 端口，支持由 Patchbay 全局路由[cite: 3]。

| 模块名称 | 部署形式 | 前端 (Frontend / UI) | 后端/逻辑 (Backend & Logic) | 音频引擎 (Audio Engine) | 选型与改造理由 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **主总控与托盘守护<br>(Tray & Daemon)** | **主程序 (`.exe`)** | **Qt6** (`QSystemTrayIcon` + `QMenuBar` + QSS 主题控制) | **C++20** (DLL 动态扫描注入、Scene 预设解析、全局热键) | **Native JACK C API** (全局连接拓扑守护) | 混合架构。负责全局 UI 状态调度、全局主题样式分发与 DLL 模块动态加载[cite: 3]。 |
| **音频适配器<br>(Audio Adapter)** | **核心 DLL (`mod_adapter.dll`)** | **Qt6** (标准设置窗口 / 设备与 Buffer 列表) | **C++20** (配置持久化、采样率与 Buffer 状态绑定) | **Native JACK C API** (导出 System Input/Output 端口) | 混合架构。常驻后台，暴露物理/虚拟设备端口供 Patchbay 和其他模块接线[cite: 3]。 |
| **模块化虚拟插线板<br>(Interactive Patchbay)** | **核心 DLL (`mod_patchbay.dll`)** | **Qt6** (`QGraphicsView` 画布，QSS 自定义节点/连线风格) | **C++20** (聚合画布拓扑计算，内置 MIDI Splitter / Matrix 节点逻辑) | **JACK Graph API** (`jack_connect` / `jack_disconnect`) | 混合架构。**全局接线盘控制中枢**。对系统中所有其他模块的暴露端口进行图形化画线与路由调度[cite: 3]。 |
| **基础调音台<br>(Basic Mixer)** | **功能 DLL (`mod_mixer_basic.dll`)** | **Qt6** (`QPainter` 自绘通道条、推子与 Pan，支持 QSS 换肤) | **C++20** (通道 Gain/Mute/Solo 状态机，SPSC 逻辑控制) | **Native JACK Audio / JUCE DSP Engine** (导出通道 In/Out 端口) | 混合架构。轻量推子调节，所有通道抽象为标准的 JACK 端口，支持在 Patchbay 中任意插入[cite: 3]。 |
| **专业调音台<br>(Pro Mixer)** | **功能 DLL (`mod_mixer_pro.dll`)** | **Qt6** (高密度高级数字调音台 UI，支持可缩放 QSS 皮肤) | **C++20** (Bus 路由表、Aux 发送矩阵、DCA 编组逻辑) | **JUCE Audio DSP Engine** (导出 Bus/Aux/Main 端口) | 混合架构。JUCE 提供高精度混音，多路 Bus/Aux/Surround 均暴露为 JACK Port 供 Patchbay 连线[cite: 3]。 |
| **多通道电平桥监控<br>(Meterbridge)** | **功能 DLL (`mod_meterbridge.dll`)** | **Qt6** (`QPainter` 60-120fps 高刷渲染，QSS 色彩表配置) | **WinJACKNexus::Common** (SPSC 环形缓冲区提取电平数据) | **Native JACK Audio RingBuffer** (导出 Monitor Input 端口) | 混合架构。暴露旁路监控端口，可在 Patchbay 中接到任意信号线上监听 Peak/RMS 数据[cite: 3]。 |
| **信号路由切换开关<br>(Signal Switch)** | **功能 DLL (`mod_switch.dll`)** | **Qt6** (状态切换 Toggle，QSS 按钮动画) | **C++20** (指针交换 + 平滑 Crossfade 状态机) | **Native C++ Audio Switch Engine** (导出多路切换 In/Out 端口) | 混合架构。暴露多路输入输出端口，通过平滑 Crossfade 在 Patchbay 通道间实现零爆音平滑切换[cite: 3]。 |
| **沙盒 VST 效果器机架<br>(Single/Chain VST Host)** | **沙盒进程 (`.exe`)** | **Qt6** (拖拽 Slot 机架外壳 + 原生 `HWND` 嵌入容器) | **C++20** (IPC 通信处理、插件预设/参数映射) | **JUCE `AudioProcessorGraph`** (导出机架 In/Out/MIDI 端口) | 混合架构。独立沙盒进程隔离 VST3 崩溃风险；机架输入输出完全暴露给 Patchbay 图形化接线[cite: 3]。 |
| **DAW 桥接 MIDI/音频插件<br>(DAW2Jack VST3)** | **第三方 DAW 插件 (`.vst3`)** | **纯 JUCE** (`juce::Component` 简易电平与状态 UI) | **纯 JUCE** (`juce::AudioProcessor` 逻辑控制) | **纯 JUCE + RingBuffer** (导出 DAW Bridge 端口) | **纯 JUCE 架构**。运行在第三方 DAW 进程内，在 JACK 系统中自动注册为 DAW 音频/MIDI 端口供 Patchbay 挂载[cite: 3]。 |

---

## 5. 线程隔离、主题渲染与数据同步规范

1. **实时性与无锁隔离**：音频线程（JACK Audio Callback / JUCE Process Block）与 Qt UI 渲染线程之间**严禁使用任何互斥锁（Mutex）**[cite: 3]。统一使用 `WinJACKNexus::Common` 内提供的无锁 SPSC 环形缓冲区传输电平与控制信号[cite: 3]。
2. **QSS 主题与样式响应**：
   - 所有自定义 Qt 控件必须支持 Qt 样式表 (QSS) 属性选择器。
   - 主主题切换时，由 `WinJACKNexus::UI::ThemeManager` 发送 `onThemeChanged` 信号，各 DLL 模块对应的窗口平滑更新样式，无需重启 APP。
3. **窗口按需渲染**：当某个模块窗口（如 Patchbay 或 Meterbridge）最小化或隐藏时，必须自动暂停 Qt UI 线程的 `QTimer` / `paintEvent` 刷新循环，确保后台静默运行时的 CPU 占用接近于 0[cite: 3]。