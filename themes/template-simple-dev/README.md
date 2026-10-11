# Simple Dev 主题模板

本模板与 [`../template`](../template/README.md) 具有相同的模块结构：根目录 `style.css`，以及 `host/`、`node/`、`native_fx/` 下全部模块 CSS。18 个命名空间和全部模块级 Token 均保留；**Simple** 指部分默认值使用 `var(...)` 继承，并非省略模块或控件。未声明的 15 个背景图片 Token 按注册表规则继承。

先修改 `meta.json` 的 `id`、名称和作者，再按模块编辑 CSS。通用背景选项、部分相同语义且相同默认值的控件与电平项引用 `global`；其他模块值可独立修改。每个图表语义与电平颜色分段都有模块级 Token，若需独立改色，可把对应的 `var(...)` 换为明确颜色。五种 `control-accent-*` 默认引用对应类型的推子 Token；通道调色板设置的单条通道自定义颜色不会改变主题 Token。

完整的模块语义、图表、电平分段和背景选项见本目录的 [`Theme-Customization.md`](Theme-Customization.md)。[`Theme-v1-Registry.json`](Theme-v1-Registry.json) 列出所有 Token 的类型、归属、默认值与继承规则，仅供开发参考，不会进入主题包。目录内的 `winjacknexus-default.wjnskin` 是默认主题对照包。

把此目录放在主程序同级 `themes/` 下可作为 Dev 主题加载。修改后使用“刷新当前 Dev 主题”；也可在本目录运行 `python build_theme.py`，在上一级 `themes/` 生成 `.wjnskin` 包。主题视觉效果由人工验收。

## MIDI Tools 节点模板

三个新增节点分别使用 `node/module-midi-tools-sf2-loader.css`、`node/module-midi-tools-midi-spilit.css`、`node/module-midi-tools-midi-matrix.css`。各文件只设置对应节点的命名空间，默认通过 `var(--wjn-theme-global-...)` 继承公共主题；修改节点 CSS 不会改变其他模块。字体继续使用全局系统字体。

SF2/SFZ 加载器复用公共旋钮、数值编辑、按钮和电平表；16 通道与 Master 左右电平范围均为 −72～+12 dB。旋钮内底、轨道、指示和电平三段 active/inactive、边框、峰值可单独定制。通道分隔线使用本节点 `border-normal`。分流节点只有公共客户端设置侧栏；矩阵节点使用公共可切换按钮。

这些扩展 Token 随节点 DLL 的可选主题描述符注册，不修改冻结的内置注册表。原有 Token 数量说明指基础模板，新节点 CSS 是额外模块。模块未安装时主题数据仍会保留。人工主题审阅已跳过（用户指令），自动实窗验证不代表人工视觉验收通过。
