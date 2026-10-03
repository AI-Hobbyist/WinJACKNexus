# 可编辑主题模板

此文件夹是完整主题的开发模板。根目录 `style.css` 管理全局 token，`meta.json` 管理主题信息；`host/`、`node/`、`native_fx/` 中每个 CSS 只对应一个模块。全部文件合计列出 807 个显式默认 token，并逐个标注类型与模块归属；另有 14 个背景图片 token 以注释形式标出继承关系。模板覆盖旋钮外环、调音台各类推子、声像器按钮、全部模块控件、图表语义和电平表颜色分段。

如希望从共享默认值开始编辑，可使用 [`../template-simple-dev`](../template-simple-dev/README.md)。它保留与本模板相同的模块结构和 Token，只将部分默认值写成 `var(...)` 继承。SEL 控件按通道类型继承推子配色；单条通道的自定义颜色只用于通道标识，不改变这些控件。

1. 修改 `meta.json` 的 `id`、`name`、`author`、`description` 和 `version`。
2. 修改 `style.css` 或对应分类文件夹中的模块 CSS。图片和字体放在 `assets/`，在 CSS 中用 `url("assets/文件名")` 引用。
3. 在此目录运行 `python build_theme.py`。生成的 `<id>.wjnskin` 位于上一级 `themes/`，可在主题管理页导入预览。

完整格式、各模块语义与背景叠层详见本目录的 [`Theme-Customization.md`](Theme-Customization.md)。本目录的 [`Theme-v1-Registry.json`](Theme-v1-Registry.json) 列出所有 Token 的类型、模块、默认值和继承关系，仅供开发参考。此目录可直接放在主程序同级 `themes/` 下作为 Dev 主题使用，托盘名称显示为 `My Theme (Dev)`；修改 CSS 后使用托盘“刷新当前 Dev 主题”即可加载新值。`meta.json`、`style.css`、三个分类文件夹下的 CSS 和 `assets/` 会进入打包主题；README、开发文档、构建脚本、AGENTS.md 和 Registry JSON 留在 Dev 文件夹中。目录里的 `winjacknexus-default.wjnskin` 只供对照；程序运行所必需的默认主题由主 EXE 同级 `wjn_default_theme.dll` 提供。
