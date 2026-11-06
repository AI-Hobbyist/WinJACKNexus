# Agent 交接手册

更新：2026-11-07（Asia/Seoul）

## 当前目标与阶段

目标ACTIVE：执行 Docs/广播推流系统实施计划书.md S0—S5，逐阶段RED/GREEN、Release验证、记录、提交、推送、远端核对、查额度，最后打包增量替换包。

S0已完成：72aec5521599ce033795b734e062b077eac7e09f。
S1已完成：05da24f8013c1dc9b133a32228e4d4846199f7f1。
两者均已推送。当前分支 broadcast-system，跟踪 origin/broadcast-system；用户要求禁止codex/前缀，旧远端已按指令删除。
S2已完成检查点：723273d306f18d9bda904235b56e3dbab933d4eb，推送origin/broadcast-system成功，ls-remote核对与本地一致。S3进行中：公共核心/状态、真实DLL、配置/凭据、控制/UDP、原生编码/带限重采样已RED/GREEN；节点发送队列/进程全局管理尚未接入，未达到阶段检查点，未提交。S4—S5未开始，无最终增量包。

## 最新检查点覆盖说明

S3实施及自动验证现已完成，待本次提交/推送/远端核对；上述“S3尚未接入”等及下文旧下一步条目属于历史状态，以此段和Progress顶部为准。S4/S5尚未开始，没有最终增量包。

- 已接入 AudioStream 预分配100ms队列、5ms增益平滑、generation隔离；CoreTests现33断言。Node实际JACK callback写此队列，runtime保留同ID/格式的队列，采样率变化在控制线程换代；未初始化JACK，不能冒称实际端口验证。
- 新增Service.hpp/cpp与共享broadcast_runtime.dll：一个进程全局Service，一个QThread创建/使用/销毁socket、timer、Encoder；主线程facade按owner维护发布流。仅用户显式start才连接，Token从Credential Manager读取；stop/最后owner释放后阻塞join自身worker，应用音频保持运行。源时间轴缺口/过期预算导致明确发布错误，监听继续。
- ServiceProbe/ServiceTests真实音源→同一会话→UDP→Rust→HTTP→CLI验证四布局完整流/每个声道双格式、两个owner、停止重启ID、本地监听继续、移除owner404及另一流继续、kill relay后retrying但本地处理继续。不是测试JACK连线，不新增VIRT入口。
- 最终必要Release构建build/bc-s3-final-build.log；最后补齐断网probe的增量build/bc-s3-service-fault-build.log。C++5/5通过（71断言），build/bc-s3-final-cpp-tests.log；Python39/39通过50.807秒，build/bc-s3-final-network-tests.log。无在途句柄，所有测试自建进程已正常清理。
- 首次受限MSBuild工作进程停在Checking Build System；核实本任务PID树后仅结束该构建树，正常权限前台重建成功，MSBUILDDISABLENODEREUSE=1。没有停止WinJACKNexus/音频进程。Service的Qt JSON uint32歧义已显式qint64修复；删除流的测试预期由503纠正为实际契约404。
- 下一步：先完成S3提交推送与远端核对/查额度，然后S4复用Protocol指定公共控件，独立GUI、分类编辑、全局服务器/凭据入口、M/S/Listen/单峰值/旋钮、主题i18n/FPS。S4完成检查点后S5实际DLL离线边界限定联调及增量包，附新codec依赖COPYING。不要重做编码器。
- 额度最近已用19%，剩余81%；无阈值交接，不启动Luna。

## S3当前原子进展

- 新增Common/BroadcastProcessor.hpp：无分配/锁/网络的平面输入→交错广播与同布局平面监听，gain/M一次处理，Listen/S仅影响本地，统一post-gain/M peak。CoreTests四布局28项断言通过，build/bc-s3-core-red.log、bc-s3-core-green.log。
- 新增Common/BroadcastState.hpp及src/BroadcastState.cpp，由src/bc的bc_state静态目标提供，未改现有Common DLL源列表。支持UUID流、多级分类（协议最多32层）、稳定JACK端口名（不随显示名变化）、增益/M持久化；Listen/S不保存且恢复关闭，不含全局服务器/Token。
- StateTests九项断言通过：分类/7.1输入监听输出/往返/监听安全/改名身份/循环/重复/URL-safe规范UUID/失败恢复事务。先missing header RED，再实现GREEN；发现Qt接受花括号UUID但不符合协议ID，新增实际FAIL后收紧规范UUID再GREEN。日志bc-s3-state-red.log、bc-s3-state-id-red.log、bc-s3-state-id-green.log。
- 已有build/release构建wjn_bc_core_tests及wjn_bc_state_tests Release成功，bc-s3-core-state-build.log；两测试均无GUI、无JACK连接。只证明Common离线边界，不证明实际节点DLL/JACK/UDP发布。
- 修改根CMake增加src/bc、Develop/CMakeLists增加Broadcast；新增Develop/Broadcast/CMakeLists.txt及两个cpp测试；上述文件尚未提交。使用VS18 clang-format仅格式新增五个C++文件（LLVM/4空格/Allman/110列），diff检查通过。
- Qt6WebSockets已安装；未决定新增Rust客户端运行库，继续优先现有Qt网络/Win32凭据接口。发送编码/带限重采样尚需实现，不假冒双格式发布已完成。
- 新增src/bc/Node.hpp、Node.cpp、Module.cpp，mod_broadcast实际输出build/release/bin/nodes/bc/mod_broadcast.dll。JACK-only适配使用JackOnlyModuleV4.inc，feature=0，S4才实现GUI并声明HAS_GUI。JACK回调复用Common process与RealtimeObject固定快照，无锁无分配；输入与监听端口按流同布局绑定。控制侧先准备新增端口，成功后交换，保留既有未删除JackPort对象与连接。没有音频发送队列/编码/网络发布，当前callback广播目标为nullptr，不能把它当推流已完成。
- ModuleTests真实DLL RED为文件不存在，GREEN为12项发现/工厂/工程恢复断言，build/bc-s3-module-{red,green}.log，构建bc-s3-module-build.log。明确未调用init，未连接JACK；不验证JACK回调/端口实际行为。验证了独立实例、VIRT/非法mode拒绝、复制冲突ID重分配、同实例恢复ID保留、无效恢复不修改、销毁释放ID。模块内注册表仅在控制侧互斥，进程总流数限64。
- 活跃clientName改变目前明确拒绝，工程恢复在init前设置；当前没有UI入口。JACK实际初始化/端口/断网不停本地处理、发布网络、凭据尚待后续证据。新增Node-API.md节点行为说明，不改现有冻结导出签名。此次新增四个cpp/hpp已clang-format。
- 新增Connection.hpp/cpp及bc_connection静态库：UserScope WinJACKNexus/Application的broadcast三项URL/发布者ID/凭据引用；Token用Win32 Generic Credential，读取/写入/删除均返回明确错误，无Token日志或普通配置存储。HTTPS+固定/bc；仅明确loopback URL可HTTP。隔离ConnectionTests的12项断言通过（bc-s3-connection-green.log）；仅唯一临时凭据目标创建/删除，没有修改用户既有凭据或全局配置。发布ID的行尾换行曾被正则$放行，实际RED后改全字符串锚点GREEN（bc-s3-connection-id-red.log）。
- 新增PublishSession.hpp/cpp及bc_session静态库，Qt6Network/WebSockets（已安装）可靠控制：Bearer头、hello/session/key/UDP端口验证、心跳/超时与退避、控制队列上限、错误Token/身份冲突终止。尚无进程单例管理器、音频UDP或节点连接，组件设计为发布worker事件线程所有；当前真实probe在其QCoreApplication线程运行，不是JACK回调。
- SessionProbe/SessionTests通过真实Rust relay验证4项：协商成功、错误Token终止、原身份不被抢占、WSS证书拒绝及仅进程临时CA信任后成功。日志bc-s3-session-red.log、bc-s3-session-auth-red.log、bc-s3-session-tls-red.log、bc-s3-session-green.log（4项0.530秒）、bc-s3-session-build.log。Qt对Bearer 401的实际诊断不含数字401、只含Unsupported WWW-Authenticate；按该标记分类修复，临时诊断已移除。曾尝试延迟disconnected处理，证据表明根因是诊断形式，已撤销该改动。
- Credential与Session新增C++已clang-format，SessionTests.py已Black单worker；git diff --check通过。当前无在途构建/测试句柄；未查询或操作任何GUI。
- 新增Wire.hpp/cpp（bc_wire静态库）：worker侧float→PCM16 nearest/clipping，64字节小端头，1104载荷分片、完整HMAC-SHA256，最多32片，按数据报递增seq。WireTests五项GREEN（bc-s3-wire-{red,green}.log），实际C++包与S0字面夹具逐字节相同；7.1/48k/20ms 15360字节正确分14片，oversize拒绝不消耗seq。
- 新增UdpTransport.hpp/cpp（bc_udp静态库），只发往已鉴权WebSocket实际peer地址+hello端口，不另信任远端host或重新DNS；会话换代后更换key/session并重置seq；记录OS接受的字节/报文数，写失败明确错误。仍为worker侧组件，节点尚未调用。
- SenderProbe和SessionTests新增真实UDP链路RED→GREEN（bc-s3-udp-{red,green}.log、bc-s3-udp-build.log）。C++实时定时器用既有预编码夹具+PCM发送7.1，Rust独立relay接收后HTTP CLI成功解码WAV all、Opus all及Rrb单声道各4800帧，PCM峰值顺序核对；Opus此次为预编码夹具，不能当发送端编码器或真实节点回调已验证。SessionTests新增项单独通过0.571秒，原4项此前通过，未在此改动后合并重跑。
- 新增Wire/UdpTransport/Probe C++已clang-format，SessionTests.py已Black单worker，diff检查通过；无在途构建句柄，自建sender/relay进程已清理。预查依赖：现有Rust opusic-sys所带libopus是1.6.1；官方源包SHA256为6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1。QtWebSockets已装，未发现可直接复用的生产带限重采样器；尚未新增/下载原生编码或重采样依赖，不要冒称已实现。
- 编码器现在已实现：src/bc/Encoder.hpp/cpp（bc_encoder静态库）接收任意连续交错源块，PCM按源时钟10ms组块（192k/7.1仍满足32片预算），Opus重组48k/20ms完整流及独立mono变体，family0/1及pre-skip/lookahead/规范位率。完整流重排为Vorbis顺序，mono按canonical身份抽取；Mono all/M共用variant0。Encoder worker侧分配/编码，不能在JACK回调调用；push位置从当前格式epoch的0开始，不连续抛错要求新epoch。
- 真实原生EncoderProbe用137帧输入生成0.4秒独立声道正弦，再由Python真实UDP→Rust→HTTP CLI解码。EncoderTests四项（内含多布局/速率子项）通过7.328秒，build/bc-s3-encoder-red.log、bc-s3-encoder-green.log、bc-s3-encoder-formats.log。覆盖1/2/6/8完整流、代表mono变体、WAV原始源时钟与Opus48k；8k/44.1k/192k输入持续重采样；96k源30kHz经下采样后稳态峰值0.00000522（阈值0.002），原始WAV仍含30kHz。并非预编码夹具，也不是实际Node callback→网络最终链路。
- MediaDependencies.cmake固定并校验官方源码：libopus1.6.1 SHA256 6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1；libsamplerate0.2.2 SHA256 3258da280511d24b49d6b08615bbe824d0cacc9842b0e4caf11c52cf2b043893。均在现有build/release/_deps下静态构建，关闭依赖测试/示例，不改主工程BUILD_TESTING。SRC使用持久SRC_SINC_MEDIUM_QUALITY状态，48k bypass，未用线性插值。Release构建日志bc-s3-encoder-build.log；首次Opus编译有上游-msse4.1被MSVC忽略警告，编译及真实编码测试通过，未改第三方代码。S5须带两库版权许可文件。
- Encoder/Probe新增C++clang-format、EncoderTests.py Black单worker、diff检查通过。没有在途编译测试。模块尚未链接使用bc_encoder；不得将组件测试等同已具备节点推流。

## S2交付与证据

新增bc/listen，reqwest HTTP、ogg流读取、libopus multistream解码、CPAL共享设备输出、rtrb整帧有界队列。支持WAV/Opus四布局/分声道，设备枚举/选择，检查/float32le样本导出、显式退避重连。CLI说明在bc/listen/README.md。CMake显式wjn_bc_listen，非默认ALL_BUILD。

- 既有build/release显式目标Release构建通过：build/bc-s2-final-build.log。
- 最终26项测试通过，21.398秒：build/bc-s2-final-tests.log。契约1、relay13、listener12，真实独立进程/socket，无JACK服务要求。
- WAV四布局/所有分声道逐样本核验；Opus四布局/所有分声道独立频率身份、pre-skip/gain/Windows顺序及granule丢包PLC。PCM缺480帧精确补静音，Opus缺960帧保留时间轴。
- WAV/Opus静音各经HTTP到共享设备实际回调，9600帧、19回调。输出队列100ms、预缓冲目标40ms；整帧避免欠载声道旋转，读取块至多20ms，8kHz不先填满队列再启动。
- FFmpeg8.1.2分别解码真实7.1 HTTP两种持续响应，各96000帧；不冒称多小时/所有播放器已验证。
- RED/GREEN日志build/bc-s2-*-red/green.log，覆盖缺程序/双格式解码/PLC/枚举/播放/设备选择/重连/样本导出。新增GenerateListenSignals.py与ListenSignals.json记录可复现独立频率夹具，LFE60Hz；FFmpeg的5.1 BL/BR对应项目六声道Ls/Rs位置。
- Rust fmt、Black workers1、git diff --check通过。Black默认并行挂起：核实唯一自身python进程命令后结束PID11904，原会话85965返回-1；单worker重跑成功。没有构建/格式会话等待，不要继续轮询旧句柄。此前g2:37已stale，34339已unknown。

## 未验证与用户决定

不使用Computer Use。浏览器原生控件、点击播放、人工界面观察/听音/安装验收：已跳过（用户指令），未记PASS，不阻塞里程碑。不得换UI自动化绕过。真实JACK连线及5.1/7.1物理硬件输出未执行，MANUAL/PENDING；离线或立体声回调不是现场环绕声PASS。后续Qt程序化测试仍正常Windows可见实窗、系统字体、每批最多100次显示，严禁offscreen。

## 下一步（严格顺序）

1. S2已提交推送并查额度，不要重复实施。S3下一步全进程Service及节点预分配队列：复用已GREEN的Encoder/Connection/Credentials/PublishSession/Wire/UdpTransport，接入节点真实处理路径。不要重复实现编码/重采样。当前尚无真正Node→网络链路或worker/心跳重连实测；还须补全计划中的平滑增益及S3最终断网/恢复/容量等既定门槛。
2. 按Protocol-v1冻结契约实现JACK-only广播节点、离线处理及共享全局WSS/UDP发布、凭据存储。已调查JackOnlyModuleV4.inc、INode.hpp、Control Deck模块/CMake/测试挂接；S3可不声明HAS_GUI，S4实现实际窗口后再补元数据。不要引入VIRT测试入口来替代离线Common测试。
3. S4独立GUI复用公共控件/Theme Token/FPS；S5实际DLL离线输入→UDP→独立relay→HTTP→CLI及增量包。

## 必守需求

仅JACK、无VIRT。节点nodes/bc，多流/多级分类、稳定身份、1/2/6/8输入及同布局本地监听输出。Listen默认关闭，M广播与监听，S仅本实例本地监听。每轨单总体峰值/M/S/旋钮/Listen，独立GUI公共控件。全进程共享服务器配置/连接，凭据Windows Credential Manager，项目JSON无Token。
发布音频UDP，会话可靠WSS控制Bearer鉴权；匿名GET publisher/stream/channel/format，两种真实audio/*响应。发送端编码/重采样及Opus单声道变体；Rust relay不解码/编码/重采样，不依赖JACK/Qt。规范Docs/Broadcast/Protocol-v1.md；minecraft-dev-mcp仅未来实际Minecraft插件/模组对接，本项目不用。

## 工作区与工具

保留原有未跟踪.omc/、midi_tools_node_ref/、themes/Furina/、themes/org.eryth.furina.wjnskin，不暂存。其他人的改动不回退。Docs受忽略，新增/修改文档使用精确git add -f。S2文件：bc/CMakeLists.txt、Cargo.toml/lock、listen/**，Develop/Broadcast/{ListenTests.py,GenerateListenSignals.py,ListenSignals.json}，计划/Protocol/Progress及本手册。

CodeGraph先于源码查找/读取；没有索引的文档/配置可直接读。现有build树、Release、必要目标，不创建额外工作区，不ALL_BUILD，不停止运行音频应用。每次构建/测试/打包前台PowerShell 2>&1 | Tee-Object -FilePath build.log，立即检查LASTEXITCODE、失败读log。不要并发覆盖日志。
VS18 CMake：D:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe；树build/release；Rust target build/bc-target。Python D:/ProgramData/miniconda3/python.exe；FFmpeg D:/Program Files/ffmpeg/bin。测试TEMP/TMP设为现有build，网络测试需获准的沙箱外localhost访问。默认pwsh会话有启动/权限问题时可用系统WindowsPowerShell，仍遵守前台日志规则。

## 额度

最近可靠接口：10080分钟窗口已用19%、剩余81%，重置1794405624。未达到阈值，无Luna委派。阶段推送后再查；只在<=2%额度交接时委派gpt-6-luna编写手册，不擅自猜测额度/订阅。历史调音台交付可查git show 66a0387:AGENT_HANDOFF.md。
