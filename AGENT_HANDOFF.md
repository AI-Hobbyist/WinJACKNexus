# Agent 交接手册

更新时间：2026-10-31

## 1. 当前任务与阶段

本轮是零散的小功能和修复（没有对应的分阶段计划书）。功能代码都已完成、提交并推送，**唯一剩下的是补充用户文档和开发文档**。

## 2. Git 状态

- 分支：`main`，已与 `origin/main` 同步，最新提交 `be328ae`。
- 工作区里没有未提交的代码改动。
- 以下未跟踪内容按用户指令**不提交**，不要加入 Git：`.omc/`、`midi_tools_node_ref/`、`themes/Furina/`、`themes/org.eryth.furina.wjnskin`。

## 3. 本轮已完成的功能（均已提交推送）

| 提交 | 内容 | 主要文件 | 验证状态 |
| --- | --- | --- | --- |
| `7ce3adb` | 启动时没有检测到可用的 JACK 服务器：先按原设置尝试自动启动；仍未就绪就以模态方式弹出「JACK 服务器设置」，服务器就绪后自动关闭并继续启动；不启动服务器直接关窗口，应用退出。离线模式跳过这一步 | `src/app/main.cpp`（`JackServerSettingsDialog::requireServerReady`） | Release 编译通过；实窗验证已跳过（用户指令） |
| `55f3f7e` | 进程优先级设置：全局设置里新增「进程优先级」（低～实时），并应用到 jackd 和 VST 扫描器子进程；新增 `wjn_main_session_probe` 工具；四语本地化；补充了 `Docs/UI-API.md`；更新 theme template 样式 | `src/common/.../ProcessPriority.*`、`ApplicationSettings.*`、`main.cpp`、`locate/*.json` | WinJACKNexus 目标编译通过；VST 扫描器和 probe 目标没有单独编译；未实测 |
| `54fdc07` | 修复：通道详情（SEL）窗口关闭后再打开时，标题栏背景和类型图标丢失（原因是标题栏颜色缓存没有随窗口销毁一起清空） | `src/ui/src/MixerWindow.cpp` | 编译通过；未实测 |
| `e926ec8` / `8e5a6da` / `4006338` | 通道总览和通道详情窗口在任务栏里有独立的缩略图；点调音台缩略图能把调音台提到最前面（显示前设置 `WS_EX_APPWINDOW`，显示后清除原生 owner；QObject 父对象保留，所以 `findChild` 和窗口生命周期不变） | `MixerWindow.cpp`（`showInTaskbar` / `detachFromOwnerWindow`） | **用户实测正常** |
| `be328ae` | 在通道详情里切换通道（包括 Monitor）时，总览高亮和主调音台的滚动位置会同步跟随 | `MixerWindow.cpp`（`focusChannel`） | **用户实测正常** |

行为变化提醒：总览和详情窗口不再从属于调音台，所以最小化调音台时它们**不会**再跟着一起最小化。

## 4. 增量包

都在 `build/release/` 下。最新的 `WinJACKNexus-Release-Overlay-SelLink-20261031-153037.zip` 里的 `ui.dll` 包含本轮所有界面改动。`WinJACKNexus.exe` 的改动在 `WinJACKNexus-Release-Overlay-JackStartup-20261031-144848.zip` 里。两个都是增量包，不是完整包。

## 5. 未完成：文档（下一个 Agent 的任务）

按建议顺序：

1. **README 四语**（`README.md`、`README.en.md`、`README.ja.md`、`README.ko.md`）：在「下载与运行」一节补充新的启动行为。没有就绪的 JACK 服务器时会先弹出 JACK 服务器设置，服务器启动成功后才会继续；直接关闭窗口会退出应用。
2. **README 或用户文档**：补充「全局设置 → 进程优先级」的说明，包括作用范围（应用本身和 jackd/VST 扫描器子进程）以及选择「实时」可能带来的风险。说明前先读一下 `src/common/src/ProcessPriority.cpp` 和 `ApplicationSettings.cpp`，确认实际行为。
3. **调音台相关文档**：说明总览和通道详情窗口有独立的任务栏缩略图、可以单独前置，不会随调音台一起最小化；以及在通道详情里切换通道时会联动总览和主调音台。
4. **`Docs/UI-API.md`**：`55f3f7e` 已经补了 18 行，核对是否完整覆盖了 `ApplicationSettings` 新增的进程优先级接口。这份文档是冻结的 API 合同，只能如实描述现有接口。
5. 视需要更新 `Docs/主会话音频设备开发计划书.md` 等计划书里有关启动流程的描述。只在内容确实不一致时才改。

## 6. 必须遵守的约束

- 只写文档，不改代码。发现代码问题就记为后续事项，不要扩大范围。
- 文档只描述已经验证或已经实现的行为。表里标着「未实测」的功能，文档里不要写成已验证。
- 四语 README 的内容要保持一致。
- 如果后面需要构建：只用现有 `build` 目录，只编 Release，用 VS18 自带的 CMake，在前台运行并通过 `2>&1 | Tee-Object -FilePath build.log` 记录完整输出，然后检查 `$LASTEXITCODE`（`build.log` 是 UTF-16 编码）。不要使用 offscreen 模式。
- 提交和推送需要用户批准。
