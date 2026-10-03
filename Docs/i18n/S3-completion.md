# S3 接线盘和适配器完成记录

完成日期：2026-10-03。范围依据根计划书第 8–12 节与 S3 完成条件。接线盘、节点库、侧栏、工程/画布菜单、子图及四类 WASAPI/MIDI 适配器已接入四语言，Release 构建与系统字体 Windows 实窗验证通过。译文为待母语/领域审校草稿，审校属于 S6。

## 逐项验收

| 要求 | 当前实现与实际证据 | 结论 |
| --- | --- | --- |
| 节点库、分类、描述、侧栏按钮/下拉 | Library.cpp 的独立显示元数据及 QObject 绑定；实际节点树/属性栏、四种适配器 DLL 表单和状态逐域/key 比较；五种探针名称分别验证，原生树省略并保留全文 tooltip | 通过 |
| 工程与上下文菜单、子图 | 原生 QAction 的显示绑定与稳定 data；真实打开/保存、重命名、自动保存整数、未保存提示与画布弹出菜单热切换；实际双击子图进入宿主创建的子图页签和四种接口图形 | 通过 |
| 首次创建、运行中切换、重开、晚加载 | 四语言分别保存临时 INI 后启动真实窗口子进程，首次节点库/侧栏/适配器/画布显示与热切换一致；实际侧栏 hide/show、电平窗口销毁重开、扫描目录外社区 DLL 晚加载及已有实例切换 | 通过 |
| 稳定身份与显示分离 | category identity role、模块路径/index、端口及实例 ID 保持原值；特殊模块行为依文件身份；托盘原始 name/category 与单独 displayNames、用户 clientName、动作回调和旧重载分别验证 | 通过 |
| 状态与用户数据保留 | 实际检查搜索、展开、滚动、同一选中条目/实例、焦点、名称选区、未提交设备文本与声道数；适配器持久 settings 和主图/子图工程序列化字节不变；语言回调只更新显示，不调用设备刷新/应用或音频初始化 | 通过 |
| 模块注册与卸载 | 注册先于实例创建；节点库与实例持共享租约；实际两消费者共享租约、存活实例热切换；源代码销毁 UI/实例→释放租约→FreeLibrary，S1 真实 DLL attach/detach/unload 合同回归通过 | 通过 |
| 字体、长文本、高 DPI | QT_QPA_PLATFORM=windows，可见实窗，QFontDatabase::GeneralFont 与既有系统字体 Token；实际 Microsoft YaHei UI；绑定文字与节点 tooltip 的 glyph fallback 检查，100%/150%/200% 每档四语言；长标题按既有字体省略、tooltip 与工程全文保留 | 通过 |
| 清单、语言包、忽略边界 | S3 132 个唯一 key（adapter 22、patchbay 110），四语言与 manifest、部署包、核对表逐条一致；S0 源码范围 155 次翻译源、122 个 key 均属于 S3 清单；完整 S0 哈希/分类对账 PASS、未分类 0 | 通过 |
| 公共接口与调用点 | Docs/UI-API.md 与 Docs/I18n-API.md 记录 LocalizedText、托盘显示重载与租约行为；Node-API.md 补充实际宿主消费路径；新旧托盘接口、绑定与社区 descriptor 在实际 Windows 窗口验证。必需 Node/Native Fx ABI 未改 | 通过 |
| Release 与回归 | 现有 build 中所需 Release 目标成功构建，三档 S3 各 8,037 次检查；S2 402 次断言、S1 201 次离线合同检查通过，CTest 3/3 | 通过 |

表中的源码范围次数以最终 S3-verification.json 为准；它统计用户显示源，不包含有意保留的 SDK/端口/工程身份。子图默认接口名称用于 JACK 注册，原样保留；节点库类型说明单独翻译。用户文件夹、第三方插件名、设备名、技术单位等忽略边界已同步根 i18n翻译核对.md 表二。

## 界面抽查与修正

自动检查生成 204 张 PNG（17 种窗口/菜单 × 四语言 × 三档缩放）；画布使用 Windows 原生窗口表面捕获，包含真实 OpenGL 视口。人工抽查涵盖四语言、三档缩放、接线盘/长标题、子图接口、适配器表单、电平窗口、输入/整数/文件/未保存对话框及设备/画布菜单。检查文字缺失、换行/省略、控件边界、可见编辑选区与实际绘图；未宣称逐张人工检查全部 204 张图。

修正了五类电平探针误用同一名称、设备菜单重建残留旧子菜单，以及 Qt LanguageChange 把输入对话框内部提示恢复为默认文字的问题。输入提示在 Qt 标准按钮刷新后恢复应用译文，并对实际可见 QLabel 增加检查。测试长名称与普通节点分开放置，截图确认省略和完整绘图。新头文件顺序暴露 Windows max 宏污染，Canvas 显式 NOMINMAX 后重编通过；失败日志保留为 S3-build-input-refresh-failed.log。早期 7,966/8,010/8,020/8,021 次结果是历史版本，最终验收使用 8,037 次结果。

实际抽查示例：[日文 100% 接线盘](S3-rendered/scale-1/patchbay-ja-JP.png)、[英文 150% 接线盘](S3-rendered/scale-1.5/patchbay-en-US.png)、[韩文 200% 接线盘](S3-rendered/scale-2/patchbay-ko-KR.png)、[中文 150% 重命名](S3-rendered/scale-1.5/rename-zh-CN.png)、[韩文 200% 自动保存](S3-rendered/scale-2/autosave-ko-KR.png)、[日文子图](S3-rendered/scale-2/subgraph-ja-JP.png)。

## 验证命令与证据

使用 VS18 自带 cmake/ctest；全部构建/测试前台 PowerShell 经 `2>&1 | Tee-Object -FilePath "build.log"`，立即检查 LASTEXITCODE。构建目标为 `wjn_patchbay_i18n_tests` 与 `mod_patchbay` 及已有依赖；未使用 ALL_BUILD 或备用构建目录。100% 执行 `ctest --test-dir build -C Release -R '^(patchbay_i18n_tests|host_i18n_tests|i18n_contract_tests)$' --output-on-failure -V`；150%/200% 使用同一 Release S3 程序、Windows 平台、系统字体，分别设置 QT_SCALE_FACTOR。所有退出码 0。

证据：[最终构建](S3-build.log)、[100% 与回归](S3-regression.log)、[150%](S3-tests-scale-1.5.log)、[200%](S3-tests-scale-2.log)、[语言包/源码/Release/204 张截图 SHA-256](S3-verification.json)、[S0 校验](S0-verification.json)、[核对表同步](S3-audit-final.log)。S0/S1/S2/S3 资产验证器全部 PASS。

UI、离线模型与实际模块加载是本阶段验证范围，未执行 JACK 客户端/端口连接集成测试。没有初始化测试适配器或改变正在运行的音频；WinJACKNexus PID 37716、jackd PID 36020 持续运行，用户 JACK sampleRate 仍为 48000。未生成发行覆盖包。

S3 完成不表示整个国际化完成。调音台/电平表桥内部属于 S4，八个正式 Native Fx 属于 S5，VST 工具与母语/领域审校属于 S6，发行包属于 S7。根计划书第 11.1 节同步阶段状态与证据。
