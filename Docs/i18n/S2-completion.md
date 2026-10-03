# S2 宿主与托盘完成记录

日期：2026-10-03。状态：已完成 S2 宿主与托盘；S3–S7 尚未完成。

| 要求 | 实际实现与证据 |
| --- | --- |
| 第一条界面及启动提示前恢复语言 | main.cpp 在 QApplication 后、单实例提示/托盘/主题窗口前初始化 HostI18n；编译内嵌 manifest 和完整 English，外部包缺失仍能显示启动说明 |
| 五态选择与持久化 | 原生 QMenu 的 QActionGroup 与全局设置 ChoiceField 共用 HostI18n；稳定 action data、互斥检查、自动项显示有效语言；独立进程验证四个手动语言及五组 auto 偏好，保存 auto |
| 快捷栏与无托盘降级 | initializeFallback 创建快捷栏及原生右键菜单；全局设置按钮可打开实际设置窗口；快捷栏静态标签、tooltip、无障碍名称、菜单 chrome 随语言刷新 |
| 全局/JACK 设置与提示 | 84 个宿主 key 提供四语言译文；标题、分组、标签、按钮、输入提示、启动/模块错误和 JACK 状态接入；目录、设备、驱动/命令身份、用户内容及数值保持原样 |
| 热切换保留编辑状态 | 实际 JACK 数字输入使用关闭 keyboard tracking 的测试，检查未提交文本、选择、焦点和数值不被重译；晚创建窗口使用当前快照 |
| 冷启动一致 | 子进程读取同一临时语言设置和应用 INI 根；首次显示属性、语言选中项及选项文字与运行时切换后对照 |
| 损坏与回退 | 损坏目标包保留旧快照/选择/持久化值；无 locate 时使用编译 manifest/English；缺失磁盘 Qt QM 时使用同版内嵌 QM |
| Qt 标准文本 | 部署并内嵌 Qt 6.10.3 的 qtbase_zh_CN.qm、qtbase_ja.qm、qtbase_ko.qm；标准 Close 与 Qt 翻译器同步；目录选择使用 Qt 自身的 Windows 实窗，热切换后的五组标准标签与新建窗口一致，路径/未提交输入/选择保持；补齐 Qt LanguageChange 将目录接受按钮改回通用 Open 的行为；DialogButtonBar 保持原对象大小 |
| 公共 UI 与主题 | 快捷栏局部 QSS/自绘图标改用已有共享控件及主题图标绑定；IconActionButton 按字体省略长文本并保留完整 tooltip；语言不重建主题 |
| 系统字体与实窗 | Windows 平台、QFontDatabase::GeneralFont、既有 font-source=system Token；实际系统字体 Microsoft YaHei UI，四语言标题无缺失 glyph；禁止 offscreen 的规则已进入 AGENTS.md 和计划书 |

Release 在现有 build 中编译所需宿主、UI 与测试目标通过；最终 Windows 实窗宿主测试 402 次断言、S1 合同测试 201 次检查全部通过。100%、150%、200% 缩放各完成四语言的全局/JACK 设置、快捷栏及目录选择框渲染，共 48 张 PNG；已检查字体、换行、控件边界和快捷栏标签。没有使用 ALL_BUILD，没有创建备用构建树或完整发行包，没有执行 JACK 连接集成测试。

证据：[Release 修复构建](S2-build-final.log)、[最终实窗测试](S2-tests.log)、[150% 验证](S2-windows-scale-1.5.log)、[200% 验证](S2-windows-scale-2.log)、[资源及哈希对账](S2-verification.json)、[渲染结果](S2-rendered/)、[接口合同](../I18n-API.md)。S0 已同步，266 个源码文件、7,826 次字符串出现、1,329 个唯一显示 key，未分类 0；S1 同步复验 PASS。

早期诊断保留历史失败日志：新宿主析构时租约发布通知访问已析构译器，已通过提前断开自身通知修复，所有冷启动子进程正常退出。早期 Qt 设置隔离失效曾把用户保存的采样率改为 12345；已按正在运行的 JACK 启动命令恢复 48000，现使用明确的 INI 格式和临时设置根，并检查路径。最终复验后用户采样率仍为 48000；现有 WinJACKNexus（PID 37716）和 jackd（PID 36020）保持运行。历史 offscreen 结果不得作为完成证据。

当前完成范围为宿主。接线盘内部工程菜单、主题管理页面、调音台、电平表桥、正式八效果与 VST 工具按 S3–S6 迁移。四语言包是实际可用的译文草稿，根 i18n翻译核对.md 按实际包逐条记录，尚待母语/音频领域审校；本阶段完成不代表整个国际化或 S7 覆盖包完成。

S3 后回归（2026-10-03）：系统字体 Windows 实窗宿主 402 次断言、S1 201 次检查再次通过，最终日志为 S2-tests.log/S3-regression.log，原 S2 日志保留为 S2-tests-pre-S3.log。当前宿主资源 catalogVersion 3，84 个 host key 保持一致，Qt QM、核对表与实际 Release 资源对账 PASS，最新 SHA-256 见 S2-verification.json。S3 增加 adapter/patchbay 两域，不把 S3 回归替代既有 S2 三档缩放验证。最新完整 S0 基线为 268 文件、8,111 次出现、1,330 个 key，未分类 0；接线盘和适配器已独立完成，见 S3-completion.md。
