# 电平表桥历史页控件主题修复

日期：2026-10-11。

范围：MeterHistoryWindow::applyTheme，以及历史页控件主题回归测试。没有修改公共控件实现、主题包、Theme Token 定义、节点 ABI 或音频行为。

原历史页使用普通 Qt 背景样式，未覆盖公共自绘控件的 qproperty 颜色属性；更新列表漏掉 QSpinBox/QDoubleSpinBox，且下拉框和按钮统一借用 Tab 配色。全局样式初始化和主题重新应用会影响自绘控件的颜色来源。

现在历史页为 ChoiceField、IconActionButton、IntegerField/DecimalField 和 RangeSlider 声明局部 qproperty 规则。下拉框读取已有 control-preset Token，按钮读取 control-action Token，数值框读取 control-number Token；数值编辑器的文字颜色同步更新。滑条保持既有历史页配色。所有新增颜色解析的回退均使用公共 ThemePalette 或已有模块调色板，没有新增固定业务颜色。

## 验证

定向 Release 编译 meterbridge_mod_ui、mod_meterbrigde、wjn_mixer_meter_i18n_tests 成功。现有 build 目录及 Visual Studio 18 CMake，未使用 ALL_BUILD。

同一回归测试先链接修复前的静态库，失败于频道下拉框：实际背景 #24482e（Tab），期望 #315424（下拉框）。编译修复后的静态库和模块后，同一测试通过。

```powershell
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_SCALE_FACTOR = '1'
$env:QT_FONT_DPI = '96'
& ./build/release/bin/wjn_mixer_meter_i18n_tests.exe "$PWD/build/release/bin" "$PWD/build/meter-history-theme-evidence" --theme-only 2>&1 | Tee-Object -FilePath build.log
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) { Get-Content build.log; exit $buildExitCode }
```

结果：METER HISTORY THEME PASS checks=217 realWindows=2 platform=windows DPR=1。

使用离线数据提供器，未连接 JACK。全局背景刻意设置紫色，模块控件设为绿色；首次显示、模块热切换、全局颜色变化和隐藏后重新显示均通过背景及边框检查。静音阈值编辑器文字颜色检查通过。实窗截图检查下拉框、数值框、按钮与滑条已同步模块配色，中文正常；四语言字体 glyph 检查通过。仅使用实际可见的 Windows HWND 截图，没有 offscreen。整个修复验证批次累计窗口显示 3 次（失败复现 1 次，成功验证 2 次），低于 100 次限制。

日志：build/meter-history-theme-red.log、build/meter-history-theme-build.log、build/meter-history-theme-green.log。截图：build/meter-history-theme-evidence/history-theme-initial.png、history-theme-switched.png。

人工安装验收：已跳过（用户指令）。运行中的安装版 WinJACKNexus（验证时 PID 66140）及其音频进程未被停止。

## 增量替换包

build/WinJACKNexus-MeterHistory-theme-fix-Release-overlay.zip 仅包含 runtime/mod_meterbrigde.dll、manifest.json 和 INSTALL.txt。适用于已安装 S1～S6 Release 版本的完整运行环境。由用户退出应用后，把 runtime 下的 DLL 覆盖到安装根目录；保留原有工程和主题配置。

覆盖包大小：178472 字节。SHA256：aaf3b7f067f8bd4e9ce91f8c013aa228ad24cac03c0f161bda1161388e372496。
