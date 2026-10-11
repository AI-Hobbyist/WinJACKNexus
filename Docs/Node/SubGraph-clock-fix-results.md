# 子图静音与接线盘卡顿修复

日期：2026-10-11。范围仅限根子图时钟通知处理及对应回归测试。

## 根因与修复

JACK 客户端激活时会通知当前缓冲区大小，参见 [JACK2 官方变更记录](https://github.com/jackaudio/jack2/blob/develop/ChangeLog.rst)。原实现收到相同的时钟值仍把图标记为未准备。接线盘刷新调用 synchronizeClock，停用、重新准备并激活客户端；激活通知再次清除就绪状态，形成循环。音频回调持续输出静音，界面线程重复执行阻塞的 JACK 停启操作。

rateChanged 和 sizeChanged 现在只在请求值改变时清除就绪状态。真正的时钟变化仍暂停旧图并重新准备；路由、端口、节点 ABI 和界面行为不变。未修改或提交工作区原有的 ProcessPriority 等其他任务代码。

## 复现与验证

离线 JACK 边界替身现在保存真实回调并模拟激活通知。最小复现只有一个根子图、两个音频接口和一条直通路由，无模块、嵌套或真实 JACK 服务。

| 检查 | 修复前 | 修复后 |
| --- | --- | --- |
| 20 次空闲时钟检查产生的重新激活次数 | 20 | 0 |
| 0.25 输入的音频输出 | 0 | 0.25 |
| 图就绪状态 | false | true |
| 96 kHz / 128 帧变化后的完整块输出 | — | 0.5 输入、128 帧输出全部为 0.5 |
| 可见接线盘 500 ms 空闲刷新产生的重新激活次数 | — | 0 |

Release 定向编译 mod_patchbay、wjn_subgraph_jack_fixture、wjn_subgraph_tests 成功。已有的 NativeFx/Node i18n C4273 声明警告仍存在，不属于本次范围。

测试命令（运行环境为 QT_QPA_PLATFORM=windows、QT_SCALE_FACTOR=1、QT_FONT_DPI=96，使用系统字体）：

```powershell
& ./build/release/bin/wjn_subgraph_tests.exe "$PWD/build/release/bin" "$PWD/build/release/bin/tests/node-v4/wjn_subgraph_jack_fixture.dll" "$PWD/build/subgraph-fix-evidence" 2>&1 | Tee-Object -FilePath build.log
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
```

最终结果：S3 PASS checks=2582 realWindows=14 platform=windows DPR=1。三层音频/MIDI直通、项目恢复、关闭/重开编辑器、删除子图、原有四语言提示和禁用 GUI 行为通过。检查实际可见 HWND 的截图，韩文和中文正常，无方块字。整个诊断/验证批次累计显示少于 100 次，没有使用 offscreen。

日志保存在 build/subgraph-fix-red.log、build/subgraph-fix-minimal-red.log、build/subgraph-fix-build.log 和 build/subgraph-fix-green.log；实窗截图位于 build/subgraph-fix-evidence。无匹配 WinJACKNexus 崩溃转储或安装目录时间戳日志可供使用。

真实 JACK 服务、硬件监听及人工安装验收：已跳过（用户指令）。音频验证采用离线边界回放，不代表硬件端到端监听已经验收。运行中的 WinJACKNexus（PID 79980）始终保留，未中断其音频进程。

## 增量交付

build/WinJACKNexus-SubGraph-clock-fix-Release-overlay.zip 只包含 runtime/mod_patchbay.dll、manifest.json 和 INSTALL.txt。用于已安装 S1～S6 Release 覆盖包的完整运行环境。由用户退出应用后，将 runtime 下的 DLL 覆盖安装根目录；无需修改或重连已有工程。

二进制基于当前工作区编译，保留原有 ProcessPriority 调用；其依赖 common.dll 已包含于 S1～S6 覆盖包。本次补丁无需再次替换 common.dll。

覆盖包大小：332555 字节。SHA256：690a30b8921538bfe78abfe0400b7b2f6daa7fa1d4a982dd59873d453521f2c5。
