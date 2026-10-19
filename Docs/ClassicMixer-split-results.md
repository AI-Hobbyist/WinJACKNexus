# Classic Mixer 三 DLL 拆分交付

日期：2026-10-19。范围：按用户追加要求将已交付的三个布局拆成三个节点 DLL；保留现有 DSP、公共控件、主题、端口及工程预设行为。

| 型号 | Release 相对路径 | 输入配置 | i18n 域 |
| --- | --- | --- | --- |
| Classic Mixer 8 | `nodes/classic_mixer_8/mod_classic_mixer_8.dll` | 6 Mono + 1 Stereo | `classicMixer8` |
| Classic Mixer 16 | `nodes/classic_mixer_16/mod_classic_mixer_16.dll` | 8 Mono + 4 Stereo | `classicMixer16` |
| Classic Mixer 24 | `nodes/classic_mixer_24/mod_classic_mixer_24.dll` | 16 Mono + 4 Stereo | `classicMixer24` |

每型另有固定 Master、Monitor。类别均为 `Classic Mixer`。模块编译时固定布局，初始状态直接就绪，创建时无布局弹窗；准备前后均拒绝另一布局的设置。每型仍支持多个独立实例及 JACK/VIRT 两种模式。三个目标编译同一份 Module.cpp、链接相同 DSP 库并调用公共窗口；主题描述符与 Token 命名空间继续共用。i18n API 要求 DLL 身份与域独立，因此四语言资源从公共源模板生成到各 DLL 的 locate 目录，无需更改公共 i18n API。

宿主忽略旧 `mod_classic_mixer.dll` 的发现条目；旧工程的标准 `nodes/classic_mixer/mod_classic_mixer.dll` 路径按有效 layoutId 映射到相邻型号目录。恢复不覆写工程，用户保存时才持久化新路径。缺失 DLL 或不匹配布局按既有不可加载记录策略处理。没有修改端口 ID、端口名字或设置 schema，原连接与内嵌预设继续使用。

## 验证

现有 build 目录、Visual Studio 18 CMake、Release 定向构建通过。首次编译发现私有翻译回调缺少 this 捕获，修正后构建通过；日志保留在 `build/classic-mixer-split-build-first-failure.log` 与 `build/classic-mixer-split-build.log`。现存 Node/NativeFx i18n 声明的 C4273 警告仍在，本次未扩展修改其 API。

| 可执行程序 | 结果 | 覆盖 |
| --- | --- | --- |
| classic_mixer_core_tests | PASS 120 | 离线 DSP、状态、预设及固定布局 |
| classic_mixer_module_tests | PASS 154 | 三个实际 DLL、双模式声明、准备前跨布局拒绝、实例隔离、生产虚拟图、改名事务 |
| classic_mixer_gui_tests | PASS 646；6 次实窗显示 | 三型号、四语言、系统字体无缺字、主题/FPS、公共控件、推子与表头、编辑草稿和窗口生命周期 |
| classic_mixer_host_tests | PASS 78；5 次实窗显示 | 三型号发现、直接创建无弹窗、旧路径迁移、实际主图及两级子图恢复、真实模块音频缓冲、失败改名回滚与重连、Base64 预设、保存迁移路径后重开 |

测试逐项在前台 PowerShell 运行，全部输出经 Tee-Object 写日志并立即检查退出码。日志为 `build/classic-mixer-split-{core,module,gui,host}-tests.log`。GUI 使用 windows QPA、QT_SCALE_FACTOR=1 和系统字体；无 offscreen。测试窗口显示共 11 次，低于单批 100 次预算。实窗截图位于 `build/classic-mixer-split-evidence`；已检查 24 通道中文与宿主截图，中文/韩文正常，推子对齐、灰/红/黄推子与固定 Master/Monitor 正常，节点库显示三个型号。

真实 JACK server、硬件音频及现场连接未测试；宿主测试使用显式进程内 JACK 替身，证明其实际覆盖的生产加载、序列化、图和模块适配路径，不能代替真实 JACK 集成。人工审校、人工安装/部署验收已跳过（用户指令）；未验证影响为真实安装环境及设备行为。

## 增量包

`build/ClassicMixer-Release-Incremental-20261019.zip`，由 tools/package_classic_mixer.py 生成并校验 CRC/SHA-256。运行文件 21 个：三个节点 DLL、common.dll、ui.dll、mod_patchbay.dll 和每型号五个语言 JSON；另有 manifest.json、替换说明.md。旧单 DLL 和测试替身不入包。部署路径及旧文件处理见包内说明；未关闭任何用户音频进程或自动部署。

共享 DLL 保留实施前已有工作区的进程优先级改动，common.dll 是现有 ui.dll 的必要运行依赖。这些既有改动未纳入本次提交；包清单记录实际工作区源码及二进制哈希，不宣称干净提交构建。包为增量包，继续依赖现有兼容 Node v4 / Qt 6.10.3 x64 安装。
