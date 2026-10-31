# Agent 交接手册

更新时间：2026-10-31 16:30

## 1. 当前任务与阶段

本轮只做文档，已完成：

1. 四语 README 补充：启动前 JACK 检查、进程优先级、调音台窗口行为（主仓库 `bf6d5fb`）。
2. Wiki 建立并扩展：安装流程、全部模块使用方法、主题自定义、术语表与常见问题。

代码未改动，上一轮功能状态不变。后续事项见第 6 节。

## 2. Git 状态

### 主仓库

- 分支 `main`，已与 `origin/main` 同步；本手册提交前的最新功能/文档提交为 `bf6d5fb`。
- 工作区没有未提交的代码改动。
- 以下未跟踪内容按用户指令**不提交**：`.omc/`、`midi_tools_node_ref/`、`themes/Furina/`、`themes/org.eryth.furina.wjnskin`。

### Wiki 仓库

- 独立仓库：`https://github.com/AI-Hobbyist/WinJACKNexus.wiki.git`（`master`）。
- 本地克隆：`D:\Temp\wjn-wiki-probe`，远端最新 `24d0c20`，工作区干净。若该临时目录已被清理，重新 clone 上面的地址即可。
- 共 18 个内容页面，首页、侧边栏和页脚已接好导航。
- 主要提交：`0884a19` 骨架、`ab090ca` / `8459a27` 安装流程、`89000df` 起各模块页面、`2ae6440` 术语表与导航、`8e984dc` 第一个信号链、`b743896` / `24d0c20` 主题自定义。

## 3. 本轮文档完成内容

### 3.1 四语 README（`bf6d5fb`）

`README.md`、`README.en.md`、`README.ja.md`、`README.ko.md` 同步补充：

- 启动时检查 JACK 服务器；无服务器时按自动启动设置尝试，仍未就绪则显示模态「JACK 服务器设置」，就绪后继续；直接关窗退出。
- 「全局设置 → Windows 进程优先级」的六档、作用范围（应用自身和之后启动的 jackd、VST 宿主进程、VST 扫描器）以及实时档的风险。
- 数字调音台总览/详情窗口的独立任务栏缩略图、不随主调音台最小化，以及切换通道联动。

### 3.2 Wiki 页面

| 页面 | 内容 |
| --- | --- |
| 首页 / 安装与首次启动 | 三步安装、JACK/VAC 说明、首次启动与进程优先级 |
| 接线盘 | 节点、连线、子图、工程与自动保存 |
| 设备节点 | WASAPI/MIDI 节点、设备配置、VAC 同名线规则 |
| 数字调音台 / Classic Mixer | 通道、SEL、AUX、Sub Mix、FX、监听、预设 |
| Native Fx | 8 个效果、节点/调音台两种用法、All 声道规则 |
| 电平桥 | 探针、响度指标、历史图表、CSV |
| VST 宿主 | 扫描、加载、沙盒、端口、预设 |
| MIDI Tools | 通道拆分、矩阵、SF2/SFZ |
| Control Deck | 开关、音效、峰值表、增益，布局/锁定/穿透 |
| 主题与自定义 / 制作主题包 | 主题切换、颜色/字体/背景、导出、包结构与校验 |
| 术语表 / 常见问题 / 开发 | 名词、FAQ、构建与接口入口 |

### 3.3 安装流程

整合包计划包含三个文件，Wiki 已按此编写：

`1_安装JACK.exe` → `2_安装VAC.exe` → `3_解压WinJACKNexus.7z`

注意：

- 整合包**尚未打包**，Wiki 安装页明确标注「准备中」；发布前仍按 README 的现有方式安装。
- VAC 同一条线的播放端点和录音端点内部连通，同一条线只能用于录制或播放其中之一。例如系统播放使用 `Line 1` 时，WASAPI 输入应选择 `Line 2`。
- 三文件清单里没有 VC++ 运行库安装程序，而应用依赖它；干净系统可能缺少 `VCRUNTIME140.dll` / `MSVCP140.dll`。

## 4. 历史功能（代码未变）

| 提交 | 内容 | 验证状态 |
| --- | --- | --- |
| `7ce3adb` | 启动时没有检测到可用的 JACK 服务器：先按原设置尝试自动启动；仍未就绪就以模态方式弹出「JACK 服务器设置」，服务器就绪后自动关闭并继续启动；不启动服务器直接关窗口，应用退出。离线模式跳过这一步 | Release 编译通过；实窗验证已跳过（用户指令） |
| `55f3f7e` | 进程优先级设置：全局设置里新增「进程优先级」（低～实时），并应用到 jackd 和 VST 扫描器子进程；新增 `wjn_main_session_probe` 工具；四语本地化；补充了 `Docs/UI-API.md`；更新 theme template 样式 | WinJACKNexus 目标编译通过；VST 扫描器和 probe 目标没有单独编译；未实测 |
| `54fdc07` | 修复：通道详情（SEL）窗口关闭后再打开时，标题栏背景和类型图标丢失（原因是标题栏颜色缓存没有随窗口销毁一起清空） | 编译通过；未实测 |
| `e926ec8` / `8e5a6da` / `4006338` | 通道总览和通道详情窗口在任务栏里有独立的缩略图；点调音台缩略图能把调音台提到最前面（显示前设置 `WS_EX_APPWINDOW`，显示后清除原生 owner；QObject 父对象保留，所以 `findChild` 和窗口生命周期不变） | **用户实测正常** |
| `be328ae` | 在通道详情里切换通道（包括 Monitor）时，总览高亮和主调音台的滚动位置会同步跟随 | **用户实测正常** |

行为变化提醒：总览和详情窗口不再从属于调音台，所以最小化调音台时它们**不会**再跟着一起最小化。

## 5. 增量包

都在 `build/release/` 下。最新的 `WinJACKNexus-Release-Overlay-SelLink-20261031-153037.zip` 里的 `ui.dll` 包含上一轮所有界面改动。`WinJACKNexus.exe` 的改动在 `WinJACKNexus-Release-Overlay-JackStartup-20261031-144848.zip` 里。两个都是增量包，不是完整包。本轮文档工作没有产生新包。

## 6. 后续事项

1. **整合包**：打包完成后更新 Wiki 安装页——去掉「准备中」、补下载入口、核对三个文件名；同时确认是否需要增加 VC++ 运行库步骤，或把安装程序放进 `3_解压WinJACKNexus.7z`。
2. **VAC 再分发**：把 VAC 安装程序放进公开整合包前，确认其许可与再分发条款。
3. **Wiki 完善**：目前只有简体中文，没有截图或录屏；`Development` 页的构建环境与测试流程仍是「待补充」；可补主题 Token 参考页。
4. **旧文档核对（未做）**：`Docs/UI-API.md` 是否完整覆盖进程优先级接口；`Docs/` 下的调音台文档；计划书中的启动流程描述。用户把本轮文档范围限定为主 README 与 Wiki，这些未执行。

## 7. 必须遵守的约束

- 只写文档，不改代码。发现代码问题就记为后续事项，不要扩大范围。
- 文档只描述已经验证或已经实现的行为；未实测的功能不要写成已验证。
- 四语 README 的内容要保持一致。
- Wiki 修改在 `D:\Temp\wjn-wiki-probe` 进行，提交并推送 wiki 的 `master`；页面用简体中文，链接到主仓库 Docs/源码时先确认路径存在。
- 如果后面需要构建：只用现有 `build` 目录，只编 Release，用 VS18 自带的 CMake，在前台运行并通过 `2>&1 | Tee-Object -FilePath build.log` 记录完整输出，然后检查 `$LASTEXITCODE`（`build.log` 是 UTF-16 编码）。不要使用 offscreen 模式。
- 提交和推送需要用户批准。

## 8. 2026-10-31 17:05 追加：signal_switch 并入 control_deck（未提交）

- 源码 `src/signal_switch/` → `src/control_deck/`；命名空间 `SignalSwitch` → `ControlDeck`（含 `Common::ControlDeck`、`ControlDeckProcessor.hpp`）；UI 文件 `ControlDeck{Editor,Layout,Overlay}.cpp`；CMake 目标 `control_deck_state`、测试 `wjn_control_deck_*_tests`；对象名/属性 `controlDeck*`；打包脚本 `tools/package_control_deck.ps1`。
- 主面板 `signalSwitch` 域并入主语言目录 `locate/`，不再有 `signal_switch/locate`；BASS 插件目录改为 `control_deck/bass_plugins/`，仍兼容读取旧的 `signal_switch/bass_plugins/`。
- **刻意保留**（持久化或 API 合同）：节点 DLL 名 `wjn_signal_switch_<kind>`、`moduleId` `winjacknexus-signal-switch*`、i18n 域 `signalSwitch` / `signalSwitch.<kind>`、主题命名空间 `signal-switch` 及其 Token、工程字段 `signalSwitchStateBase64`、旧路径 `nodes/signal_switch/` 的映射。
- 发布目录 `D:\UserData\Desktop\WJN_Release\WinJACKNexus` 的 `signal_switch/locate` 已合并到它的 `locate/` 并删除；原文件备份在 `D:\Temp\wjn-merge\backup`。该目录里仍是旧版二进制，主面板会回退成英文，直到换成新包。
- 验证：VST2=ON 的 Release `ALL_BUILD`（clean-first）通过；`host_i18n_tests`、`i18n_contract_tests`、`control_deck_state/module/final` 通过（实窗，弹窗 17 次）。`patchbay_i18n_tests` 失败于「four audio/MIDI interface types exist」：该测试在 HEAD 上就编译不过（缺 `Service.hpp` 的包含路径），这次只补了链接依赖，断言失败记为后续事项。
- 完整包：`build/WinJACKNexus-Release-x64-VST2-Full-20261031-170354.zip`（含 `.sha256`；打包脚本 `D:\Temp\wjn-merge\package-full-vst2.ps1`）。没有附带 JACK2 安装程序。
