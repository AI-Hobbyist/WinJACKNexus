# 公开仓库前优化 S1 — SF2/SFZ 按需加载

状态：S1 实施、Release 构建、离线测试与 100% Windows 实窗验证完成；不代表 S2～S6 完成。

## 设置与资源语义

设置 JSON 保留 version 1、resources 和 preferred 字段兼容性。resources 是稳定 ID、路径、格式和 SFZ 映射的目录；preferred 是明确激活的资源 ID。旧工程 preferred 为空时不自动选择第一项。只浏览列表不改变声音，点击“加载所选资源”才激活。删除当前条目会清空激活 ID；删除其他条目不重载当前引擎。保存和恢复保留目录及激活 ID，不保存引擎句柄，不自动覆写工程。

导入 SF2/SFZ 不准备引擎，状态为“未加载”。未选中条目不读取预置列表。激活后异步准备并展示 ready、missing、failed 或 budget。Bank/Program 仅在激活资源内匹配，不回退目录中的其他资源。重复激活成功资源不重载；失败资源允许重新激活重试。

切换、清空、删除当前条目及变更采样率先解除声音和 generation 引用，由工作线程回收旧引擎再准备新引擎。允许短暂静音，不保留旧库音尾。加载失败保持静音，legacy preserveOnFailure 参数不再恢复旧库。资源预算仅针对当前所选资源；preparedBytes、activeResources 和 destroyedResources 提供生命周期验证证据。

音频处理仅尝试一次独占标志，竞争时输出预先清零的缓冲，不等待工作线程。工作线程在块之间取得独占权后解除引用，独占权释放后回收旧引擎；没有后续音频回调也可完成切换和清空。文件加载、引擎释放和等待均不进入音频回调。设置发布与 DSP 访问共用该保护；退出停止并 join 工作线程后释放剩余实例。引擎算法、JACK 端口名、主题与刷新率未变。

## 验证记录

使用现有 build，Visual Studio 18 CMake，Release 定向目标：mod_sf2_loader、midi_tools_synth_tests、midi_tools_synth_gui_tests、midi_tools_synth_sidebar_tests。首次构建退出码 0，日志 `PrePublic-S1-build.log`。所有编译和测试均为前台 PowerShell，输出通过 `2>&1 | Tee-Object -FilePath 'build.log'` 保存，每次立即检查 `$LASTEXITCODE` 并保存阶段日志。

离线命令：`build/release/bin/midi_tools_synth_tests.exe Develop/MidiTools/fixtures`（实际传入绝对路径）。首次退出码 1：新增删除用例将目录从 5 项减为 4 项，后续采样率验证仍等待 5 项，造成测试超时。仅修复该预期并重编测试目标，退出码 0（`PrePublic-S1-build-repair.log`）；重跑退出码 0（`PrePublic-S1-synth-repair.log`），销毁计数 13。通过目录仅登记、重复选择不重载、SF2/SFZ 出声与单资源切换、跨库回退拒绝、快速切换、失败、预算变化/拒绝、Unicode/损坏/缺失样本、删除/清空、采样率、工程恢复、无后续回调及销毁取消。损坏 SF2 和缺失 SFZ 样本的引擎错误为预期夹具输出。

实窗环境：`QT_QPA_PLATFORM=windows`、`QT_SCALE_FACTOR=1`、`QT_FONT_DPI=96`，系统字体 Microsoft YaHei UI，检查实际平台、原生可见窗口和 DPR=1。GUI 命令：`midi_tools_synth_gui_tests.exe <runtime> <repository> <screens>`，三个参数均为绝对路径；侧栏使用相同参数模式。首次 GUI 与侧栏退出码均 0（`PrePublic-S1-gui.log`、`PrePublic-S1-sidebar.log`）。补齐既定的当前资源删除断言及实际显示计数后，仅重编 GUI 测试，退出码 0（`PrePublic-S1-gui-build-final.log`）；最终 GUI 退出码 0（`PrePublic-S1-gui-final.log`）。最终批次通过 Show 事件统计窗口/弹出层实际显示 14/100；侧栏实际 1/100。首次 GUI 沿用原测试预算 <48/100，未安装实际计数器；本阶段全部运行的保守累计上限 63/100，没有绕过批次上限。

真实 DLL 不 init JACK，验证独立窗口、参数保存/重开、空选择不加载、明确激活、当前项删除释放、SFZ 映射、增益/静音/独奏/主输出、编辑器关闭后合成继续及语言切换不提交设置。沿用的 30/60/120 FPS 检查验证刷新设置，不改变 100% 缩放。`PrePublic-S1-real-window` 与 `PrePublic-S1-real-sidebar` 保存实际 Windows 表面截图；检查中文主窗口与韩文信息 tooltip，字符正常，没有 tofu；测试对四语言标签检查系统字体 glyph fallback。

修改范围的 diff 和可读性检查完成，新增核心区域应用仓库既有 Microsoft clang-format 规则，`git diff --check` 通过。保留既有工作树修改，UI API 文档仅暂存本阶段新增契约段落。

人工审校、人工界面验收、硬件/听音和安装验收：已跳过（用户指令）。自动实窗验证仍执行；不声称真实 JACK 连接或实际部署通过。
