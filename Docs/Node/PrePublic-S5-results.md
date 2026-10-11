# 公开仓库前 S5 完成记录

2026-10-11：VST Host 与原有 VSTProcess32/64 双模式实现、定向 Release 构建、离线测试及 100% Windows 实窗检查完成。

VIRT 使用根采样率、块长、samplePosition 与借用音频/MIDI，经固定宽度无指针共享协议 v1 送入原有隔离工作进程。逻辑端口来自实际插件；VIRT 不启动 JACK，JACK 实例拒绝 VIRT prepare。路径、位数、class/shell ID、既有预置状态和隔离/崩溃旁路保持。扫描条目复用 Host metadata 和实例创建入口，不重写扫描流程。详细容量、至少一块异步延迟、拥塞静音和不支持 MIDI 输出等边界见 Develop/Vst/Virtual-Processing.md。

## 构建和测试

所有命令在现有 build 中使用 Release，前台 PowerShell 以 2>&1 | Tee-Object -FilePath build.log 记录并立即检查 LASTEXITCODE。WJN_ENABLE_VST2=ON；最终退出码均为 0。

- VS18 CMake configure -S . -B build -DWJN_ENABLE_VST2=ON。
- --build build --config Release --target wjn_vst_mode_tests wjn_vst_i18n_tests；前期同时构建 mod_patchbay、wjn_node_v4_tests。实际构建 Host、两种工作进程和两种位数 VST2/VST3 离线夹具。
- wjn_node_v4_tests.exe <runtime>：27 个实际 DLL、完整能力/ABI 矩阵通过，VST Host 现在为双能力。
- wjn_vst_mode_tests.exe <runtime>：VST2 x86、VST3 x86、VST2 x64、VST3 x64 全部通过实际 JACK/VIRT 工作者创建、预置保存/恢复、五个逻辑端口、音频值、根采样率/位置、MIDI 帧偏移、有界溢出、stop 和失败清理。JACK 只用测试目录的替身，没有真实服务器。VIRT 零 JACK 注册；x64 VST2 主动终止仅该测试创建的 worker 并验证原有隔离旁路。错误协议 999 被实际 worker 拒绝，日志中的 Incompatible VST block transport v1 是预期负例。
- wjn_vst_i18n_tests.exe <runtime> <截图路径>：VIRT 设置窗口、四语热切换、草稿/选择/焦点、状态不变和租约释放，共 78 项通过。

GUI 使用 QT_QPA_PLATFORM=windows、QT_SCALE_FACTOR=1、QT_FONT_DPI=96、系统 Microsoft YaHei UI；检查真实可见 HWND 和 DPR=1，等待稳定后以 Win32 BitBlt 捕获实际窗口。单窗口复用热切换，实际显示 1/100；诊断期间此前另有两次失败 Qt 启动对话框，累计远小于 100。韩文、草稿日文和中文字符正常无方块，截图 PrePublic-S5-vst-real-window.png。测试窗口已关闭，用户音频进程未停止。

## 失败及修复证据

首次构建补齐目录内 Qt Test 导入；夹具以 .def 导出匹配已有 JACK 头的链接属性，VST3 单组件基类不重复编译其内嵌 EditController，补足 SDK pluginview/commoniids。Qt 启动错误在测试临时目录复现两次；最新 dmp 异常 0xc0000409，加载了运行目录 Qt DLL 但无 qwindows。临时部署漏平台插件，现复制同版本 platforms/qwindows.dll，并断言实际 Windows backend；原命令已转绿。旁路恢复发现合法空通道标签被旧参数解析拒绝，现仅标签参数允许空字符串并保留通道索引，原崩溃用例转绿。所有 DEBUG-s5 临时诊断已删除，保留有用的失败部署/退出码报告。

最终构建、SDK、模式、GUI 日志为 PrePublic-S5-{build,sdk,modes,gui}.log；失败原始日志留在 build/PrePublic-S5-*。测试替身只在临时目录部署，禁止放入交付包。真实 JACK 连接、第三方实际插件兼容、听音、硬件、人工审校和安装验收已跳过（用户指令），不视为通过。本阶段无新增编辑器功能或插件算法范围。
