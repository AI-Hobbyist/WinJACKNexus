# Classic Mixer S4 结果与交付

日期：2026-10-18。S3 检查点 b211a67，文档换行修正 bd64644，均已推送。

## 完成与证据

- 现有 build 内 Release 相关目标成功构建；没有全量 ALL_BUILD、没有替代构建树。日志 build/classic-mixer-s4-build.log；首次新增目标需重新配置，日志 build/classic-mixer-s4-configure.log。已有 Node/Native Fx 重复 i18n 声明 C4273 警告保留，未扩展范围处理。
- 核心：PASS 120，build/classic-mixer-s4-classic_mixer_core_tests.log。
- 真实 DLL/生产 VirtualGraph/改名事务：PASS 134，build/classic-mixer-s4-classic_mixer_module_tests.log。
- GUI：PASS 630 Windows 100% GUI checks; window shows=6，build/classic-mixer-s4-classic_mixer_gui_tests.log。计数包含字形检查，并非 630 个独立业务场景。
- 实际宿主：PASS 61 offline host integration checks; window shows=6，build/classic-mixer-s4-host-tests.log。GUI 均为 Windows 可见实窗、DPR=1、系统字体；禁止 offscreen，没有运行其他缩放。
- 宿主检查使用真实 LoadedModuleNode、PatchbayNode、ProjectStateCodec 和独立 Classic Mixer DLL；JACK 接缝显式加载测试 DLL，未连接/启动真实服务。覆盖首次布局选择、三布局保存恢复、多实例、主图 JACK 适配样本、两级子图 DSP、嵌入 Base64 预设、Stereo 改名第二端口失败回滚及连接保留、子图侧栏改名与稳定身份、端口改名后路由、实际保存动作、销毁全部实例后重开、恢复不覆写源文件，以及所有测试客户端/端口清理。
- 为满足子图名称/端口标签同步，增加可选模块修订入口 wjn_classic_mixer_state_revision，宿主只对本模块使用；避免逐帧序列化内嵌预设。原 Node v4 ABI 与 Digital Mixer 路径保持。
- 实窗核对发现通用侧栏 Split 开关违背“只允许节点改名”，已仅对 Classic Mixer 隐藏，并由实际宿主检查确认。
- 截图：build/classic-mixer-s3-evidence 下三布局四语言窗口，以及 build/classic-mixer-s4-evidence/restored-host.png。自动截图/检查已完成，CJK 字形正常；人工审校另行跳过。

## 增量包

`build/ClassicMixer-Release-Incremental-20261018.zip`；SHA-256 文件为同名 `.zip.sha256`。打包命令为 `python tools/package_classic_mixer.py`，前台通过 Tee-Object 写 build.log 并立即检查退出码。脚本检查 x64 PE、JSON、ZIP CRC、归档条目集合及每个运行文件 SHA-256；日志 build/classic-mixer-package.log。最终包 manifest 在阶段提交推送后更新 sourceHead，运行文件哈希保持一致。

9 个运行文件：common.dll、ui.dll、mod_patchbay.dll、nodes/classic_mixer/mod_classic_mixer.dll，以及本模块 locate/manifest.json 和 en-US/zh-CN/ja-JP/ko-KR.json。包内另有 manifest.json、替换说明.md。没有测试替身、测试程序、工程、截图、其他效果/插件或完整 Qt 运行库。

适用基线：已有 Node v4 / Qt 6.10.3 x64 安装。未全量重编，此包不是完整包。只覆盖本任务必需文件；不修改工程或宿主主题/语言目录。common.dll 必须一并替换，因为现有工作区 ui.dll 已引用进程优先级 API，已通过 dumpbin 导入表核对。共享运行库保留实施前已有的相关脏工作区代码，这些已有修改未纳入 Classic Mixer 提交；包内记录实际源码哈希，不能将二进制描述为干净 HEAD 构建。

人工审校、人工安装/部署、真实硬件音频验收：已跳过（用户指令）。未验证影响：没有证明人工安装体验或设备兼容性；真实 JACK server 客户端/端口连接、断连恢复和重连行为没有现场证据。离线替身通过只证明对应接缝和处理逻辑，不能替代实际 JACK 集成。应用和音频进程未被关闭/中断。

## 最终范围核对

三布局精确数量、可复用双模式多实例、独立窗口全公共控件、灰/红/黄主题角色、通道分隔与推子对齐、固定 Master/Monitor 和输入滚动、Gain 后 AFL/条内互斥、Pan 后立体声 AUX Pre/Post、两路总控、Master/Monitor 隔离、默认推子后及 -72..+12 表头、主题/i18n/FPS、侧栏唯一名称编辑、端口改名/回滚及工程重开、内嵌 Base64 状态与预设，均有上述源码与对应自动证据。阶段验收范围至此结束，不增加相邻功能。
