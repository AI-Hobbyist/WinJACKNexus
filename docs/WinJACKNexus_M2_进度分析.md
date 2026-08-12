# WinJACKNexus M2 进度分析

> 分析日期：2026-08-12
> 对应提交：`f0c1d74`（基础实现）；本次 M2 收尾改动待提交
> 对照文档：[WinJACKNexus_合并计划书.md](WinJACKNexus_合并计划书.md) § M2

## 1. 结论

当前 M2 已完成 Common 层的第一批共享 UI 基础设施、主题/字体/本地化基础 API，以及 PureMixer 自绘控件的可复用视觉骨架提取。代码已经纳入 Common CMake，Adapter 已开始使用 Common 默认主题，构建和 Common 测试均通过。

按合并计划书的完整验收标准，M2 目前应标记为：

> **Common M2 基础设施和资源基础完成；跨三个 APP 的 M2 集成与完整手工验收仍未完成。**

因此当前进度不是完整的“M2 全部验收通过”，而是完成了 M2 的 Common 基础实现，为后续 Mixer/MeterBridge 应用迁移提供了接口和控件基础。

## 2. 已完成内容

### 2.1 Common 主题基础

已新增并纳入 `WinJACKNexus.Common`：

- `ThemeContext`：默认颜色 token、尺寸 token、控件样式 token，以及主题 JSON 覆盖。
- `ThemePackage`：读取普通 JSON 主题和 `.netheme` ZIP 中的 `theme.json`。
- `ThemeAssetCache`：按文件路径缓存图片，并限制图片尺寸。
- `NexusLookAndFeel`：增加主题设置入口，并支持通过 `FontManager` 提供字体。
- `.netheme` ZIP 条目路径校验，拒绝绝对路径和路径穿越条目。
- 主题包 `manifest.json` 的 schema/version 校验。
- 主题包条目数量、单文件解压大小和压缩包大小限制。
- 先加载 `Common/theme.json`，再按 manifest 的 `defaultModule` 加载模块覆盖主题。

已有默认 flat 风格的颜色和绘制路径，Common 控件不依赖具体应用窗口或业务模型。

### 2.2 字体基础

已新增 `FontManager`：

- `common:lcd-zpix` 逻辑字体 ID。
- `common:lcd-ds-digi` 逻辑字体 ID。
- 字体加载失败时返回系统字体回退。
- 适配 JUCE 9 的内存字体加载 API。

根目录 `LCD/` 中的 `zpix.ttf` 和 `DS-DIGI.TTF` 已纳入 Adapter Debug 构建输出；Common 仍通过 `FontManager::loadBuiltIns` 接收资源目录，尚未建立独立的 Common 安装包产物。

### 2.3 本地化基础

已新增：

- `TextCatalog`：语言 JSON schema 基础校验、字符串查询、模板占位符替换和 fallback catalog。
- `LocaleManager`：Common catalog 与模块 catalog 的加载及回退链。

当前已验证 `zh-CN` 测试数据能够解析和查询，但尚未建立项目级 `.lang` 资源目录，也尚未把 Adapter、Mixer、MeterBridge 的普通用户文案全部迁移到 catalog。

本次收尾已建立 `locales/zh-CN.lang` 和 `locales/Adapter/zh-CN.lang`，并复制到 Adapter Debug 输出目录。Mixer/MeterBridge 语言资源仍由各自 APP 开发阶段建立。

### 2.4 PureMixer 自绘控件提取

已提取到 Common 的控件包括：

- `MeterComponent`：电平、响度、真峰值、LRA 的基础绘制和 Peak Hold 状态。
- `ChannelCard`：通道名称、预设、重置/记录回调和多个 meter 的卡片骨架。
- `MixerChannelStripComponent`：增益、声像、峰值/RMS meter 和 fader 的基础绘制/交互。
- `SpatialPannerComponent`：5.1/7.1 声像平面、位置拖动和强度显示。

这些控件只持有绘制状态和回调，不持有 Mixer 引擎、窗口、项目模型或完整产品工作流。完整 `MixerConsoleView` 未迁移，符合 Common 不拥有应用页面的边界。

### 2.5 构建和测试

本次提交已完成：

- Common CMake 注册全部新增 M2 源文件。
- 新增 `WinJACKNexus.Common.M2UiTests`。
- `scripts\\configure.cmd`：通过。
- `scripts\\build.cmd`：通过。
- 根目录 CTest：6/6 通过。

现有 M1 五项测试全部通过，M2 测试覆盖主题 JSON、控件状态、中文目录查询、通道条和声像控件基本状态。

## 3. 按计划书逐项状态

| 计划项 | 当前状态 | 说明 |
|---|---|---|
| 1. 提取 MeterComponent | 已完成基础版 | 已进入 Common，但仍需补齐更完整的 tooltip、历史/分组等应用层配合 |
| 2. 合并 Theme/LookAndFeel/LED 规则 | 部分完成 | Common token 和 LookAndFeel 已接入；Adapter 仍大量直接使用旧 `theme::` 常量 |
| 3. ThemePackage/Context/AssetCache | 收尾基础完成 | 已支持 JSON、ZIP、manifest、Common/defaultModule 覆盖、路径校验、条目数量和压缩大小限制；JSON 深度、资源索引和完整模块选择 API 仍未完成 |
| 4. FontManager 和三个 APP 默认字体 | Common/Adapter 基础完成 | API、系统字体回退、LCD 字体资源和 Adapter Debug 输出已完成；Mixer/MeterBridge 接入由各自模块计划负责 |
| 5. ChannelCard/ChannelStrip 骨架 | 已完成基础版 | Common 控件和回调接口已建立；尚未接入 Mixer/MeterBridge 产品页面 |
| 6. 统一 flat 控件风格 | 部分完成 | 新控件使用 Common token；既有 Adapter 控件和未来 APP 控件尚未全部切换到统一 token |
| 7. 三个 APP 主题合并结果 | 未完成 | 当前只有 Adapter 初始化默认主题的接入；Mixer 和 MeterBridge 尚未建立 |
| 8. `.lang` 解析和 Common/模块覆盖 | Common/Adapter 基础完成 | 解析、查询、fallback API、项目级 Common 资源和 Adapter 覆盖文件已建立；Mixer/MeterBridge 文案迁移由各自模块计划负责 |
| 9. 语言切换/诊断/占位符校验/实时线程隔离 | 部分完成 | 加载在非实时路径；已有模板替换，但缺少占位符一致性校验、缺失键诊断、语言切换刷新和无效文件保留策略 |
| 10. 控件状态和手工验收 | 基础自动测试完成 | 已有 1 个 M2 窄测试；主题切换、无信号/静音/过载、窄窗口、字体失败、三个 APP 手工验收尚未完成 |

## 4. 当前未完成项和边界

### 4.1 尚未进入 M2 的应用模块

根据计划书，以下工作仍属于后续阶段，不应宣称已经完成：

- `modules/WinJACKNexus.Mixer` 尚未创建。
- `modules/WinJACKNexus.MeterBridge` 尚未创建。
- PureMixer 应用入口、`MixerConsoleView`、项目模型和应用级交互尚未迁移。
- Meter Bridge 应用入口、历史窗口、设置、CSV 工作流和应用级通道管理尚未拆入 MeterBridge。

这些内容分别对应计划书的 M3 和 M4，不应为了宣称 M2 完成而塞入 Common。

### 4.2 资源和文案缺口

- Mixer/MeterBridge 尚未建立各自模块级 `.lang` 资源。
- Adapter 中仍存在直接写死或直接使用旧主题常量的用户界面路径。
- 缺少普通英文文案扫描和中文完整性检查。
- 主题包尚未完成 JSON 深度、资源索引、字体覆盖和完整安全限制矩阵。

### 4.3 测试缺口

当前通过的 M2 测试属于基础 API/状态测试，尚未覆盖：

- `.netheme` manifest 和 Common/模块主题覆盖合并。
- 重复路径、超大 ZIP、超大图片、JSON 深度和资源数量限制（当前已覆盖路径、条目数量、单文件和压缩包大小）。
- zpix/DS-DIGI 实际字体加载、损坏字体和字体用途回退。
- 主题切换后的现有控件刷新。
- 语言切换、无效语言文件回退、占位符不一致和缺失键诊断。
- 无信号、静音、过载、Peak Hold、窄窗口的控件级视觉/手工验收。
- Adapter、Mixer、MeterBridge 同时运行时的主题、字体和资源隔离。

## 5. 已验证结果

```text
scripts\\configure.cmd                         PASS
scripts\\build.cmd                             PASS
WinJACKNexus.Common.AudioEngineTests            PASS
WinJACKNexus.Common.MeterEngineTests            PASS
WinJACKNexus.Common.SilenceDetectorTests        PASS
WinJACKNexus.Common.MidiAndFifoTests             PASS
WinJACKNexus.Common.M1IntegrationBoundaryTests  PASS
WinJACKNexus.Common.M2UiTests                    PASS
根目录 CTest                                      6/6 PASS
```

本次 M2 收尾新增验证：

```text
主题包 manifest/Common/defaultModule 覆盖测试       PASS
LCD/zpix.ttf 与 LCD/DS-DIGI.TTF 加载测试            PASS
Common + Adapter zh-CN.lang fallback 测试           PASS
Adapter Debug 资源复制（字体/语言文件）              PASS
```

CodeGraph 已能够索引本次新增的 `ThemeContext`、`ThemePackage`、`ThemeAssetCache`、`FontManager`、`TextCatalog`、`LocaleManager`、`MeterComponent`、`ChannelCard`、`MixerChannelStripComponent` 和 `SpatialPannerComponent`，并识别 M2 测试对控件和主题 API 的调用关系。

## 6. Git 和资源状态

- M2 基础代码提交：`f0c1d74`；本次 M2 收尾改动待提交。
- 当前分支：`main`。
- 文档生成前，`f0c1d74` 尚未推送到 `origin/main`。
- `ref/` 和 `third_party/` 保持未跟踪，未加入忽略规则，也不进入本次文档提交。
- 原有未跟踪的 `adapter_led_ref.png` 和其他文档保持不变。

## 7. 下一步建议

建议将下一阶段拆成两个明确工作包：

1. **M2 收尾第二批**：补充 JSON 深度、资源索引、字体覆盖、语言切换刷新、无效文件保留策略和 Adapter 手工验收。
2. **M3/M4 应用迁移**：分别建立 Mixer 和 MeterBridge，迁移应用入口和产品工作流，只复用 Common 控件与数据契约；各 APP 的 `.lang` 资源在对应模块计划中建立。

当前更准确的状态是“Common M2 基础设施和 Adapter 资源基础完成，跨 APP 集成与完整手工验收待完成”。
