# 国际化中间文档

后续 S0–S7 的中间清单、决策、核对记录和验证证据统一放在本目录。Windows 文件系统不区分大小写，本仓库既有目录名为 `Docs`，因此用户指定的 `docs/i18n` 对应此目录。

实施依据为仓库根目录 `i18n国际化.md`。本目录不替代冻结的 UI、Node、Native Fx API 契约；接口变更仍须同步这些契约。正式翻译阶段仍按计划维护根目录 `i18n翻译核对.md`，中间材料在这里保存。

## S0 当前状态

S0 文本和身份清单已完成。完成范围是静态文本/动态来源/拟定语义 key/术语/忽略项/身份风险的基线，S1 核心及两套开发示例已实施；业务窗口迁移、完整四语言包、母语审校与最终 UI 验收尚未完成。

- `S0-source-literals.csv`：所有扫描范围内的原始字符串证据，包含纯英文、中文、原始 JSON、协议标识和非 UI 文本。原始表的 `pending` 是采集初始值；最终处理结果见 classified 表，不能直接把证据表导入语言包。
- `S0-source-manifest.json`：扫描文件、SHA-256 和范围限制。
- `scan_s0.py`：生成上述证据的工具；重新生成会覆盖证据表，人工清单应另存。
- `S0-identity-risks.md`：显示字符串参与逻辑和稳定标识的审查记录。
- `S0-dynamic-sources.md`：运行时元数据、用户内容、错误 detail 与模型缓存的来源边界。
- `S0-glossary.md`：术语建议、DSP 语义证据与待审校项。
- `S0-semantic-map.tsv`：中文和短英文的人工语义 key 映射。
- `S0-classified-literals.csv`：完整源码分类结果，保留每次出现与理由。
- `S0-keys.csv`：逐调用点的 key、用途、布局来源、占位符及周边源码。
- `S0-key-catalog.csv`：去重的显示 key 提案，共 1,329 条；身份比较出现位置单独保留，不作为显示调用点。
- `S0-exclusions.csv`：逐项固定标识、非 UI 文本和技术数值保留清单。
- `S0-descriptor-fields.csv`：七份原始 JSON 描述符的 43 个字符串字段。
- `S0-resource-fields.csv` / `S0-resource-manifest.json`：外部/QRC 引用与内嵌默认主题元数据、registry 字段对账。
- `S0-identity-uses.csv`：355 处直接字符串身份判断证据；间接依赖见身份风险文档。
- `S0-file-coverage.csv`：264 个源码文件的逐文件归属和数量对账。
- `S0-migration-notes.md`：清单使用规则、场景与宽度来源、完整模板及后续迁移约束。
- `classify_s0.py` / `verify_s0.py`：可重现分类与离线校验；`S0-verification.json` 保存最终校验结果。
- `S0-completion.md`：S0 验收对照与下一阶段边界。
- `S0-refresh.md` / `S0-refresh-delta.json`：本轮代码变化的新增、删除、使用变更和 key 退休记录；`refresh_s0.py` 用于后续基线更新。
- `S0-refresh-pre-S1.json`：S1 实施前主程序更新轮的历史差异，不覆盖当前基线。
- `S1-completion.md` / `S1-verification.json`：核心与契约的完成对照、源码/Release 产物哈希、实际语言包和离线验证结果。
- `schema/` / `verify_s1_assets.py`：三类公开 JSON schema 和当前包的结构/English 完整性对账。
- `S1-build.log` / `S1-test.log`：前台 Release 编译与真实模块/UI 离线测试证据。

S1 核心与契约已完成（见 S1-completion.md），后续进入 S2 宿主与托盘，不应把本清单直接当作已实现的语言包。代码新增/移动文本时重生成证据并重新分类；禁止用旧 manifest 的通过结果验收新源码。生成器中的规则和语义映射也是维护对象；新文本必须审阅分类结果，即使规则没有产生 pending，也不能省略新增/变更文案的人工核对。

## 当前完成审计

| S0 门槛 | 当前证据 | 结论 |
| --- | --- | --- |
| 包含纯英文和中文的源码出现位置 | 264 个文件，7,585 次出现，5,211 个文件内去重证据 ID；逐文件 SHA-256 | 完成证据基线 |
| 可追踪文件/控件/语义 key 清单 | 1,501 个可翻译源码出现位置及 descriptor/resource 显示字段；1,329 个唯一显示 key 提案 | 完成 S0 清单；未实施译文 |
| 术语表与语义疑点 | S0-glossary.md、semantic-map、迁移说明；Gate 滞回与 Limiter 未读取参数已有 DSP 证据 | 完成语义基线；译文人工审校留到后续 |
| 用户内容和动态排除边界 | S0-dynamic-sources.md；6,077 个源码固定/非 UI 项、描述符/资源字段逐项排除原因 | 完成静态与动态边界 |
| 显示字符串参与逻辑 | identity-uses 与 identity-risks；额外补充 SEL 页签文案拼接 objectName 的间接链路 | 完成风险定位；业务分离尚未实现 |
| S0 覆盖对账 | translate 1,501 + exclude 6,077 + structured 7 = 7,585；pending 0，descriptor 字段单独闭合 | 离线证据校验见 S0-verification.json |

S0 本身仅维护文档与证据；本轮同时把 S1 新增源码和实际语言包纳入当前基线。S1 的 Release 编译及模块/UI 验证另见 S1-completion.md，未操作运行中进程或改变端口。
