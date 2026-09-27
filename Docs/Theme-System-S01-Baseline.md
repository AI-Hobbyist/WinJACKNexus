# 主题系统 S01 默认外观与覆盖基线

记录编号：R-20260927-01

范围：仅盘点现有视觉实现，不改变音频、DSP、布局或交互语义。

## 1. 公共入口与兼容基础

| 单元 | 当前来源 | 后续主题接入点 | 当前基线 |
| --- | --- | --- | --- |
| 主题兼容层 | `src/ui/src/ThemeManager.cpp`、`ThemePalette.cpp` | 旧 `ThemePalette`、`${key}` 与 QSS 调用映射到新快照 | 内置 `dark-rack` / `light-console`；持久化键为 `theme/name`、`theme/source`、`variables[]` |
| 共享控件 | `src/ui/include/WinJACKNexus/UI/Controls.hpp`、`src/ui/src/Controls.cpp` | `Palette`、动态属性、按钮/开关/旋钮/滑块/推子/图表/电平表绘制函数 | 共享默认色仍为源码常量；必须保留旧 API |
| 托盘/快捷栏 | `src/ui/src/TrayController.cpp` | `global`、`tray`；后续只给原生 Qt 菜单增加主题子菜单 | 快捷栏含硬编码 QSS/绘制色；菜单保持 Qt 原生 |
| 应用刷新 | `src/ui/src/ApplicationSettings.cpp`、各 `bindRefreshTimer` 调用 | 主题刷新沿用应用 30/60/120 FPS；静态主题不增加计时器 | `Application/frameRate`，默认 60 FPS |
| 全局字体 | QApplication/各 QWidget 当前字体与局部 `QFont` 派生 | `--wjn-theme-global-font-source` | 默认必须为 `system`；现有局部字号、粗细、图标字体保持 |

## 2. 宿主界面和模块覆盖台账

下表中的“硬编码计数”是 S01 扫描 `QColor`/十六进制颜色字面量得到的定位辅助，不等价于最终 token 数；同一字面量可能服务多个状态，最终以 S02 注册描述符为准。

| 模块 / 稳定 ID | 绘制源文件 | 建议命名空间 | 控件、图表、电平、推子 | 背景 / 字体 | 基线与迁移要点 |
| --- | --- | --- | --- | --- | --- |
| Shared UI / `global` | `Controls.hpp/.cpp` 及 `src/ui/src/*.cpp` | `--wjn-theme-global-` | 共享按钮、开关、旋钮、范围滑块、滚动条、图表网格/曲线、通用 level meter；`Controls.hpp` 54 处颜色默认 | 全局 surface；字体 system | 旧动态属性和 `applyPalette` 必须来自同一快照 |
| Tray / `tray` | `src/ui/src/TrayController.cpp` | `--wjn-theme-global-` | 快捷栏按钮 normal/hover/checked、文字、边框；11 处颜色 | 无图片背景；system | 系统托盘菜单不自绘 |
| Patchbay / `patchbay` | `src/patchbay/mod/src/PatchbayCanvas.cpp`、`resources/theme/style.css`、`Library.cpp` | `--wjn-theme-patchbay-` | canvas、node card/header/border/selected、separator、port label、port/wire；Audio/MIDI/CV/OSC/unknown | patchbay 是背景图片继承源；system | 当前 Audio `#66bb6a`、MIDI `#ef5350`（dark），由真实 `PortKind` 选择；窗口 1440×860 |
| Mixer / `mixer` | `src/ui/src/MixerWindow.cpp` | `--wjn-theme-mixer-` | input/AUX/master strip；knob/button/toggle；input/AUX/master fader；strip level meter 与 send meter（各 3 段）；77 处颜色 | mixer 空白区继承 patchbay；system | 默认窗口 1024×768，最小 760×560；实例自定义 strip color 优先于主题 |
| SEL Main / `sel-main` | `src/ui/src/MixerWindow.cpp` (`SelMainPage`) | `--wjn-theme-sel-main-` | header/tab、gain/trim、polar plot、布局按钮、meter | 独立背景作用域；system | SEL 窗口 1394×885，最小 1100×760；实例 header 色继续优先 |
| SEL AUX / `sel-aux` | `src/ui/src/AuxSettingsWindow.cpp` | `--wjn-theme-sel-aux-` | send knob/toggle/button、8 通道 send level meter：低/中/高三段 | 独立背景作用域；system | 窗口 1360×813，最小 980×640；40 处颜色 |
| SEL FX / `sel-fx` | `src/ui/src/FxSettingsWindow.cpp`、`FxLevelMeterWidget.cpp` | `--wjn-theme-sel-fx-` | FX chain、module list、channel selector、input/output level meter、宿主壳 | 独立背景作用域；system | 窗口 1398×874，最小 1100×720；Native Fx 控制仍在独立顶层窗口 |
| Meterbridge / `meter-bridge` | `src/meter/mod/src/MeterbridgeWindow.cpp`、`MeterCard.cpp` | `--wjn-theme-meter-bridge-` | meter card、level/reduction/readout、按钮；MeterCard 34 处颜色 | meter bridge 空白区继承 patchbay；system | 默认 1080×740；单卡 242×834 |
| Meter history / `meter-history` | `src/meter/mod/src/MeterHistoryWindow.cpp` | `--wjn-theme-meter-history-` | 8 条稳定曲线：peak/rms/lfe/true-peak/momentary/short-term/integrated/lra；图例、统计、扫描滑块；36 处颜色 | 继承 meter bridge 背景；system | 曲线颜色必须按 ID，不按顺序 |

## 3. Native Fx 覆盖台账

所有效果都归类为 `Native Fx`，均具有独立顶层窗口和 All/L/R/C/LFE/Ls/Rs/Lb/Rb 声道选择；主题不会改变每声道参数状态或 All 广播语义。

| 稳定 ID | 绘制源文件 / 固定尺寸 | 建议命名空间 | 控件、图表、电平与分段 | 硬编码定位 |
| --- | --- | --- | --- | --- |
| `compressor` | `effects/compressor/src/Ui.cpp`，770×665 | `--wjn-theme-native-fx-compressor-` | knob、toggle、channel selector、transfer curve/grid/fill、gain-reduction meter；reduction 单段上限 0 dB，active `#ef3748` / inactive `#242c39` | 36 |
| `deesser` | `effects/deesser/src/Ui.cpp`，768×650 | `--wjn-theme-native-fx-deesser-` | knob、toggle、channel selector、spectrum/threshold/response curves、legend | 34 |
| `delay` | `effects/delay/src/Ui.cpp`，750×645 | `--wjn-theme-native-fx-delay-` | knob、toggle、combo、channel selector、tap/time/amplitude chart、filter controls | 29 |
| `gate` | `effects/gate/src/Ui.cpp`，754×654 | `--wjn-theme-native-fx-gate-` | knob、toggle、channel selector、transfer/history chart、status controls | 23 |
| `graphic-eq` | `effects/graphic_eq/src/Ui.cpp`，760×658 | `--wjn-theme-native-fx-graphic-eq-` | 31 band sliders、channel selector、frequency grid、EQ/analyzer curves and fill | 27 |
| `limiter` | `effects/limiter/src/Ui.cpp`，768×650 | `--wjn-theme-native-fx-limiter-` | knob/toggle/channel selector、transfer curve；level meter 三段：≤-12 `#1ecd6f/#0d322a`、≤0 `#f5e034/#36321b`、≤12 `#f23c4c/#391c27`；reduction 单段 ≤12 `#e13d51/#0c2230` | 31 |
| `parametric-eq` | `effects/parametric_eq/src/Ui.cpp`，1229×909 | `--wjn-theme-native-fx-parametric-eq-` | band knobs/toggles、channel selector、frequency response/analyzer/grid/markers/legend | 45 |
| `reverb` | `effects/reverb/src/Ui.cpp`，770×645 | `--wjn-theme-native-fx-reverb-` | knob/toggle/channel selector、scene/preset panels、room visualization | 28 |

## 4. Node 类型台账

这些是实际可枚举类型。通用 node card/port/wire 默认继承 `patchbay`；按用户指定顺序，S01～S12 完成前不向现有 DLL 添加主题导出。S06 只完成宿主侧 API、探测、注册、兼容和测试夹具能力，现有发布模块导出留到 S12 完成之后。

| DLL / 类型 | 稳定主题 ID（S02 冻结） | 专属界面与主题边界 |
| --- | --- | --- |
| Patchbay / Patchbay | `org.winjacknexus.patchbay` | 宿主接线盘窗口；通用节点视觉来源 |
| Digital Mixer | `org.winjacknexus.digital-mixer` | Mixer 与 SEL；节点卡继承 patchbay，设置界面绑定 mixer/SEL |
| Meterbridge | `org.winjacknexus.meterbridge` | Meterbridge 顶层窗口与 meter cards |
| Meter Mono | `org.winjacknexus.meter-probe.mono` | Probe 设置面板；节点卡继承 patchbay |
| Meter Stereo | `org.winjacknexus.meter-probe.stereo` | 同上 |
| Meter 5.1 | `org.winjacknexus.meter-probe.5-1` | 同上 |
| Meter 7.1 | `org.winjacknexus.meter-probe.7-1` | 同上 |
| Meter Custom | `org.winjacknexus.meter-probe.custom` | 同上 |
| WASAPI In | `org.winjacknexus.adapter.wasapi-in` | Adapter 设置面板和 meter array；Audio 端口类型来自模型 |
| WASAPI Out | `org.winjacknexus.adapter.wasapi-out` | 同上 |
| System MIDI In | `org.winjacknexus.adapter.midi-in` | Adapter 设置面板；MIDI 端口类型来自模型 |
| System MIDI Out | `org.winjacknexus.adapter.midi-out` | 同上 |
| Native Fx Node ×8 | 与第 3 节效果 ID 一一对应 | Node/Native Fx 双入口后续按稳定效果 ID 去重 |
| Example Node（测试夹具） | `org.winjacknexus.example-node` | 只用于 API/兼容测试，不作为发布 UI 模块 |

## 5. 电平表用途与默认分段基线

默认主题模板必须为每个用途分别导出 `active` / `inactive`，不能只提供一个通用 meter 色。

| 用途 | 范围 / 分段 | 默认颜色来源 |
| --- | --- | --- |
| shared level | 调用方范围；warning/danger/max 三段 | `MeterWidget.cpp`：`#42c875/#1b2928`、`#e1b84d/#1b2928`、`#ef615f/#1b2928`；peak `#f4f3dc` |
| mixer strip level | -72..12 dB；-12/0/12 三段 | `MixerWindow.cpp`：`#26dc68/#163e2e`、`#ffe15a/#4a3f1d`、`#ff5353/#4c242a` |
| mixer large/SEL level | -72..12 dB；-12/0/12 三段 | `#17d675/#123e2d`、`#f2df48/#483d1e`、`#f2484d/#4a2429` |
| mixer AUX send | -72..12 dB；-12/0/12 三段 | `#17d675/#102234`、`#f2df48/#102234`、`#f2484d/#102234` |
| SEL AUX send | -72..12 dB；-12/0/12 三段 | 与 mixer AUX send 当前值一致，但保持独立 token |
| Native Fx input/output | -72..12 dB；共享 FX level 样式 | `FxLevelMeterWidget.cpp`；模板按 SEL FX 宿主与效果模块分别提供继承入口 |
| Compressor reduction | reduction，单段至 0 dB | `#ef3748/#242c39` |
| Limiter input/output | -60..12 dB；-12/0/12 三段 | 见 Native Fx 表 |
| Limiter reduction | 0..12 dB，单段 | `#e13d51/#0c2230` |
| Meterbridge level/reduction | 由 `MeterCard.cpp`、`MeterWidget.cpp` 量程与用途决定 | level 与 reduction 分开注册；不得复用未经转换的阈值 |

所有用途还要分别导出 `peak`、`clip`、`tick`、`label`、`background`、`border`；当前未显式存在的 clip 视觉以绘制层内置默认值登记，不能因主题缺项停止绘制。

## 6. 推子、图表与背景基线

- 推子真实类型：Mixer input、AUX、master；Graphic EQ band slider；共享 parameter/range slider；meter-history scan cursor。不存在的 bus/output 专属类型不虚构。
- 图表稳定序列：Meter history 的 8 个 ID；Compressor transfer；De-Esser spectrum/threshold/response；Delay tap/time/amplitude；Gate transfer/history；Graphic EQ response/analyzer；Limiter transfer/ceiling；Parametric EQ response/analyzer/band marker；Mixer polar plot。
- 背景区域：patchbay、mixer、SEL Main、SEL AUX、SEL FX、meter bridge；Native Fx 是否支持背景由各描述符逐项声明。默认主题全部为 `none`，模块未定义时保留“继承 patchbay”语义，不能预填局部 `none`。
- 字体：默认包只声明 `system`；局部代码通过现有字体派生保留字号/粗细。Emoji/图标字形视为专用图标用途，不用主题正文字体强制替换。

## 7. 入口、持久化与验证基线

- Patchbay 当前在 `src/patchbay/mod/src/Library.cpp:2254` 建立 dark/light 互斥主题动作，属于 S11 要移除的重复入口。
- 托盘当前没有主题子菜单；后续在 `TrayController` 的原生 Qt 菜单中增加。
- 旧主题设置位于 `WinJACKNexus/WinJACKNexus` 的 INI `QSettings`；应用刷新设置位于 `WinJACKNexus/Application`；patchbay 项目设置独立保存。
- 自动证据入口：`tests/ui/UITests.cpp` 已覆盖 ThemePalette/ThemeManager 与共享设置控件截图，输出到现有 `build/shared-settings-*.png`。
- 本阶段应做的固定 DPI 多窗口截图对比和肉眼默认外观确认属于完全人工验收。按用户指示，本轮标记为“跳过人工验收”，不伪造通过结论；后续自动测试只验证图像生成、尺寸和非空像素。

## 8. 后续构建/测试目标

- S01 文档阶段：构建 `WinJACKNexusUITests`（以实际生成目标名为准），运行 UI 测试；人工截图比对跳过并记录。
- S02 起：新增主题契约、包样例和解析单元测试，并保持现有 UI 调用方测试通过。
- S03～S05：只改宿主主题核心、资源、字体与兼容层。
- S06：只完成宿主 Native Fx/Node 可选 API 的探测、注册、冲突、生命周期和测试夹具验证，不修改现有发布模块导出。
- S12 完成之后：才逐个修改现有 Node/Native Fx DLL，增加命名空间描述符导出并单独构建、测试和记录。
