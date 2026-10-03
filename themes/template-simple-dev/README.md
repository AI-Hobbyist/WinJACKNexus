# Simple Dev 主题模板

这是一个可直接放在主程序同级 `themes/` 下加载的精简完整主题。未声明的外观沿用程序默认主题。先修改 `meta.json` 中的 `id` 和名称，再编辑 `host/mixer.css` 的五种推子颜色；SEL 控件默认继承对应类型推子的颜色，和通道调色板设置的单条通道自定义颜色无关。电平表的颜色分段保持独立。

需要让 SEL 控件与同类型推子使用不同颜色时，在 `host/sel-main.css` 将相应的 `control-accent-*` 值改为明确的颜色。完整的模块级 Token、图表语义、电平表颜色分段及背景选项见 [`../template/Theme-Customization.md`](../template/Theme-Customization.md)。本目录的 [`Theme-v1-Registry.json`](Theme-v1-Registry.json) 列出 Token 规范，仅供开发参考，不会进入主题包。

在此目录运行 `python build_theme.py`，生成包位于上一级 `themes/`。也可以直接选择 Dev 目录并通过“刷新当前 Dev 主题”预览编辑结果。
