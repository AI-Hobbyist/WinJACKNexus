# S1 核心与契约完成记录

日期：2026-10-03。状态：已完成 S1 核心与契约。

| S1 要求 | 实际产物与验证入口 |
| --- | --- |
| JSON schema 与严格验证 | schema/manifest、descriptor、catalog.schema.json；I18nManager.cpp 的严格 UTF-8/重复键/大小/身份/English 完整性/占位符校验 |
| English fallback | 每模块 requiredKeys 完整 English，逐项和缺文件回退；可信内嵌 English 损坏降级；核心 host English 编入 UI QRC |
| auto、设置、快照 | 独立 selected/effective 状态，系统偏好解析，QSettings 重启恢复，原子只读快照、GUI 线程事务切换 |
| 注册、租约与生命周期 | 同模块引用计数、域隔离、首次当前语言绑定、真实 DLL pin、双宿主一次 attach/通知、最后引用 detach 与卸载 |
| Qt/C++ API 与 C ABI | Docs/I18n-API.md、UI-API、Node-API、NativeFx-API 与公共头同步；C header 编译为真实 C |
| 两套开发示例 | ExampleNode 和 ExampleNativeFx 可选导出与独立 locate；缺项回退、实际 GainPanel 热刷新和本地 English 降级 |
| 无 JACK 验证 | Develop/I18nTests.cpp、I18nCAbi.c；i18n_contract_tests，真实模块加载/UI 渲染/卸载，不创建 JACK 客户端 |

Release 编译目标 `wjn_i18n_tests`（所需 ui/node/两个示例及依赖）通过，退出码 0；`ctest --test-dir build -C Release -R '^i18n_contract_tests$' --output-on-failure -V` 通过，退出码 0，201 次断言（包含 fixture 建立检查）。验证自动语言/设置重启、首次绑定、逐项 English 与内嵌回退、损坏切换保留旧状态、字段/版本/复数/占位符限制、C buffer/错误码/线程/域隔离、重复/双宿主租约、真实模块 detach/unload、实际 GainPanel 热刷新保留参数与离线渲染。

实际三套包的 schema/English requiredKeys 对账通过；S0 来源清单已纳入新增源码和语言包并重新 PASS，pending 0。结果见 [S1-verification.json](S1-verification.json)、[S1-build.log](S1-build.log)、[S1-test.log](S1-test.log)，翻译及缺译/审校状态见根目录 `i18n翻译核对.md`。计划书第 11.1 节同步记录本阶段。

S1 不代表所有 S0 文案已经迁移：核心 English 只覆盖当前 manifest 声明的核心项；业务窗口、正式八效果、四语言审校和部署按 S2–S7 推进。没有调用 VST 编译目标或 ALL_BUILD，没有创建备用 build，没有产出完整发行包。

Node 示例输出移到 examples/node，原 NodeTests 调用点随之更新，避免其 locate 与宿主包冲突。现有用户主题、工程及运行中音频进程不参与验证。

S2 后重新验证：使用 Windows 实窗平台（禁止 offscreen），S1 的 201 次离线检查再次通过；更新了当前源码、宿主 catalogVersion 2 四语言包及 Release 产物的 SHA-256 证据。此前日志保留在 S1-build-pre-S2.log 和 S1-test-pre-S2.log，仅作历史记录。

S3 后回归（2026-10-03）：Windows 实窗、系统字体，201 次离线合同检查再次通过，S1-test.log 保存最终 3/3 CTest 回归；此前证据保留在 S1-test-pre-S3.log/S1-build-pre-S3.log。当前宿主 catalogVersion 3 包含 host/adapter/patchbay 三域、216 个 key；两套示例及其域不变。S1-verification.json 更新当前实际语言包、源码和 Release 哈希，三套包/schema 对账 PASS。S3 构建出现公共 Node/Native Fx 同名可选生命周期声明的非致命 C4273 警告，真实 DLL 合同与调用点回归通过，未更改必需 ABI。
