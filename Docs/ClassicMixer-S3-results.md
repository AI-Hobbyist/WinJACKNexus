# Classic Mixer S3 结果

日期：2026-10-18。S2 检查点 8795964 已推送。

- 新增公共 ConsoleKnob、ConsoleFader、ConsoleSeparator 和 bindConsoleTheme；新增公共 ClassicMixerWindow，模块只绑定状态/提交/真实电平，不写业务私有绘制。
- 三布局独立窗口；固定行槽/对齐推子，输入区水平滚动、Master/Monitor 固定。截图检查后移除了固定区的占位滚动条。公共推子角色默认灰/红/黄，表头 -72..+12 dBFS。
- Node GUI feature 已启用；首次节点准备选择固定布局，项目恢复跳过选择。侧栏继续只提供公共节点名编辑。
- 主题使用模块描述符、作用域 module-winjacknexus-classic-mixer；i18n v2 Node 描述符、classicMixer 域、四语言完整 catalogs；已知设置诊断在显示层翻译，名称与稳定身份不翻译。
- Release 相关目标最终构建通过：build/classic-mixer-s3-build.log。构建期间已修正构造参数遮蔽、测试 QPoint 重载，以及增量编译中更新源文件造成的旧对象缺少新增符号问题。
- 最终实窗批次：PASS 630 Windows 100% GUI checks; window shows=6。日志 build/classic-mixer-s3-gui-tests.log。实际 Windows QPA、DPR=1.0、系统字体；没有 offscreen。
- 覆盖三个布局、每条分隔、单/双表、推子行程对齐、固定区、真实 DLL 电平输入、键盘参数/推子真零、AFL/PFL 互斥、Pre 与测点、预设新建/应用/覆盖/删除、热切换四语言不提交草稿、CJK 字形非零、主题/FPS 不改音频状态、侧栏唯一编辑项、关窗 DSP 继续和重开状态。630 包含各语言字形逐项检查，不表示 630 个不同场景。
- 真实窗口截图位于 build/classic-mixer-s3-evidence，代表检查 8-zh-CN.png、24-ko-KR.png；可见字体正常、没有方框，主/监听区域保持固定。最终批次显示次数为 6；先前两批也是各 6，均低于每批 100。
- 离线模块回归 PASS 134，build/classic-mixer-s3-module-tests.log。没有启动 JACK，真实连接集成未执行。人工验收已跳过（用户指令），跳过不等于通过。
- 根 UI API 文档已有用户改动，本阶段仅提交新增加的 Classic Mixer 契约段；接线盘源码也仅提交类别映射，保留已有进程优先级改动。

S4 将验证实际宿主工程保存/重开及主子图组合，并制作增量包。
