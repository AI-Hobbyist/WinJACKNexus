# S6：MIDI Tools 集成交付完成

2026-10-11。S0～S6 实施、定向 Release 构建和规定离线验证完成。

三个 DLL 按 `nodes/midi_tools/{sf2_loader,midi_spilit,midi_matrix}` 部署，
各有一个 Node v2 类型、Midi Tools 分类、独立 locate 和可选主题描述符。
Prepare-Delivery.ps1 在现有 build 中部署节点并复制许可/对应源码，
不复制、不删除用户配置或工程。两套模板各有三个独立节点 CSS。

DeliveryTests 从部署目录递归枚举、加载真实 DLL，验证发现条件、主题租约、
四语言目录以及新实例恢复 Unicode 客户端名和完整 JSON 设置；非法恢复不改变
已有状态。宿主 scanNodeModules 的递归目录和导出条件已核对，宿主实际 JACK
节点初始化和工程连线恢复没有运行，不能将离线状态回读等同真实工程连线验收。

分流的畸形消息、非法时间戳和事件上限丢弃新增累计诊断，客户端重置保留计数；
计数由处理线程持有，不引入锁、日志或跨线程读取。

verify_delivery.py 解析 PE imports，递归核验 14 个 DLL 依赖及 Windows 系统依赖；
生成 build/MidiTools-S6-deployment-manifest.json。FluidSynth、sfizz、GCEM 对应源码
校验和通过，主库及传递依赖许可文本、FluidSynth 最小补丁/重建与 DLL 替换说明
已纳入部署材料，详见 DEPENDENCIES.md。没有把开发音色夹具作为运行时音库。

验证证据：build/MidiTools-S6-{configure,build,deploy,dependencies,delivery,foundation,core}.log
及 build/MidiTools-S6-{split,matrix,synth_sidebar,synth_gui}.log。
真实双引擎合成、资源/旧声部/队列/预算、分流/矩阵/改名模型、真实 DLL 恢复、
四语言窗口交互、关闭重开和卡片背景边界断言均通过。
GUI 使用 Windows、100%/DPR=1、系统字体和可见 HWND 的 QScreen 截图；
本批预计显示上限 96，实际顶层显示合计 10 次，含编辑器/tooltip 未超过预算。
截图保存在 build/MidiTools-S6-real-window，仅重用阶段回归证据，不重复提交截图。

人工审校、视觉、安装/部署和真实工程连线验收：SKIPPED／已跳过（用户指令）。
真实 JACK 音频/连接行为未验证，没有启动或停止 JACK 或现有应用。

S6 独立提交推送确认后才开始自维护源码格式整理。最终完整替换 ZIP 按用户
最新顺序，在格式独立提交推送后、现有 build 全量 Release 重编成功后生成；
本记录是节点及部署清单完成证据，不提前宣称最终完整 ZIP 已生成。
