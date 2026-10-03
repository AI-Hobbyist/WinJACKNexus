# S0 清单使用与迁移约束

日期：2026-10-03。本文件把源码清单映射到实施场景。S0 没有创建语言包，没有修改业务、窗口、端口或公开 API。

## 清单语义

`S0-keys.csv` 每行是一次源码出现位置，不是去重后的翻译条数。domain + key 是拟定语义键；同文件同义文字可共享，所属模块不同不能误用同名 key。`text.<english words>` 由完整英文短语生成，含义可读且不依赖行号、原文中文、序号或哈希。专门审阅的中文/短英文语义键来自 `S0-semantic-map.tsv`。S1 冻结包 schema 后可以整合同义键，但须同步映射记录，不能丢失原调用点。

`source_id=s0.<hash>` 仅供证据对账，不是公开 key。`source` 保留 C++ 的转义和前后空格；`placeholders` 保留当前 `%1/%2` 等占位方式。`usage=identity-guard` 表示这次出现用于身份判断，不得把比较常量变成翻译查询；它对应的显示源需要翻译，身份迁移见 S0-identity-risks.md。真正语言包工作以 `S0-key-catalog.csv` 为入口，回溯 `S0-keys.csv` 全部使用位置。

相同 key 的不同源拼写可作为中英双语原文别名，但占位符必须相同；例如同一控件语义的“阈值”和 Threshold。中英并排标题/参数说明不得在单语言下产生重复的同一句译文；S5 结合布局决定合并标题还是保留两种不同说明角色。

## 场景、控件与宽度

| 场景/文件组 | UI 文本来源与用途 | 当前宽度证据/约束 | 首次应用和刷新方式 |
| --- | --- | --- | --- |
| app/main.cpp | 全局/JACK 设置、启动 QMessageBox、托盘提示 | QFormLayout/QVBoxLayout；自定义固定控件宽度仍以源码 setFixedWidth/setFixedSize 为准，菜单原生度量 | 语言恢复先于窗口；重译标签/选项/tooltip，保留 driver data、数字 validator 和未提交值 |
| TrayController.cpp | 原生 action、快捷栏、动态节点/分类 | 原生 QMenu；快捷栏固定高度、按钮布局需按当前字体度量 | 菜单重赋值，动态菜单/快捷栏重建保留稳定动作身份 |
| Controls.cpp 与共享读数/统计控件 | 名称错误、侧栏提示、min/max 模板 | 共享布局/数字编辑区域；省略不截断底层内容和 accessibleName | 构造首次应用；正在编辑数字/名称时保留文本 |
| ThemeManagementDialog/ThemeCatalog/ThemeCore/ThemePackage/ThemeNamespaceRegistry | 管理标签、主题错误、包/描述符诊断 | 管理窗口 860×680；字段与原生菜单按布局度量，错误长文本允许换行 | 主题快照与语言独立；更新状态缓存、列表、默认主题元数据 |
| Patchbay Library/PatchbayCanvas | 节点库、菜单、子图、侧栏、自绘卡片、状态 | 接线盘初始 1440×860；canvas 节点宽度/侧栏列宽来自当前几何，重算省略与 tooltip | 分类 identity 与 DisplayRole 分离；保留选择/展开/搜索/连接/用户标签 |
| Adapter Library | WASAPI/MIDI 表单、meter 标题、状态 | SettingsPanel 的布局、固定字段高度；在线设备名来自系统，布局按字体计算 | 设备 ID 与名称原样；刷新时保留选中 device data 和未应用设置 |
| MixerWindow/MixerOverview/AuxSettingsWindow | SEL、自绘通道、总览、AUX/Sub Mix、监听 | 自绘 QRectF/缩放因子与命中区域见各 key 的 control_context/source_window；SEL 页签当前固定宽 106，不能靠缩字号塞文本 | 模型和身份不重建；更新 tab/menu/cache，自绘 update；保留通道/未提交名称 |
| FxSettingsWindow | FX 链、预设、选择/模块标题、窗口级声道操作 | Qt 共享布局与 endpoint/chip；按实际文本测量，完整名称 tooltip | All 使用 -1，声道使用 index；用户预设名不改；菜单晚建时用当前语言 |
| MeterCard/MeterHistoryWindow/MeterSettingsWidget/MeterbridgeWindow | 測量标签、图例、参考线、CSV/PNG、布局 | 共享 card/form/chart；图表轴和缓存图例单独测量，不将模型 ID 翻译 | 重新生成显示角色与曲线标签；保持历史、暂停、选中通道和记录会话 |
| 八个 Native Fx Ui.cpp / Meta.hpp / Logic.inc | 独立窗口、参数、工厂预设、曲线、显示选项、元数据 | 固定绘图 rect 和字形尺寸见每条 source_window；Compressor 770 px、748 px strip 与 x=10 主面板边界保持；其它模块以各自 setFixedSize 为证据 | 构造立刻应用当前域；语言变更触发 update 和菜单更新；All 参数广播语义不改 |
| NativeFxApi.cpp、Develop 示例 | 共享参数/预设描述、示例窗口与可选元数据 | 宿主传入控件/独立窗口尺寸；示例固定布局须测量 | 独立模块租约与首次绑定当前语言；卸载前清空 UI/回调 |
| VST Library/scanner MainWindow/Scanner/ProbeMain | 宿主错误、表单、进度、隔离探测结果 | Qt 表单/table/status；长路径完整 tooltip；隔离结果是显示模板加原始 detail | scanner 独立初始化；第三方名不翻译；VST process 保持英文诊断且不引入 UI 翻译依赖 |

清单 `source_window` 保留文字前后源码及固定宽度/rect 的直接证据，间接数组使用处由上表定位。S0 记录布局来源与限制，四语言字体、高 DPI、溢出及 hit rect 的实际渲染验证属于 S2–S6，不能以静态清单认定通过。

## 组合模板与错误

现在的拼接片段（例如重复主题 ID、先发现位置、CSS 错误行号、字段名 + 英文诊断）均在源码清单保留。迁移时先组装完整语义模板，用 `{id}/{path}/{field}/{line}` 命名参数替换拼接；不能分别翻译零散片段再按中文语序拼接。日志事件仍稳定英文，项目提示和可呈现错误翻译，OS/JACK/VST 原始 detail 原样附上。

English 的复数 one/other、参数名、重复参数与转义规则在 S1 冻结。语言切换不改变数值 locale、CSV 字段、工程键、DLL/module/port/preset ID。元数据 name 与 descriptor displayName 的查询应绑定所属模块域，不能使用 CWD 找包。

## 资源与忽略边界

`S0-resource-fields.csv` 包括 EXE 内嵌默认主题包 meta 的两条显示文字，以及模板/registry 逐字段保留项。当前主题编辑器显示 token 名，不显示 registry 的 description；这些开发者契约说明记录为排除，若后续 UI 呈现它们，必须新增翻译 key。CSS/QSS/SVG/图像不含当前需翻译的自然语言 UI 标签；QRC 路径保持原样，引用链记录在 resource manifest。

固定缩写与技术标识逐次记录在 S0-exclusions.csv，动态用户数据按 S0-dynamic-sources.md 记录，不收集用户实际工程/设备名。MUTE、MASTER、IN、POST 的现有可见操作/模式文字列入 key；M/S/SEL/MIX/PFL/AFL/SIP/AUX/FX/MAIN 等既有专业短标记原样，说明单独翻译。Gate 滞回与 Limiter 未实现参数的问题在术语/身份文档中明确保留，后续不得以未审校译文代替功能语义确认。
