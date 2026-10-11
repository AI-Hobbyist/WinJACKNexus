# 公开仓库前 S4 完成记录

2026-10-11：实现、所需 Release 构建及离线/100% Windows 实窗验证完成。

MIDI Split、MIDI Matrix、八个 Native Fx 和 ExampleNativeFx 现在声明 JACK_NODE 与 VIRT_NODE。实例模式不可变，JACK 包装只在 JACK 实例初始化；VIRT prepare/process/reset/stop 复用原处理核心，不启动 JACK。Native Fx 边界升至 v2，宿主分别校验 JACK 实际布局与 VIRT 逻辑布局，固定八声道 L、R、C、LFE、Ls、Rs、Lrb、Rrb。mixer placement、All/每通道参数及独立控制窗口保持原契约。

Split 显示名修正为 MIDI Split，并接入中文“ MIDI 通道拆分”、日文“ MIDI チャンネル分割”、韩文“ MIDI 채널 분할”的现有目录和热切换。技术身份 midi_spilit、DLL 名、默认 JACK 名和工程字段保持，旧工程无需因显示名称修改重新连线。无自带 GUI 的能力仍为否。

## 验证

以下命令均在现有 build、Release、前台 PowerShell 执行，输出通过 `2>&1 | Tee-Object -FilePath 'build.log'`；立即检查 LASTEXITCODE，最终均为 0。

- CMake build 目标：mod_patchbay、midi_tools_split_tests、midi_tools_matrix_tests、wjn_native_fx_boundary_tests、wjn_node_v4_tests；使用 VS18 自带 CMake，parallel 4。mod_patchbay 在初次构建通过；后续修复仅重建必要目标。
- `wjn_node_v4_tests.exe build/release/bin`：C/C++ ABI、八种兼容夹具及 27 个实际模块能力矩阵通过，未迁移 VST 仍声明 JACK-only。
- `midi_tools_split_tests.exe <runtime> <repo> Docs/MidiTools/PrePublic-S4-rendered/split`：原离线路由/故障回归、实际 VIRT 逻辑端口/字节/帧偏移/有界溢出/stop，实际设置控件、四语名称及系统字体 glyph 检查通过。
- Matrix 同样参数，截图目录为 `Docs/MidiTools/PrePublic-S4-rendered/matrix`：原合并/扇出/释放/快照回归、实际 VIRT、设置恢复、四语 GUI、关闭再打开通过。
- `wjn_native_fx_boundary_tests.exe` 输入 ExampleNativeFx 和 gate/graphic_eq/deesser/compressor/limiter/delay/parametric_eq/reverb 的实际 DLL 路径，最后参数为 `Docs/Node/PrePublic-S4-native-real-window`：逐模块加载、双能力、VIRT 拒绝 JACK init、prepare/layout/stop、JACK 未初始化布局拒绝、独立实例窗口、mixer 窗口和原有 SDK 参数快照检查通过。不新增效果算法验收。

GUI 环境：QT_QPA_PLATFORM=windows、QT_SCALE_FACTOR=1、QT_FONT_DPI=96，系统字体 Microsoft YaHei UI。每窗等待稳定后核对可见 HWND、DPR=1，再用 QScreen::grabWindow 抓取实际窗口；窗口均已关闭。预算 40/100，实际 Split 1、Matrix 3、Native Fx 27，合计 31/100；Native Fx 全窗口/菜单/tooltip 事件计数为 27。已逐效果检查 VIRT 截图及 MIDI 中文/日文/韩文截图，文字正常无方块。沿用既有标签省略行为，不作无关视觉重排。

首次编译因枚举条件表达式到 uint32_t 窄化失败，补明确转换；第二次 Split 测试缺少 LocalizedText.hpp，补头文件后成功。失败日志保留 build/PrePublic-S4-build-initial.log、build-repair1.log，未追认为通过。最终证据见同目录 PrePublic-S4-{build,sdk,split,matrix,native}.log；原始日志在 build/PrePublic-S4-*。

真实 JACK 连接、听音、硬件、人工界面审校、安装验收已跳过（用户指令）；离线通过不代表这些场景已验证。仅同步既有重复 MIDI 输出目录，未创建其他构建树或停止用户进程。
