# I18n API v1（S1）

共享实现为 `ui.dll` 的 `WinJACKNexus::UI::I18nManager`，公开头为 `src/ui/include/WinJACKNexus/UI/I18nManager.hpp`。C ABI 头为 `src/common/include/WinJACKNexus/Common/I18nApi.h`。本契约独立于 Node v2、Native Fx v1 和主题 API；不修改这些必需 ABI 的结构布局。

单独编译社区模块时同时提供 Node/Native Fx 与 common 的 include 根；仓库 CMake 的 Node/各 Native Fx 目标已导出所需 include 路径。纯 C 模块只需公共 I18nApi.h，无 Qt/STL 依赖。

## 管理器与快照

`I18nManager(I18nOptions options = {}, QObject* parent = nullptr)` 在 GUI 线程构造，在创建界面前读取 `WinJACKNexus/Application` 的 `i18n/language`。`settingsFile` 非空时使用隔离 INI 文件；`systemUiLanguages` 非空时替代系统偏好，供离线验证。默认选择 `auto`，无匹配回退 `en-US`；非法已保存值恢复 auto 并记录诊断。规范化语言主标签的大小写、连字符和下划线，中文各地区/文字系统映射 zh-CN，英语各地区 en-US，日语 ja-JP，韩语 ko-KR，按偏好顺序匹配。

`install(error)` 设置宿主唯一已安装实例，另一存活实例安装失败；`installed()` 返回借用指针。管理器必须比界面和租约活得更久。测试可以构造未安装的独立管理器，但同一个实际 DLL 不得同时接到不同宿主。

| API | 合同 |
| --- | --- |
| `selectedLanguage()` / `effectiveLanguage()` / `revision()` | 返回当前只读快照的选择、有效语言和版本。auto 不固化成某一语言 |
| `supportedLanguages()` / `resolveAutomatic(preferences)` | 四语言标识与确定性的自动解析 |
| `setLanguage(selection, error = nullptr)` | 支持 auto 或四个精确标识；在 GUI 线程完整验证所有存活模块的新包，再保存选择并发布同一快照。缺目标文件/条目回退同模块 English；损坏目标文件拒绝切换，旧快照和选择保留。重复选择 auto 会重新解析系统偏好 |
| `registerModule(descriptor, absoluteRoot, embeddedEnglish = {}, error = nullptr)` | 验证描述符和 locate manifest，完整 English 必需；可信内嵌 English 可在外部损坏/缺失时回退。外部与内嵌有效 English 不一致时使用内嵌基线。无有效 English 拒绝注册 |
| `registerLibrary(absoluteDllPath, Extension::node/nativeFx, index = 0, error = nullptr)` | 规范化绝对 DLL 路径并从 DLL 所在目录读取 locate；动态探测版本/描述符与生命周期导出。旧模块返回空租约并说明未接入，宿主继续原 API，不宣称已国际化 |
| `text(domain, key, englishFallback, arguments = {}, count = -1)` | 查询只读内存快照，无文件 I/O；当前语言→同域 English→调用点 English。均缺失时发行模式 Unknown，开发选项 showMissingKeys 显示 domain/key。缺失诊断去重 |
| `bind(context, apply)` | context 与管理器同 GUI 线程；建立连接后立即 apply，以后每次 languageChanged 重译；context 销毁自动断开。回调仅改显示，不覆盖正在输入的名称、数字或选择 |
| `format(source, arguments, count, result, error = nullptr)` | 命名参数格式化，失败返回 false，具体规则如下 |
| `languageChanged(effectiveLanguage, revision)` / `diagnostic(code, detail)` | 发布后的 GUI 通知；稳定英文诊断码与明细。通知期间禁止重入注册/切换；释放租约排队到通知完成 |

注册同 moduleId、同根、同目录合同的模块增加引用计数，重复域归属不同模块时失败。`displayNameKey` 可按节点类型不同，其余描述符必须一致。`I18nDomainLease` 仅可移动，析构/reset 释放引用；最后一个引用移除域。DLL 额外持有独立 Windows loader 引用，双宿主只 attach/通知一次；先销毁模块界面与回调，再释放全部租约，detach 后释放 DLL。非 GUI 线程释放会排队，卸载必须等待 GUI 清理；音频回调禁止调用服务或释放租约。

## 数据合同

规范 schema 位于 `Docs/i18n/schema`，运行时同时验证语义约束。UTF-8 JSON 顶层对象最大 2 MiB、嵌套最多 64 层，拒绝重复键（包括转义后的同名键）、非法 UTF-8、NUL、空译文和未知字段。标识为 `[A-Za-z][A-Za-z0-9_.-]{0,255}`。示例在 `locate/`、`Develop/Node/locate/`、`Develop/NativeFx/locate/`。

- 描述符：descriptorVersion=1、i18nApiVersion=1、moduleId、非空不重复 domains、locatePath="locate"、正整数 catalogVersion；可选 displayNameKey 为 `{domain,key}`，必须在 requiredKeys 中。
- manifest：formatVersion=1、moduleId、domains、defaultLanguage="en-US"、catalogVersion、languages、requiredKeys。languages 的文件名只能是对应语言的 `语言标识.json`，English 必有；requiredKeys 对每个域给出完整、不重复的 key 数组。
- catalog：formatVersion=1、catalogVersion、moduleId、language、domains。English 必须精确覆盖 manifest 所有 requiredKeys；目标语言可以缺项，但不得新增未知域/key。版本和模块身份必须一致。采用扁平语义 key，不用中文原文做身份。
- 文件读取使用 canonical path，并限制在所属根内；拒绝绝对/越界路径和解析到根外的链接。模块仅拥有自己的域，C 服务强制域隔离。Qt/C++ 宿主拥有全局读权限。
- 单条可以是非空字符串或复数对象。English 复数必须有 one/other，其他三语言仅 other；同条各形式与各语言的占位符集合必须一致，类型也须一致。count=1 选择 English one，其他值选择 other，count=-1 不提供自动 count 参数。

`{name}` 参数名符合 `[A-Za-z_][A-Za-z0-9_]*`；`{{`/`}}` 为字面花括号。重复参数替换多次，参数内容不递归解释。count≥0 注入 `{count}` 并覆盖同名传入参数；其他参数为 string/bool/number，缺必需参数或非法模板失败。多余参数允许。输出不隐式 HTML 转义，富文本调用点须先 `toHtmlEscaped()`；数值使用 QVariant 的稳定表示，不更改既有数字输入 locale。S0 中旧 `%1` 模板需要在迁移调用点显式转换为命名参数，核心不把任意 `%` 文本当占位符。

## C ABI v1

可选导出精确签名在公共头中：

```c
uint32_t wjn_get_node_i18n_api_version(void);
const char* wjn_get_node_i18n_descriptor_json(uint32_t index);
uint32_t wjn_fx_i18n_api_version(void);
const char* wjn_fx_i18n_descriptor_json(uint32_t index);
int32_t wjn_i18n_attach_host(const WjnI18nHostV1* host);
void wjn_i18n_language_changed(const char* effectiveLanguage, uint64_t revision);
void wjn_i18n_detach_host(void);
```

Node 与 FX 的版本/描述符成对出现；有生命周期能力时 attach/change/detach 三者齐全，否则拒绝扩展。纯显示元数据模块可以不导出三者。非法 index 返回 NULL。描述符 DLL 所有，宿主立即复制，最大 2 MiB。一个 DLL 的多个类型和双宿主共享同一 attach 状态，通知在首次绑定和每次版本变化时发出。

`WjnI18nHostV1` 字段依次为 uint32 structSize、uint32 apiVersion、void* context、WjnI18nTextV1 text；默认平台结构对齐、cdecl。模块检查 structSize≥sizeof、apiVersion=1、text 非空，attach 成功返回 WJN_I18N_OK。服务表只在 attach 到 detach 间有效，模块不得缓存 detach 后的表/函数 context，也不得在 DllMain 或 DSP 回调调用。

text 的参数依次为 context、UTF-8 domain/key/englishFallback/argumentsJson、int64 count、char* buffer、uint32 capacity、uint32* requiredBytes。所有输入借用至调用结束；domain/key 最大 256 字节，fallback 最大 32768 字节，args 最大 64 KiB JSON 对象，值仅 string/number/bool。count≥-1。requiredBytes 必填，含末尾 NUL；NULL buffer+0 查询长度返回 OK，过小缓冲区返回 BUFFER_TOO_SMALL 且不写入，足够时完整写入 UTF-8 和 NUL。失败时 requiredBytes 初始为 0，缓冲区过小时返回所需大小。状态值为 OK=0、INVALID_ARGUMENT=1、BUFFER_TOO_SMALL=2、FORBIDDEN_DOMAIN=3、WRONG_THREAD=4、INTERNAL_ERROR=5；异常不能跨 DLL。

服务与 attach/change/detach 在宿主 GUI 线程调用。不依赖 Qt 的 HWND 模块可用同一 ABI 查询文字；独立 UI 线程自行投递已复制文字的刷新消息，不能从工作线程直接调用 host text。无宿主时使用模块本地 English，保持界面可用。示例辅助头演示两次 buffer 调用、本地 English 降级和 QObject 存活界面刷新；它不是供业务模块共享的公开 API。

## S1 与后续边界

S1 提供核心、schema、两套真实 DLL 示例和无 JACK 验证。`locate/en-US.json` 是当前核心宿主目录的完整 English 基线并编入 UI 资源，当前 requiredKeys 仅为核心项；S2–S6 必须随着实际调用迁移扩展 manifest 和四语言包，不能把示例包或核心条目的完整性称为全 S0 四语言完成。Qt QM 安装、实际应用启动与托盘接入由 S2 实施；八个正式 Native Fx 的迁移由 S5 实施；部署覆盖包由 S7 验收。

Qt 行为依据：[QLibrary](https://doc.qt.io/qt-6/qlibrary.html)、[QStringDecoder](https://doc.qt.io/qt-6/qstringdecoder.html)。实际编译版本以 Qt 6.10.3 SDK 为准。

## S2 宿主扩展合同（S2 Release 与实窗验证通过）

`HostI18n` 在 QApplication 创建后、任何宿主窗口或首条启动消息前初始化。`initialize(absoluteExecutableDirectory, error)` 安装唯一管理器，并使用编入 ui.dll 的 manifest 与完整 English 注册宿主；外部 manifest 缺失、损坏或与编译基线不一致时使用可信副本并记录诊断。`registerBundledModule(descriptor, root, embeddedManifest, embeddedEnglish, error)` 是此可信内置入口；普通社区 `registerModule` 仍必须提供有效外部 manifest。

`HostI18n::manager()` 提供当前管理器，`installed()` 返回当前宿主会话；`createLanguageMenu(parent)` 返回 Qt 原生五项互斥菜单，action data 为 auto/zh-CN/en-US/ja-JP/ko-KR。`bindLanguageChoice(choice)` 接入同一五态设置模型，首次立即同步；失败恢复原选择并发出 `languageSelectionFailed(detail)`。显示语言名称使用自称，自动项包含当前有效语言。语言切换不调用主题初始化。

`hostText(key, english, arguments)` 查询 host 域，没有已安装管理器时格式化调用点 English。`bindHostText(object, property, key, english)` 首次立即设置显示属性，随后按语言修订更新；对象销毁断开绑定。调用方只绑定显示属性，不能绑定数字 value、编辑中的 text、业务 ID 或用户内容。

Qt 标准文本由当前宿主会话管理 QTranslator；中文、日语、韩语读取 locate/qt 的 qtbase QM，缺失时使用 ui.dll 内同一 Qt 6.10.3 QM。English 移除翻译器。会话销毁移除翻译器。语言包本身不改数字输入 locale。

## S3 宿主域与节点接入

宿主 catalogVersion 为 3，包含 host 84、adapter 22、patchbay 110 个 key，总计 216 个；四语言外部包、编入 ui.dll 的 manifest/English 与实际部署保持一致。新增域不改变 descriptorVersion、i18nApiVersion 或 C ABI。通用域显示查询与 QObject 绑定使用 `LocalizedText.hpp`，精确显示绑定合同及托盘显示映射重载见 `Docs/UI-API.md`。

接线盘节点扫描和实际实例加载都使用 `registerLibrary(path, Extension::node, index)`。规范化 DLL 路径与 index 共享弱缓存的租约，扫描树和存活实例各保持强引用；注册在 createNode/init 前完成。工程直接加载扫描目录外 DLL 时同样读取已验证的 displayNameKey。先清除实例 UI、destroyWindow/shutdown/destroyNode，再释放主题与语言租约，最后 FreeLibrary。旧模块缺少可选导出时继续使用原始 metadata/UI；未知分类、第三方插件名称与用户内容不由宿主猜译。

显示角色与模块/JACK/工程身份分离。内置适配器与五种电平探针在节点库中使用独立显示映射，原 SDK 名称/分类及实例 ID 保留；技术端口默认名称保持原样。节点库热切换更新已有 item 与 tooltip，保持选择/搜索/展开/滚动，不重新扫描和创建音频客户端。Windows 实窗调用点验证见 `Develop/PatchbayI18nTests.cpp`，离线模型与实际模块加载不证明 JACK 连接集成。
