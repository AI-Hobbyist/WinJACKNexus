# S6 VST 工具与全清单审校进度

2026-10-04，实施中，尚未完成。

历史 Probe 修改前清单（对应 e05802d）的刷新与对账通过：275 个源码文件、9,690 次字符串出现、1,302 个唯一显示 key、未分类 0，见历史 `S6-S0-verification.log`；当前 S0-verification.json 已更新为本轮结果。VST 宿主/扫描器调用点已映射实际 `vst` / `vstscanner` key，独立包纳入资源清单。扫描器 `Instruments/Effects` 是已交付 info 文件目录身份，位数 `%1-bit` 是标准单位格式，均有明确排除理由，不替换路径或数值。

本次 Probe 修改前的差异导航有 369 次源码出现等待核对：theme 250、meterbridge 49、mixer 45、vstscanner 10、patchbay 5、ui 5，其他 5。它包含旧 key 提案、后台诊断、默认用户内容和真正未接入的显示项，不能视为缺译数量或覆盖率。扫描器十项均位于 `ProbeMain.cpp`，会经 IPC 返回 GUI，本轮已处理其显示/协议边界，变更后的 S0 清单和候选数须另行刷新。共享 UI 的筛选输入、侧边栏提示等亦待处理；其余项须逐条以调用路径判定，不通过统一排除掩盖遗漏。见 `S6-coverage-gaps.json`。该清单刷新轮未启动模块测试或 UI 窗口。

VST 宿主已接入独立 `vst` 域及可选 Node i18n v1 服务，57 个四语言草稿 key，完整 English 和部署 manifest。原 Node v2、主题 ID、插件 class ID、进程/端口枚举及工程字段保持。提示状态以 key/English/参数保存；打开的控件和状态按语言通知刷新，Qt 文件/类选择窗口也建立标题、描述及筛选文字绑定。

所需 Release 编译通过，保持 VST2=ON，并继续包含 VST2/VST3 支持。首次编译因缺少 QVariant 的完整定义失败，补 include 后只重编必要目标；失败与修复日志分别保留。唯一一次实际 DLL 实窗测试通过 77 项检查，默认系统字体 Microsoft YaHei UI，实际 windows 平台，1 次窗口显示，无追加缩放矩阵。四语言复用同一窗口，操作/已存状态更新、未提交路径草稿/选区/焦点、枚举 data 和工程字节保持，双租约、最后 detach 回退 English 和 DLL 卸载通过。韩语实际桌面截图已查看，控件和状态可读，日文/韩文路径草稿及选区保留，无黑图或其他应用画面。见 `S6-vst-verification.json`、`S6-vst-ko-KR.png`、`S6-vst-tests.log`。未运行真实插件/VSTProcess/JACK 集成或模态对话框交互测试，不将这次检查扩大为这些范围通过。

VST Scanner 使用现有 HostI18n 在创建窗口前恢复宿主保存的语言选择，扫描路径/输出目录设置继续由扫描器独立保存。独立 `vstscanner` 域共 77 个四语言草稿 key（首轮 67 个，本轮增加十个 Probe 错误）：43 个窗口/操作/状态 key，24 个扫描错误与进度消息 key。运行目录为 `vstscanner/locate`，模块内部仍遵守固定 `locate` 合同，English manifest/包同时内嵌。插件类别 ID、格式 ID、路径、info 文件与插件元数据保持为稳定身份。

扫描核心仅接收每次请求的显示翻译 callback，不依赖 UI 库。作用域退出时恢复 callback，避免后续独立扫描继续借用 GUI；未提供 callback 时使用 English。业务窗口复用公共控件，移除扫描器原有局部硬编码调色板与提示文字 QSS。

必要 Release 构建通过，保持 VST2=ON，包含扫描器、32/64 位探测器及支持 VST2/VST3 的 `mod_vst_host`。首次且唯一模块验证在创建窗口前失败：`locatePath must be locate`，退出码 2。已将注册调整为扫描器模块根目录及其固定 `locate`，必要目标重编通过。按用户每模块只测试一次的要求，不重复运行模块测试；未生成实窗截图，修正后的语言恢复、界面与扫描交互均保留未验证说明，不能将编译或资源核对作为实窗通过。

证据见 `S6-scanner-build.log`、`S6-scanner-tests.log`、`S6-scanner-build-repair.log` 与 `S6-scanner-verification.json`。保留失败日志，不追认为 PASS。当前修改后 S0 清单尚需刷新；其余 VST 宿主显示、共享遗漏项和完整双表闭合继续处理。四语言人工审校由用户提供意见，目前仅“体积→大小”已确认，其他语言/术语尚未批准。

2026-10-04 Probe 错误协议完成本轮实施与有限验证：项目错误以稳定 `error_key/error_args` 或 `probe_error_key/probe_error_args` 传输，旧 `error/probe_error` 保留完整 English 回退，SDK 原始错误不套用项目 key。Scanner 白名单映射 key 后才调用请求内文本服务，并校验参数数量与字符串类型；未知 key/不合规参数退回原文，成功/跳过/verified 判断仍基于原协议字段。四语言资源与双表同步新增十项，共 77 个草稿 key。

必要 Release 目标编译成功，实际 Probe32/Probe64 各执行一次离线缺失 DLL 场景，通过 exit=1、ok=false、skippable=false、英文错误及稳定 key/空参数数组核对。见 `S6-probe-build.log`、`S6-probe-tests.log`、`S6-probe-verification.json`。此验证只覆盖 VST2 LoadLibraryExW 失败路径；其他 VST2/VST3 分支和 Scanner 运行时译文显示尚未验证。未复跑 Scanner/VST 宿主/Native Fx 实窗测试，未追加缩放测试、没有 UI 弹窗。资源及部署静态核对独立记录，不替代实窗或用户审校。

2026-10-04 共享 Controls 显示补漏：新增 `ui/choice.filterPlaceholder`、`ui/dock.expand`、`ui/dock.collapse` 三项四语言草稿，宿主 manifest/catalog 升为 v6，共 536 个 key，外部部署及内嵌 English 输入同步。ChoiceField 只刷新默认 placeholder，不写输入文本、选区或 item data；调用方覆盖 placeholder 后停止默认刷新。DockPanel 按当前折叠状态刷新箭头提示，构造时没有 i18n manager 则回退完整 English。冻结 UI 文档与既有调用点测试同步。

必要 Release 编译通过，目标为既有共享实窗 fixture，依赖完成 ui.dll 和宿主资源部署，并编译新增调用点检查。没有执行 fixture、没有 UI 显示或缩放测试；遵守每模块只测一次限制，不扩大历史共享实窗证据为新增三项运行时通过。静态资源/部署核对 PASS，新增默认 placeholder、侧栏提示及草稿/调用方覆盖保持的运行行为仍为未验证，人工译文审校待用户。见 `S6-shared-build.log`、`S6-shared-assets.log`、`S6-shared-verification.json`。

历史 S5 v5/533 key 和 DLL 哈希是对应提交的阶段证据，当前 S6 v6/536 与重编 DLL 由本轮证据记录。不要直接重跑历史固定 v5 验收脚本或将其旧哈希宣称为当前发行哈希。Probe 和共享补漏后 S0 清单/候选数仍待统一刷新，Theme 等范围继续审计，S6 尚未完成。

2026-10-04 Probe/共享补漏后的 S0 清单统一刷新完成并通过：275 文件、9,743 次出现、1,658 翻译源、8,078 保留项、7 份结构化描述符、未分类 0、1,298 唯一显示 key。相对 e05802d 清单新增 13 key、退休 17 key；这些变化是来源映射和临时资源 metadata 分类调整，不是缩减翻译目标。首次核对发现新 IPC 字段/key 比较未分类，补明确身份理由；随后发现 Probe English 回退前缀与 Scanner 完整 %1 模板被误视为相同源模板，分清传输拼接片段与完整显示模板后校验通过。失败日志与修正证据均保留。

当前差异导航 352 次候选：Theme 246、meterbridge 49、mixer 45、patchbay 5、common 3、ui 2、host 1、node 1。Probe 与三项共享 Controls 已对齐实际 key。Theme 明确排除四项 importImage/importFont 的临时资源载体 metadata：完整/片段导出使用 editedMetadata()，用户内容和包身份保留。其余 Theme 候选继续保持开放，不能将错误一律归为内部日志：refresh() 实际显示 snapshot diagnostic.message，颜色/导入/应用/导出回调将 error 显示到 status。

已保存 `S6-theme-source-audit.json` 与当前源码哈希，标明 246 次候选及另外需核对显示副本的 scope/size/repeat enum 站点。后续语言刷新不能直接复用会重载编辑值的 refresh()/refreshBackgroundEditor()，须单独更新显示并保持草稿、选区、dirty 状态与 token/currentData。来源核对通过见 `S6-theme-audit-verification.json`，它仅验证清单成员和哈希，不证明翻译/实窗完成。本轮没有模块编译、UI 测试或弹窗。Theme 实施、其他候选核对、用户审校与 S7 仍待完成。
