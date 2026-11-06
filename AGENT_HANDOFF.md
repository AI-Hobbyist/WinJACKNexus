# Agent 交接手册

更新：2026-11-07（Asia/Seoul）

## 当前目标与阶段

目标ACTIVE：执行 Docs/广播推流系统实施计划书.md S0—S5，逐阶段RED/GREEN、Release验证、记录、提交、推送、远端核对、查额度，最后打包增量替换包。

S0已完成：72aec5521599ce033795b734e062b077eac7e09f。
S1已完成：05da24f8013c1dc9b133a32228e4d4846199f7f1。
两者均已推送。当前分支 broadcast-system，跟踪 origin/broadcast-system；用户要求禁止codex/前缀，旧远端已按指令删除。
S2实现及自动验证完成，本次进入提交/推送检查点；检查点成功前不开始S3。S3—S5未开始，无最终增量包。以git实际提交/远端状态核对本手册的提交前快照。

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

1. 核对本次S2 diff/status，精确暂存本阶段文件，提交并推送broadcast-system，核对远端哈希，查额度。不要把原有未跟踪目录加入提交。
2. 检查点完成才进入S3：按Protocol-v1冻结契约实现JACK-only广播节点、离线处理及共享全局WSS/UDP发布、凭据存储。使用CodeGraph与现有Node API调用点，先RED后GREEN。
3. S4独立GUI复用公共控件/Theme Token/FPS；S5实际DLL离线输入→UDP→独立relay→HTTP→CLI及增量包。

## 必守需求

仅JACK、无VIRT。节点nodes/bc，多流/多级分类、稳定身份、1/2/6/8输入及同布局本地监听输出。Listen默认关闭，M广播与监听，S仅本实例本地监听。每轨单总体峰值/M/S/旋钮/Listen，独立GUI公共控件。全进程共享服务器配置/连接，凭据Windows Credential Manager，项目JSON无Token。
发布音频UDP，会话可靠WSS控制Bearer鉴权；匿名GET publisher/stream/channel/format，两种真实audio/*响应。发送端编码/重采样及Opus单声道变体；Rust relay不解码/编码/重采样，不依赖JACK/Qt。规范Docs/Broadcast/Protocol-v1.md；minecraft-dev-mcp仅未来实际Minecraft插件/模组对接，本项目不用。

## 工作区与工具

保留原有未跟踪.omc/、midi_tools_node_ref/、themes/Furina/、themes/org.eryth.furina.wjnskin，不暂存。其他人的改动不回退。Docs受忽略，新增/修改文档使用精确git add -f。S2文件：bc/CMakeLists.txt、Cargo.toml/lock、listen/**，Develop/Broadcast/{ListenTests.py,GenerateListenSignals.py,ListenSignals.json}，计划/Protocol/Progress及本手册。

CodeGraph先于源码查找/读取；没有索引的文档/配置可直接读。现有build树、Release、必要目标，不创建额外工作区，不ALL_BUILD，不停止运行音频应用。每次构建/测试/打包前台PowerShell 2>&1 | Tee-Object -FilePath build.log，立即检查LASTEXITCODE、失败读log。不要并发覆盖日志。
VS18 CMake：D:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe；树build/release；Rust target build/bc-target。Python D:/ProgramData/miniconda3/python.exe；FFmpeg D:/Program Files/ffmpeg/bin。测试TEMP/TMP设为现有build，网络测试需获准的沙箱外localhost访问。默认pwsh会话有启动/权限问题时可用系统WindowsPowerShell，仍遵守前台日志规则。

## 额度

最近可靠接口：10080分钟窗口已用17%、剩余83%，重置1794405624。未达到阈值，无Luna委派。阶段推送后再查；只在<=2%额度交接时委派gpt-6-luna编写手册，不擅自猜测额度/订阅。历史调音台交付可查git show 66a0387:AGENT_HANDOFF.md。
