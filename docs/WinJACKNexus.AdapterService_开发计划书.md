# WinJACKNexus.AdapterService 开发计划书

> **版本**：1.0  
> **日期**：2026-08-16  
> **状态**：阶段四已实现，阶段五手工验收中  
> **模块**：`WinJACKNexus.AdapterService`

---

## 一、项目目标

新增一个独立的 `WinJACKNexus.AdapterService` 模块，用于在无 GUI 的情况下管理 Windows WDM 音频设备、Windows MIDI 设备和 JACK Client。

核心目标：

- 作为纯后台工具运行，不依赖 Adapter 主窗口和设备卡片 UI。
- 首次启动自动生成 JSON 配置文件。
- 配置文件保存设备清单、JACK Client 自定义名称、启用状态和正则筛选规则。
- 启动时一次性枚举全部支持的 WASAPI 音频设备和 WinMM/WinRT MIDI 设备。
- 按稳定顺序串行加载所有启用的客户端。
- 退出或重启时串行卸载所有已加载客户端。
- 新设备在下次重启时自动补充到配置文件。
- 已配置但重启时找不到的设备默认标记为 `missing` 并保留预设，也可通过全局开关自动移除。
- 支持 `--quiet` 静默启动并显示系统托盘图标。
- AdapterService 与 JACK 服务端必须运行在同一个用户 Session 中。

---

## 二、运行模式与安装方式

### 2.1 默认控制台模式

```text
AdapterService.exe
AdapterService.exe --config D:\Config\adapter_service.json
```

行为：

- 显示控制台窗口。
- 输出设备同步、客户端启动、运行时错误和退出日志。
- 支持 Ctrl+C、控制台关闭等可处理退出信号。
- 退出前等待所有客户端按顺序完成卸载。

### 2.2 `--quiet` 静默托盘模式

```text
AdapterService.exe --quiet
AdapterService.exe --quiet --config D:\Config\adapter_service.json
```

行为：

- 不显示命令行窗口。
- 不创建 Adapter 主窗口。
- 在当前登录用户会话创建系统托盘图标。
- 托盘菜单至少提供退出操作，并显示当前运行状态。
- 托盘退出必须进入统一的串行客户端卸载流程。
- 托盘模式不改变音频、MIDI 或 JACK 数据路径。

### 2.3 当前用户 Session 运行约束

AdapterService 只支持当前登录用户的交互式 Session：

- JACK 服务端和 AdapterService 必须由同一个 Windows 用户、在同一个 Session 中运行。
- 默认控制台模式和 `--quiet` 托盘模式都遵守这一约束。
- 如果使用计划任务启动，任务必须配置为“仅当用户登录时运行”，并使用启动 JACK 的同一用户账户。
- 不支持用户未登录时的无人值守运行，也不支持跨 Session 连接 JACK。
- `--quiet` 只在当前用户 Session 创建托盘图标，不创建 Adapter 主窗口。

---

## 三、设备范围

AdapterService 自动管理以下真实设备：

| 设备类型 | Windows 方向 | Windows 接口 | JACK 方向 |
|---|---|---|---|
| 音频 | 录制 | WASAPI Capture | JACK Output Port |
| 音频 | 播放 | WASAPI Render | JACK Input Port |
| MIDI | 输入 | WinMM / WinRT MIDI Input | JACK MIDI Output Port |
| MIDI | 输出 | WinMM / WinRT MIDI Output | JACK MIDI Input Port |

第一版不自动生成 Adapter GUI 中的逻辑设备：

- Loopback 系统回放抓取。
- Injector 虚拟注入。
- 其他仅存在于 GUI 菜单中的虚拟音频卡片。

Loopback 和 Injector 仍属于 Adapter GUI 的手动设备管理范围，不纳入 AdapterService 的自动全量设备清单。

### 3.1 全量加载原则

首次启动时，Service 扫描当前系统所有支持的 WASAPI 录制/播放端点和 MIDI 输入/输出设备。设备是否进入配置清单由 Adapter 现有正则筛选语义决定：

- 普通物理设备默认放行。
- 命中虚拟设备正则的设备继续应用输入或输出方向正则。
- MIDI 设备不应用音频方向正则。
- 所有通过筛选的设备都会写入配置，默认 `enabled = true`。

---

## 四、Adapter 实现复用边界

### 4.1 可直接参考或提取的实现

- [CascadeDeviceSelector.cpp](../modules/WinJACKNexus.Adapter/Source/UI/CascadeDeviceSelector.cpp)  
  参考 WASAPI/MIDI 枚举、方向判断和正则过滤语义。
- [MainComponent.cpp](../modules/WinJACKNexus.Adapter/Source/UI/MainComponent.cpp)  
  参考配置恢复、串行启动队列、定时 `refresh()` 和释放顺序。
- [RealEngine.h](../modules/WinJACKNexus.Adapter/Source/Engine/RealEngine.h)  
  复用单客户端配置和生命周期接口。
- [RealEngine.cpp](../modules/WinJACKNexus.Adapter/Source/Engine/RealEngine.cpp)  
  复用 WASAPI、WinMM、JACK、FIFO、重采样和单客户端启动/停止实现。
- [AdapterConfig.cpp](../modules/WinJACKNexus.Common/include/WinJACKNexus/Common/Serialization/AdapterConfig.cpp)  
  参考 JUCE `var` 和 JSON 序列化方式。

### 4.2 不复用的 GUI 内容

AdapterService 不创建或依赖：

- `MainComponent`
- `DeviceItemCard`
- `AdapterMainWindow`
- Tab 页面和 In/Out 分栏控件
- PopupMenu 设备选择器
- Audio LED、MIDI LED 和 LCD
- 文件选择器
- `adapter_saves` 最近存档逻辑
- OpenGL 加速、主题、字体和本地化 UI

### 4.3 后端抽取要求

当前 `RealEngine` 已位于共享后端 target，AdapterService 不直接链接 Adapter 的 UI 目标。共享后端 target 为：

```text
WinJACKNexus.AdapterBackend
```

该 target 负责承载：

- `RealEngine`
- WASAPI/MIDI 设备枚举
- 正则过滤 helper
- 设备稳定标识生成
- 纯后台配置同步依赖
- 可被 Adapter 和 AdapterService 共同使用的后端接口

Adapter GUI 保持原有行为，AdapterService 只链接后端 target，不直接编译 Adapter 的 UI 源文件。

AdapterService 的后台协调代码编译为 `WinJACKNexus.AdapterServiceCore` 静态库，阶段四入口再链接该库生成 `WinJACKNexus.AdapterService` GUI 子系统可执行文件。GUI 子系统只用于承载控制台/托盘宿主，不创建 Adapter 主窗口。

---

## 五、正则筛选规则

默认正则与 Adapter 保持一致：

```text
虚拟设备正则：
virtual audio cable

录制设备正则：
\bLine\s*\d*[13579]\b

播放设备正则：
\bLine\s*\d*[02468]\b
```

筛选逻辑：

1. 如果设备名称不匹配虚拟设备正则，则直接放行。
2. 如果设备名称匹配虚拟设备正则：
   - 录制方向使用输入正则。
   - 播放方向使用输出正则。
3. 空方向正则表示放行该方向的虚拟设备。
4. MIDI 设备不使用上述音频正则。
5. 非法正则不应用，并写入错误日志。
6. 已存在且仍可识别的设备条目不会因为正则改变而自动删除。
7. 正则主要用于控制新发现设备是否加入配置清单。

正则规则写入 AdapterService 配置文件，不再依赖 Adapter GUI 的 `config.json`。

---

## 六、配置文件设计

### 6.1 文件路径

默认配置文件：

```text
AdapterService.exe 同级目录\adapter_service.json
```

支持自定义路径：

```text
--config <配置文件路径>
```

如果配置文件不存在：

1. 创建父目录。
2. 枚举当前设备。
3. 生成完整客户端清单。
4. 写入 `adapter_service.json`。
5. 使用同步后的清单启动客户端。

AdapterService 不使用 Adapter 的 `adapter_saves` 目录和 `.adapter` 存档选择流程。

### 6.2 配置根对象

```json
{
  "format": "WinJACKNexus.AdapterService",
  "version": 1,
  "created": "2026-08-16T12:00:00Z",
  "updated": "2026-08-16T12:00:00Z",
  "autoRemoveLostDevices": false,
  "filters": {
    "virtualDevicePattern": "virtual audio cable",
    "inputDevicePattern": "\\bLine\\s*\\d*[13579]\\b",
    "outputDevicePattern": "\\bLine\\s*\\d*[02468]\\b"
  },
  "clients": []
}
```

### 6.3 客户端条目

```json
{
  "id": "svc-001",
  "clientName": "WDM_AudioOut_01",
  "enabled": true,
  "status": "available",
  "kind": "Audio",
  "driver": "WASAPI",
  "direction": "Out",
  "streamType": "Playback",
  "device": "扬声器",
  "guid": "Windows 设备唯一标识",
  "channels": [0, 1],
  "sampleRate": 48000,
  "wasapiMode": "shared"
}
```

每条客户端至少包含：

- `id`：Service 内稳定 ID。
- `clientName`：JACK Client 名称，可由用户修改。
- `enabled`：是否在下次启动时加载。
- `kind`：`Audio` 或 `Midi`。
- `driver`：`WASAPI` 或 `WinMM / WinRT MIDI`。
- `direction`：`In` 或 `Out`。
- `streamType`：`Record`、`Playback`、`Input` 或 `Output`。
- `device`：显示名称。
- `guid`：WASAPI 设备标识或 MIDI identifier。
- `channels`：音频声道列表，MIDI 为空数组。
- `sampleRate`：音频采样率，MIDI 为 `0`。
- `wasapiMode`：`shared` 或 `exclusive`。
- `status`：`available` 或 `missing`。丢失设备保留在配置中时使用 `missing`。

运行时状态不写入配置：

- JACK 连接状态。
- Xrun 计数。
- 当前重采样状态。
- 线程状态。
- 临时启动错误。
- 当前电平和 LED 状态。

### 6.4 `enabled` 语义

```json
"enabled": true
```

表示该设备在本次启动时创建 JACK Client。

```json
"enabled": false
```

表示保留配置条目，但跳过启动。禁用设备不能因为未加载而被同步算法删除。

全局配置项：

```json
"autoRemoveLostDevices": false
```

为 `false` 时，丢失设备保留在 JSON 中并标记为 `status = "missing"`，以保留用户自定义的 Client 名称和其他预设；设备恢复后状态更新为 `available`。为 `true` 时，丢失且启用的设备从配置中物理删除，保持原有自动清理行为。

### 6.5 Client 名称

默认命名沿用 Adapter：

- 音频输入：`WDM_AudioIn_01`、`WDM_AudioIn_02`。
- 音频输出：`WDM_AudioOut_01`、`WDM_AudioOut_02`。
- MIDI 输入：`WDM_MidiIn_01`、`WDM_MidiIn_02`。
- MIDI 输出：`WDM_MidiOut_01`、`WDM_MidiOut_02`。

用户在配置文件中修改过的名称必须优先保留。新增设备只使用没有被占用的下一个编号。

JACK 重名冲突继续由 JACK2 服务端处理，Service 记录请求名称和启动结果。

### 6.6 文件写入

配置保存使用临时文件替换策略：

1. 写入同目录临时文件。
2. 完成 JSON 写入并刷新文件。
3. 替换目标配置文件。
4. 可选保留 `.bak` 备份。

如果配置保存失败，应记录错误并阻止可能导致配置漂移的启动，除非实现了明确的只读降级策略。

---

## 七、启动时设备清单同步

每次启动按以下顺序执行：

1. 解析命令行参数。
2. 确定配置路径、运行模式和日志路径。
3. 获取 AdapterService 专用 Named Mutex。
4. 如果已有 AdapterService 实例运行，记录日志后退出。
5. 读取配置文件；文件不存在时创建空配置。
6. 校验根对象、版本和正则表达式。
7. 枚举当前全部 WASAPI 录制设备。
8. 枚举当前全部 WASAPI 播放设备。
9. 枚举当前全部 MIDI 输入设备。
10. 枚举当前全部 MIDI 输出设备。
11. 应用与 Adapter 一致的正则筛选。
12. 为每个当前设备生成稳定匹配键。
13. 合并已有配置条目和当前设备清单。
14. 追加新发现设备，默认 `enabled = true`。
15. 保留已匹配设备的自定义名称、启用状态、声道数和 WASAPI 模式。
16. 根据 `autoRemoveLostDevices` 将配置中存在但当前系统找不到的设备标记为 `missing` 或物理移除。
17. 将同步后的配置原子写回磁盘。
18. 按稳定顺序串行启动启用的客户端。

### 7.1 稳定匹配键

首选匹配键：

```text
kind + direction + streamType + guid
```

优先使用：

- WASAPI 设备端点 ID。
- MIDI 设备 identifier。

如果当前 JUCE/WASAPI 路径只能得到设备名，则以设备名作为兼容 fallback，并在日志中标记该设备缺少稳定 GUID。

### 7.2 新设备处理

当前系统发现、配置文件中不存在的设备：

- 新建客户端条目。
- 自动生成稳定 `id`。
- 自动生成默认 `clientName`。
- 默认设置 `enabled = true`。
- 首次同步使用默认 2 声道；已有条目的用户自定义声道继续保留。
- 使用 shared WASAPI 模式，除非存在可匹配的用户配置。

新设备只在下一次启动或重启时加入，不在运行中改变 JACK 拓扑。

### 7.3 丢失设备处理

配置文件存在、但本次启动枚举不到的设备：

- `autoRemoveLostDevices = false` 时，视为设备已丢失并保留条目，设置 `status = "missing"`。
- `autoRemoveLostDevices = true` 时，丢失且启用的设备从配置文件删除。
- 保留的条目仍然保存设备名称、GUID、原 Client 名称和其他用户预设。
- 设备恢复后通过稳定匹配键重新找到条目，并将状态改回 `available`。

以下情况不能删除条目：

- `autoRemoveLostDevices = false`。
- `enabled = false`，即使开启自动清理也保留禁用预设。
- 客户端启动失败。
- JACK 服务端暂时不可用。
- WASAPI 打开失败但设备仍能枚举到。
- MIDI 设备暂时无法打开但仍能枚举到。

---

## 八、后台运行时和生命周期

### 8.1 运行时对象

新增无 UI 的运行时协调器：

```text
ServiceRuntime
├── ServiceConfig
├── ConfigSynchronizer
├── DeviceEnumerator
├── ServiceLogger
└── ClientRuntime[]
      └── std::unique_ptr<RealEngine>
```

`ClientRuntime` 持有：

- 对应配置条目。
- 一个 `RealEngine` 实例。
- 启动结果和运行状态。
- 日志上下文。

不创建：

- `DeviceItemCard`。
- `MainComponent`。
- GUI 回调。
- LED/LCD 状态对象。

### 8.2 串行启动

按同步后的 `clients` 稳定顺序启动：

1. 跳过 `enabled = false` 条目。
2. 创建 `RealEngine::Configuration`。
3. 调用 `RealEngine::start()`。
4. 等待 `RealEngine::isStartComplete()`。
5. 记录成功或失败。
6. 完成后才启动下一个客户端。

单个设备启动失败时：

- 写入错误日志。
- 保留配置条目。
- 继续启动后续启用设备。
- 不因单个设备失败而卸载已经成功启动的客户端。
- `status = "missing"` 的条目不创建客户端；只有恢复为 `available` 且 `enabled = true` 的条目才进入启动队列。

全局致命错误，例如配置无法读取、互斥体创建失败或运行时初始化失败，可以阻止后续启动。

### 8.3 运行调度

启动完成后维持后台调度循环，建议周期约为 50 ms：

- 调用活动 `RealEngine::refresh()`。
- 处理 JACK 到系统 MIDI 输出的轮询发送。
- 处理退出信号。
- 记录周期性错误。
- 在托盘模式下处理托盘事件循环。

不能在启动客户端后直接永久阻塞，因为当前 `RealEngine::refresh()` 仍负责处理部分 MIDI 输出路径。

### 8.4 串行退出

退出流程：

1. 设置停止标志。
2. 停止后台调度循环。
3. 按确定顺序逐个调用 `RealEngine::stop()`。
4. 等待当前客户端线程和设备回调完全退出。
5. 销毁当前客户端对象。
6. 继续处理下一个客户端。
7. 释放托盘资源。
8. 释放 Named Mutex。
9. 退出进程。

禁止并行调用多个客户端的 `stop()`。

建议按启动顺序逆序卸载，以降低 JACK 图谱和设备释放时的相互影响。

---

## 九、模块与目录计划

建议目录：

```text
modules/
├── WinJACKNexus.AdapterBackend/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── WinJACKNexus/AdapterBackend/
│   │       ├── DeviceEnumerator.h
│   │       ├── DeviceFilter.h
│   │       └── RealEngine.h
│   └── Source/
│       ├── DeviceEnumerator.cpp
│       ├── DeviceFilter.cpp
│       └── RealEngine.cpp
└── WinJACKNexus.AdapterService/
    ├── CMakeLists.txt
    ├── Source/
    │   ├── Main.cpp
    │   ├── ServiceApplication.h/.cpp
    │   ├── ServiceTrayIcon.h/.cpp
    │   ├── CommandLineOptions.h/.cpp
    │   ├── ServiceRuntime.h/.cpp
    │   ├── ServiceConfig.h/.cpp
    │   ├── ConfigSynchronizer.h/.cpp
    │   └── ServiceLogger.h/.cpp
    └── Resources/
```

### 9.1 CMake 接入

顶层 [CMakeLists.txt](../CMakeLists.txt) 新增：

1. `WinJACKNexus.AdapterBackend` 子目录。
2. `WinJACKNexus.AdapterService` 子目录。
3. Adapter 对后端 target 的链接关系。

AdapterService 只依赖后台运行所需的 Common、后端和 JUCE 模块。

纯控制台模式不依赖：

- `LCD`。
- `locales`。
- OpenGL。
- Adapter GUI 资源。
- `CommonResources` 的资源复制步骤。

`--quiet` 托盘模式需要额外链接 JUCE GUI 基础模块和托盘图标资源，但不能依赖 `AdapterMainWindow`。

### 9.2 单实例

当前 [SingleInstanceGuard.cpp](../modules/WinJACKNexus.Common/include/WinJACKNexus/Common/App/SingleInstanceGuard.cpp) 包含窗口置前逻辑，不能原样用于无窗口进程。

AdapterService 应使用独立的 Named Mutex，例如：

```text
WinJACK_Nexus_AdapterService_Lock
```

重复启动时：

- 写入日志。
- 返回非零或约定的“已有实例”退出码。
- 不查找窗口。
- 不尝试置前 Adapter GUI。

---

## 十、可以精简的内容

### 10.1 可以完全移除

AdapterService 不需要：

- GUI 主窗口。
- Tab 页面。
- In/Out 分栏控件。
- 设备卡片。
- 音频 LED 和 MIDI LED。
- LCD 采样率显示。
- 文件选择器。
- `adapter_saves` 目录。
- 最近存档列表。
- OpenGL 加速。
- 主题初始化。
- 字体加载。
- 本地化 UI。
- GUI PopupMenu 设备选择器。
- `MockEngine`。

### 10.2 后台必须保留

- WASAPI 录制/播放设备枚举。
- WinMM/WinRT MIDI 枚举。
- Adapter 正则过滤语义。
- JACK 音频和 MIDI Client。
- SPSC FIFO。
- Lagrange 重采样。
- WASAPI 与 JACK 桥接。
- MIDI 输入输出桥接。
- JSON 配置读写。
- 设备清单同步。
- 日志系统。
- 串行启动和退出协调器。
- `--quiet` 的最小托盘宿主。

### 10.3 首版不做

- 用户未登录时的无人值守运行。
- 跨用户或跨 Session 的 JACK 连接。
- 运行时热插拔。
- 运行中自动重建 JACK Client。
- GUI 式在线改名。
- 运行中在线切换声道。
- Loopback/Injector 自动客户端。
- GUI 配置编辑器。
- JACK 拓扑恢复 UI。
- 自动化的 8 小时稳定性验收。

### 10.4 可以延后优化

当前 Common 目标通过 PUBLIC 方式链接了部分 GUI 模块。首版可以先复用 Common，等 AdapterService 构建和运行验证完成后再评估：

- 是否拆分更窄的纯后台 Common target。
- 是否进一步减少 GUI 模块链接。
- 是否单独拆出 JACK/WASAPI/MIDI 核心库。

该构建体积优化不作为 AdapterService 首版的阻塞条件。

---

## 十一、实施阶段

### 阶段一：后端抽取（已完成）

- 将 `RealEngine` 提取到 `WinJACKNexus.AdapterBackend`。
- 抽出 WASAPI/MIDI 设备枚举。
- 抽出正则匹配和设备稳定键生成。
- 让 Adapter GUI 先改为链接后端 target。
- 确认 Adapter 原有构建和行为不回归。

### 阶段二：配置同步（已完成）

- 实现 AdapterService 专用 JSON Schema。
- 实现首次启动自动生成配置。
- 实现正则规则保存和校验。
- 实现全量设备扫描。
- 实现新设备追加。
- 实现丢失设备状态标记和可选物理删除。
- 实现 `enabled` 开关。
- 实现自定义 Client 名称保留。
- 实现原子文件写入。

### 阶段三：串行运行时（已完成）

- 实现单客户端运行时容器。
- 实现严格串行启动。
- 实现启动完成等待。
- 实现启动失败后继续处理。
- 实现约 20 Hz 的 `refresh()` 调度。
- 实现信号处理。
- 实现逆序串行卸载。

### 阶段四：入口与托盘（已完成）

- 实现默认控制台入口。
- 实现 `--quiet` 静默托盘模式。
- 实现 `--config` 路径覆盖。
- 实现 Service 专用 Named Mutex。

阶段四当前实现细节：

- `ServiceApplication` 使用 JUCE `Timer` 以约 50 ms 驱动 `ServiceRuntime::tick()`。
- 控制台控制回调和托盘菜单只设置退出请求，实际停止由消息线程推进，客户端按逆序串行卸载。
- 默认模式建立当前用户控制台并注册 Windows 控制台信号处理；`--quiet` 释放控制台并仅创建系统托盘图标。
- 托盘菜单显示运行状态并提供退出操作。
- 重复实例使用 `WinJACK_Nexus_AdapterService_Lock` Named Mutex 拒绝，不查找或置前窗口。
- 支持 `--help`、`--version`、`--config <path>`、`--config=<path>` 和未知参数错误退出。

### 阶段五：安装与回归（进行中）

- 编写当前用户登录任务的启动配置说明。
- 增加配置同步单元测试。
- 增加串行生命周期单元测试。
- 完成 JACK、WASAPI、MIDI 手工验证。
- 完成控制台、托盘和当前用户 Session 运行验证。

当前已完成的自动验证：

- `WinJACKNexus.AdapterService` Debug 目标成功构建并链接。
- `--help`、`--version` 和未知参数路径已执行验证。
- `WinJACKNexus.AdapterService.CommandLineOptionsTests` 通过。
- `WinJACKNexus.AdapterService.ConfigSynchronizerTests` 通过。
- `WinJACKNexus.AdapterService.ServiceRuntimeTests` 通过。
- `WinJACKNexus.AdapterBackend`、`WinJACKNexus.Adapter` 与 AdapterService 相关目标联编通过。

仍待手工验收：真实 JACK/WASAPI/MIDI 设备链路、控制台长时间运行、托盘退出和同一用户 Session 下的启动配置。

---

## 十二、验证与验收

### 12.1 配置同步测试

覆盖以下场景：

- 首次生成完整 WASAPI/MIDI 设备清单。
- 正则规则保存和读取。
- 非法正则被拒绝并记录日志。
- `enabled = false` 条目被保留。
- 自定义 Client 名称被保留。
- 自定义声道和 WASAPI 模式被保留。
- 新设备在重启后追加。
- 丢失设备默认在重启后标记为 `missing` 并保留命名预设；开启自动删除时物理移除。
- 重复设备不会产生重复条目。
- 稳定 GUID 匹配优先于设备名称匹配。
- 配置文件原子写入失败时不会破坏旧配置。

### 12.2 生命周期测试

使用 fake engine 或可观察的测试 double 验证：

- 只启动 `enabled = true` 的条目。
- 每个客户端必须等待前一个启动完成。
- 单个客户端启动失败不阻塞后续客户端。
- `refresh()` 按周期调用。
- 退出时不会并行停止多个客户端。
- 所有客户端停止后进程才退出。
- 停止顺序稳定且符合约定。

### 12.3 手工验证

- 默认控制台模式可以连接同一用户 Session 中运行的外部 `jackd`。
- `--quiet` 不显示命令行窗口。
- `--quiet` 在当前用户会话显示托盘图标。
- 托盘退出会等待所有客户端串行卸载。
- `--config` 可以切换不同配置文件。
- 重启后新增 WDM/MIDI 设备自动补全。
- 重启后丢失设备默认保留为 `status = "missing"`，开启 `autoRemoveLostDevices` 时才从配置文件移除。
- 禁用设备不会创建 JACK Client。
- 自定义 Client 名称在重启后继续生效。
- WASAPI 采样率不一致时重采样链路正常。
- MIDI 输入和输出链路正常。
- 计划任务仅在用户登录时启动，并与 JACK 使用同一用户 Session。

---

## 十三、范围边界

本计划不修改：

- Adapter GUI 的交互模型。
- Adapter 现有 `.adapter` 存档格式。
- Adapter 手动添加设备的行为。
- Loopback/Injector GUI 逻辑设备。
- 现有 Adapter 的用户界面和托盘工作流。

本计划明确不包含：

- 用户未登录时的无人值守运行。
- 跨用户或跨 Session 的 JACK 连接。
- 运行时热插拔重建。
- 在线配置编辑器。

本文件只定义 AdapterService 的设计、模块边界、实施顺序和验收标准，实际源码实现按阶段逐步进行。
