# 公开仓库前 S6 与最终交付记录

2026-10-11：S1～S6 实现、定向 Release 构建、规定离线及 100% Windows 实窗验证完成。每阶段单独提交推送，S1 27493e3、S2 aa0a12e、S3 6e9ff04、S4 70dc5d6、S5 f6653d2；S6 见本文件 Git 历史。没有更改仓库公开状态。

## 最终核对

完整节点矩阵见 PrePublic-S6-inventory.md：23 内置模块、3 个 MIDI 重复输出、2 SDK 示例，共 28 实际 DLL；另核对子图和四接口内建条目、四种位数/格式 VST 扫描条目。除计划指定双能力节点外均 JACK-only。11 个内置双模式模块逐个同时创建两个固定模式实例，修改 VIRT 设置及保存恢复不会改变 JACK 状态；不为此调用真实 JACK init。实际四种 VST 组合还同时运行私有替身 JACK sandbox 与 VIRT sandbox，保留独立 gain=0.5/0.25 和真实输出值。

S6 扩展原图调用点测试，保持主图/子图库、搜索、添加菜单、直接创建与工程恢复的共同权限限制。2550 项检查通过，含三层音频/MIDI 偏移、一个根一客户端/两个根分别注册、主图仅 JACK/内部仅 VIRT DSP、关闭编辑器继续处理、删除及恢复、状态独立。工程有 6 条模块记录，包含子图中的 JACK-only、旧 v3、缺失模块；读取恢复不覆写源文件，用户触发保存后仍保留所有参数与待恢复连线。

侧栏 tooltip 的三能力和是/否四语热切换、无 GUI 按钮与原生菜单禁用仍通过。Split 显示名称及四语翻译在 S4 通过并在最终子图实窗可见，稳定 midi_spilit 身份及 JACK 端口名没有变更。SF2/SFZ 单资源生命周期离线回归通过，destroyed=13，损坏库、缺失样本和错误 VST IPC 协议的错误输出为预期负例。

## 命令与证据

所有构建、测试及打包都在原 build 内、前台 PowerShell 使用 2>&1 | Tee-Object -FilePath build.log，立即捕获 LASTEXITCODE。VS18 CMake、Release、WJN_ENABLE_VST2=ON；没有 ALL_BUILD 或完整重编。

- --build build --config Release --target wjn_subgraph_tests wjn_node_v4_tests wjn_vst_mode_tests midi_tools_synth_tests。依赖含 WinJACKNexus 与所需模块；新增调用点测试依赖实际 Host、worker 和离线夹具。最后清单改动只重建 wjn_node_v4_tests。最终所有退出码 0。
- wjn_node_v4_tests.exe <runtime>：28 实际输出与 C/C++ ABI、正负契约夹具。
- wjn_subgraph_tests.exe <runtime> <tests/node-v4/wjn_subgraph_jack_fixture.dll> <PrePublic-S6-real-window>：2550 项、14 次实窗显示。
- wjn_vst_mode_tests.exe <runtime>：实际 VST2/VST3 x86/x64 双模式、同时参数/DSP 独立、失败清理通过。
- midi_tools_synth_tests.exe <Develop/MidiTools/fixtures>：S1 既定单资源回归通过。
- python Docs/Node/package_prepublic_overlay.py：58 个运行文件；ZIP CRC、逐文件 SHA-256、重复 MIDI DLL 一致、无夹具/PDB/JACK 替身检查通过。

GUI 环境 windows/100%（QT_SCALE_FACTOR=1、QT_FONT_DPI=96），Microsoft YaHei UI 系统字体。实际 HWND 可见、DPR=1，稳定后捕获真实窗口；截图包括中日韩 tooltip、子图节点库及禁用 GUI 按钮，检查无方块。预算 25/100，实际 14/100，已关闭测试窗口。前面的扫描测试失败在打开图窗口前停止，无 GUI 显示；没有用 offscreen 替代。

扫描负例修复仅改测试记录：最初误填 canonical UID；实际 32/64 扫描器探针均返回 7856341222114433556677881234ABCD。改为真实 class ID 后四条均通过，未改生产扫描器或插件身份规则。失败日志留在 build/PrePublic-S6-*，调试输出已移除。构建保留原有 NativeFx/Node i18n 导出声明 C4273 警告，未扩大到无关清理。

## 增量替换包

build/WinJACKNexus-PrePublic-S1-S6-Release-overlay.zip，58 个运行文件，3,602,982 字节（约 3.6 MB）。SHA-256：a697487c41a1fadc5d5457c605ca78d48beca661c1720f5221d9e47ee4c089f5。

包只覆盖本次宿主、SDK 运行库、v4 内置节点、必要 VST 工作进程和变化语言资源；不含完整 Qt、JACK、第三方库、测试替身、SDK 示例或用户文件。需要已有完整安装。详细文件大小/哈希、Node 4/NativeFx boundary 2/IPC 1/Qt 6.10.3 前置版本见 PrePublic-S6-package-manifest.json，安装顺序见 PrePublic-S6-INSTALL.txt 及 ZIP 的 INSTALL.txt。runtime/ 内容按相对路径一次覆盖运行目录，不能混用新宿主和旧 ABI 模块。

二进制按当前工作区构建：本任务提交排除了工作区原有未提交修改，但相关二进制可能含这些既有代码；清单记录该来源与 S5 代码检查点。S6 新改动只涉及验证/文档/打包，不改变生产 DSP。打包及测试没有结束用户应用或音频进程。

真实 JACK/硬件/听音、第三方实际插件广泛兼容、人工审校和安装验收已跳过（用户指令）；自动离线和实窗通过不代表这些场景通过。未发现需要改变端口名的情况，旧工程诊断与保存仍由用户在更新后执行。

阶段证据：S1 Docs/MidiTools/PrePublic-S1-results.md，S2～S5 Docs/Node/PrePublic-S*-results.md；本阶段日志与截图见同目录 PrePublic-S6-*。范围外旧 i18n 编译警告仅记录，未修复。
