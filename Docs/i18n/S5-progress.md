# S5 Native Fx 与社区接入实施记录

日期：2026-10-03。状态：实施中，未编译、未实窗验收、未完成。本次按用户“提交并推送”指令保存进行中改动，不作为阶段完成证据。S4 已在 25df7a7 提交推送。

已按 CodeGraph 与 Native Fx API 核对八个 DLL 的入口和实际 UI 源码。S4 完成时的 Native Fx 显示来源基线为 372 次，保存于 `S5-source-baseline.json`；这是逐调用点接入和排除项审阅的输入，不能直接当作翻译 key 数。

新增 `NativeFx/I18n.hpp` 与每 DLL 编译的 `src/I18n.cpp`：独立域为 `nativefx.<Meta.hpp 的稳定 id>`，双方宿主提供同一个 optional v1 描述符；QObject 绑定保留弱引用、刷新时遍历快照，服务 detach 后界面退回调用点 English。GUI 线程检查、不兼容服务拒绝及 DLL 租约行为仍须实际模块测试。无 host 时使用本地完整 English 调用点；不读取音频线程或改变参数。公开 optional ABI 签名不变。

八个模块建立独立 locate 目录及清单；CMake 所需模块目标部署各自语言资源。当前共 275 个四语言草稿 key，八个效果界面均完成首轮显示接入，覆盖标题、参数、声道、预设及操作菜单。FX 宿主增加语言域租约和现有列表/预设显示刷新，Native Fx 预设名称导出改为查询当前显示语言，索引继续作为稳定身份。完整覆盖、English 降级和参数不变性尚未验收。

Gate、Compressor、De-Esser 接入自绘标题、声道说明/All、控件/轴说明、预设名和操作。独立节点窗口标题按当前用户客户端名称刷新；不改 NodeInfo、端口、JSON、Preset index、DSP 或声道参数。三个模块的已打开预设/恢复菜单也建立绑定。De-Esser 的参数以 Hz 保存、数值以 kHz 显示，原“带宽 (Q)”改为“带宽 / Bandwidth / 帯域幅 / 대역폭”，需列入 S6 领域审校。

公共 Controls 新增 `drawElidedText`，按实际字体逐行省略，使用可见 QWidget 的绘制变换登记全文 tooltip 区域；绘制区域按 paint 生命周期刷新，不更改数据。八个模块使用该公共方法，相关字体族改为继承系统/主题配置，弹出菜单使用 Qt 原生样式。共享 ClientNameWidget 已加入标签、操作和状态语言绑定，但其五个 root UI 语言包 key 尚未补齐。公共函数合同已补充，实际缩放/变换/tooltip 行为尚待调用点验证。

后续必须完成：ClientNameWidget 语言包及完整 English 降级；S4 同步脚本对后续共享 UI 条目的完整保留；社区示例/冻结 API 与调用点测试；S0 更新、四语言双表对账；八模块多实例、双宿主租约、卸载、待提交输入、All/单声道状态不变的 Release 与三档系统字体 Windows 实窗验证。不得使用 offscreen，不初始化 JACK，不终止现有应用或音频进程。全部 S5 修改完成后集中构建与验证，再为每模块记录完成文档。Limiter 的 Ceiling 中文主标签已改为“输出上限”，需进入 S6 领域审校。

同时修正计划书第 11 节 S4 工作表被阶段状态行误覆盖的格式，并将 S0 阶段表更新为 S4 已验收基线。当前 S5 源码变化尚未刷新 S0，历史 S0/S4 PASS 仅证明已提交的 S4 基线。
