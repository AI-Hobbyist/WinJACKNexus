# S0 显示与身份风险

记录日期：2026-10-03。下列是当前磁盘代码的证据；S0 只记录，业务修复安排在对应实施阶段。行号随后续修改可能变化，以符号和原文共同定位。

| 位置 | 当前判断/用途 | 后续要求 |
| --- | --- | --- |
| `src/patchbay/mod/src/Library.cpp:632`，`addVstPluginItems` | `module.name == "VST Plugin"` 找宿主 | 用模块身份和节点 index 查找，翻译显示名称不能改变选择 |
| 同文件 852，`LoadedModuleNode::hasCustomGui` | `category_ == "Digital Mixer"` | 分离类别稳定 ID 与显示值，优先使用 GUI 能力/模块身份 |
| 同文件 1427、2244、2252、2326、2354 | `category() ==/!= "Meter"` 控制托盘入口、菜单和独立界面 | 存储稳定节点类别/能力，显示翻译不能决定行为 |
| `src/ui/src/TrayController.cpp:312,314,790` | 比较 `Meter` / `Digital Mixer` | `TrayNodeGuiEntry` 需要独立身份，修改公开行为须同步 UI 契约与调用点验证 |
| 同文件 492–500 | 比较 `Windows Native Driver` 和 WASAPI/System MIDI 节点名称 | 稳定 adapter 类型 ID 与显示名分离，不能对译文做比较 |
| `src/patchbay/mod/src/Library.cpp:671–704` | 用分类显示值分组并保存展开状态 | 分类稳定 ID 保存展开状态；重译后保留选择、搜索和展开 |
| `src/ui/src/FxSettingsWindow.cpp:403,405,578,732,734` | `All` 决定 -1/单声道操作 | 用稳定 -1/声道 index；All 是应翻译的操作文本 |
| `src/ui/src/MixerWindow.cpp:344` 等 | 多处 `number == "MAIN"` 决定主输出功能、SIP 静音和布局 | MAIN 保持原样；后续尽可能以 strip kind 判断，不能让编号显示成为可翻译身份 |
| 同文件 2296 | 用窗口标题比较决定 AUX 窗口更新 | 改为当前通道 ID/状态；标题只作为显示结果 |
| 同文件 2524、2585–2602 | `tabs[i]` 同时作为按钮文案与 `selTab%1` objectName 后缀，点击回调按英文后缀 findChild | 页签稳定 ID/enum 与译文分离；保留现有自动化可用的对象身份，不能将翻译后的 Main/Sub Mix 拼入对象名 |
| `src/patchbay/mod/src/PatchbayCanvas.cpp:834` | `displayName == "Sub Graph"` 判定默认子图名 | 默认名状态与用户名分开；已有工程名称保持原样 |
| `src/meter/mod/src/MeterSettingsWidget.cpp:212` | `findText(channel.speakerRole)` | speaker role 是技术身份；保持声道标记，若有说明则 itemData 单独存身份 |

## 已有稳定身份，须保留

`isDigitalMixerModule`（Library.cpp:1289）按 DLL 文件名和节点 index 判断；节点树 UserRole 存 modulePath/index（697–699）；VST scanner 的类型和分类过滤通过 itemData（MainWindow.cpp:212–220、373–375）。这些身份不翻译，只翻译显示角色。

`audio_in/audio_out/midi_in/midi_out`、`vst2/vst3`、`effect/instrument`、`main`、`system`、工程 format、theme token 和预设 ID 是协议/持久化标识。不能因它们有英文词形而进入翻译替换。

## 语义待核对

Gate `src/native_fx/effects/gate/src/Ui.cpp:18` 显示“滞后 (Hold)”，但 `Dsp.cpp:74–77` 用 `thresholdDb - holdDb` 作为关闭阈值，实际是 dB 滞回宽度，不是保持时间。建议语义 key `control.hysteresis`，英文 Hysteresis；保留工程字段 `holdDb`。

Limiter 的 `lookaheadDb` 在 `Ui.cpp:355` 显示为 dB；当前 `Dsp.cpp` 没有读取该字段，不能宣称实现了时间前视。这是翻译前需明确的功能文案问题：S0 记录现状，后续不能仅把“前瞻”翻译成四语言并称语义验收通过；是否改功能或标记未实现须在 S5 处理，工程字段保持不变。

`DigitalMixerProcessor.cpp:516–517` 证实 PFL 取 gainTap（输入增益后、FX/Mute/推子前），AFL 取 post。术语和帮助必须体现该区别。

## 完整证据与处理边界

`S0-identity-uses.csv` 保存全扫描范围内直接字符串比较、compare/startsWith/endsWith/indexOf/findText 的调用行，包括机器身份和已识别的显示词分支。`classification=translate` 的项必须先拆身份，再接入对应显示 key。动态的 `category_`、展开状态及 tabs/objectName 链路由本文件补充，不能只依靠直接比较正则。

本阶段没有修改这些分支。S0 完成表示已列出需迁移的身份依赖，不表示稳定身份分离已经实现。
