WinJACKNexus i18n Release 增量覆盖包（人工审校后续）

这不是完整安装包；按用户“直接继续，审核后续再搞”指令交付，四语言/音频领域人工审校后续进行。既有未验证项继续明示，不声称实窗或安装验收通过。
仅供与现有 WinJACKNexus、Qt 6.10.3 Release 和 MSVC 运行库兼容的安装使用。
Probe32 使用 x86 MSVC 运行库，其他文件使用 x64；保留既有 Qt platforms/qwindows.dll、默认主题、VSTProcess32/64 及 JACK 环境。

相对路径与程序安装根目录对应；examples 为可选开发示例，无需加载为业务节点。
计划部署时先自行退出目标安装的程序并备份受覆盖文件，再将包中同名运行时和 locate 文件覆盖到安装根目录。
本次打包没有操作或停止任何运行中的应用/音频进程，也没有覆盖用户安装。
不要删除原安装目录、主题、预设、工程或设置。包不携带 QSettings 数据，故不会主动改写保存的语言选择。
现有运行时依赖不在本增量包中；不可用作新机器的完整安装。

13 模块资源有完整 English。Node 开发示例按 S1 合同有意在三种非 English 中缺 description/greeting，以演示 English fallback；缺项未标成译文。
此前每模块单次测试结果保留其原基线。Scanner 修复后、共享控件新增显示和 S6 补漏没有新增实窗验收；不再做缩放或重复模块测试。
归档 CRC、成员、逐文件 SHA-256、PE 架构及直接/延迟导入检查仅证明包结构和静态依赖，不代表运行时/译文审校通过。
更多证据与待审校词表见仓库 Docs/i18n/S6-human-review.md、S6-supplement-verification.json 和 S7-overlay-verification.json。
