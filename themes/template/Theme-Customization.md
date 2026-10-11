# WinJACKNexus 主题自定义

主题可以是 `.wjnskin` 标准 ZIP，也可以是开发用的文件夹（Dev 主题）。主程序旁的 `themes/` 是安装与开发目录；主题列表从这里读取直属包文件和主题文件夹。两种形式都在根目录放 `meta.json`、全局 `style.css`、`assets/`，可选 `host/`、`node/`、`native_fx/` 模块 CSS 文件夹。旧版仅含根目录单个 `style.css` 的主题包仍可使用。可以从 [`themes/template`](../themes/template/README.md) 复制开发模板，也可以在“主题管理”中导出主题包。

运行时的唯一默认主题由主程序同级 `wjn_default_theme.dll` 提供；DLL 缺失或无效时主程序拒绝启动。模板目录中的 `winjacknexus-default.wjnskin` 是默认配色对照包，不会作为第二个默认主题进入列表。

只需改动几种通道推子及 SEL 控件配色时，可使用 [`template-simple-dev`](../themes/template-simple-dev/README.md)。SEL 的五种 `control-accent-*` 默认引用对应的 `mixer-fader-*-thumb`；通道调色板的单条通道自定义颜色不会改变 SEL 控件，电平表分段也独立。

## 制作与安装

1. 复制 `themes/template` 到个人工作目录，修改 `meta.json` 中稳定的 `id`、名称、版本、作者、介绍和可选主页。
2. 全局外观修改根目录 `style.css`；宿主界面修改 `host/<模块>.css`，接线盘节点修改 `node/patchbay.css`，Native Fx 修改 `native_fx/<效果>.css`（共享项在 `native_fx/common.css`）。每个文件保留单个 `:root` 和 token 名称。模板合计写出 817 个显式默认值，并注释列出 15 个继承图片 token。`Docs/Theme-v1-Registry.json` 是类型、归属与继承规则的完整清单。
3. 把图片、TTF/OTF 字体放在 `assets/`，仅用 `url("assets/文件名")` 引用。无图片时使用 `none`，字体默认 `system`。
4. 开发时可将整个模板文件夹放进主程序同级 `themes/`，直接以文件夹形式加载。托盘主题名称末尾标记 `(Dev)`；当前选中 Dev 主题时，托盘子菜单提供“刷新当前 Dev 主题”，可在修改文件后重新读取。也可以运行模板中的 `python build_theme.py` 得到 `.wjnskin`，在“主题管理”导入、预览、应用。应用完整包会安装到主程序同级 `themes/`。重新展开托盘菜单会重新扫描包文件和 Dev 文件夹；同一 ID 的 Dev 文件夹优先于包文件。

Dev 文件夹保留 `README.md`、`Theme-Customization.md`、`AGENTS.md`、`build_theme.py` 和模板 `.wjnskin` 供开发使用；这些辅助文件不作为主题数据加载。主题数据仍只从元数据、CSS 和 `assets/` 读取。开发主题应先修改 `meta.json` 的稳定 ID，避免与其他主题重名；编辑 CSS 后使用刷新入口检查效果。

`kind=full` 是完整主题；`kind=fragment` 只包含指定命名空间，导入时叠加到当前完整主题。导出当前完整主题会固化已解析值；导出模块片段会固化该模块继承和 `var()` 得到的值及图片资源。片段不携带全局字体，文字继续使用目标主题字体。未安装模块的合法命名空间数据会保留，待模块注册时重新校验。

## 模块级控件

token 格式是 `--wjn-theme-<模块>-<角色>`。同类控件按模块分别设置；例如调音台推子不会改变 Graphic EQ 的频段滑条。下表中的每一项都能在模板对应模块 CSS 中找到完整 token 名、默认值和类型。加载顺序为根目录 `style.css`，再按路径排序的模块 CSS；重复键以后声明为准并给诊断。

| 命名空间 | 控件和语义 |
| --- | --- |
| `global` | 共享窗口/面板、文字、边框、按钮/输入/选择/列表、开关、旋钮内底/外环轨道/指示、普通 slider 与通用推子、图表、通用电平表；`font-source` 控制全局字体。 |
| `patchbay` | 节点卡片、标题/标签、Audio 与 MIDI 输入/输出端口、连线正常/悬停/选中/无效状态、工具栏自绘按钮与缩放控件、右键框选、全览框节点和视口，以及背景。`overview-viewport` 是全览框中可拖动的小方框；缩放画布时它反向变化，并保持最小可抓取尺寸。 |
| `mixer` | 输入、AUX、Submix、监听、主控推子的 `thumb`、`track`、`grip`，PFL/AFL 状态按钮、监听面板与三段电平。未自定义的通道颜色以及总览类型标题跟随对应 `fader-{input,aux,submix,master}-thumb`；监听使用 `fader-monitor-thumb`。监听通道条的 SEL 与其他通道条共用 `control-button-surface-normal`、`control-button-text-normal`，其边框色由 `monitor-select-accent` 控制。 |
| `sel-main` | 通道旋钮、控制按钮、主电平及发送电平、输入/输出极坐标曲线与背景；声像模式、输入/输出曲线和重置按钮分别使用 `control-panner-{mode,input,output,reset}-button-*`。标题旁的调色板菜单可为 Input、AUX、Submix、Master 设置或恢复默认通道颜色。 |
| `sel-monitor` | 监听 SEL 的独立背景图片、位置、缩放、模糊与叠层；默认继承接线盘背景。监听控件及图表继续使用 `sel-main` Token。 |
| `sel-aux` | AUX 与 Submix 共用界面的旋钮、开关、发送电平、输入/输出极坐标曲线与背景；声像模式、输入/输出曲线和重置按钮分别使用 `control-panner-{mode,input,output,reset}-button-*`。 |
| `sel-fx` | 效果链卡片、端点和状态控件、电平与背景；效果本身窗口属于各 `native-fx-*` 作用域。 |
| `meter-bridge` | 电平卡片、读数/标签、三段电平、历史图表与八条测量曲线、背景。 |
| `native-fx-compressor` | 旋钮内底、外环轨道、指示、开关、传输曲线、曲线下动态填充及压缩量表。 |
| `native-fx-deesser` | 旋钮、开关、齿音响应钟形曲线、输入/输出频谱。 |
| `native-fx-delay` | 旋钮、开关、主延迟抽头、重复抽头和基线。 |
| `native-fx-gate` | 旋钮、开关、下/上传输曲线。 |
| `native-fx-graphic-eq` | 旋钮、频段滑条 `slider-effect-band-{track,border,fill,thumb}`、输入/输出频谱、输出频谱填充和响应曲线。频段滑条不是推子。 |
| `native-fx-limiter` | 旋钮、开关、传输曲线及填充、三段 level 表和单色压缩量表。 |
| `native-fx-parametric-eq` | 四频段旋钮、输入/处理后频谱、四条频段曲线及填充、总响应曲线。 |
| `native-fx-reverb` | 旋钮、开关、声场图示、紫/青指示；没有 dB 响应曲线或独立电平表。 |

现有 Native Fx DLL 的自绘窗口及描述符按计划在 S12 后逐模块接入。模板现在已给出它们的完整角色与默认值；主题包可先保存这些命名空间。

## 图表与电平颜色

`chart-series-<语义>-stroke` 只给线条改色；`-fill`、`-fill-start`、`-fill-end` 改面积或渐变。SEL Main/AUX 的 `polar-input` 与 `polar-output` 是两条不同的极坐标曲线。电平表桥的 `peak`、`rms`、`lfe`、`true-peak`、`momentary`、`short-term`、`integrated`、`lra` 是八条独立历史序列。Compressor 的 `level-fill` 是响应曲线下的动画面积；Limiter 的 `transfer-fill` 是传输曲线填充；Graphic EQ 的输出频谱填充有 start/end 两端；Parametric EQ 的 `band-1`～`band-4` 与 `total-response` 分开设色。

SEL 空间声像图和 FX 链箭头在图表 Token 保持内置默认值时，随所选通道类型的强调色同步。完整主题或覆盖层若为相应图表 Token 指定不同颜色或 `var(...)`，则优先按该自定义值绘制；输入/输出曲线及箭头线段/箭头分别保留独立的自定义入口。

`meter-<种类>-zone-<段>-active/inactive` 分别控制亮起与未亮起格。`level` 三段边界固定为低段 `< -12 dB`、中段 `-12～0 dB`、高段 `>= 0 dB`，可见标尺上限为 `+12 dB`；颜色不能修改边界。调音台、SEL、电平表桥和 Limiter 的 level 表使用 low/mid/high；Compressor/Limiter 的 reduction 表使用 `single` 段。峰值、削波、刻度、标签、底色和边框是同表的独立 token。建议在模板中同时调整 active/inactive 成对颜色，以免未亮起区域掩盖分段。

## 背景与叠层

监听 SEL 使用独立的 `sel-monitor-background-*` 配置。未单独指定时，它的图片和背景参数继承接线盘；可在主题管理的背景模块列表中单独选择 `sel-monitor`。

除 `global` 外，各模块的背景图片及其相关选项默认继承 `patchbay`；默认主题和模板通过 `var(--wjn-theme-patchbay-background-...)` 保持联动，模块内显式设置可覆盖继承。`background-image` 可不声明以继承接线盘图片、写 `none` 禁用，或写 `url("assets/...")` 自定义。`size` 选 original/contain/cover/stretch；`scale` 为 0.05～4 的等比缩放；repeat、position-x/y、opacity、blur-radius、component-opacity、overlay-enabled、overlay-opacity 均可按模块设置。`position-x/y` 支持百分比和 px，可让角色固定四角或一侧。上层覆盖模式：透明背景图片最佳。

垫底模式的叠层是组件上、图片下，可同时调组件透明度与图片高斯模糊。上层覆盖模式的叠层是图片上、组件下，图片点击穿透，`overlay-opacity` 可调高以清楚辨别；这一模式忽略模糊和组件透明度。示例素材位于 [`background`](../background/)：`grid.jpg`、`grid2.jpg` 是全界面图，`character.png`、`character2.png` 是角色图。四角测试先等比缩放角色，再调位置。

## 验证与限制

导入前先按 [`Theme-v1-Contract.md`](Theme-v1-Contract.md) 校验元数据、CSS、图片/字体大小、扩展名与资源路径。诊断在主题管理页展示；无效值会回退并给出行号，结构错误不会替换正在使用的主题。主页只在用户点击按钮时打开。`tests/preview/<阶段>/` 保存实际 Windows 弹窗截图；改过 token 的截图以 `-modified` 结尾。截图可供人工审阅，自动像素变化通过不等于人工观感已验收。

## MIDI Tools 节点模板

三个新增节点分别使用 `node/module-midi-tools-sf2-loader.css`、`node/module-midi-tools-midi-spilit.css`、`node/module-midi-tools-midi-matrix.css`。各文件只设置对应节点的命名空间，默认通过 `var(--wjn-theme-global-...)` 继承公共主题；修改节点 CSS 不会改变其他模块。字体继续使用全局系统字体。

SF2/SFZ 加载器复用公共旋钮、数值编辑、按钮和电平表；16 通道与 Master 左右电平范围均为 −72～+12 dB。旋钮内底、轨道、指示和电平三段 active/inactive、边框、峰值可单独定制。通道分隔线使用本节点 `border-normal`。分流节点只有公共客户端设置侧栏；矩阵节点使用公共可切换按钮。

这些扩展 Token 随节点 DLL 的可选主题描述符注册，不修改冻结的内置注册表。原有 Token 数量说明指基础模板，新节点 CSS 是额外模块。模块未安装时主题数据仍会保留。人工主题审阅已跳过（用户指令），自动实窗验证不代表人工视觉验收通过。
