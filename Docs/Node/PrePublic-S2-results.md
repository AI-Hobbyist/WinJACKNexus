# 公开仓库前优化 S2 — Node v4 契约

状态：S2 实施、Release 构建、离线及 100% Windows 实窗验证完成。S3～S6 尚未完成。

## 契约与范围

Node v4 保持一 DLL 一节点类型，保持 NodeInfo/INode 布局。新增创建前 metadata
查询、显式 JACK/VIRT 工厂及版本化 C 处理表。metadata 为 24 字节，包含
capabilities、独立自带 GUI feature 和两个保留零字段。第三方 v3 模块需重编。
错误规则、逻辑端口、缓冲容量、MIDI 偏移、时钟及生命周期见 Node-API.md。

宿主加载和 patchbay 加载均先检查完整契约再选择 JACK 模式。旧版本、缺失
metadata、零/未知能力、未知 feature、非零保留字段及不完整 VIRT 表拒绝。
扫描不创建实例、不 init JACK；不假定第三方模块具有任何默认能力。

所有现有内置模块及两个 SDK 示例升级声明。S2 仍只声明已实现的 JACK 能力；
MIDI Split/Matrix 与八个 Native Fx 的双模式在 S4 实施，VST 在 S5 实施。
内建子图/接口在 S3 接入。GUI 位包含窗口 hook 和既有 show_node_gui 路径，
不把参数设置控件当作自带 GUI。Split 和无编辑器 ExampleNode 为否。
侧栏提示、其 i18n 及无 GUI 入口禁用属于 S3，本阶段只提供可查询数据。

## 验证记录

仅使用现有 build，Release 所需目标，不运行 ALL_BUILD，不修改 VST2=ON。
所有构建/测试通过前台 PowerShell Tee-Object 捕获日志，立即检查退出码。
配置日志为 PrePublic-S2-configure.log；构建日志为 PrePublic-S2-build.log。
离线测试覆盖 C/C++ 布局/签名、合法 JACK/VIRT/双能力、缺失/非法/旧契约、
无创建和 JACK 副作用的扫描、显式模式、音频和帧偏移 MIDI、溢出丢弃、
缺失输入清零、prepare/reset/stop，以及实际部署 DLL 清单。

GUI 环境为 windows、100% 缩放、系统字体。计划批次为九个适配器/电平表
设置窗口及一次废弃模块提示（10 次），九个 Native Fx 节点和 mixer 窗口
（18 次）；合计 28 次，预留 popup/tooltip 余量，单批不超过 100 次。
实际截图在渲染稳定后从可见 Windows 窗口抓取，并检查平台、原生可见性、
DPR=1 和系统 CJK fallback。测试只关闭其自建窗口。

人工审校、人工界面验收、硬件/听音和安装验收：已跳过（用户指令）。
不运行 JACK 客户端/端口连接集成测试；上述离线结论不代表实际音频或安装通过。

## 最终结果

配置退出码 0。首轮 Release 所需目标：node、WinJACKNexus、mod_patchbay、
mod_meterbrigde、mod_vst_host、mod_digital_mixer、mod_native_fx、mod_midi_spilit、
mod_midi_matrix、mod_sf2_loader、wjn_node_v4_tests、wjn_single_node_tests、
wjn_native_fx_boundary_tests、midi_tools_foundation_tests、midi_tools_delivery_tests。
生产模块已成功编译，首轮命令因新 SDK 夹具中 enum 条件表达式初始化 uint32_t
触发 C2397 而退出 1。修正夹具显式转换后，仅重编测试目标，退出 0；
补齐 MIDI 逻辑端口 channels=0 断言后仅 SDK 目标重编退出 0。
补上 GUI 显示计数后仅两个 GUI 测试目标重编退出 0。没有扩大生产修改范围。
原始日志保留在 build；本文档目录的日志副本仅去掉行尾空白以通过 Git 检查。

前台 cmake -E copy_directory 将 canonical midi_tools 同步到 nodes/midi_tools，
退出 0，避免加载既有部署目录中的旧 ABI 副本。模块扫描实际 27 个（22 个
nodes、3 个 canonical MIDI 副本、2 个示例），全部 JACK-only；8 类合法/
非法夹具、C/C++ ABI、显式模式和离线处理测试退出 0。MIDI foundation 和
delivery 测试退出 0，验证模块发现、独立实例、Unicode 工程状态、主题
描述符及四语言 catalog 部署。所有这些测试不调用 JACK init。

实窗测试参数：single_node_tests 为 runtime、截图目录、repo 三个绝对路径；
native_fx_boundary_tests 为 ExampleNativeFx、八个实际效果 DLL、截图目录。
首次两项退出码均 0；补齐 Show 事件计数后最终两项均 0，系统字体
Microsoft YaHei UI、windows 平台、DPR=1。最终适配器/电平表批次共 14 次
显示（含 popup/tooltip），Native Fx 共 18 次，合计 32/100。首次运行未装
Show 计数器，沿用 28 个显式窗口并保守预留 20 次 popup/tooltip；本阶段
累计保守上限 80/100。未通过连续进程绕过预算。

检查实际中文废弃提示、包含中文/日语/韩语的设置窗，以及八个效果独立
窗口截图，字符正常、无 tofu。截图均取自稳定后的原生可见窗口；相关
mixer 窗口也逐模块显示与抓取。每个效果的完成文档记录 S2 ABI 范围，
不把本次 JACK-only 声明升级误报为 S4 双模式完成。

本阶段 diff 可读性与修改范围已检查，按既有 Microsoft clang-format 规则
仅格式化修改区域，git diff --check 通过。宿主、patchbay 和 VST 原有
ProcessPriority 等修改保留且不暂存；不夹带其他主题、语言或会话工作。
