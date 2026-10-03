# S0 基线更新记录

日期：2026-10-03。以当前磁盘代码对比上次完成的 S0 快照；本文记录各轮清单同步；应用实现与验证见对应阶段完成记录。

## S1 实施前的主程序变化

12 个源码文件变化（含两个新增头文件），3 个资源文件变化。`src/app/main.cpp` 与原基线一致；本次主程序相关修改集中在 common 模型及共享 UI 中。该轮完整差异保存在 `S0-refresh-pre-S1.json`。当前 `S0-refresh-delta.json` 记录随后 S1 代码与资源的同步，见下节。

| 原文/字段 | 当前处理 | 证据 |
| --- | --- | --- |
| `FX 设置` | 新 key `fxhost / fxsettingswindow.window.fxSettingsTitlePlain`，无占位符 | FxSettingsWindow.cpp:352、1342 |
| `FX 设置 - %1` | 旧 key 退出活动清单；不再要求 `{name}` 或 `%1` | 同文件两处标题已替换 |
| `作用声道：` | 旧静态标签及 key 退出活动清单；All/单声道操作仍有现存调用点 | 删除记录与当前 keys/identity-uses 对照 |
| `恢复所有通道到默认颜色` | 新 key `mixerwindow.action.restoreAllChannelColors` | MixerWindow.cpp:3255 |
| `恢复默认颜色` | 新 key `mixerwindow.action.restoreDefaultColor` | MixerWindow.cpp:2498 |
| `设置通道颜色` | 新 key `mixerwindow.action.setChannelColor` | MixerWindow.cpp:2497 |
| `设置通道颜色 · RGB / HEX` | 新 key `mixerwindow.dialog.setChannelColor`；旧 `dialog.customChannelColor` 退休 | MixerWindow.cpp:2505 |
| `customColor` | 新工程布尔字段，记录到 exclusions；不翻译、不因语言变化改写 | DigitalMixerState.cpp:75、205–212 |
| `selSourceKind` | 对象属性/通道类型身份，记录到 exclusions 和直接判断证据 | FxSettingsWindow、MixerWindow、AuxSettingsWindow |
| `sel-monitor`、`control-accent-%1`、`control-button-`、声像按钮角色 | 主题作用域/Token 前缀，保留原样；中文按钮文案独立进入原有语义 key | SelThemeAccent.hpp、MixerWindow、AuxSettingsWindow |
| 颜色常量、图标和主题样式变化 | 不作为自然语言翻译项；清单按当前源码重新采集 | 新的 MixerPolarAccent/SelThemeAccent 与现存图标代码 |

所有 112 处“文件中新增原文”的出现位置和 58 处“文件中删除原文”的出现位置均已核对；另有 141 组原文的次数/调用上下文变化、901 组行号移动。112−58 不等于净增长，因为已有原文的出现次数也会变化。当前总量为 7,380，比原基线增加 161 次出现。

活动显示 key 新增 5 个、退休 3 个，总数由 1,315 变为 1,317。退休 key 已从活动目录和对应语义映射移除，旧原文仍保留在 delta 中供追溯，不能当作当前待翻译字段。用户名称、端口、工程字段和主题身份的翻译边界保持明确；恢复颜色是显式操作，语言切换不得触发该操作。

## 主程序更新轮基线与资源

260 个源码文件，分类为 translate 1,495、exclude 5,880、structured 5，pending 0。描述符 35 个字段；资源字段由 3,964 更新到 4,174，覆盖当前 832 个 registry token（此前 790）和内嵌默认主题 meta。资源逐字段采用当前实际内容，并更新 QRC/JSON/内嵌包的哈希。旧资源快照只保存了哈希和总量，不能据此声称逐 token 的新增/删除历史已经恢复；当前字段清单及包内容是接入依据。

已重新生成所有 CSV、源/资源 manifest、占位符和周边源码证据；同步风险文档中的 FxSettingsWindow/MixerWindow 行号和模型默认名位置。可见文案仍使用共享 UI/原生菜单；本阶段不扩展控件，不改变主题视觉代码。

## 下次更新流程

每轮代码变化先运行 `refresh_s0.py` 捕获旧清单与新源码差异，再审阅新增、删除、上下文变化和资源变化，维护 semantic-map/分类规则后运行 `classify_s0.py`，最后运行 `refresh_s0.py --finalize` 对账活动 key。不要在同一轮重复采集而覆盖旧基线。

`verify_s0.py` 除原有来源/域/占位符校验，还检查新增字段存在、删除字段已退出原文件、新 key 存在、退休 key 缺席、变化后的上下文与次数一致。最终结果保存在 S0-verification.json；新源码不能沿用旧 PASS。所有验证仍在前台 PowerShell 经 Tee 写入 build.log，并立即检查退出码。

## S1 实施后的当前基线

新增 I18nManager/I18nDomainLease、C ABI 公共头和示例辅助头，更新两套示例及可选导出声明；8 个源码文件变化、UI QRC 变化。新增 205 次文件内原文出现、删除 2 次，另有 34 组行号移动及 2 组上下文变化。新增字段/语言标识/设置键/API 校验 detail 已明确排除；Unknown 的显示兜底与 Channel 英文源已单列。两份新 i18n 描述符逐字段记录。

当前 264 个源码文件、7,585 次出现、5,211 个证据 ID；translate 1,501、exclude 6,077、structured 7、pending 0。描述符 43 个字符串字段，资源 4,248 个字段，直接身份比较 355 处。三套实际 locate 包的显示值和稳定元数据及其哈希纳入资源清单；活动 key 新增 12、退休 0，总计 1,329。多语言/复数形式共用一个 domain/key，目录已按稳定 key 合并。示例增益/声道的 S0 提案分别保留 Gain/Channel 的最新原文；实际运行时短 key 以语言包及 Docs/I18n-API.md 为准。

S0 验证按当前源码重新通过；S1 核心、schema、实际包与真实 DLL/UI 验证独立见 S1-completion.md。语言包译文/缺译/审校状态同步根目录 i18n翻译核对.md。

## S2 宿主接入同步

初始完整差异见 `S0-refresh-S2-initial.json`，S1 同步快照保留为 `S0-refresh-pre-S2.json`。13 个源码文件变化，3 个已记录资源变化；新增 355 次字符串出现、移除 116 次，247 组位置移动、10 组次数/上下文变化。扫描新增 HostI18n 头及实现，共 266 个源码文件、7,824 次出现。宿主语义 key 纳入真实调用点，新增/退休 key 各 80 个，唯一显示 key 仍为 1,329；未分类 0，PASS。四语言宿主包及 UI QRC/图标新增资源已进入当前资源清单，共 4,669 个字段。

## S2 最终目录选择框同步

该轮增量保留在 S0-refresh-pre-S3.json：main.cpp 变化，新增 Qt 目录接受标签的上下文和原文两处，均明确归为 Qt QM 所有，205 组行号移动。该轮 266 个源码文件、7,826 次出现，translate 1,542、exclude 6,277、structured 7、pending 0；显示 key 仍为 1,329，无新增/退休 key，源码校验 PASS。初始 S2 的 80/80 key 迁移完整历史保留在 S0-refresh-S2-initial.json。

## S3 接线盘与适配器最终同步

当前 268 个源码文件、8,111 次出现，translate 1,522、exclude 6,582、structured 7、pending 0；43 个描述符字符串字段、5,331 个资源字段、375 处身份比较，唯一显示 key 1,330。四语言 132 个 S3 key 纳入实际运行时清单，源码显示/SDK 原始身份、接口端口名、动态用户/设备/第三方内容分开记录。

初次完整 S3 差异保存为 S0-refresh-S3-initial.json：7 个源码文件、5 个资源变化，新增 360 次、移除 95 次原文，373 组位置及 33 组上下文变化；活动 key 新增 132、退休 131。分类程序曾错误给实际 S3 key 附加组件前缀，已修复，旧分类差异保留供追溯；S2 目录基线另保留在 S0-refresh-pre-S3-catalog.json。历史差异没有被后续空刷新替代。

后续长标题/捕获调整记录在 S0-refresh-S3-canvas.json，最终 Qt 输入提示刷新与 NOMINMAX 调整记录在 S0-refresh-delta.json：两文件变化、0 新增/移除原文字面种类、196 组位置移动及 1 组次数/上下文变化，重复技术域名增加一次出现；无新 key 或退休 key。最终 SHA-256、源出现次数、资源、占位符、显示与忽略清单对账 PASS。S3 验证器同时检查当前源码范围内的 155 次显示源/122 个 key 全部属于实际 S3 四语言清单。

## S4 最终同步

当前 270 个源码文件、9015 次字面出现；translate 1628、exclude 7380、structured 7、pending 0；1368 个显示 key、6896 个资源字段、43 个描述符字符串字段。S4 312 个运行时 key 覆盖其源码范围内 420 次显示出现。当前源码与资源重新校验 PASS。

历史 S3 最后差异保留为 S0-refresh-pre-S4.json；S4 初始完整差异、Sub Mix 补漏、控件与 tooltip 修正分别保存为 S0-refresh-S4-initial.json、S0-refresh-S4-submix.json、S0-refresh-S4-controls.json、S0-refresh-S4-tooltip.json；S0-refresh-delta.json 为最终普通标签绘制条件调整。S4 完成与运行时证据见 S4-completion.md 和 S4-verification.json。
