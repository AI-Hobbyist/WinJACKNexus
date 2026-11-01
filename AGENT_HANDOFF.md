# Agent 交接手册

更新时间：2026-11-01（Asia/Seoul）

## 当前任务与最终状态

WinJACKNexus 1.0.1 调音台修复已完成：实现、Release 构建、限定回归、提交推送和增量打包均已完成。无剩余实施任务，不创建下一开发阶段。

本轮用户确认的最终范围：

1. 修复 AUX/Sub Mix 经典 Pan 模式下分控旋钮拖动，包含独立及联动调整。
2. Input/AUX 的路径为：输入 → 输入增益 → FX → Pre → MAIN 分控电平、声道 Mute/Solo → 总推子及整体 Mute/Solo → Post → Master 分支。
3. 普通通道 MAIN Pan 保留现有独立分支，不进入 AUX Post；声道布局不变。Sub Mix 保留原有同布局输出 Pan。
4. MIX 仅控制 Master 汇总，不影响 AUX Post。Sub Mix 本身不支持 AUX 发送，回归验证其实际输出口。

上一轮“只写文档”的任务已结束，不再限制本轮已获授权的代码修改。历史交接原文可通过 `git show 739a33e:AGENT_HANDOFF.md` 查看。

## Git 与文件状态

- 分支：`main`。
- 实现、回归与更新日志提交：`739a33e4ea13abf2822c94d425cb775308c8bd9a`，已成功推送 `origin/main`。
- 本交接文档作为本轮收尾单独提交并推送；只改交付记录，不改变增量包的代码来源提交。
- 本次修改：`CMakeLists.txt`、`Develop/CMakeLists.txt`、`Develop/MixerPostRegressionTests.cpp`、`Docs/CHANGELOG-1.0.1.md`、`Docs/UI-API.md`、`src/app/WinJACKNexus.rc`、`src/common/src/DigitalMixerProcessor.cpp`、`src/common/src/Library.cpp`、`src/ui/src/AuxSettingsWindow.cpp`。
- 没有待继续修改的代码。原有未跟踪内容保持原状且未提交：`.omc/`、`midi_tools_node_ref/`、`themes/Furina/`、`themes/org.eryth.furina.wjnskin`。
- 本地 `tests/` 被仓库忽略；本轮正式回归源码放在已提交的 `Develop/MixerPostRegressionTests.cpp`。

## 已完成的实现

- `AuxSettingsWindow::mouseMoveEvent()` 先处理分控拖动，再限制经典模式下的空间参数拖动。
- `DigitalMixerProcessor` 在 Pre 缓冲区保持不变的情况下，将分控应用到 Post。普通通道送 Master 的矩阵仅保留 Pan，避免分控增益重复应用。
- Sub Mix 原输出矩阵在总推子前执行，保留原有同布局输出处理。
- CMake 项目及公共库版本为 1.0.1；主程序文件版本为 1.0.1.0，产品版本为 1.0.1。
- UI 行为契约及更新日志已更新。没有变更公开函数签名或工程序列化格式。

## 已验证

- 现有 `build/release` 构建树，VS18 自带 CMake；`WJN_ENABLE_VST2=ON`。
- Release 目标 `WinJACKNexus`、`wjn_mixer_post_regression_tests`、`wjn_digital_mixer_tests` 构建成功；没有执行 ALL_BUILD 或全量清理重编。
- CTest 限定运行 3 项，全部通过：`digital_mixer_offline_tests`、`mixer_main_post_tests`、`mixer_classic_pan_drag_tests`。
- 新音频回归覆盖 Input/AUX 的 Pre/Post、Sub Mix 输出、Mono/立体声/5.1/7.1，验证分控/Mute/Solo、MIX 独立性、Master 单次增益与普通通道 Pan 分支保留。
- GUI 使用 Windows Qt 后端和系统字体，实际显示 2 次窗口；独立拖动得到 L=4/R=0 dB，随后联动拖动得到 L=8/R=4 dB，经典 Pan 值不变。窗口已关闭。
- 原生窗口截图已检查，中文“分控”正常显示，无方框：`build/release/aux-classic-1.0.1.png`、`build/release/submix-classic-1.0.1.png`。
- scoped clang-format 已应用；`git diff --check` 通过。
- 日志：`build/release/1.0.1-configure.log`、`1.0.1-build.log`、`1.0.1-tests.log`、`1.0.1-package.log`；逐项测试输出见 `build/release/Testing/Temporary/LastTest.log`。

## 增量包

- 路径：`build/release/WinJACKNexus-1.0.1-Release-x64-Overlay.zip`。
- 大小：1,349,269 字节。
- SHA-256：`e214e66c6a2882ab1a939f417d48876b9ee75e6a46d57e84a28f303d14e9b779`；同目录有 `.zip.sha256`。
- 运行文件仅包含 `WinJACKNexus.exe`、`common.dll`、`ui.dll`，另附 `Docs/CHANGELOG-1.0.1.md` 和 `overlay-manifest.json`。清单记录代码提交、文件大小及 SHA-256，ZIP 内容逐文件校验通过。
- 本地打包脚本：`build/release/package-1.0.1.ps1`。未上传 GitHub Release；用户本轮要求的是提交推送和生成增量包。
- 这是增量包，不是完整包。关闭应用后覆盖现有完整安装；原工程 MAIN 分控非默认时，AUX Post 电平可能因新路径而变化。

## 未验证、警告及范围外事项

- 现场 JACK/端口连接、真实音频设备流未运行测试；未将离线回归声称为现场端到端验证。
- 人工界面及安装验收：已跳过（用户指令），不代表通过。
- 本次构建没有失败项；日志包含未修改的 i18n API 声明 C4273 警告，以及 windeployqt 的 `VCINSTALLDIR` 未设置警告；均未导致构建失败，未扩展范围处理。
- 截图中既有英文标题/标签存在裁切，属于本轮之外的布局事项，未修改。

## 下一步与约束

无剩余实施任务。若用户反馈实际音频行为，再按具体症状及日志定位；不自动扩大范围。

继续遵守：先读 `AGENTS.md`；有 `.codegraph/` 时先用 CodeGraph；仅用现有 build 构建树与 Release；构建/测试/打包必须前台 PowerShell 经 Tee-Object 写 build.log 并立即检查退出码；Qt 禁用 offscreen，实窗每批显示上限 100；不停止正在运行的应用或音频进程；保留用户未跟踪文件。当前无外部多阶段计划，范围以本次用户指令及后续澄清为准。
