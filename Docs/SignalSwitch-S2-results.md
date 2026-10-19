# Signal Switch S2 完成记录

2026-10-19，Release。主模块 `signal_switch.dll` 位于 EXE 同级；五种节点 DLL 分别位于 `signal_switch/mono`、`stereo`、`5_1`、`7_1`、`midi`。

已实现共享服务注册、独立及互斥目标发布、处理侧关闭确认、停止实例交接，以及无 GUI 的 Node v4 JACK/VIRT 双模式节点。实时音频逐路复制或清零；MIDI 透传并在关闭时定向清理活动音符和延音，有界输出不足时在后续块继续清理。

根据最新方向，状态移除了无边框、可选置顶与贴边隐藏设置，保存按钮透明度、穿透、编辑器滚动位置及桌面坐标原点。界面实现属于 S3/S4，尚未进行 GUI 验证。

使用现有 build 目录和 Visual Studio 18 CMake 构建 `wjn_signal_switch_state_tests`、`wjn_signal_switch_module_tests` 的 Release 目标。前台 PowerShell 保留输出并检查退出码，最终构建和测试退出码均为 0。

状态测试 13 项通过；模块测试覆盖五个 DLL 的加载、导出、元数据、无服务创建、生命周期、逐通道数据、缺失输入、关闭确认、MIDI 数据/帧偏移、延音和重复音符清理、velocity-zero Note On，以及跨调度域先关后开和停止实例交接。完整结果见同目录 `SignalSwitch-S2-state-tests.log`、`SignalSwitch-S2-module-tests.log`。

验证全程不依赖运行中的 JACK 服务；未执行真实 JACK 连接或真实 MIDI 接收端验证，不将离线结果当作这些场景的通过证据。未停止用户运行中的应用或音频进程。
