# Simple Dev 主题模板

本模板与 [`../template`](../template/README.md) 具有相同的模块结构：根目录 `style.css`，以及 `host/`、`node/`、`native_fx/` 下全部模块 CSS。18 个命名空间和全部模块级 Token 均保留；**Simple** 指部分默认值使用 `var(...)` 继承，并非省略模块或控件。未声明的 15 个背景图片 Token 按注册表规则继承。

先修改 `meta.json` 的 `id`、名称和作者，再按模块编辑 CSS。通用背景选项、部分相同语义且相同默认值的控件与电平项引用 `global`；其他模块值可独立修改。每个图表语义与电平颜色分段都有模块级 Token，若需独立改色，可把对应的 `var(...)` 换为明确颜色。五种 `control-accent-*` 默认引用对应类型的推子 Token；通道调色板设置的单条通道自定义颜色不会改变主题 Token。

完整的模块语义、图表、电平分段和背景选项见本目录的 [`Theme-Customization.md`](Theme-Customization.md)。[`Theme-v1-Registry.json`](Theme-v1-Registry.json) 列出所有 Token 的类型、归属、默认值与继承规则，仅供开发参考，不会进入主题包。目录内的 `winjacknexus-default.wjnskin` 是默认主题对照包。

把此目录放在主程序同级 `themes/` 下可作为 Dev 主题加载。修改后使用“刷新当前 Dev 主题”；也可在本目录运行 `python build_theme.py`，在上一级 `themes/` 生成 `.wjnskin` 包。主题视觉效果由人工验收。
