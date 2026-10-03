# S6 来源补漏技术记录

2026-10-04 S6 剩余来源与显示补漏完成源码/编译核对：原 bd1a8f6 清单的 59 次候选全部逐项审查，31 项保留后台/技术/用户内容，25 项保留原始 API 字符串并在已确认的 GUI 边界翻译，3 项旧 UI literal 已替换。未分配角色使用 itemData 保存原始值，语言刷新阻断信号；极坐标输入/输出复用现有 key；四类 mixer 错误显示映射新增三 key/复用一 key，FX 旁通标题及正文使用两 key 和原始模块 ID 参数。六项新增四语言草稿、三项复用：宿主源码/实际部署 v11/780。必要 Release wjn_mixer_meter_i18n_tests 及依赖编译成功 exit 0，调用点检查只编译未执行；零新增模块测试、零弹窗。源码/资源/部署核对 PASS，当前 277 文件/10420 出现、1676 翻译源/8737 保留项/7 结构化、未分类 0、1206 唯一显示 key，差异六 key 新增、53 旧提案退休，未映射候选 0。原始 SDK/序列化/错误/警告去重保持。双表与审校词表同步。新增角色、图例、错误及旁通警告行为实窗未验证，旧实窗报告保留原基线，不借新 DLL 哈希扩称通过。S6 人工审校未完成，S7 尚未完成。见 [补漏记录](S6-supplement-completion.md)、[核对证据](S6-supplement-verification.json)、[逐项来源决定](S6-remaining-source-audit.json)。

本项结论为源码、四语言资源、部署及 Release 编译通过，未标记整个 S6 完成。实际模块执行次数 0、实窗显示次数 0。用户规定每模块只测试一次，因此先前 Mixer/Meter/共享 UI 的运行结果保留历史基线，新增行为不借旧证据认定通过。

逐项审查依据见 S6-remaining-source-audit.json：保留原始模型/接口诊断、默认可编辑名称、声道角色/技术标签、未消费的 legacy SDK 元数据及 i18n 失败时 English fallback。需要显示的 canonical 错误保留后台内容，由 MixerErrors 或结构化模块 ID 在 GUI 边界转换。不是将所有后台英文一律排除。

构建使用现有 build、VS18 CMake、Release 目标 wjn_mixer_meter_i18n_tests，无 ALL_BUILD、无新目录、无运行 JACK 集成测试。日志 S6-supplement-build.log，进程退出码 0；编译包括 WinJACKNexus、ui.dll、mod_digital_mixer.dll、根目录 mod_meterbrigde.dll 及调用点检查。已有 Node/NativeFx 可选接口重复声明的 C4273 警告与 windeployqt VCINSTALLDIR 警告仍出现，未作为运行时成功证据。一次静态 verifier 初次因误写 Meter DLL 相对路径失败，按实际输出修正为根目录 mod_meterbrigde.dll 后必要核对通过；失败日志保留。不涉及第二次模块执行。

S0 差异使用原 meter refresh 单独保存为 S0-refresh-S6-meter.json。本轮清单六 key 新增、53 旧提案退休；逐来源分类、占位符、全部域、资源 membership 和现有译文保留通过。部署五 JSON 与源码逐字节一致，宿主 v11/780；现有 Theme 238 项保持，Theme 唯一一次实窗报告仍对应 v10/旧 DLL 基线。verify_s6_supplement.py 对新构建输出记 SHA-256，哈希仅用于交付定位。

四语言译文仍待用户母语/音频领域审校；当前只有此前 Size→大小明确批准。扫描器修复后的实窗、共享控件新增显示与此次补漏没有新实窗验收，这些限制应随 S7 交付记录保留。
