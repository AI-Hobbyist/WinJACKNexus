# S6 VST 工具与全清单审校进度

2026-10-04，实施中，尚未完成。

VST Scanner 使用现有 HostI18n 在创建窗口前恢复宿主保存的语言选择，扫描路径/输出目录设置继续由扫描器独立保存。独立 `vstscanner` 域共 67 个四语言草稿 key：43 个窗口/操作/状态 key，24 个扫描错误与进度消息 key。运行目录为 `vstscanner/locate`，模块内部仍遵守固定 `locate` 合同，English manifest/包同时内嵌。插件类别 ID、格式 ID、路径、info 文件与插件元数据保持为稳定身份。

扫描核心仅接收每次请求的显示翻译 callback，不依赖 UI 库。作用域退出时恢复 callback，避免后续独立扫描继续借用 GUI；未提供 callback 时使用 English。业务窗口复用公共控件，移除扫描器原有局部硬编码调色板与提示文字 QSS。

必要 Release 构建通过，保持 VST2=ON，包含扫描器、32/64 位探测器及支持 VST2/VST3 的 `mod_vst_host`。首次且唯一模块验证在创建窗口前失败：`locatePath must be locate`，退出码 2。已将注册调整为扫描器模块根目录及其固定 `locate`，必要目标重编通过。按用户每模块只测试一次的要求，不重复运行模块测试；未生成实窗截图，修正后的语言恢复、界面与扫描交互均保留未验证说明，不能将编译或资源核对作为实窗通过。

证据见 `S6-scanner-build.log`、`S6-scanner-tests.log`、`S6-scanner-build-repair.log` 与 `S6-scanner-verification.json`。保留失败日志，不追认为 PASS。当前修改后 S0 清单尚需刷新；其余 VST 宿主显示、共享遗漏项和完整双表闭合继续处理。四语言人工审校由用户提供意见，目前仅“体积→大小”已确认，其他语言/术语尚未批准。
