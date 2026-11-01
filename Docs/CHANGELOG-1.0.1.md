# WinJACKNexus 1.0.1 更新日志

发布日期：2026-11-01

## 修复

- 修复 AUX 和 Sub Mix 在经典 Pan 模式下无法拖动分控旋钮的问题，支持独立调整及联动调整。

## 信号流调整

- MAIN 页分控电平、声道 Mute/Solo 移到通道总推子之前，使 AUX Post 跟随这些分控；AUX Pre 保持不变。
- 实际路径：通道输入 → 输入增益 → FX → Pre 采样点 → MAIN 分控 / 声道 Mute/Solo → 通道总推子 / 整体 Mute/Solo → Post 采样点 → Master 分支。
- 根据用户确认，普通通道 MAIN Pan 保留现有独立分支，不作用于 AUX Post；Sub Mix 保留原有同布局输出 Pan。声道布局保持不变。
- MIX 开关仅决定是否汇入 Master，关闭 MIX 不影响 AUX Post。Master 不会重复应用 MAIN 分控增益。

## 升级说明

本次为 1.0.1 Release 增量覆盖包，需在现有完整安装上使用。关闭应用后将包内运行文件覆盖到安装目录；保留现有工程、主题和配置。已有工程的 AUX Post 会按新的分控路径处理，MAIN 分控非默认时，发送电平可能与旧版不同。

## 验证

- Release 编译通过：主程序、公共音频库、UI 库及回归测试目标；使用现有 `build/release`，VST2 配置保持 ON，未执行全量构建。
- `digital_mixer_offline_tests`、`mixer_main_post_tests`、`mixer_classic_pan_drag_tests` 共 3 项全部通过。
- 离线信号回归覆盖 Input/AUX 的 Pre/Post、Sub Mix 实际输出，以及 Mono/立体声/5.1/7.1 布局；检查分控电平、声道 Mute/Solo、MIX 独立性、Master 单次增益和普通通道 Pan 分支保留。
- Windows 实窗验证使用 `QT_QPA_PLATFORM=windows` 和系统字体；AUX/Sub Mix 共显示 2 次窗口，独立与联动拖动通过，截图检查中文显示正常。
- 主程序文件版本为 `1.0.1.0`，产品版本为 `1.0.1`。
- 现场 JACK/音频设备未测试；人工界面及安装验收已跳过（用户指令），不代表通过。
- 构建日志保留 i18n DLL 声明 C4273 和 Qt 部署工具 `VCINSTALLDIR` 未设置警告，本次构建未失败。
