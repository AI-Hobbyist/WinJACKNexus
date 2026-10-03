# S0 文本和身份清单完成记录

2026-10-04 Theme 来源及双表闭合：当前应用源码快照 4c06e27，277 文件、10,389 次字符串出现，1773 翻译源、8609 保留项、7 结构化描述符，未分类 0；唯一显示 key 1297。Theme 238 实际 key 已逐源纳入，canonical 后台模板与显示 detail 模板分别记录；发行内置说明使用实际 identity.defaultDescription，作者主题名称/编辑/导出元数据保持。核对表三项 Catalog 草稿归入表一，Theme 排除理由归入表二，代表实窗验证状态同步。S0 分类及 Theme 源码/key/双表核对 PASS；差异为新增 238/退休 239 个显示 key。仅证明 Theme 对账闭合，其他 106 次候选出现（host1/common3/meter49/mixer45/node1/patchbay5/ui2）仍待调用链审查，用户人工审校未完成，S6/S7 不标完成。本轮无应用变更、无构建/模块重测/弹窗。见 S6-theme-source-closure.json 与 S6-coverage-gaps.json。

日期：2026-10-03。依据：根目录 `i18n国际化.md` 的第 2、6、8、10、11 节及 S0 完成条件。所有中间产物位于 `Docs/i18n`，对应用户指定的 `docs/i18n`。

## 验收对照

| 要求 | 交付证据 | S0 结论 |
| --- | --- | --- |
| 按模块建立 key 清单 | S0-keys.csv 逐源码位置，S0-key-catalog.csv 唯一显示 key；覆盖 host/共享 UI/theme/patchbay/adapter/mixer/aux/FX 链/meterbridge/VST scanner、八个 Native Fx 与两套示例 | 已完成 |
| 包含中文、纯英文及动态文字 | 字符串词法提取不按中文筛选；原始 JSON 逐字段；QRC 的默认主题内嵌 meta；S0-dynamic-sources.md 对应运行时生产者和显示场景 | 已完成 |
| 可追踪文件/控件/key | file、line、source_id、原文、调用行、周边源码、domain/key、usage、布局和占位符均记录；文件 manifest 带 SHA-256 | 已完成 |
| 术语表、上下文与语义疑点 | S0-glossary.md、S0-semantic-map.tsv 与迁移说明；专业缩写保留边界、PFL/AFL/SIP、Gain/Volume/Sub Mix、Gate 滞回及 Limiter 前视现状有源码证据 | 已完成 S0 基线；不认定母语审校完成 |
| 用户内容排除项 | 动态来源文档按类别/来源排除用户名称、注释、设备/路径/第三方元数据；固定项在 exclusions 与 resource/descriptor fields 逐次记录理由和保留方式 | 已完成 |
| 找出显示字符串参与逻辑的分支 | S0-identity-uses.csv 全范围直接比较证据；S0-identity-risks.md 解释节点/托盘分类与名称、All、MAIN、默认子图、窗口标题、展开状态和 SEL tabs→objectName 的间接依赖 | 已完成定位；不修改现有行为 |
| 对账无遗漏 | source→classified→keys/exclusions/structured；descriptor/resource 字段；文件集合/哈希；唯一 key 与占位符一致性；所有要求域均存在 | 最终结果以 S0-verification.json 的 PASS 为准 |

当前 S3 同步基线共 268 个源码文件、8,111 次字符串出现，分类为 1,522 次翻译源、6,582 次保留项及 7 份结构化描述符，未分类 0。七份描述符有 43 个字符串字段，资源表有 5,331 个字段；最终唯一显示 key 为 1,330 条，已迁移范围采用真实运行时 key。以上是当前基线统计，不能当作去重后的翻译工作量、四语言覆盖率或 UI 验收结果。

主程序更新轮新增 5 个显示 key、退休 3 个旧 key，并核对新增工程字段、SEL 属性及主题身份；随后 S1 同步轮新增 12 个显示 key，退休 0 个，并补录核心与示例字段、实际语言包。具体变化与历史资源快照的限制见 [S0-refresh.md](S0-refresh.md)，逐源码差异见 `S0-refresh-delta.json`。

## 已固定的后续边界

继续采用四语言、`auto` 独立状态、`locate/` 固定拼写、逐模块完整 English fallback、可选 i18n v1 扩展与显示/身份分离。精确 API、formatter、schema、域注册/租约及 ABI 生命周期由 S1 冻结，不能把 S0 拟定 key 认作已经导出的接口。

NativeFxApi.cpp 的工厂预设按 `WJN_FX_MODULE_INDEX` 分支归入各效果域，共享源码不意味着共享其他效果的语言包。旧 Node/FX 元数据接口里的 ID 不随语言变化；新显示元数据走可选扩展。社区第三方内容保持作者所有权与逐模块 fallback。

S2–S6 执行实际布局与热切换验证，S6 做领域/母语审校，S7 才做资源部署与 Release 覆盖包。后续翻译实施同步根目录 `i18n翻译核对.md` 的翻译/忽略两个表格，中间决策和验证证据继续写在本目录。

## 验证和影响

使用 `verify_s0.py` 在前台 PowerShell 离线运行，输出经过 `2>&1 | Tee-Object -FilePath "build.log"`，立即检查退出码；验证结果保存为 S0-verification.json。此工具验证清单完整性、来源一致性、模块覆盖及 key/占位符关系；动态与间接链路的语义结论由伴随文档给出，不把正则匹配当作程序行为测试。

S0 清单维护本身不改应用或 ABI；S1 代码与可选 ABI 扩展及对应 Release/模块/UI 验证另见 S1-completion.md。本轮未操作运行中的 WinJACKNexus、音频进程和用户工程，也未改端口名。

S2 同步轮：13 个源码文件及 3 个既有资源文件变化，新增/退休显示 key 各 80 个；已接入宿主的调用点使用实际运行时 key，对应四语言包与审校表，不再作为待定 key 提案。当前清单未分类 0，校验 PASS。

S2 最终目录选择框补充：新增两个 Qt 标准翻译上下文/原文字符串，均由同版 Qt QM 负责，排除宿主语言包；当前 exclude 6,277，显示 key 不变。初始 S2 完整差异保存在 S0-refresh-S2-initial.json，最终差异见 S0-refresh-delta.json。
