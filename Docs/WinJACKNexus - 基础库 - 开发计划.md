# WinJACKNexus - 基础库 - 开发计划

> 版本:v1.0
> 日期:2026-08-30
> 依据:«WinJACKNexus 开发规范» v2.0
> 范围:本阶段**完整实现** `WinJACKNexus::Common` / `WinJACKNexus::UI` / `WinJACKNexus::Audio` 三个基础库;`WinJACKNexus::Plugin` 及各功能模块**预留接口**,随开发进度逐步填充。本阶段编译产物为对应 DLL: `common.dll` / `ui.dll` / `audio.dll`(接口层 `plugin.dll` 为预留桩)。

---

## 1. 当前仓库现状

仓库目前只包含文档、参考实现与第三方依赖,尚无项目源码与构建脚本:

```
WinJACKNexus/
├─ .github/          # Copilot 指令等
├─ Docs/             # 开发规范
├─ Ref/Cadence/      # 参考实现(JACK2 时代 Cadence/Catia,Python+Qt 与 C++ Qt 参考)
└─ third_party/
   ├─ JACK2/         # JACK2 (含 jackd.exe、JackRouter、头文件与导入库)
   ├─ JUCE/          # JUCE 9.0.1
   ├─ vst2sdk/       # VST2 SDK
   └─ vst3sdk/       # VST3 SDK 3.8.x
```

本计划在保留第三方库的前提下,新增 `src/` 项目源码树与 CMake 构建系统,并以 3 个 DLL 作为本阶段交付物。

---

## 2. 现有依赖分析

### 2.1 依赖清单

| 依赖 | 版本 | 路径 | 用途 | 许可 |
| :--- | :--- | :--- | :--- | :--- |
| **Qt6 (msvc2022_64)** | 6.10.3 | `C:\Qt\6.10.3\msvc2022_64` | UI 库前端 (Widgets/Gui/Core),仅此一个 kit | LGPLv3 / 商业 |
| **Windows SDK** | 10 | `D:\Windows Kits\10` | `HWND` 嵌入、DPI、系统 API | MS EULA |
| **JACK2 头文件** | JACK2 (≥0.125) | `third_party/JACK2/include/jack/*.h` | Native JACK C API 封装 (`jack.h` `types.h` `transport.h` `session.h` `metadata.h` `ringbuffer.h` `midiport.h` 等) | LGPL-2.1 |
| **JACK 导入库 (x64/MSVC)** | — | `third_party/JACK2/lib/libjack64.lib` | 链接 `libjack64.dll` 导入符号 | — |
| **JACK 导入库 (MinGW)** | — | `third_party/JACK2/lib/libjack64.dll.a` | 备用 (MSVC 场景不使用) | — |
| **JACK 运行时 (x64)** | — | `C:\Windows\libjack64.dll` | 运行时客户端库(已安装,采用动态加载策略) | LGPL-2.1 |
| **JACK 运行时 (x86)** | — | `C:\Windows\libjack.dll` | 备用 (x86 场景) | LGPL-2.1 |
| **JACK 服务器** | — | `third_party/JACK2/jackd.exe` | 开发/测试用 JACK 守护进程 | LGPL-2.1 |
| **Windows Core Audio (WASAPI/MMDevice)** | SDK 10 | `D:\Windows Kits\10` | Adapter 数据源:设备枚举/UUID/声道数/采样率/共享模式音频流 | 系统 API |
| **WinMM MIDI (Win32)** | SDK 10 | `D:\Windows Kits\10` | 系统 MIDI 设备枚举/事件流/通道信息(Windows MIDI Services 预留为新后端) | 系统 API |
| **JackRouter (ASIO 驱动)** | — | `third_party/JACK2/jack-router/win64/JackRouter.dll` + `.ini` | **不采用**:本项目以 JACK 为核心,音频链路统一走 JACK 端口(系统设备由 WASAPI 共享模式接入),**不需要 ASIO 支持**;JackRouter 仅是 JACK2 自带的第三方附件,不参与本项目功能,亦不链接/分发 | LGPL-2.1 |
| **JUCE** | 9.0.1 | `third_party/JUCE` | `WinJACKNexus::Audio` 的 JUCE DSP 包装层、沙盒 VST Host、DAW2Jack 插件(本阶段仅 Audio 库可选引用) | AGPLv3 / 商业 |
| **VST 3 SDK** | 3.8.x | `third_party/vst3sdk` | 后续 DAW2Jack `.vst3` 插件与 VST Host 插件加载 | GPLv3 例外 / 商业 |
| **VST 2 SDK** | — | `third_party/vst2sdk` | 后续兼容 VST2 插件(预留) | 专有许可 |
| **Ref/Cadence** | — | `Ref/Cadence` | **API 范围与 UI 参考**:`src/jacklib.py` (ctypes 封装全量 JACK API)、`catia.py` (Patchbay UI)、`patchcanvas` (画布) | GPLv2 |

### 2.2 工具链约束

- 编译器:**MSVC 2022 (x64)**(由 Qt `msvc2022_64` kit 决定);
- C++ 标准:**C++20**;
- 构建:**CMake ≥ 3.25**(Qt 官方 CMake 支持);
- Qt 定位:`Qt6_DIR = C:\Qt\6.10.3\msvc2022_64\lib\cmake\Qt6`;
- 目标平台:Windows x64(主),x86 仅作为可选兼容目标。

### 2.3 参考实现要点 (Ref/Cadence)

`jacklib.py` 给出了 Native JACK API 的**完整功能面**,直接作为 `WinJACKNexus::Audio` 封装范围清单:

- 客户端:`jack_client_open/close`、`jack_get_sample_rate/get_buffer_size/set_buffer_size`、`jack_cpu_load`、`jack_set_freewheel`、回调 (`process` `buffer_size` `sample_rate` `shutdown` `graph_order` `freewheel` `latency`);
- 端口:`jack_port_register/unregister/get_buffer/name/short_name/flags/type/is_mine/connected/connected_to/get_connections/all_connections/tie/untie/set_name/alias/aliases/monitor`、`jack_get_ports/port_by_name`;
- 连接:`jack_connect/disconnect/port_disconnect`、`jack_port_set_latency`、`jack_recompute_total_latencies`、`jack_port_get_latency/get_total_latency`;
- MIDI:`jack_midi_get_event_count/event_get/event_write/clear_buffer/max_event_size`、`jack_midi_event_t`;
- 传输/会话/元数据:`jack_transport_*`、`jack_session_*`、`jack_set_property` 等(基础库先封装核心子集,传输/会话/元数据列为 V1.1 增量)。

---

## 3. 目标与范围

### 3.1 本阶段完整实现(编译产物 = DLL)

| 命名空间 | 目标 DLL | 依赖 | 说明 |
| :--- | :--- | :--- | :--- |
| `WinJACKNexus::Common` | `common.dll` | 仅 std C++20 + Windows API | 无锁队列、环形缓冲、实时对象、日志、元数据 |
| `WinJACKNexus::UI` | `ui.dll` | Qt6 Widgets + `common.dll` | ThemeManager、NativeWindowUtils、自绘控件基类、渲染节流 |
| `WinJACKNexus::Audio` | `audio.dll` | libjack64 + Windows Core Audio/WinMM(系统 API)+ `common.dll`(JUCE 层可选编译) | Native JACK C API 封装、WASAPI(共享)设备与音频流、系统 MIDI 设备与事件流、环缓冲、JUCE DSP 包装 |

### 3.2 本阶段预留接口(只定义契约,不实现业务)

| 命名空间 / 模块 | 预留内容 |
| :--- | :--- |
| `WinJACKNexus::Plugin` | `IModulePlugin` 抽象 + C-API 导出规范(见 §7),`plugin.dll` 仅含接口符号桩,供模块 DLL 实现 |
| 功能模块(Adapter / Patchbay / Mixer Basic/Pro / Meterbridge / Switch / VST Host / DAW2Jack) | 各自端口契约与生命周期约定(见 §7.2),本阶段**不创建源码**,仅登记接口 |
| 主进程 `WinJACKNexus.exe` | 模块扫描与动态菜单逻辑(依赖 Plugin 接口),本阶段不实现,仅约定 |

### 3.3 交付物

1. CMake 构建系统(根 + 各库);
2. `common.dll` / `ui.dll` / `audio.dll` / `plugin.dll`(接口桩)输出至 `bin/`;
3. 每个库的公开头文件(安装到 `include/WinJACKNexus/...`);
4. Common 单元测试 + Audio 冒烟测试脚本(仅开发期使用)。

---

## 4. 源码目录规划

```
WinJACKNexus/
├─ CMakeLists.txt                 # 顶层工程
├─ cmake/
│  ├─ FindJACK2.cmake             # 定位 JACK2 头文件/导入库,或动态加载探测
│  └─ WJNMacros.cmake             # 导出宏、dll 输出、版本脚本公共逻辑
├─ src/
│  ├─ common/                     # → common.dll
│  │  ├─ CMakeLists.txt
│  │  ├─ include/WinJACKNexus/Common/
│  │  │  ├─ Export.hpp            # WINJACK_COMMON_API
│  │  │  ├─ LockFreeSpscQueue.hpp
│  │  │  ├─ RealtimeObject.hpp
│  │  │  ├─ AudioRingBuffer.hpp
│  │  │  ├─ Atomic.hpp            # PaddedAtomic/AtomicFlag
│  │  │  ├─ Thread.hpp
│  │  │  ├─ Logger.hpp
│  │  │  ├─ Version.hpp
│  │  │  ├─ ScopedGuard.hpp
│  │  │  └─ Timer.hpp             # 高精度时间戳/CPU
│  │  └─ src/*.cpp
│  ├─ ui/                         # → ui.dll
│  │  ├─ CMakeLists.txt
│  │  ├─ include/WinJACKNexus/UI/
│  │  │  ├─ Export.hpp            # WINJACK_UI_API
│  │  │  ├─ ThemeManager.hpp
│  │  │  ├─ ThemePalette.hpp
│  │  │  ├─ NativeWindowUtils.hpp
│  │  │  ├─ PaintableWidget.hpp
│  │  │  └─ IconManager.hpp
│  │  └─ src/*.cpp
│  ├─ audio/                      # → audio.dll
│  │  ├─ CMakeLists.txt
│  │  ├─ include/WinJACKNexus/Audio/
│  │  │  ├─ Export.hpp            # WINJACK_AUDIO_API
│  │  │  ├─ JackClient.hpp
│  │  │  ├─ JackPort.hpp
│  │  │  ├─ JackMidiBuffer.hpp
│  │  │  ├─ JackRingBuffer.hpp
│  │  │  ├─ JackServerGuard.hpp
│  │  │  ├─ JackVersion.hpp
│  │  │  ├─ WasapiDevice.hpp      # WASAPI 设备枚举/信息 (MMDevice)
│  │  │  ├─ WasapiStream.hpp      # 共享模式 Capture/Render 流
│  │  │  ├─ WasapiFormat.hpp      # WAVEFORMATEXTENSIBLE ↔ JACK float32
│  │  │  ├─ MidiDevice.hpp        # 系统 MIDI 设备枚举/信息
│  │  │  ├─ MidiStream.hpp        # 输入/输出事件流 (WinMM)
│  │  │  ├─ MidiEvent.hpp         # 通道/消息类型解析
│  │  │  ├─ Resampler.hpp         # 采样率适配 (可选)
│  │  │  ├─ JuceDspWrapper.hpp    # 可选编译 (WJN_WITH_JUCE)
│  │  │  └─ AudioThreadSafe.hpp
│  │  └─ src/*.cpp
│  ├─ plugin/                     # → plugin.dll (接口桩,预留)
│  │  ├─ CMakeLists.txt
│  │  ├─ include/WinJACKNexus/Plugin/
│  │  │  ├─ Export.hpp            # WINJACK_PLUGIN_API
│  │  │  ├─ IModulePlugin.hpp     # C++ 虚接口
│  │  │  ├─ ModuleApi.h           # extern "C" 导出规范
│  │  │  └─ ModuleInfo.hpp
│  │  └─ src/ModuleApi.cpp        # 接口桩实现
│  └─ app/                        # (预留,本阶段不实现) → WinJACKNexus.exe
├─ bin/                           # 构建产物 (common/ui/audio/plugin.dll)
├─ tests/
│  ├─ common/                     # ctest 单元测试
│  └─ audio/                      # 冒烟脚本 (启动 jackd + 回环)
└─ Docs/
```

### 4.1 头文件与命名空间层级

```cpp
namespace WinJACKNexus::Common { ... }   // 无锁、实时安全容器、基础工具
namespace WinJACKNexus::UI { ... }       // Qt 前端公共组件
namespace WinJACKNexus::Audio { ... }    // Native JACK / JUCE DSP
namespace WinJACKNexus::Plugin { ... }   // IModulePlugin 与 C-API
```

---

## 5. WinJACKNexus::Common — 完整实现设计

> 依赖:仅 `std` C++20 + Windows API。**不依赖 Qt**、**不依赖 JUCE**,保证模块/主进程可独立复用。

### 5.1 无锁容器(音频线程安全核心)

| 类型 | 接口 | 说明 |
| :--- | :--- | :--- |
| `LockFreeSpscQueue<T, Capacity>` | `try_push(const T&)` / `try_pop(T&)` / `clear()` / `size()` | 模板化 SPSC 环形队列,基于 `std::atomic` 索引 + acquire/release 内存序;`intrusive` 可选重载;支持 `noexcept`。**禁止在音频线程阻塞**。 |
| `AudioRingBuffer<float>` | `write(const float*, size_t)` / `read(float*, size_t)` / `available()` / `clear()` | 字节/采样级 SPSC 环形缓冲(Meterbridge 电平数据通道、Adapter 桥接);内部用 `AudioRingBuffer` 语义,零分配。 |
| `RealtimeObject<T>` | `getRead()` / `getWrite()` / `set(T&&)` / `mutate` | 参考 `farbot::RealtimeObject` 的三槽模型(write / read / pending),音频线程仅读 `getRead()`,UI 线程经 `set()` 无锁换入新值;保证实时线程永不分配/等待。 |
| `PaddedAtomic<T>` `AtomicFlag` | — | 缓存行填充原子(cache-line padding),避免伪共享。 |

### 5.2 线程与性能

| 类型 | 说明 |
| :--- | :--- |
| `Thread` | 命名线程;`setRealtimePriority()`(Windows `AvSetMmThreadCharacteristics` / `SetThreadPriority`),音频线程优先级封装。 |
| `CpuClock` / `Timestamp` | `QueryPerformanceCounter` 封装,高精度时间戳;`cpuLoad()` 供 Meterbridge/DSP 统计。 |
| `ScopedGuard` | RAII 作用域守卫;`ScopeExit`。 |

### 5.3 基础工具

| 类型 | 说明 |
| :--- | :--- |
| `Logger` | `WJN_LOG_INFO/WARN/ERROR` 宏;SPSC 环形日志缓冲(UI 侧异步消费);`LogSink` 可扩展;默认输出到 `%LOCALAPPDATA%\WinJACKNexus\logs\`。 |
| `VersionInfo` | `major.minor.patch` + `git hash`;`toString()`;`fromString()`,用于模块版本协商。 |
| `ModuleMetadata` | 模块名/分类/版本/作者/描述/依赖,JSON 序列化(轻量自定义或 `nlohmann` 可选,默认手写小型 JSON 读写)。 |
| `PathUtils` | exe/module 目录定位(`GetModuleFileNameW`)、`%LOCALAPPDATA%` 布局、路径规范化。 |
| `win32::Window` 辅助 | 仅在需要时:`HWND` 封装、`GetWindowTextW`、消息常量(`WM_COPYDATA` 预留 IPC 基础,仅声明不实现)。 |

### 5.4 导出与 ABI

- 统一 `WINJACK_COMMON_API` 宏:`__declspec(dllexport/dllimport)`;
- 所有导出类使用 `final` + 显式 `export`(MSVC `WINDOWS_EXPORT_ALL_SYMBOLS` 不开启,避免意外导出);
- STL 类型不出现在 ABI 边界(以指针/跨度 API 暴露),保证 DLL 间 ABI 稳定。

### 5.5 里程碑与验收

- 实现顺序:容器 → 线程 → 时间 → 日志 → 元数据;
- 单元测试(`tests/common`):
  - SPSC 压力测试(1 生产者 + 1 消费者,≥10M 次,校验顺序与无丢失);
  - `RealtimeObject` 换入换出竞态(TSAN 下运行);
  - 环形缓冲读写边界(任意 size/wrap);
  - Logger 多线程写;
- 验收:`ctest` 全部通过,`TSAN`(debug 构建)无告警。

---

## 6. WinJACKNexus::UI — 完整实现设计

> 依赖:Qt6 Widgets/Gui/Core(`C:\Qt\6.10.3\msvc2022_64`)+ `common.dll`。供主进程与各功能模块 DLL 复用。

### 6.1 ThemeManager(主题/QSS 管理)

| 能力 | 说明 |
| :--- | :--- |
| 加载外部 `.qss` | `loadTheme(const QString& path)`;支持内置主题(默认暗色机架风 `dark-rack`,明亮调音台风 `light-console`)。 |
| 变量替换引擎 | QSS 内 `${varName}` 占位符 → `ThemePalette` 值注入(`--accent`、`--panel-bg` 等);`setVariable(key, value)` 支持运行时替换并重刷。 |
| 全局/窗口应用 | `applyToApp()`(QApplication 级 `setStyleSheet`)与 `applyToWidget(QWidget*)`;提供 `reload()`。 |
| 信号 | `signal onThemeChanged(const ThemePalette&)`;各模块窗口连接后平滑换肤,**无需重启**。 |
| 持久化 | 当前主题名 + 变量覆盖持久化到 `%LOCALAPPDATA%\WinJACKNexus\theme.ini`。 |

### 6.2 ThemePalette

- 颜色(`QColor`)键值集合(QSS 变量 + 图标着色基调);
- 图标绑定:主题名 → 图标目录(`bitmaps/`/`scalable/`),`icon(name, size)`;
- 与 `ThemeManager::onThemeChanged` 联动。

### 6.3 NativeWindowUtils(HWND ↔ Qt/JUCE 嵌入)

| 能力 | 说明 |
| :--- | :--- |
| `embedNativeWindow(HWND, QWidget* parent)` | 将外部原生窗口(如 JUCE `AudioProcessorEditor` 的 HWND、第三方 VST 编辑器)嵌入 Qt 容器:构造 `QWindow::fromWinId(hwnd)` 并 `QWidget::createWindowContainer`,处理父级关系与 Z 序。 |
| `obtainNativeHandle(QWidget*)` | `winId()` 反向获取 HWND(用于把 Qt 画布注册给 JACK 面板等)。 |
| DPI 适配 | Windows 10+ Per-Monitor V2:监听 `QScreen::logicalDotsPerInchChanged`,缩放 `setGeometry`;覆盖 `QEvent::DevicePixelRatioChange`。 |
| 嵌入生命周期 | `detachNativeWindow()`:销毁容器时同步 `SetParent(hwnd, nullptr)` 避免野指针;崩溃隔离策略(沙盒进程容器)由 VSTHost 模块负责。 |

### 6.4 自绘控件基类(渲染节流)

| 类型 | 说明 |
| :--- | :--- |
| `PaintableWidget : QWidget` | 支持 **QSS 属性选择器** 的基类(`setProperty("wjnRole", ...)`);统一的 `paintEvent` 骨架,控件子类只实现 `paintContent(QPainter&)`。 |
| `AnimatedWidget : PaintableWidget` | `QTimer` 驱动的动画帧管理;`setFrameRate(int)`。 |
| **按需渲染** | `setRenderingActive(bool)` + 重写 `showEvent/hideEvent/changeEvent`,窗口隐藏/最小化时自动 **暂停** `QTimer` 与 `paintEvent` 刷新 → 后台静默 CPU ≈ 0(规范 §5.3)。 |
| `MeterWidget` 基类 | 预留给 Meterbridge(meter/peak-hold/RMS 缓动),本阶段仅基类与渲染节流,不含业务。 |

### 6.5 导出与构建

- `WINJACK_UI_API` 导出宏;链接 `Qt6::Widgets` + `common.dll`;
- qrc 资源(atbase qss/图标)嵌入 `ui.dll`(`resources-ui.qrc`);
- **ABI 约定**:所有跨 DLL 传递的 Qt 对象参数一律使用指针,要求主进程与模块 DLL 使用同一 Qt 6.10.3 msvc2022_64(在构建脚本中强制校验 `Qt6_VERSION` 与 `QT_ARCH`)。

### 6.6 里程碑与验收

- 实现顺序:ThemeManager+Palette → PaintableWidget/节流 → NativeWindowUtils(含 DPI)→ 资源;
- 验收:主题切换 demo(纯 Qt 窗口 + 两套 QSS 实时切换);HWND 嵌入 demo(嵌入 `notepad` 或 JUCE 空编辑器);窗口隐藏后 CPU 探针(开发期自行验证)。

---

## 7. WinJACKNexus::Audio — 完整实现设计

> 依赖:libjack64(`C:\Windows\libjack64.dll`,采用 **运行时动态加载** 策略,见 §7.8)+ `common.dll`;WASAPI 与系统 MIDI 使用 Windows SDK 自带 API(无需第三方库)。JUCE DSP 包装层为**可选编译**(`WJN_WITH_JUCE`),默认关闭,确保 `audio.dll` 核心零 JUCE 依赖。

### 7.1 JackClient(客户端封装)

| 能力 | 说明 |
| :--- | :--- |
| 打开/关闭 | `open(name)` / `openExact(name)` / `openUnique(name)`;映射 `JackOptions`(`JackNoStartServer` 等);解析 `jack_status_t`(`JackNameNotUnique` → 自动追加后缀,`JackServerStarted/Failed` → 可配置)。 |
| Process 回调 | `std::function<void(const ProcessContext&)>`;`ProcessContext` 提供帧数、样本率、各已注册端口缓冲(音频 `float*` / MIDI `jack_midi_event_t` 列表)的**零拷贝访问**。 |
| 端口注册 | `registerAudioPort(name, {in|out})` / `registerMidiPort(name, {in|out})`;内部缓存 `jack_port_t*`,实时安全。 |
| 动态回调 | `setBufferSizeCallback` / `setSampleRateCallback` / `setLatencyCallback` / `setGraphOrderCallback` / `setFreewheelCallback`;`jack_set_process_callback`。 |
| 引擎信息 | `sampleRate()` / `bufferSize()` / `cpuLoad()` / `isRealtime()`;`setBufferSize()`、`setFreewheel()`,均从 UI 线程调用。 |
| 生命周期 | `shutdown` 回调(服务器退出)→ 自动标记 `zombie`,UI 侧收到 `onClientZombie` 信号(Audio 库以回调/`JackEventSink` 形式,不依赖 Qt;Qt 集成由调用方完成)。 |
| 传输(核心子集) | `transportStart/Stop`、`transportState`、`getCurrentTransportFrame`(后续增量:BBT/timecode/`jack_set_timebase_callback`)。 |

### 7.2 JackPort(端口封装)

| 能力 | 说明 |
| :--- | :--- |
| 查询 | `name()` `shortName()` `flags()` `type()` `isMine()` `connected()` `connectedTo(name)` `connections()`(自动 `jack_free` 释放)。 |
| 缓冲 | `getAudioBuffer(nframes)` → `float*`;MIDI 经 `JackMidiBuffer` 遍历/写入。 |
| 路由 | `connectFrom(const char*)` / `connectTo(const char*)` / `disconnectAll()` / `getPorts(filter)`(名字/类型/方向过滤,支持 `JACK_DEFAULT_AUDIO_TYPE`、`JACK_DEFAULT_MIDI_TYPE`)。 |
| 别名/监控 | `setAlias/unsetAlias/aliases`、`requestMonitor/setMonitor`、`setLatency/getLatency/getTotalLatency`。 |
| 图查询 | `JackGraph` 辅助:`walkPorts()`、`isConnected(a,b)`、`connect/disconnect`(Patchbay 模块将基于此实现;`jack_set_graph_order_callback` 保证 UI 可感知拓扑变化)。 |

### 7.3 MIDI 与环形缓冲

| 类型 | 说明 |
| :--- | :--- |
| `JackMidiBuffer` | `jack_midi_get_event_count/event_get` 遍历只读事件;`event_write/clear_buffer/max_event_size` 写入;会话内零拷贝。 |
| `JackRingBuffer` | JACK `ringbuffer.h` 封装(`jack_ringbuffer_create/free` + `read/write` 实时安全);`ResizeGuard`。 |
| `AudioThreadSafe<T>` | 基于 `Common::RealtimeObject<T>` 的引擎参数快照(增益、哑音、路由矩阵指针),UI 改参不碰音频线程锁。 |

### 7.4 JackServerGuard(服务管理)

- 可选启动/探测:`startIfNeeded(jsonPath)`(默认 `third_party/JACK2/jackd.exe` 或系统 PATH 中的 `jackd`),`waitForServer(timeout)`(轮询 `jack_client_open` 轻量探针);
- 守护策略与路径在 `%LOCALAPPDATA%\WinJACKNexus\jackd.ini` 可配;用于开发测试与 Adapter 后稳定。

### 7.5 JUCE DSP 包装层(可选,`WJN_WITH_JUCE=ON`)

| 类型 | 说明 |
| :--- | :--- |
| `JuceDspWrapper` | 将 `juce::AudioProcessor` 的 `processBlock` 封装为可挂到 JACK 回调的结构(`AudioBuffer<float>` ↔ `float*`,`MidiBuffer` ↔ `jack_midi_event_t`);仅用于 Mixer Pro / VST Host 模块复用,本阶段只定义并实现最小通用样例。 |
| `JuceAudioDeviceBridge`(预留) | JUCE 自带 `JackAudioIODevice`(juce_JackAudio.cpp)参考;VST Host 沙盒进程使用 JUCE 直接驱动,不经过 `audio.dll`(保持沙盒隔离)。 |

### 7.6 WASAPI 设备信息与数据获取(共享模式)

> 定位:Audio Adapter 将 WASAPI(共享)音频转为 JACK 客户端;本库提供**设备发现 + 音频流获取**,由 Adapter 模块写入 JACK 端口(规范 §3「Audio Adapter」)。

| 类型 | 说明 |
| :--- | :--- |
| `WasapiDevice` | 表示一个音频端点(MMDevice)。**设备信息**:友好名称、**设备 UUID**(`IMMDevice::GetId()`,用于持久化配置/设备匹配)、数据流方向(`eCapture`/`eRender`)、**声道数**(mix 格式 `WAVEFORMATEXTENSIBLE` 的 `nChannels`)、**采样率**(共享模式混合格式,典型 48000)、位深/子格式(通常 float32)、设备状态(`DEVICE_STATE_*`)、是否默认设备。 |
| `WasapiDeviceEnumerator` | 基于 `IMMDeviceEnumerator` 枚举全部端点;`getDefault(eRender/eCapture)`、`getById(id)`、**热插拔通知**(`IMMNotificationClient` → `onDeviceAdded/Removed/StateChanged/DefaultDeviceChanged` 回调)。 |
| `WasapiCaptureStream` | 共享模式(`AUDCLNT_SHAREMODE_SHARED`)初始化(`IsFormatSupported`/`Initialize`)、**事件驱动**(`SetEventHandle` + `IAudioCaptureClient::GetNextPacketSize/GetBuffer/ReleaseBuffer`)与轮询两种模式;`start(callback)/stop()`;回调提供 `float*` 缓冲、帧数、时间戳;自动处理 `AUDCLNT_BUFFERFLAGS_SILENT/DATA_DISCONTINUITY`。 |
| `WasapiRenderStream` | 回放方向:`IAudioRenderClient::GetBuffer/ReleaseBuffer`,将 JACK 端口数据写回系统设备(Adapter 可导出系统输出)。 |
| `WasapiFormat` | `WAVEFORMATEXTENSIBLE` ↔ 声道布局(`KSAUDIO_CHANNEL_CONFIG`)、交错 ↔ planar 转换;统一输出 float32 到 JACK(共享模式默认即 float32)。 |
| `Resampler`(可选) | 共享模式采样率通常被系统固定(如 48 kHz),与 JACK 引擎采样率不一致时用轻量线性/样条重采样器适配(分数步长、实时安全);复杂场景后续可换 `libsamplerate`(预留)。 |

> **音频后端定位**:本项目以 **JACK 为核心**——所有音频/MIDI 路由、混合、连线均在 JACK 图中完成。WASAPI 仅作为 **Adapter 的物理设备数据源**(共享模式,系统混音器接管,稳定无独占冲突),用于把系统音频/MIDI 接入 JACK,不承担混合/路由职责。
>
> **范围约定**:**不需要 ASIO 支持**(无 ASIO 驱动依赖、无 ASIO 枚举/初始化代码、不链接 JackRouter)。若未来确有独占模式需求,优先评估 WASAPI 独占(`AUDCLNT_SHAREMODE_EXCLUSIVE`),而非 ASIO;该决策不属于本计划范围。

### 7.7 系统 MIDI 设备信息与事件流

> 定位:将系统 MIDI(输入/输出)设备接入 JACK 的 `JACK_DEFAULT_MIDI_TYPE` 端口;WinMM(`mmsystem.h`)为主实现,Windows MIDI Services (WMIDI, Win11) 预留为可选后端。

| 类型 | 说明 |
| :--- | :--- |
| `MidiDeviceInfo` | **设备信息**:显示名称、**唯一 ID**(WinMM 为设备索引;WMIDI 为 endpoint ID/UUID)、方向(In/Out)、制造商/产品 ID(`MIDIINCAPS2.wMid/wPid`)、技术类型(`MOD_*`:FM/MPU/软合成器等)、端口数(参考)、**通道能力**(WinMM 设备为 16 通道;WMIDI 可查询 endpoint 通道协议)、在线/离线状态。 |
| `MidiDeviceEnumerator` | 枚举输入/输出设备;`openInput(index/id)` / `openOutput(index/id)`;**热插拔**:WinMM 无通知 → 提供 `refresh()`(UI 定时/手动);WMIDI 后端走 `MidiEndpointManager` 通知。 |
| `MidiInputStream` | `midiInOpen`+`midiInStart`(回调/`HWND` 消息两种传输);**MIDI 事件流**:每个事件产出 `MidiEvent { timestamp, status, data1, data2, channel }` —— **通道信息**解析(`status & 0x0F` → 0~15 通道号;NOTE_OFF/ON、POLY_AFTERTOUCH、CC、PROGRAM_CHANGE、CHANNEL_AFTERTOUCH、PITCH_BEND(14-bit));系统通用消息(SysEx/Clock)单独标记(`MidiEventKind`)。事件经 `Common::LockFreeSpscQueue<MidiEvent>` 进入音频线程,再由 `Audio::JackMidiBuffer::writeEvent()` 写入 JACK MIDI 端口(实时安全)。 |
| `MidiOutputStream` | `midiOutOpen`/`midiOutShortMsg`;接收 JACK MIDI 端口读出的事件,经通道/状态解析后发送到系统输出设备(Adapter 系统 MIDI Out)。 |
| `MidiEvent` / `MidiMessageParser` | 通道号、消息类型(支持 running status)解析;后续 DAW2Jack 桥的 MIDI 逻辑复用同一解析器。 |

> 事件流链路(Adapter):`midiIn 回调`(实时优先线程)→ `SPSC<MidiEvent>` → JACK process 回调内 `jack_midi_event_write` 到 `midi_in` 端口;音频链路:WASAPI capture → `WasapiCaptureStream` → JACK `capture_*` 端口。

### 7.8 动态加载策略(关键决策)

- 默认 **不链接** `libjack64.lib`,改为 `Audio::Library::instance()` 运行时 `LoadLibraryW(L"C:\\Windows\\libjack64.dll")` → `GetProcAddress` 获取全部函数指针(函数指针表在 `JackApi.hpp` 中声明,与 `jack.h` 原型一致);
- 优点:模块 DLL 不绑定 JACK 客户端库版本;缺失时给出明确错误而不是启动失败;便于未来多 JACK 后端切换;
- 开发期提供 `WJN_LINK_JACK=ON` 直接链接导入库(CI/调试用);
- **约定:头文件对外一律用 `JackApi` 函数指针转发,不直接暴露 `jack_*` 符号,保证 ABI 兼容。**

### 7.9 里程碑与验收

- 实现顺序:Library 动态加载 → JackClient → JackPort/Graph → JACK MIDI → ServerGuard → WASAPI 枚举/流 → 系统 MIDI 枚举/流 → Resampler(可选)→ JUCE 层(可选);
- 冒烟测试(`tests/audio`):
  1. 启动 `jackd.exe`;打开客户端,注册 1 对 audio port;`jack_lsp` 可见端口;回环(`jack_connect` 自连)验证音频样本计数与 CRC;
  2. JACK MIDI 事件计数;
  3. WASAPI:枚举到默认设备(名称/UUID/声道数/采样率);`WasapiCaptureStream` 捕获若干帧,校验声道数与 float32 数据;共享模式采样率与 JACK 引擎不一致时 Resampler 正确转换;
  4. 系统 MIDI:`MidiInputStream` 打开后注入/触发事件,校验事件流中的**通道号与消息类型**解析;`MidiOutputStream` 回发;
  5. 客户端关闭后 `jack_lsp` 端口消失;
- 验收:冒烟脚本通过;错误路径(无服务器、缺 DLL、`JackNameNotUnique`、无 WASAPI 设备、设备热插拔)行为明确。

---

## 8. WinJACKNexus::Plugin — 预留接口设计(本阶段只定义契约)

> `plugin.dll` 本阶段仅导出接口描述符号(桩实现),供后续模块 DLL `import`;业务实现随功能模块逐步填充。

### 8.1 C++ 抽象接口(`IModulePlugin.hpp`)

```cpp
namespace WinJACKNexus::Plugin {

// 模块生命周期(可选,避免依赖 Qt 头文件;Qt 对象以 HWND 传递)
enum class ModuleLifecycle { init, shutdown };

struct ModuleInfo {
    const char* name;          // "Interactive Patchbay"
    const char* category;      // "视图(V)" | "路由(R)" | "工具(T)"
    const char* version;       // "1.0.0"
    const char* author;
    const char* description;
};

class IModulePlugin {
public:
    virtual ~IModulePlugin() = default;
    virtual ModuleInfo info() const noexcept = 0;
    virtual bool init() noexcept = 0;                 // 注册 JACK 端口等
    virtual void shutdown() noexcept = 0;
    // 创建模块窗口:返回 HWND(主进程经 UI::NativeWindowUtils 包装为 QWidget)
    virtual HWND createWindow(HWND parent) noexcept = 0;
    virtual void destroyWindow(HWND hwnd) noexcept = 0;
};
}
```

### 8.2 C-API 导出规范(`ModuleApi.h`)

```cpp
extern "C" {
    WJN_PLUGIN_API const char* wjn_get_module_api_version();   // 协商版本,如 "1"
    WJN_PLUGIN_API const char* wjn_get_module_name();
    WJN_PLUGIN_API const char* wjn_get_module_category();
    WJN_PLUGIN_API WinJACKNexus::Plugin::IModulePlugin* wjn_create_module(); // DLL 内存模型:同 CRT/同 Qt
    WJN_PLUGIN_API void wjn_destroy_module(WinJACKNexus::Plugin::IModulePlugin*);
}
```

> ABI 约定:主进程与所有模块 DLL 使用**同一 MSVC 2022 + 同一 Qt 6.10.3 msvc2022_64**;`IModulePlugin` 跨 DLL 传递指针,类内**不含 STL/Qt 成员**,字符串用 `const char*`,窗口用 `HWND`。

### 8.3 模块注册表(主进程侧约定,预留)

- 扫描 `modules/*.dll` → `LoadLibraryW` → `GetProcAddress("wjn_get_module_api_version")` 校验 → `wjn_create_module` → 按 `category` 注入 `QMenuBar`/托盘菜单;
- 本阶段仅定义上述契约与 `plugin.dll` 桩,主进程扫描逻辑在后续「主程序」计划中实现。

### 8.4 后续功能模块登记表(接口预留,逐步填充)

| 模块 | DLL / EXE | 预留端口契约(全部暴露 JACK 端口,Patchbay 可全局接线) | 计划阶段 |
| :--- | :--- | :--- | :--- |
| Audio Adapter | `mod_adapter.dll` | `system:capture_*` / `system:playback_*`(物理/虚拟设备);数据源 = `Audio::Wasapi*`(WASAPI 共享)+ `Audio::Midi*`(系统 MIDI) | P2 |
| Patchbay | `mod_patchbay.dll` | 无自身音频端口;基于 `Audio::JackGraph` 可视化接线 | P2 |
| Basic Mixer | `mod_mixer_basic.dll` | `mixer/ch_{i}_in/out` | P3 |
| Pro Mixer | `mod_mixer_pro.dll` | `mixer/bus_{i}_*`、`aux_{i}_*`、`main_*`(JUCE DSP) | P3 |
| Meterbridge | `mod_meterbridge.dll` | `metermon/in_{i}`(监听输入,SPSC 电平数据) | P4 |
| Signal Switch | `mod_switch.dll` | `sw/in_{i}` / `sw/out_{i}`(平滑 crossfade) | P4 |
| VST Host (沙盒) | `WinJACKNexus-VSTHost.exe` | `vsthost/rack_{i}_*` + MIDI(JUCE AudioProcessorGraph) | P5 |
| DAW2Jack | bridge `.vst3` (纯 JUCE) | `daw/{dawname}_*`(DAW 内插件自动注册) | P5 |

---

## 9. 构建系统设计

### 9.1 顶层 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.25)
project(WinJACKNexus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(WJN_WITH_JUCE "Build JUCE DSP wrapper in audio.dll" OFF)
option(WJN_LINK_JACK "Link libjack64.lib instead of runtime-load" OFF)

find_package(Qt6 6.10 REQUIRED COMPONENTS Widgets Gui Core)
set(Qt6_DIR "C:/Qt/6.10.3/msvc2022_64/lib/cmake/Qt6")  # 或由 -D 传入

add_subdirectory(src/common)
add_subdirectory(src/ui)
add_subdirectory(src/audio)
add_subdirectory(src/plugin)
```

### 9.2 各库 `CMakeLists.txt` 关键点

- `add_library(common SHARED ...)` → `target_compile_definitions(common PRIVATE WJN_COMMON_BUILD)`;公共头 `target_include_directories(... PUBLIC include)`;
- `ui`: `target_link_libraries(ui PUBLIC Qt6::Widgets PRIVATE/common common)`,`qt_add_resources(ui resources-ui.qrc)`;
- `audio`: `find_package(JACK2 REQUIRED)`(自定义 `cmake/FindJACK2.cmake`:优先 `WJN_LINK_JACK` 时用 `third_party/JACK2/lib/libjack64.lib`,否则仅提供运行时加载宏,不产生链接依赖);`WJN_WITH_JUCE=ON` 时 `add_subdirectory(third_party/JUCE)` + `JUCE::juce_audio_processor jassert juce_audio_basics`;
- `plugin`: `<WINDOWS>` 无窗口子系统 DLL,导出 `wjn_*` 桩符号;
- 全部库:`set_target_properties(... VERSION ${PROJECT_VERSION} SOVERSION 0)`(MSVC 生成 `.dll` + PDB);`target_link_libraries(... PUBLIC WinJACKNexus::Common)` 通过 `add_library(WinJACKNexus::Common ALIAS common)` 统一命名空间别名。

### 9.3 运行时部署

| 项 | 说明 |
| :--- | :--- |
| `common/ui/audio/plugin.dll` | `bin/` 输出;`windows` 目标 |
| `libjack64.dll` | 已安装于 `C:\Windows`(x64),动态加载;不随包分发 |
| `jackd.exe` | 开发期来自 `third_party/JACK2`;发布期由安装器处理 |
| Qt DLL | 开发机走 Qt `bin` PATH;发布期 `windeployqt` 收集(后期打包计划) |

---

## 10. 里程碑与任务分解

| 里程碑 | 内容 | 预计工作量 | 验收 | 状态 |
| :--- | :--- | :--- | :--- | :--- |
| **M1 工程骨架** | 顶层 CMake、目录树、导出宏、4 个空 DLL 成功编译到 `bin/` | 0.5 天 | `cmake --build` 产出 4 个 DLL | ✅ 已完成（2026-08-30） |
| **M2 Common 完整实现** | §5 全部容器/线程/日志/元数据 + 单元测试 | 4~6 天 | `ctest` 通过;TSAN 无告警 | ✅ 已完成（2026-08-30） |
| **M3 Audio 完整实现** | §7 JackClient/Port/Graph/MIDI/ServerGuard + **WASAPI 设备与共享流** + **系统 MIDI 设备与事件流** + 冒烟 | 7~10 天 | 冒烟脚本通过;错误路径明确 | 📋 未开始 |
| **M4 UI 完整实现** | §6 ThemeManager/PaintableWidget/NativeWindowUtils/资源 | 4~6 天 | 主题切换 demo + HWND 嵌入 demo | 📋 未开始 |
| **M5 Plugin 接口桩** | §8 IModulePlugin/ModuleApi.h + `plugin.dll` 桩 + 示例模块骨架(仅编译验证) | 2~3 天 | 示例模块可被 LoadLibrary 导出符号可解析 | 📋 未开始 |
| **M6 基础库集成** | 三库联调:UI 调用 Audio 状态、Theme 信号贯通、进程内演示程序(临时) | 2~3 天 | 集成 demo 运行;作为后续功能模块的开发基座 | 📋 未开始 |

> 备注(遵循既有工作流):每阶段**编译通过即可交付**,冒烟/验证脚本仅用于开发期自查,交付后由用户手动验证;不反复跑自动化验证。
>
> 状态更新规则:每个里程碑完成并通过对应验收后,在本表“状态”列记录 `✅ 已完成（完成日期）`;未开始的里程碑保持 `📋 未开始`。

## 11. 风险与对策

| 风险 | 对策 |
| :--- | :--- |
| JACK 服务器未安装/未运行 | `JackServerGuard` 自动拉起 `jackd.exe`;`open()` 返回明确错误;文档说明 `jackd` 需求 |
| `libjack64.dll` 架构不匹配 | 动态加载失败时尝试 `libjack.dll`;构建/运行日志输出实际加载路径与版本 |
| Qt/moc/ABI 不一致 | 全工程锁定 Qt 6.10.3 `msvc2022_64`;`find_package(Qt6 6.10 EXACT)`;交付文档注明 |
| 无锁队列正确性 | SPSC 严格 acquire/release;`tests/common` 压力 + TSAN;`RealtimeObject` 三槽模型 |
| WASAPI 共享模式采样率/格式固定(通常 48 kHz float32) | 明确共享模式范围;`Resampler` 负责到 JACK 引擎采样率的转换;文档标注设备实际 mix 格式 |
| WASAPI/MIDI 设备热插拔 | `WasapiDeviceEnumerator` 走 `IMMNotificationClient`;系统 MIDI 提供 `refresh()`;设备引用失效时安全重建 |
| MIDI SysEx 大消息与 running status | parser 维护 running-status 状态;SysEx 分块写入,受 `jack_midi_max_event_size` 限制;测试覆盖 |
| JUCE AGPLv3 传染 | 默认 `WJN_WITH_JUCE=OFF`,仅 VSTHost/DAW 桥等独立进程使用;商业发布前评估商业许可 |
| 窗口嵌入崩溃(第三方 VST) | 嵌入仅在沙盒 `WinJACKNexus-VSTHost.exe` 内发生;`NativeWindowUtils` 负责 detach 防野指针 |
| 导出符号膨胀/ABI 漂移 | 显式 `WINJACK_*_API`;公共 API 不暴露 STL 成员;版本宏 `WJN_API_VERSION` |

## 12. 后续衔接

1. **P2**「主程序与 Audio Adapter」计划:基于 `Plugin::IModulePlugin` + `Audio::JackClient` + `Audio::WasapiCaptureStream/RenderStream` + `Audio::MidiInputStream/OutputStream` 实现 `WinJACKNexus.exe` 与 `mod_adapter.dll`;
2. **P3**「Patchbay / Mixer」计划:基于 `Audio::JackGraph` 与 `UI::ThemeManager`;
3. **P4**「Meterbridge / Switch」计划:基于 `Common::AudioRingBuffer`(SPSC 电平管道);
4. **P5**「VST Host 沙盒 / DAW2Jack」计划:基于 JUCE + VST3 SDK;
5. 每个模块对应的计划书文件按约定命名:`WinJACKNexus - <模块名> - 开发计划.md`,与本文档同目录存放。

---

*本计划书随开发反馈迭代,修改后递增版本号。*
