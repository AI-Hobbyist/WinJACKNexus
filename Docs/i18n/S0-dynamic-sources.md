# S0 动态内容来源清单

日期：2026-10-03。静态源字符串证据表不能覆盖运行时值；本表补充生产者、显示位置和翻译边界。

| 来源与显示场景 | 源码位置 | 处理规则 | 后续验证 |
| --- | --- | --- | --- |
| NodeInfo 名称、分类、description；节点库和托盘 | `src/node/include/WinJACKNexus/Node/NodeInfo.hpp`；`src/patchbay/mod/src/Library.cpp` 的 scanNodeModules/populateNodeLibrary/LoadedModuleNode；`TrayController.cpp` | 内置元数据给稳定 key；社区声明自己的域。旧模块原文保留。身份与 DisplayRole 分离 | 翻译后载入、搜索、选择、展开、GUI 能力不变 |
| Native Fx 元数据、描述、参数、工厂预设 | `src/native_fx/effects/*/Meta.hpp`、`src/Ui.cpp`、`include/Logic.inc`；`src/native_fx/src/NativeFxApi.cpp`；`FxSettingsWindow.cpp` | 各模块独立域；效果/预设身份不改，名称说明翻译，用户预设名称保留 | 八模块、多实例、双宿主、独立窗口和晚打开编辑器 |
| VST 产品、厂商、版本、路径、类别与扫描结果 | `tools/vst_scanner/src/Scanner.cpp`、`MainWindow.cpp`；`src/patchbay/mod/src/Library.cpp` 的 scanVstPlugins/addVstPluginItems | 第三方元数据原样；宿主分组、状态、说明翻译。用户文件夹名保留，默认 Instruments/Effects 分组须区分 | scanner 和宿主显示；过滤继续用 itemData |
| WASAPI/MIDI 真实设备名与 ID | `src/audio/src/WasapiDevice.cpp`、`MidiDevice.cpp`；`src/adapter/src/Library.cpp` 的 deviceCombo | 名称/ID 原样；设备表单、无设备、应用失败等项目提示翻译 | 刷新与切换语言保留选中设备 ID |
| JACK 客户端、端口与 OS/驱动错误 detail | `src/audio`；`src/patchbay/mod/src/Library.cpp`、`PatchbayCanvas.cpp` | 原始数据不翻译；项目连接状态和错误外层模板翻译 | 保存工程与端口连接身份相同 |
| 通道名、注释、AUX 源名、默认 CH/AUX/SUB 和 Main L/R | `src/common/src/DigitalMixerState.cpp:300,338,358,476`；`MixerWindow.cpp`、`AuxSettingsWindow.cpp` | 已保存名称原样；默认名如成为 JACK 端口前缀也不能随热切换改写。显示默认名需独立默认状态 | 重命名、保存、重新打开、切换时正在编辑名称 |
| 子图名称、节点标签、备注和标签页 | `src/patchbay/mod/src/PatchbayCanvas.cpp` 的子图创建与恢复；`Library.cpp` canvasTabs | 用户内容保留，默认 Sub Graph 需独立状态；菜单和提示翻译 | 旧工程、重名子图、当前标签和焦点 |
| 主题名、作者、描述、错误和注册描述符 | `src/ui/src/ThemeCatalog.cpp`、`ThemePackage.cpp`、`ThemeNamespaceRegistry.cpp`、`ThemeManagementDialog.cpp`；示例原始 JSON | 外部主题作者内容原样；项目自带说明及项目校验错误翻译。token/命名空间不变 | 语言与主题独立、导入错误、列表缓存刷新 |
| 响度参考标准、speaker role、自定义预设 | `src/common/include/WinJACKNexus/Common/MeterLoudnessPresetCatalog.hpp`；`MeterCustomLoudnessPresets.cpp`；`MeterCard.cpp`、`MeterSettingsWidget.cpp` | 标准名称/ID、声道技术标记和用户预设保留；测量说明、操作和自定义项标签翻译 | 通过 ID 保存和恢复；预设菜单晚创建 |
| metric/readout/statistics/history 图例的模型文本 | `src/meter/mod/src/MeterHistoryWindow.cpp`、`MeterCard.cpp`；`src/ui/src/MetricStatisticsWidget.cpp`、`HistoryLegendWidget.cpp`、`MetricReadout.cpp` | 生产者提供 key 或重建显示角色；通用控件不能翻译任意传入用户字符串 | 已打开窗口、模型缓存、导出标签 |
| CSV 字段、PNG 内容、输出文件和工程选择器 | `src/meter/ui/src/MeterCsvWriter.cpp`；`MeterHistoryWindow.cpp`；`FxSettingsWindow.cpp`；`Library.cpp` | CSV 机器字段稳定；文件路径/用户文件名保留；文件选择标题、格式说明和图形展示标签翻译 | 比较数据字段；tooltip 完整路径；Qt 标准对话框翻译资源 |
| 扫描进度、日志、计数与错误路径 | `tools/vst_scanner/src/MainWindow.cpp`、`Scanner.cpp`；`src/app/main.cpp` | 翻译项目模板，占位参数原样；稳定日志码/原始第三方 detail 保留 | 命名参数、English one/other、错误含路径 |
| Qt 标准按钮、原生菜单和系统 UI | QMessageBox/QFileDialog/QMenu 调用点；app 部署 CMake | Qt 自带文字用 QM；项目 action 文案用自身 key；OS 所有内容不由项目改写 | 确认 Qt 6.10.3 实际 QM 依赖；无托盘降级入口 |

本表是来源边界清单，不是译文完成证明。所有项目拥有的动态模板仍须进入静态 key 清单；不能把“动态”用作忽略翻译的理由。

本轮模型新增 `customColor`，SEL/FX/AUX 新增 `selSourceKind` 属性和 `sel-monitor` 主题作用域，均为稳定身份。恢复默认颜色的操作文案翻译，颜色保存状态不因语言切换重置。FX 顶层标题现为固定“FX 设置”，不再含通道名占位符；具体变化及退休 key 见 S0-refresh.md。
