# S7 发行准备记录

2026-10-04 S7 发行资源准备：已从现有 Release 输出建立显式增量清单，13 个资源模块、68 个 JSON/QM 资源文件、25 个项目运行时候选文件，1196 个 manifest 必需 key。宿主、VST 宿主/Scanner、八内置 Native Fx 及 NativeFx 示例四语言来源与部署逐字节匹配；Node 示例 English 三 key 完整，三非 English 包按 S1 合同各缺 description/greeting 演示 fallback，六处明确列出，未标为已有译文。词表检查拒绝 JSON 重复 key，核对域/版本、文件根限制、English 完整性、非空值及复数/占位符。显式清单不纳入 Debug、测试程序、PDB、TS、翻译工具、用户主题/设置/工程，不改用户语言选择。当前仅资源清单核对 PASS，零模块执行、零弹窗、未生成发行压缩包；最终依赖/归档核对和 S6 用户人工审校未完成，S7 保持实施中。见 [资源清单](S7-resource-inventory.json)、[核对日志](S7-resource-inventory.log)、[准备记录](S7-progress.md)。

源码实施基线 e0b1f6e；当前新增的是资源清单与校验工具，不改 CMake 或运行时行为。prepare_s7_resources.py 只读取已编译的现有 build/release/bin，显式选择必要运行时文件及各模块部署目录，记录模块 ID、catalogVersion、四语言覆盖、SHA-256 与字节数。未创建 build/workspace 目录，未追加编译或模块测试，没有实际覆盖用户安装。

S7 资源工具最初按所有值均为字符串进行断言，遇到 ExampleNode English 复数对象失败；按已有 i18n JSON 合同补充 plural 验证后，又遇到该示例有意缺项，核对 S1 合同和 source 后增加严格限于 example-node/description,greeting/三非 English 语言的例外。两次工具失败日志保留，未运行或重跑 UI 模块。最终所有实际内置应用模块四语言完整性仍要求严格相等；没有泛化允许缺项。

当前未提供最终覆盖压缩包。资源准备不代表 S6 人工审校完成、Scanner/补漏实窗通过、已有安装兼容或发行验收通过。既有 S4/S5 冷启动/缩放与模块运行记录保持其原基线和已知缺口；按用户规则不增加缩放或第二次模块测试。后续归档需仅含变化所需运行时及 JSON/QM，检查成员/哈希、现有安装依赖和用户选择保护，保留所有未验证项。社区两套示例为可选 API 样本，不自动安装成业务节点。
