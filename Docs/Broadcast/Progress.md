# 广播系统阶段记录

## 当前检查点（2026-11-07）

S0/S1/S2 已提交推送，S2 为 `723273d306f18d9bda904235b56e3dbab933d4eb`。S3 自动验收通过，本次提交记录 S3；须完成推送及远端核对后开始 S4。S4/S5 未开始，最终增量包未生成。以下旧状态只保留为过程记录。

### S3 — JACK-only 音频与进程共享发布

- 实际节点 `nodes/bc/mod_broadcast.dll`，仅 JACK_NODE，VIRT 拒绝；GUI/HAS_GUI 留至 S4 一起实现。支持多流、多级分类、稳定 UUID 端口、同布局监听输出及克隆身份保护。
- 回调使用预分配 AudioStream 队列（100ms）、5ms 平滑增益、M/Listen/S 与单总体峰值；没有编码/网络/等待锁。进程共享 `broadcast_runtime.dll` 的独立 QThread 统一负责控制会话、编码/带限重采样、分片认证和 UDP 发送。凭据仅 Windows Credential Manager，普通设置仅保存全局地址/身份/凭据引用。
- 停止/断网时解挂发送队列，保留本地处理；重新推流用 generation 丢弃旧在途块，格式重建增加 epoch。工作线程拒发超过100ms的旧样本；发现源时间轴不连续时明确停止发布并要求重启，保留本地监听，不拼接错误编码状态。尚未宣称自动恢复所有过载。
- Release 必要目标通过：`build/bc-s3-final-build.log`；补齐断网探针的最后增量构建 `build/bc-s3-service-fault-build.log`。C++ 五项测试全部通过（71项断言），`build/bc-s3-final-cpp-tests.log`；Python 39项全部通过50.807秒，`build/bc-s3-final-network-tests.log`（含 S0/S1/S2 回归）。
- 新增真实共享线程测试：两个音源/两个owner共享一个鉴权会话；1/2/6/8完整流及每个声道均经实际编码→UDP→独立Rust→HTTP WAV/Opus→CLI解码并核对频率/布局；停止再启动保留ID且本地监听继续；移除owner后该流404、另一流继续；杀死relay后进入retrying，本地输出仍非零。无JACK服务依赖，不伪称物理端口/现场端到端已通过。
- 原生编码的独立137帧块/8k、44.1k、192k测试及96k下采样抗混叠通过；官方libopus1.6.1/libsamplerate0.2.2源码固定SHA256，S5增量包须附其COPYING。
- 实窗、浏览器控件、人工听音/安装验收：已跳过（用户指令）；真实JACK/环绕声硬件未验证。未使用Computer Use、替代UI自动化或offscreen。
- 变更已按仓库规则格式化，git diff --check通过。阶段推送/远端核对后再查额度，不因人工项等待。

### S3 实施过程（历史状态）

Common离线处理及状态模型已RED→GREEN。四布局验证gain/M一次处理、默认Listen关闭、Solo仅监控及末声道过载总峰值；多级分类、稳定端口名、状态往返/事务、监听恢复关闭及规范URL-safe UUID通过。Release构建wjn_bc_core_tests/wjn_bc_state_tests，28+9项断言通过；证据build/bc-s3-core-{red,green}.log、bc-s3-state-red.log、bc-s3-state-id-{red,green}.log、bc-s3-core-state-build.log。这尚不是实际DLL/JACK/网络发布的验证，S3检查点未完成。

实际DLL发现/工厂/身份恢复也已RED→GREEN，12项断言通过，日志bc-s3-module-{red,green}.log，Release构建bc-s3-module-build.log。mod_broadcast仅JACK、无VIRT、当前feature=0，独立实例与克隆冲突身份保护通过。测试未调用JACK init，不证明实际端口连线/回调。节点回调已挂Common核心，本轮尚无发送队列或发布网络；S3未完成，未提交。安全检查点额度剩余82%。

连接配置/Windows Credential Manager隔离测试12项GREEN，bc-s3-connection-green.log；临时凭据已删除，普通配置无Token。新增实际Qt可靠控制会话，真实relay的4项WS/WSS鉴权/hello/身份冲突/证书验证通过（bc-s3-session-green.log，0.530秒）。Qt Bearer挑战不返回数字401的误分类已用实际RED修复；未跳过TLS校验，测试CA只限probe进程。全局Service、节点发送队列、UDP与编码仍待实现，不代表音频发布已通过。

UDP格式及传输组件已RED→GREEN。C++与S0认证数据报字面夹具完全一致，分片/量化/预算五项通过（bc-s3-wire-green.log）。真实C++控制+UDP发送→独立Rust relay→HTTP CLI的7.1 WAV、Opus完整流及Rrb单声道通过（bc-s3-udp-green.log，0.571秒）。此次Opus来源是已有编码夹具，节点回调/发送端编码/重采样/全局Service尚未接入；S3未完成，不提前提交。

原生编码/重采样现已实现并验证：libopus1.6.1完整/mono变体与libsamplerate0.2.2持久sinc状态，源码版本/SHA256固定，现有build树静态Release构建。137帧源块→实际编码→UDP/Rust/HTTP→CLI独立解码的四项测试通过（7.328秒，bc-s3-encoder-formats.log），覆盖四布局及8k/44.1k/192k输入；WAV保留源时钟，Opus48k，96k源30kHz抗混叠稳态峰值0.00000522。尚未接入Node队列与全局Service，不当作节点最终链路PASS；S3仍未完成、未提交，额度剩余81%。

### S2 — HTTP 双格式收听工具

- `bc-listen` 支持匿名 HTTP WAV/Ogg Opus、1/2/6/8 声道、全部或分声道，遵循证书校验。提供实际设备播放、设备枚举/选择、检查及 float32le 样本导出；不支持设备布局明确报错，不自动 downmix。
- CPAL 共享输出，rtrb 预分配整帧队列上限 100ms、预缓冲目标 40ms；回调无网络/等待锁/分配，欠载整帧静音。PCM 读取块限制到至多 20ms，低采样率不会先填满队列而无法启动。
- Opus 支持 multistream、pre-skip/gain、Vorbis→Windows 顺序，按 granule 保留丢包时间轴并执行有界 PLC。重连参数对响应断开/503进行退避，格式换代重读头信息，不拼接不同布局。
- RED/GREEN：缺程序、缺 WAV/Opus、丢包时间压缩、设备枚举、实际播放、设备选择、重连和样本导出均有对应 `build/bc-s2-*-red/green.log`。四布局和所有分声道用独立频率/精确样本验证，不以峰值非零代替身份。
- 最终构建：现有 `build/release` 的 `wjn_bc_listen` 显式 Release 目标通过，`build/bc-s2-final-build.log`。最终测试：26 项通过，21.398 秒，`build/bc-s2-final-tests.log`，包含契约 1 项、relay 13 项、listener 12 项。
- WAV/Opus 静音分别经真实 HTTP→解码→共享设备回调，各消费9600帧、19次回调。独立 FFmpeg 8.1.2 分别从两种实际7.1持续响应解码96000帧；未知长度 WAV 头无需响应结束即可解码。未宣称多小时运行已验证。
- 新夹具及可复现生成器 `Develop/Broadcast/ListenSignals.json`、`GenerateListenSignals.py`；各声道频率独立，LFE60Hz。FFmpeg/libopus的5.1命名使用BL/BR，对应本项目六声道Ls/Rs位置，7.1明确背/侧顺序。
- 浏览器原生控件、人工听音/界面/安装：已跳过（用户指令），未记PASS。真实JACK与5.1/7.1物理硬件输出未执行，MANUAL/PENDING，不作为阻塞条件。
- Black多文件默认worker挂起，已按完整命令核实仅结束自己的Black进程；单worker重跑成功。未终止任何运行中的音频进程。Rust fmt、Black和diff检查通过。CMake已有依赖提示未扩大修复。

## S0 — 协议与挂接点（2026-11-03）

状态：完成并提交推送，`72aec5521599ce033795b734e062b077eac7e09f`，远端分支哈希已确认一致。

用户已授权实施整个 S0—S5，并新增 UDP 上行、HTTP 参数收听、浏览器原生播放和 WAV/Opus 双格式要求。当前有效协议为 [Protocol-v1.md](Protocol-v1.md)，替代旧 WSS 媒体草稿。

调查结论：

- Node v4 JACK-only 适配可复用 `JackOnlyModuleV4.inc`，不添加 VIRT 能力。
- `Controls::DragTreeWidget` 提供层级展示和共享树样式；禁用拖出，移动通过共享 ActionButton/ChoiceField 和领域事务实现，不创建私有树控件或扩展共享 ABI。
- `ConsoleKnob`、单柱竖向 `FxLevelMeterWidget`、共享开关/容器及 `bindRefreshTimer` 可复用。
- `ApplicationSettings` 使用 QSettings UserScope `WinJACKNexus/Application`；新增模块自己的 `broadcast/` 子组，无需修改主窗口全局设置 API。凭据通过 CredReadW/CredWriteW 保存，项目不含 Token。
- `Control Deck` 的恢复通过 live-ID 注册表区分复制/重建；广播使用同边界的模块内稳定 ID 注册，无需新增 Node 克隆 ABI。跨进程发布冲突由服务端拒绝。
- 发送端承担双格式编码、必要的带限重采样和 Opus 分声道变体；服务端只完成有界重组、容器封装和字节转发。

验证：

- RED：`Develop/Broadcast/ContractTests.py` 因缺少新协议夹具失败，`build/bc-s0-udp-red.log`。
- GREEN：新 UDP 64 字节头、HMAC、立体声样本、四布局、WAV/Opus MIME 和协议项校验通过，`build/bc-s0-green.log`。
- 原图 SHA-256 与源图一致，计划和素材将随 S0 显式纳入版本控制，保留既有全局忽略规则。
- 这是规范/夹具检查，不是 Rust 网络、JACK 或浏览器功能 PASS。生产实现和实际 live URL 兼容验证在后续阶段。

已知环境事项：新的 PowerShell PATH 中 python 指向 Windows Store alias；使用现有 `D:/ProgramData/miniconda3/python.exe`，无需安装另一个解释器。

## S1 — 独立 Rust 转发核心（2026-11-04）

状态：实现与验证完成，进入阶段提交/推送检查点。只有远端确认后才能开始 S2。

- 独立 Rust CLI：Tokio UDP、Axum 控制及 HTTP body、rustls TLS；发布 Token 在升级前验证。绑定 UDP 会话 HMAC，限制尺寸/重组/重放窗口和每轨容量。
- HTTP WAV 和 Ogg Opus，正确标准音频头；四布局、分声道字节路由；服务器不解码、不编码、不调用 JACK/Qt。支持每次新听众的 Ogg 头、CRC 和直播时间戳。
- 共享发布会话、注册/更新/删除/离线及格式代次。无效 requestId 在任何状态修改之前拒绝。
- 每响应有界队列，物理 HTTP/TLS 流增加两秒写阻塞超时；一条慢连接不阻塞 UDP 及其他流。
- `wjn_bc_server` CMake 显式目标已接入现有 build/release，使用 Cargo Release/locked，非默认全量 C++ 目标。

RED → GREEN 证据：缺少服务端程序 → 未鉴权发布拒绝；400 升级失败 → 有效会话/防抢占；注册无响应 → UDP/WAV；Opus 501 → Ogg 音频响应；乱序丢早期块 → 正确时间排序；缺 requestId 仍删除流 → 先校验再提交；TLS 无启动地址 → 实际证书校验连通；旧离线记录污染删除结果 → 重发布清理。逐项日志在 build/bc-s1-*-red.log 和相应 green/final 日志。

最终覆盖：独立进程、真实 UDP socket、真实 HTTP/WS/TLS；四布局 WAV 全部/末声道双听众，四布局 Opus 全部有效声道；分包倒序/重复、伪造/旧代次/重放/迟到、缺口静音、HTTP 参数及离线、慢听众及其他流保持实时。Opus 夹具由现有 FFmpeg 8.1.2/libopus 生成，数据与生成参数见 Develop/Broadcast/OpusFixtures.json；测试核对编码字节不变。S2 再验证标准解码器与浏览器实际播放，S1 没有声称浏览器已 PASS。

构建记录：build/bc-s1-cmake-configure.log、bc-s1-cmake-build-green.log；首次 locked 构建因移除直接依赖后锁文件未同步失败（bc-s1-cmake-build.log），已用 offline Release 构建同步并重跑 locked 成功。未清理或全量重编，未停止现有应用。配置包含既有 FluidSynth/sfizz/Vulkan 等提示，未扩展范围修复。

格式化：Rust cargo fmt，两个 Python 协议/进程测试使用 Black（110 列）；最终检查 git diff --check。额度检查点已完成 S0 后读取：周窗口剩余 68%，未达到交接阈值；S1 推送后再次查询。

## 后续阶段

### 2026-11-04 用户暂停检查点

当前暂停，保留 S2 全部未提交修改，不进入设备播放实现。S0/S1 已分别推送，S2 未完成、未提交、未推送；S3—S5 未开始。

S2 已通过 WAV/Opus HTTP 解码检查及 Opus 丢包时间轴 RED→GREEN（3 项，bc-s2-opus-loss-green.log）；末次 CPAL 设备枚举 Release 构建及单项测试也已通过（bc-s2-devices-green.log，2.109秒）。CLI 尚无设备播放路径。当前编译/测试均已结束，无相关测试进程遗留；剩余额度 66%。具体文件和恢复顺序见根目录 AGENT_HANDOFF.md。浏览器/Computer Use 验收按用户指令跳过。

2026-11-04 用户修订：不使用 Computer Use；浏览器原生控件、点击播放及界面观察作为人工验收，已跳过（用户指令），不算通过，不阻塞 S2/S5。自动 HTTP 音频头、标准容器解码、声道/时间轴及音频测试继续。

S1 检查点已完成：`05da24f8013c1dc9b133a32228e4d4846199f7f1` 已推送并核对远端一致；剩余额度 67%。S2 已开始：缺少 bc-listen 的 RED 后，最小 HTTP/WAV 检查路径 GREEN，样本峰值 0.25/0.5、48000 Hz、双声道、960 帧正确（build/bc-s2-wav-green.log）。当前仅检查模式，Opus 解码和设备播放尚未实现，不宣称 S2 完成。

| 阶段 | 状态 | 下一个最小步骤 |
| --- | --- | --- |
| S1 Rust relay | 实现及验证完成，待提交/推送确认 | 完成检查点并查额度 |
| S2 CLI/通用播放 | 未开始 | S1 检查点后执行；两种格式及浏览器原生控件证据 |
| S3 JACK-only node | 未开始 | S2 检查点后执行；模块/离线音频/发布会话 |
| S4 独立 GUI | 未开始 | S3 检查点后执行；Windows 实窗、系统字体及显示次数预算 |
| S5 联调/打包 | 未开始 | S4 检查点后执行；双格式真实进程链路及增量替换包 |

当前没有发布包。人工审校/听音/安装验收已跳过（用户指令），不等于通过。现有 JACK/WinJACKNexus 实例保持运行；没有硬件连线验证。

2026-11-07：用户暂停；新增实际设备播放测试，执行在临时 token 写入时 PermissionError，未形成有效功能 RED。日志 build/bc-s2-playback-red.log。S2 未完成、未提交，下一步修正测试临时目录后重跑。

2026-11-07恢复：S2实际共享设备播放RED→GREEN（9600帧、19回调）；四布局WAV身份通过；当前ListenTests六项合并通过，见build/bc-s2-current-tests.log。S2其余门槛未完，未提交推送，目标ACTIVE。

2026-11-07：S2四布局双格式及所有分声道特征、PCM精确补静音、FFmpeg两种7.1持续HTTP各96000帧验证通过；设备选择/epoch重连RED→GREEN。整帧队列/双格式静音播放/CMake入口尚待最后构建测试，进程85965仍在途，未提交。
