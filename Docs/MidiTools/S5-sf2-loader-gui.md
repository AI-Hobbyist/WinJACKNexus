# S5：SF2/SFZ 加载器独立窗口

日期：2026-10-11。状态：实施、Release 构建和规定离线验证完成。

窗口复用公共 TabField、ListField、ActionButton、IntegerField、TextLabel、GroupPanel，以及公共 drawKnob / editNumericValue / drawMeter / MeterBallistics API。资源列表、信息与 SFZ 映射、16 通道混音、Master 分为四个公共卡片区。通道旋钮直径至少 40 px，Master 布局直径至少 72 px；实际公共绘制留有边距。通道间有 15 条主题边框色分隔线。没有复制旋钮、电平表实现，没有业务 QSS、固定字体或固定配色。

16 通道独立增益、Mute、Solo、实际音色名及真实音频峰值；Master 独立增益、Mute 与 L/R 电平。所有电平表范围 −72～+12 dB，静音读数 −∞ dB；电平条高度减半，增加 −72、−60、−48、−36、−24、−12、0、+12 刻度。卡片中的标签、旋钮读数与布局背景继承卡片面板色，去除多余黑底。峰值与释放使用公共 MeterBallistics，刷新遵循应用 30/60/120 FPS。关闭窗口只隐藏编辑器并停止界面刷新，合成继续；重开保留同一窗口及状态。

导入、重复拒绝、仅移除引用、显式首选资源、SFZ 的 0-based MSB/LSB/Program 映射均接入真实资源核心。浏览不改变演奏首选。失败导入保留可用音源，显示失败元数据；删除引用不删除磁盘文件。增益编辑先合并核心报告的当前 MIDI Bank/Program，避免界面操作覆盖演奏选择。修复跨格式导入后的选中资源丢失问题。

S5 还修复 SFZ 重载的预算核算：sfizz 的统计是进程全局缓冲计数，使用工作线程专属空实例读取创建前后的增量，不再把其他已加载实例重复乘以 16。预算仍是有界缓冲估算，不是进程 RSS 证明；S4 的首实例统计说明由本记录更新。

## 主题与语言

默认跟随主程序唯一默认主题 DLL 的公共 Token，字体跟随全局系统字体。三个节点新增可选主题描述符，命名空间分别为 `module-midi-tools-sf2-loader`、`module-midi-tools-midi-spilit`、`module-midi-tools-midi-matrix`；不修改冻结 ABI 或内置注册表。

完整模板与 Simple Dev 模板各提供三个独立 `node/module-midi-tools-*.css`，默认用 `var(...)` 继承公共颜色。SF2/SFZ 旋钮内底、轨道、指示、电平分段/边框/峰值及公共按钮可单独定制。矩阵窗口与三个公共改名侧栏接入对应节点配色。SF2/SFZ 描述符注册、两套 Dev 模板加载及独立旋钮 Token 热改已在本阶段验证；MIDI Matrix 已加载实际默认主题并验证按钮默认色继承，截图位于 S5-matrix-default-theme/；三节点完整发现/部署核验继续由 S6 完成。

四个语言包 en-US、zh-CN、ja-JP、ko-KR 完成。热切换不提交设置，不改变增益、Solo、SFZ 映射、所选标签页或浏览资源。

## 自动验证证据

- Release 构建：`build/MidiTools-S5-card-build.log`（最终卡片背景边界修正）；`build/MidiTools-S5-public-controls-build.log`（三个节点及必要测试目标）。
- 实窗交互：`build/MidiTools-S5-gui.log`，真实生产 DLL 独立窗口与持久化、数值输入、关闭/重开；同一窗口组合接真实离线双引擎，覆盖信号、静音/Solo/Master、映射/首选/导入/移除、四语言与 FPS、两套模板和节点 Token。
- 合成回归：`build/MidiTools-S5-core-regression.log`，真实 SF2/SFZ、16 通道隔离、旧声部、混音/控制器重置、失败导入、取消/队列/预算及采样率生命周期通过。
- 公共侧栏、分流、矩阵回归：`build/MidiTools-S5-{sidebar,split,matrix}-regression.log`，模型、真实 DLL 状态与四语言实窗通过。
- 所有 GUI 使用 `QT_QPA_PLATFORM=windows`、`QT_SCALE_FACTOR=1`，DPR=1、Microsoft YaHei UI 系统字体；原生可见 HWND 和 QScreen 抓取，检查字形非零并审阅真实截图。未使用 offscreen。GUI 主用例可见顶层显示少于 10 次，包含数值编辑/潜在 tooltip 的保守单批预算少于 40；各回归独立检查，未达到 100 次上限。
- 最终截图位于 `S5-real-window/`，中文/日文/韩文、卡片、放大旋钮、分隔线、Master、电平及主题修改均正常显示。

公共 GroupPanel 修正空标题仍绘制整宽背景的问题；有标题时背景仅覆盖文字区域。节点卡片自身使用窗口背景，内部内容继承面板背景。实窗截图增加空标题卡片边框上方的像素断言，确认没有背景外溢。

人工审校、人工视觉验收、原生文件选择器的人工操作、安装/部署人工验收：SKIPPED／已跳过（用户指令）。这些跳过项不记为通过。真实 JACK 音频/端口/工程连线集成未验证；本阶段测试未调用节点 JACK init，不启动或停止 JACK，也不干扰已有音频进程。
