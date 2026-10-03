# S6 人工审校记录

2026-10-03。审校负责人：用户（已在本会话明确确认）。已收到一条文件大小术语修改意见并落实；其余条目待用户意见，尚无任何语言的整体人工审校通过结论。

自动 key、占位符、字体与界面检查不代替母语或音频领域审校。当前译文依据为根目录 `i18n翻译核对.md` 表一、`S0-glossary.md` 语义基线及 `S0-identity-risks.md`。S6 VST 工具剩余词条尚待接入，本记录不表示词汇清单已经闭合。

## 首批需明确的音频术语

| 范围 | 核对项 | 审校要点 | 结论 |
| --- | --- | --- | --- |
| Gate | module.name / module.description | 中文目前使用“Gate 门限”和“向下门限”，核对是否改为“噪声门”；描述须符合实际处理语义 | 待用户意见 |
| Gate | control.floor / control.hysteresis | Floor 为最大衰减下限；Hysteresis 为开关门限差，单位 dB，不是保持时间或输入底噪 | 待用户意见 |
| Limiter | 输出上限 / Ceiling | 限制输出上界，区分 Threshold；前视字段的实现边界见身份风险记录 | 待用户意见 |
| De-Esser | Bandwidth | 当前参数以 Hz 保存、以 kHz 显示，说明应为带宽，不应称 Q 值 | 待用户意见 |
| Compressor | Threshold / Knee / Makeup gain | 各模块“阈值/门限/しきい値/スレッショルド”等用词是否需统一；Soft Knee 是宽度语义 | 待用户意见 |
| Mixer | PFL / AFL / SIP | 分别核对输入增益后、推子后及就地独奏的真实信号位置；缩写本身保留 | 待用户意见 |
| Meter | Momentary / Short-term / Integrated / LRA | 核对响度时间窗口及中文、日文、韩文专业术语 | 待用户意见 |
| Native Fx 参考标签 | hint.* 中的括号 English 对照 | 主标签已有四语言译文；是否保留参考窗口的括号 English 形式 | 待用户意见 |
| 标准文件对话框 | QDirModel / QFileSystemModel 的 Size 列标题 | 用户明确要求“体积换成大小”。已新增项目 Qt 中文覆盖词表，并用于部署包与内嵌回退；200% 简体中文 EQ 实窗已显示“大小”，目录、账户与文件名保持原样 | 用户已确认修改；实施及必要实窗验证通过，仅覆盖这两条中文译文 |

## 记录规则

收到意见后逐项记录域/key、语言、原译文、建议译文、语义依据和最终结论，再同步源词表、实际语言包和双表。范围未明确的意见不扩大为全部四语言或全部模块通过。S6 完成前仍需记录四语言与音频领域审校的最终覆盖范围。

## S6 探测错误增补

2026-10-04 新增 `vstscanner/probe.*` 十项错误草稿，扫描器共 77 项。见 `S6-scanner-strings.tsv`。需核对 VST3 factory/component/class ID、VST2 entry point/AEffect 等专业术语在四语言中的表达；插件名称和 class ID 参数保持原始内容。当前没有新增人工批准。

2026-10-04 共享 Controls 增补三项草稿（输入筛选/展开侧边栏/收缩侧边栏），四语言详见 `S6-shared-strings.tsv`。专业译文审校待用户；这次编译与静态部署核对没有新增人工批准或实窗验收。

2026-10-04 主题管理新增 80 项四语言草稿，见 `S6-theme-ui-strings.tsv`。包括背景尺寸/平铺选项、Opacity/透明度语义和主题包导入导出术语，待用户意见；该批仍在实施，只有源码/语言包静态核对，没有新增 Release 或实窗验收。

2026-10-04 新增 ThemeCore 46 项诊断草稿（S6-theme-core-strings.tsv），包含 CSS/registry/资源/导出语义。原始 token、类型、路径和诊断代码保持，显示说明待用户四语言审校；目前只有源码一致性检查，无人工批准或运行时验收。
