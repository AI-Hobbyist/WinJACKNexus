# S01 主题接入基线（2026-09-27）

本文件记录当前代码的外观来源，供 S02 token 注册及 S07–S09 逐模块迁移对照。`Theme-S01-Color-Literals.csv` 保存 21 个绘制源文件及 3 份 QSS/CSS 中 886 处十六进制、`QColor(r,g,b[,a])` 与 Qt 颜色常量的文件、行号、原值；它是硬编码定位清单，不把同一颜色的不同语义合并。运行时动态颜色、调色板继承与变量映射另由下表说明。

## 现有入口和基线

| 区域 | 当前入口与来源 | 当前尺寸或规则 |
| --- | --- | --- |
| 共享 UI | `src/ui/src/ThemeManager.cpp` 从内置 QSS 加载 `dark-rack`/`light-console`；`ThemePalette.cpp` 给出十项基础颜色；`Controls.cpp` 用 `fxBackground`、`fxPanel`、`fxText`、`fxAccent`、`fxBorder` 等动态属性，且共享 `drawKnob`、`drawFader`、`drawMeter`、`drawCurve` | 默认 `dark-rack`，基础窗口 `#11181a`、面板 `#1b2527`、文字 `#e7f0ec`、强调 `#43c7a8` |
| 接线盘 | `src/patchbay/mod/src/PatchbayCanvas.cpp` 的 `CanvasTheme` 从 `src/patchbay/mod/resources/theme/style.css` 注释键读颜色；`Library.cpp` 另有 Dark/Light 菜单 | 初始 1440×860；暗色画布 `#17212b`、节点 `#263542`、标题 `#33495b`、Audio `#66bb6a`、MIDI `#ef5350` |
| 调音台 | `src/ui/src/MixerWindow.cpp` 自绘条、按钮、电平表和推子；实例颜色 `Channel::accent` / `customColor` 为用户配置 | 初始 1024×768，最小 760×560；输入、AUX、主控三类条，推子强调色分别由业务类型和用户颜色决定 |
| SEL Main | `MixerWindow.cpp` 的 `SelMainPage` 与 `MixerPolarPlot.cpp`；主面板使用大量局部 `QColor`，输入/输出极坐标曲线分别为 `#28c9f3`/`#ffa44a` | SEL 顶层窗 1394×885，最小 1100×760；主页面 950×650 最小值 |
| SEL AUX | `src/ui/src/AuxSettingsWindow.cpp`；旋钮、开关、AUX 发送电平及极坐标曲线 | 与 SEL 主窗共享外壳；发送电平区段见下文 |
| SEL FX | `src/ui/src/FxSettingsWindow.cpp` 的效果链卡片、选择态、图标及其宿主外壳；Native Fx 编辑器另有顶层窗口 | 与 SEL 主窗共享外壳；无独立电平区段 |
| 电平表桥 | `src/meter/mod/src/MeterbridgeWindow.cpp`、`MeterCard.cpp`、`MeterHistoryWindow.cpp`，以及 `src/ui/src/MeterLevelBar.cpp`、`HistoryChartWidget.cpp` | 初始 1080×740；历史曲线稳定 ID 为 peak/rms/lfe/true-peak/momentary/short-term/integrated/lra |
| 系统托盘 | `src/ui/src/TrayController.cpp` 用原生 `QMenu`；目前没有主题列表 | 后续仅增原生菜单项 |

`ThemeManager` 的设置位置是 Qt `QSettings(IniFormat, UserScope, "WinJACKNexus", "WinJACKNexus")`，键为 `theme/name`、`theme/source` 和 `variables`。应用 FPS 另由 `ApplicationSettings` 保存。默认主题字体将按计划改为 `system`，因此使用应用字体而非写死字族的区域可能出现字宽、换行变化；图标字体仍需保留。

## 逐模块台账

| 稳定 ID / 模块 | 绘制源文件 | 当前控件、图表、电平及推子语义 | 节点 / 背景 / 字体来源 |
| --- | --- | --- | --- |
| `global` / 共享 UI | `src/ui/src/Controls.cpp`, `ThemePalette.cpp`, `ThemeManager.cpp` | 按钮、开关、旋钮、文本、字段、列表、滚动条、推子、网格、曲线、电平；默认调色板十项见上 | 通用回退；字体由应用/QPainter 提供 |
| `patchbay` / 接线盘与通用节点 | `src/patchbay/mod/src/PatchbayCanvas.cpp`, `Library.cpp` | 节点卡片/标题/边框/选中边框、端口与连线；Audio/MIDI/CV/OSC/Unknown 真正端口类型由 `PortKind` 提供 | 画布背景目前只有色刷；`QApplication::font()` 用于节点标题/端口 |
| `mixer` / 调音台条 | `src/ui/src/MixerWindow.cpp` | 输入/AUX/主控推子；竖向电平表；静音、独奏、选择等状态 | 空白区目前颜色填充；实例颜色独立于主题 |
| `sel-main` / SEL Main | `src/ui/src/MixerWindow.cpp`, `MixerPolarPlot.cpp` | 主/发送电平表、参数旋钮、切换、输入/输出极坐标曲线与手柄 | 空白区颜色填充；文字由当前 painter 字体派生 |
| `sel-aux` / SEL AUX | `src/ui/src/AuxSettingsWindow.cpp`, `MixerPolarPlot.cpp` | AUX 条目、旋钮、发送电平表、极坐标输入/输出曲线 | 空白区颜色填充；文字由当前 painter 字体派生 |
| `sel-fx` / SEL FX | `src/ui/src/FxSettingsWindow.cpp` | 效果链卡片、状态按钮、图标 | 空白区颜色填充；图标字体须保留 |
| `meter-bridge` / 电平表桥 | `src/meter/mod/src/MeterCard.cpp`, `MeterHistoryWindow.cpp`, `MeterbridgeWindow.cpp`; `src/ui/src/MeterLevelBar.cpp`, `HistoryChartWidget.cpp` | level 电平表；八条命名历史曲线、图例、统计卡 | 空白区颜色填充；共享控件和局部调色板并用 |
| `native-fx-compressor` | `src/native_fx/effects/compressor/src/Ui.cpp` | 旋钮、开关、输入/输出电平、增益衰减及响应曲线/填充 | 元数据 ID `compressor`；770×665 顶层编辑器；局部 QColor/共享 Controls |
| `native-fx-deesser` | `src/native_fx/effects/deesser/src/Ui.cpp` | 旋钮、开关、频谱与去齿响应曲线 | 元数据 ID `deesser`；768×650；局部 QColor/共享 Controls |
| `native-fx-delay` | `src/native_fx/effects/delay/src/Ui.cpp` | 旋钮、开关、时间/反馈相关图表 | 元数据 ID `delay`；750×645；局部 QColor/共享 Controls |
| `native-fx-gate` | `src/native_fx/effects/gate/src/Ui.cpp` | 旋钮、开关、门限上下曲线和网格 | 元数据 ID `gate`；754×654；局部 QColor/共享 Controls |
| `native-fx-graphic-eq` | `src/native_fx/effects/graphic_eq/src/Ui.cpp` | 频段推子、旋钮、频谱与 EQ 响应曲线 | 元数据 ID `graphic-eq`；760×658；局部 QColor/共享 Controls |
| `native-fx-limiter` | `src/native_fx/effects/limiter/src/Ui.cpp` | 旋钮、开关、输入/输出电平及 reduction 表、响应曲线 | 元数据 ID `limiter`；768×650；局部 QColor/共享 Controls |
| `native-fx-parametric-eq` | `src/native_fx/effects/parametric_eq/src/Ui.cpp` | 四频段/总响应曲线、输入/处理后频谱、旋钮、图例 | 元数据 ID `parametric-eq`；1229×909；局部 QColor/共享 Controls |
| `native-fx-reverb` | `src/native_fx/effects/reverb/src/Ui.cpp` | 旋钮、开关、声场示意图（并非 dB 曲线） | 元数据 ID `reverb`；770×645；局部 QColor/共享 Controls |
| `module-example-node` / 示例 Node DLL | `src/node/examples/ExampleNode.cpp` | 无自绘控件、图表、电平或推子 | Node API v2、一个 `Example Node`；当前 API 无稳定 ID，S02/S06 需定义；无背景/字体 |

八个 Native Fx 的 ID 和 mixer/node 放置位来自各自 `Meta.hpp`，全部同时支持 mixer 与 node。现有 DLL 的命名空间描述符导出按本目标延至 S12 完成之后逐一实施。当前通用接线盘节点按 `PortKind` 着色，示例 Node DLL 不自绘 UI；Native Fx 的 node 放置复用对应效果模块，不另虚构一套绘制模块。

## 电平段与曲线默认语义

| 区域 | 当前区段或系列 |
| --- | --- |
| 调音台条 level | `-12/0/12 dB` 上界；亮色 `#26dc68/#ffe15a/#ff5353`，暗色 `#163e2e/#4a3f1d/#4c242a`（`MixerWindow.cpp:2314`） |
| SEL Main level | `-12/0/12 dB`；亮色 `#17d675/#f2df48/#f2484d`，暗色 `#123e2d/#483d1e/#4a2429`（`MixerWindow.cpp:296`） |
| SEL Main / AUX 发送 level | `-12/0/12 dB`；亮色同 SEL Main，暗色均为 `#102234`（`MixerWindow.cpp:420`, `AuxSettingsWindow.cpp:707`） |
| 电平表桥 level | 三段上界 `warning_` / `danger_` / `maximum_`，按配置量程给出；颜色分别取 `success` / `accent-alt` / `danger`，暗色 `panel-raised`（`MeterLevelBar.cpp:259`） |
| Limiter level / reduction | level 三段 `-12/0/12 dB`，亮色 RGB `(30,205,111)/(245,224,52)/(242,60,76)`，暗色 `(13,50,42)/(54,50,27)/(57,28,39)`；reduction 单独到 `12 dB`（`limiter/Ui.cpp:221,247`） |
| 电平表桥历史图 | peak `#ff5269`、rms `#ff8a47`、lfe `#ff78c7`、true-peak `#ffd143`、momentary `#43e88d`、short-term `#48c9ff`、integrated `#4b95ff`、lra `#b96aff`（`MeterHistoryWindow.cpp:76`） |
| SEL 极坐标图 | input `#28c9f3`、output `#ffa44a`；手柄与网格颜色见源文字面量清单 |

其他 Native Fx 曲线及表段需在 S02 token 注册时逐源文件按稳定语义细分，不能仅凭当前绘制顺序命名。背景图片目前未接入。

## 截图与验证界限

已有 `build/` 中的截图包括 `mixer-ui-review.png`、`sel-ui-review.png`、`aux-ui-review.png`、`sel-fx-wide-ui-review.png`、`compressor-ui-review.png`、`deesser-ui-review.png`，以及其他 Native Fx preview。它们于本轮源码改动之前由仓库既有测试生成；S01 未更改程序绘制代码，因此可作为当前源码的基线参考。当前屏幕为 1920×1200、96×96 DPI。对截图内容是否与肉眼预期一致，状态为**人工验收跳过/未人工验证**。

在现有 `build` 目录执行 Debug `ALL_BUILD`，退出码 0，日志为 `build/S01-ALL_BUILD-Debug.log`。自动测试首轮使用发行包自带 Qt 平台插件，UI 测试挂起；指定 `offscreen` 或 `minimal` 时发行包只提供 `windows` 插件，Qt Debug 弹窗报平台插件初始化失败。显式指向 Qt SDK 的 offscreen 插件后，`ui_unit_tests` 仍在 120 秒超时，其他若干 UI/Native Fx 测试异常退出；`meter_ui_model_tests` 通过。失败用例均记录为失败或未完成，不能解释为主题系统回归通过。此处只冻结 S01 基线，不把 Debug 测试环境问题扩展为无关源码修复。

后续每步构建目标为现有 `build` 的 Debug `ALL_BUILD`；主题核心优先执行新增的非窗口单元测试，UI 与 Native Fx 相关用例按模块执行并保留失败日志。用户要求的仅靠肉眼/交互才能确认的验收项单独标为“人工验收跳过/未人工验证”。
