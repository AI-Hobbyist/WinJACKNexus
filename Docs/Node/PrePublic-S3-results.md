# 公开仓库前 S3 验证记录

日期：2026-10-11。主图隔离、子图执行边界及用户补充的能力提示实现和离线自动验证完成。

主图的节点库、搜索、原生添加菜单及创建入口仅接受 JACK_NODE；子图仅接受 VIRT_NODE。工程恢复使用相同创建前能力校验。根子图持有一个 JACK 边界客户端，任意深度后代共享预分配执行图，内部模块及接口不注册 JACK。音频求和、MIDI 时间偏移和稳定排序、容量溢出丢弃最新事件、无输入静音及零延迟环拒绝均在该执行边界实现。图编辑暂停保留并恢复根边界的外部连接。

接口、节点使用稳定身份；工程格式 2 保留格式 1 读取。不可加载节点保留参数及待恢复连线，恢复不自动覆写源文件。关闭编辑器保留 DSP，删除嵌套节点只释放其子树，删除根释放对应根客户端。创建根失败移除未完成的画布节点；原生端口重命名失败恢复界面映射。控件销毁前解除编辑器语言通知，避免释放语言租约时访问销毁中的树模型。

侧栏原生 tooltip 在鼠标附近显示三项能力，四语标签及是/否随语言热切换。无自带 GUI 能力时禁用自定义 GUI 按钮及原生菜单项，不通过创建窗口探测。

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| Release 定向构建 | PASS，退出码 0 | [构建日志](PrePublic-S3-build.log) |
| SDK ABI、8 个能力夹具、27 个实际模块 | PASS，退出码 0；实际模块仍仅 JACK，双模式迁移属于 S4/S5 | [SDK 日志](PrePublic-S3-sdk.log) |
| 实际宿主与离线边界替身 | PASS，2468 项检查，退出码 0 | [实窗日志](PrePublic-S3-gui.log) |
| 主图/子图显示、搜索、菜单矩阵及直接创建拒绝 | PASS | 同上 |
| 一根三层、两根独立、三层音频/MIDI 往返、执行路径、删除、关闭重开 | PASS | 同上 |
| 实际宿主工程恢复、不可加载记录/连线保留、独立参数再次保存 | PASS | 同上 |
| 四语 tooltip、100% Windows HWND、系统字体及无 GUI 禁用状态 | PASS，截图已检查，CJK 正常 | [截图目录](PrePublic-S3-rendered/) |
| 格式及 git diff --check | PASS | 阶段提交前检查 |
| 真实 JACK、听音、硬件、人工安装验收 | 已跳过（用户指令） | 不属于离线通过结论，未验证实际服务器及硬件行为 |

最终命令均在现有 build 树、Release 配置及前台 PowerShell 执行，输出通过 `2>&1 | Tee-Object -FilePath 'build.log'`，立即检查 `$LASTEXITCODE` 并复制日志：

```powershell
& 'D:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build build --config Release --target wjn_subgraph_tests wjn_node_v4_tests --parallel 4
& './build/release/bin/wjn_node_v4_tests.exe' "$PWD/build/release/bin"
& './build/release/bin/wjn_subgraph_tests.exe' "$PWD/build/release/bin" "$PWD/build/release/bin/tests/node-v4/wjn_subgraph_jack_fixture.dll" "$PWD/Docs/Node/PrePublic-S3-rendered"
```

GUI 环境为 `QT_QPA_PLATFORM=windows`、`QT_SCALE_FACTOR=1`、`QT_FONT_DPI=96`，运行时检查真实 HWND 可见及 DPR=1，截图为稳定实窗 `QScreen::grabWindow`。计划预算 20 次，最终实际显示 12 次；包括失败定位及修复复验的保守累计不超过 90 次，未超单批 100 次上限。测试仅加载自身 JACK 替身，不连接真实服务器，清理自身夹具及窗口，保留用户运行中的应用和音频进程。

定位中发现并修复：Qt slots 宏与执行图成员名冲突、测试 tooltip 激活/鼠标事件导致失效指针、关闭编辑器时的语言租约回调，以及新建执行后端使工程接口恢复条件失效。失败结果不作为通过证据，最终日志记录完整成功复验。编译仍报告既有 Native Fx/Node i18n 声明 DLL 链接警告；本阶段未扩展到其 ABI 声明整理。

完成标准限于 S3。S4 的 MIDI Split 拼写/翻译和双模式模块迁移尚待本阶段提交推送成功后执行。
