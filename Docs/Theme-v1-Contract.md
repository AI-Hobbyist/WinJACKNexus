# 主题数据契约 v1（S02）

本文件规定主题系统的数据契约。`Theme-v1-Registry.json` 逐项固定 token 的完整名称、显式 `ownerScope`、类型、原始默认值、有序继承链和说明；`Theme-v1-Defaults.css` 是有值 token 的可编辑基线。两文件目前是数据契约，并不代表运行时已经接入。`tests/theme/BuildRegistry.py` 检查两文件同步、继承目标/类型/环、样例声明及 Compressor 描述符；运行时必须读取注册表字段，不按字符串前缀推断归属。

## 容器和能力版本

`.wjnskin` 是标准 ZIP，根目录直接包含 `meta.json`、全局 `style.css`、`assets/`，可选 `host/`、`node/`、`native_fx/` 中的单模块 CSS。旧版单文件主题仍有效。路径以 `/` 分隔，UTF-8 编码，大小写敏感。`formatVersion=1`、`themeApiVersion=1`；本版只接受整数 1。导入时先完整校验候选包，再原子激活；文件、ZIP、元数据或 CSS 结构性错误均不能改变活动快照。主题内不能有 DLL、脚本或远程资源。

安装与发现的默认文件夹是 `QCoreApplication::applicationDirPath() + "/themes"`，即主 EXE 同级 `themes`。枚举该目录直属的 `.wjnskin` 文件（扩展名大小写不敏感，ZIP 内部条目大小写仍敏感）和包含 `meta.json`、`style.css` 的直属文件夹；文件夹作为 Dev 主题，菜单名称后附 `(Dev)`，选中后可从托盘菜单刷新当前 Dev 主题。校验成功且 `kind=full` 的主题形成切换列表；片段只在管理页叠加。默认主题由主 EXE 同级必需的 `wjn_default_theme.dll` 提供，缺失或无效时拒绝启动；列表只显示这个默认主题，目录中同 ID 的包不重复列出。`themes/template` 是可编辑的 Dev 模板，内含原默认 `.wjnskin` 包、开发文档及 AGENTS.md。目录不存在时创建空目录；不可读取、损坏或 ID 冲突的文件不进入可激活列表，给可定位诊断。导入安装和默认导出使用此目录，扫描后及时刷新 UI；激活后关闭包文件句柄，重启根据保存的 ID 重新加载该目录中的包或文件夹。完整主题同 ID 冲突拒绝后扫描项，按规范化名称稳定排序保证确定性。目录扫描与 ZIP 加载不在实时音频线程。

资源预算按单个包计：包文件最多 32 MiB，ZIP 条目最多 512，声明总解压字节最多 128 MiB，实际增量解压累计最多 128 MiB，单条目最多 32 MiB，`meta.json` 最多 64 KiB，单个 CSS 及合并后的 CSS 均最多 1 MiB，图片原始文件最多 16 MiB 且解码后最多 32 Mpix，字体单文件最多 16 MiB。目录条目不计内容字节，但也计入条目数。若 ZIP 加密、条目路径重复（按大小写敏感规范名）、路径绝对/含 `..`/反斜杠、符号链接、ZIP64 声明越界、CRC 不符、条目交叉或类型不支持，则拒绝整包。压缩方法仅 Stored 和 Deflate；为防压缩炸弹，任一非空条目的未压缩/压缩比上限 200，并同时施行逐块累计上限。`assets/` 可为空但目录条目必须存在；模块目录可选，模块文件为该目录直属的小写短横线文件名 `.css`，包内其他资源放在 `assets/`。

允许的图片后缀 `.png/.jpg/.jpeg/.webp`，实际字节须可被发布版 Qt 解码；不允许 SVG、动画或多帧图片。字体只允许 `.ttf/.otf`，从内存注册；默认值 `system`。资源 URL 仅允许 `url("assets/<file>")`，双引号必需，不接受绝对/网络/查询/片段/转义路径。可选 `preview` 同样指向图片资源。资源引用携带包身份，片段和完整主题同名路径不混淆。

## 元数据

必需字段：`formatVersion` 整数、`id`、`name`、`version`、`author`、`description`、`homepage` 字符串、`themeApiVersion` 整数、`kind`、`namespaces` 字符串数组。`id` 用小写 ASCII DNS 反向域名加短横线的稳定形式（示例 `org.example.ocean-night`），长度 3～128；`version` 为 `MAJOR.MINOR.PATCH`，各段非负十进制且无前导零。`name` 1～128 字符、`author` 1～128 字符、`description` 1～2048 字符，trim 后均非空；`homepage` 为空串或有效 HTTP/HTTPS URL，禁止凭据和控制字符。`kind` 只能为 `full` 或 `fragment`。`namespaces` 必须非空、无重复，成员精确匹配注册表拥有的 scope 前缀；未知但规范合法的完整作用域可保存并警告，绝不能以字符串前缀推断其归属。片段声明的每个键必须属于声明作用域；完整主题可有缺项并由默认快照补齐。`preview` 可选。未知可选元数据原样保留并给诊断；未知必需字段值不允许改变既有字段解释。

## CSS 子集与类型

只接受 UTF-8、连续的 `:root { ... }` 块、`/* ... */` 注释以及注册变量声明。模板的每个 CSS 文件使用一个 `:root`；运行时先合并根目录 `style.css`，再按路径排序合并模块 CSS；重复键后者生效并给出行号诊断。声明名必须是注册表给出的完整键；名称字符为小写 ASCII、数字和 `-`，以 `--wjn-theme-` 开头。属性间使用 `;`；尾声明可省略末尾分号。无选择器、`@` 规则、任意 QSS、嵌套块或自定义未知变量。未知键给出行号警告但不参与绘制；未知合法模块作用域的原始声明可随包保存和再次导出。`var(--wjn-theme-..., fallback)` 可嵌套，先解析引用和循环，再按注册类型校验；缺失引用用 fallback，没有则该声明无效。无效声明作缺项处理并附行号/键/原因。CSS 语法不可恢复时拒包。

注册类型：`color` 为 `#RRGGBB`、`rgb(0..255,...)`、`rgba(0..255,...,0..1)` 或 `transparent`；禁 `#RRGGBBAA`。`opacity` 为有限数 `0..1`。`boolean` 为 `true/false`。`position` 为有限百分比或有限 `px`。`blur-radius` 为有限 `0..64px`，默认 `0px`。`background-scale` 为有限数 `0.05..4`，默认 `1`，乘以 size 模式所得图片尺寸。`image` 为 `none` 或上述包内 URL。`font` 为 `system` 或 TTF/OTF URL。`background-size` 枚举为 `original/contain/cover/stretch`，`background-repeat` 枚举为 `no-repeat/repeat/repeat-x/repeat-y`。值处理不进入实时音频线程。

## 作用域与查询顺序

已核对稳定作用域：`global`、`patchbay`、`mixer`、`sel-main`、`sel-aux`、`sel-fx`、`meter-bridge`、`native-fx`、`native-fx-compressor`、`native-fx-deesser`、`native-fx-delay`、`native-fx-gate`、`native-fx-graphic-eq`、`native-fx-limiter`、`native-fx-parametric-eq`、`native-fx-reverb`、`module-winjacknexus-example-node`。最后一个是为示例 Node v1 主题描述符拟定的稳定 ID，现有 Node API v2 仅给翻译前名称，不能拿名称当运行时实例 ID。第三方 Node 使用 `module-<vendor-id>-<module-id>`；两个部分由其描述符显式给出，宿主不拆分字符串猜测。

token 全名为 `--wjn-theme-<scope>-<component>-<role>[-<state>]`。每个注册项显式存储 `{name, ownerScope, type, default, inherits[], description}`；`ownerScope` 和 `inherits[]` 是结构字段，不能用字符串前缀推断。注册时拒绝重名、无效类型、继承环、范围越界及缺失默认值，唯独继承背景图片可用 `default: null` 表示“未声明”。同一 `component/role/state` 在模块与全局均有定义时，描述符写出模块专属 → 模块通用 → 家族 → 全局的有序继承链；不能先把默认值注入局部键。来源层级为按主题 ID 保存的用户覆盖 → 后应用片段优先 → 当前完整主题；在每个候选键上查这三层，全部未命中才取该模块注册的原始默认值。已有实例自定义颜色优先于上述查找。无描述符模块使用共享 UI 保底 Palette 并给诊断；描述符缺失不阻止模块加载。

背景 `image` 是例外：模块显式值 → patchbay 当前有效值 → 无图片；`none` 是有效显式值并阻断继承，不能作为模块预填默认。其余背景参数不从 patchbay 继承：`opacity=1`、`size=cover`、`repeat=no-repeat`、`position-x/y=50%`、`overlay-enabled=false`、`overlay-opacity=0.12`。背景图永远基于内容视口逻辑像素定位，颜色底层 → 图片 → 网格/组件 → 可选覆盖层 → 交互反馈；覆盖层输入穿透。图片加载失败保持底色并给诊断，不触发背景继承重试。

## 描述符 JSON v1

宿主接受一个 UTF-8 JSON 对象；类型和最大长度受包与模块注册器共同限制。必需顶层字段：`descriptorVersion: 1`、`themeApiVersion: 1`、`moduleId`、`namespace`（完整 `--wjn-theme-...-`）、`displayName`、`tokens` 数组。可选 `controls`、`chartSeries`、`meterStyles`、`backgroundAreas` 数组。每个 token 包含 `name`、`type`、`default`、`description`，可选 `inherits`（有序完整 token 名数组）、`values`（枚举/范围说明）、`states`。默认值必须与同一 CSS 值类型解析器一致。所有数组用稳定 ID，不以显示文本或绘制顺序编号。描述符最大 1 MiB、最多 4096 token，ID 长度 1～96，描述文本最长 2048 字符；所有字符串复制进宿主后模块即可卸载。`namespace` 必须等于注册作用域，`moduleId` 与模块元数据中的稳定 ID 匹配。

Native Fx 可选 C 导出精确为 `uint32_t wjn_fx_theme_api_version(void)` 和 `const char* wjn_fx_theme_namespace_json(uint32_t index)`；版本 1、非法索引返回 null。Node 可选 C 导出精确为 `uint32_t wjn_get_node_theme_api_version(void)` 和 `const char* wjn_get_node_theme_namespace_json(uint32_t index)`；索引对应 `wjn_get_node_count()` 节点类型而非实例。两者均不改核心 API ABI，缺少可选导出的旧 DLL 正常加载。宿主查询时立即复制 UTF-8，版本不兼容或 JSON 无效仅停用主题能力。双入口用 `{moduleId, namespace}` 去重，内容一致共享引用，内容冲突或不同模块抢占命名空间则拒绝后注册项。索引数值不用于跨 API 对齐；两入口均释放后注销注册项。S06 仅实现接口、宿主和测试夹具；现有 Node / Native Fx DLL 的导出在 S12 后逐模块加入。

## 模块语义清单

| 作用域 | 控件类别 | 图表稳定系列 ID | 电平语义及分段 | 推子业务类型 |
| --- | --- | --- | --- | --- |
| global | button/toggle/knob/input/select/list/scrollbar/fader/slider | 不适用：共享算法无业务系列 | level/reduction 只提供全局保底色，不设置量程 | generic |
| patchbay | node-card/node-title/node-port-label/node-port/node-wire | 不适用：连线非图表曲线 | 不适用：无电平表 | 不适用：无推子 |
| mixer | button/toggle/fader/meter | 不适用：调音台条无图表 | level: low(-72,-12], mid(-12,0], high(0,12] dB | input, aux, master |
| sel-main | button/toggle/knob/meter | polar-input, polar-output | level/aux-send: low(-72,-12], mid(-12,0], high(0,12] dB | 不适用：发送为旋钮 |
| sel-aux | button/toggle/knob/meter | polar-input, polar-output | aux-send: low(-72,-12], mid(-12,0], high(0,12] dB | 不适用：发送为旋钮 |
| sel-fx | fx-chain-card/button/toggle | 不适用：FX 链无独立图表 | 不适用：FX 链无独立表 | 不适用：FX 链无推子 |
| meter-bridge | meter-card/toggle/legend/chart | peak, rms, lfe, true-peak, momentary, short-term, integrated, lra | level: low/mid/high，其边界取当前表实例的 warning/danger/maximum 配置，不固化未经转换的 dB 值 | 不适用：无推子 |
| native-fx-compressor | knob/toggle/meter/chart | transfer, level-fill | reduction: 当前源码单段；其余表按源码实际用途 | 不适用：无推子 |
| native-fx-deesser | knob/toggle/chart | response-bell, input-spectrum, output-spectrum | 不适用：当前 UI 无分段表 | 不适用：无推子 |
| native-fx-delay | knob/toggle/chart | tap-primary, tap-repeat, baseline（离散 tap 线语义） | 不适用：当前 UI 无分段表 | 不适用：无推子 |
| native-fx-gate | knob/toggle/chart | lower-transfer, upper-transfer | 不适用：当前 UI 无分段表 | 不适用：无推子 |
| native-fx-graphic-eq | knob/slider/chart | input-spectrum, output-spectrum, response | 不适用：当前 UI 无分段表 | effect-band 由 `Controls::drawSlider` 绘制，非 `drawFader` |
| native-fx-limiter | knob/toggle/meter/chart | transfer | level: low(-60,-12], mid(-12,0], high(0,12] dB；reduction 单段至 12 dB | 不适用：无推子 |
| native-fx-parametric-eq | knob/toggle/chart/legend | input-spectrum, processed-spectrum, band-1..band-4, total-response | 不适用：当前 UI 无分段表 | 不适用：无推子 |
| native-fx-reverb | knob/toggle/scene | 不适用：声场场景不是值曲线 | 不适用：当前 UI 无分段表 | 不适用：无推子 |
| module-winjacknexus-example-node | 不适用：无自绘 UI | 不适用 | 不适用 | 不适用 |

## token 族

以下族在每个适用作用域逐项注册；不适用模块不虚构 token。每个可见控件实例只绑定模块 scope、类别、业务角色和状态，永不按运行时实例编号生成键。

- 界面：`surface-window/panel/raised/empty`、`text-primary/muted/disabled`、`border-normal/focus/selected`、`accent-primary/secondary`、`state-danger/success`。
- 共享控件：`control-<button|toggle|knob|input|select|list|scrollbar>-<surface|text|border|indicator>-<normal|hover|pressed|selected|disabled|focus>`；不适用状态无需注册，实际绘制优先取精确状态键，再取 normal。旋钮的 `background-base` 在 `drawKnob` 中按原算法 `darker(135)` 形成盘面，`track` 是非活动环，`indicator-normal` 同时供活动环与指针使用；模块可按业务色细分，例如 Parametric EQ 的四频段旋钮及 Reverb 的 cyan/violet 旋钮。开关有 `on/off` 指示。`drawSlider` 另用 `control-slider-track/border/fill/thumb`，业务类滑条覆盖为 `slider-<business-type>-...`。
- 图表：`chart-background`、`chart-grid-major/minor`、`chart-axis`、`chart-zero-line`、`chart-tick`、`chart-tick-text`、`chart-legend-background/border/text`、`chart-series-<stable-id>-stroke/fill/marker/legend-swatch`。现有渐变填充（Graphic EQ 输出频谱）另外注册 `fill-start/fill-end`，保持两个色标。legend-swatch 默认继承对应系列 stroke；描边绘制用 `Qt::NoBrush`，填充是独立路径。
- 电平：`meter-<level|reduction>-zone-<stable-id>-active/inactive`、`meter-<kind>-peak/clip/tick/label/background/border`。分段边界和单位是描述符语义，不可由主题值改变。
- 推子：`fader-<business-type>-thumb/thumb-border/grip/track/fill/tick/label/state-disabled`。原有 `Controls::drawFader` 用 thumb 强调色生成亮暗渐变，并将 `grip` 以 alpha 130 绘制；未声明 thumb-border 时保留原 `accent.lighter(135)` 结果，显式主题值才替换。全局 generic 是保底，mixer/input、mixer/aux、mixer/master 彼此独立。Graphic EQ 的 effect-band 实际调用 `Controls::drawSlider`，使用 `slider-effect-band-track/border/fill/thumb` 四个独立颜色，不借用 mixer 推子样式。
- 节点：`node-card-background/border/selected-border`、`node-title-background/text`、`node-port-label-background/text`；类型专属 `node-port-label-<audio|midi>-background/text`、`node-port-<audio|midi>-<input|output>-color`、`node-wire-<audio|midi>-color/hover/selected/invalid`。错误 → 选中 → 悬停 → 常态；缺失状态回基础色。拖拽预览线按真实待连接端口类型。Audio/MIDI 最终常态相同仅警告，默认主题必须分色。
- 背景：`background-image/opacity/size/scale/repeat/position-x/position-y/blur-radius/component-opacity/overlay-enabled/overlay-opacity`。图片未定义与显式 none 不同；垫底模式可独立调节上层模块组件透明度，上层覆盖模式忽略 `component-opacity`；无适用背景区域的 Native Fx 不强行注册。
- 全局字体：只有 `global-font-source`，默认 `system`；图标字体由应用保留，模块不可单独换字体。

S02 注册表固定 404 个有值 token 与 13 个继承背景图片的未声明 token。各模块的曲线系列、适用表段、推子/滑条业务类型和开关颜色以源码默认值登记；未注册的绘制装饰颜色在 S07 迁移时只能通过增补独立语义 token 接入，不得复用别的模块 token 填色或修改现有 token 的类型与含义。`BuildRegistry.py` 对默认值、样例和描述符执行同一名称与类型检查；完整 CSS 值解析及运行时继承验证属于 S04。
