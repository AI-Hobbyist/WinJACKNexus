# Classic Mixer 通道条卡片布局

日期：2026-10-19。用户要求：功能区使用卡片边框，通道名居中，按钮放入推子空白区。

实现仅调整共享 ClassicMixerWindow 的组合布局。Gain、EQ、AUX、Pan、推子复用现有公共 GroupPanel，颜色与边框由既有 bindConsoleTheme/Theme Token 提供。输入名称使用公共 TextField 的居中对齐；路由、监听、静音及独奏按钮按原顺序纵向放在推子右侧空白区，Master M 同位置。EQ 开关继续位于 EQ 卡片。通道统一宽 200 逻辑像素，缺失功能保留空槽，所有推子轨道同高、同位置；输入区水平滚动、Master/Monitor 固定。未新增公共控件、导出 API、语言 key 或 DSP 行为。

Release 定向构建通过。前台 PowerShell 输出经 Tee-Object 保存并检查退出码；日志为 `build/classic-mixer-layout-build.log`、`build/classic-mixer-layout-gui-tests.log`、`build/classic-mixer-layout-host-tests.log`。现有 GUI 测试 PASS 646 项（6 次窗口显示），宿主离线集成 PASS 78 项（5 次窗口显示）。初次截图发现按钮裁边与 AUX 测点文字截断，增加通道宽度并压缩区块留白后，重新构建及必要检查通过；两轮总计 22 次实窗显示，低于单批 100 次上限。

实窗使用 windows QPA、100% 缩放及系统字体。证据复制到 `build/classic-mixer-layout-evidence`；最终 24 通道中文截图确认边框分区、名称居中、完整按钮/中文测点文字及推子对齐。四语言和三型号由现有实窗检查覆盖，主图/嵌套子图窗口、改名与预设保存由离线宿主检查覆盖。未使用 offscreen。

本次未重跑无修改的 DSP 核心及模块单测；既有 GUI/宿主检查仍处理实际离线样本。真实 JACK server/硬件未验证；人工审校、安装及部署验收已跳过（用户指令），跳过不代表通过。没有关闭用户音频进程或自动部署。

已更新 `build/ClassicMixer-Release-Incremental-20261019.zip`，仍为三型号节点及所需共享 DLL/语言资源的增量包，CRC/SHA-256 校验通过。共享 DLL 保留已有工作区改动，具体基线依赖及实际源码/文件哈希继续记录在包内 manifest.json；人工安装与真实设备效果未验证。

## Monitor 宽度追加调整（2026-10-19）

按用户“监听那轨窄一些”的要求，仅将 Monitor 条及其固定外层宽度从 200 改为 152 逻辑像素。输入和 Master 仍为 200，所有纵向槽位及推子行程保持，Monitor 双电平表和刻度完整。

Release 定向构建及现有三型号/四语言 GUI 实窗检查 PASS 646 项，窗口显示 6 次。实际 24 通道中文截图已检查双表、刻度、名称及推子对齐，证据位于 `build/classic-mixer-monitor-width-evidence`；日志为 `build/classic-mixer-monitor-width-build.log` 与 `build/classic-mixer-monitor-width-gui-tests.log`。本次只改宽度，未重跑 DSP、模块或宿主集成检查。真实 JACK 未测试；人工审校、安装/部署验收已跳过（用户指令）。现有 Release 增量包已更新。
