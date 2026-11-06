# Agent 交接手册

更新：2026-11-07（Asia/Seoul）。目标ACTIVE：完成计划书S0—S5，逐阶段RED/GREEN、记录、提交推送/远端核对/额度查询，最后交付增量替换包。

## 当前检查点

分支broadcast-system，跟踪origin/broadcast-system，禁止codex/前缀。S0 72aec55、S1 05da24f、S2 723273d、S3 85808b05a6f73fafdffe98cc9d8add4dd96f69c4均已提交推送，S3远端一致。S4现已实现且自动验收通过，待本次提交推送核对。S5尚未开始，未生成增量包。

## 已实现和验证

- Rust relay bc/server独立于Qt/JACK，WSS Bearer控制+认证分片UDP上行，匿名HTTP GET参数返回WAV PCM16及Ogg Opus，服务器不转码。bc/listen实际CLI解码、CPAL设备播放、全/分声道、重连及检查导出。Protocol-v1固定。
- JACK-only节点nodes/bc/mod_broadcast.dll，1/2/6/8输入及同布局监听输出，多流、多级分类、稳定UUID端口、克隆身份保护、工程恢复事务。Listen/S不持久化且恢复关闭，M广播+监听，S仅本实例监听；每轨max绝对样本总体峰值。
- AudioStream预分配100ms SPSC队列、5ms增益平滑、generation隔离；节点callback只做音频/有界队列，无编码/网络/等待锁。源率改变由原子通知和控制线程重建。broadcast_runtime.dll进程全局Service/QThread拥有所有网络、Encoder/重采样和UDP组件；停止/断网保留本地监听，最后owner释放join自身worker。连续性/100ms预算失败明确停止发布要求重启，不无界追赶。
- 官方libopus1.6.1/libsamplerate0.2.2固定源码SHA256，静态构建在现有build/release/_deps，SRC_SINC_MEDIUM_QUALITY。S5需附两库COPYING。137帧输入/四布局/多采样率/抗混叠真实编码通过，96k源30kHz下采样稳态峰值0.00000522。
- S3最终Release build/bc-s3-final-build.log，加断网probe最后增量bc-s3-service-fault-build.log。C++5/5（71断言）及Python39/39（50.807秒）通过，bc-s3-final-cpp-tests.log、bc-s3-final-network-tests.log。真实共享线程四布局完整和所有分声道双格式、两个owner同会话、停止重启/移除/断网本地输出继续。实际DLL工厂状态与Common/worker音频分别验证，不冒称真实JACK或实际Node DLL音频回调端到端。
- S4 Window.hpp/cpp独立窗口，左侧共享DragTreeWidget+父分类/名字/布局事务编辑，右侧每轨GroupPanel/单列FxLevelMeterWidget/ConsoleKnob/M/S/Listen。顶端全局服务器/发布者/Token保存及Start/Stop；保存停止全局发布，空Token保留旧凭据。匿名完整或分声道URL复制。关闭隐藏不销毁节点。共享global Theme Token，无业务QSS/硬编码颜色；四语言Node i18n v2域broadcast、locate目录；刷新遵循全局FPS。
- S4 RED真实DLL未声明GUI：bc-s4-red.log。最终Release bc-s4-final-build.log；C++6/6通过2.29秒，含实际Windows GUI95检查；Service网络4/4通过21.360秒。日志bc-s4-final-cpp-tests.log、bc-s4-final-service-tests.log。GUI3次顶层显示；windows、真实HWND可见/exposed、系统Microsoft YaHei UI、96DPI/1.00、CJK glyph无缺失；截图build/bc-s4-window.png。完整GUI输出build/release/Testing/Temporary/LastTest.log。测试通过临时INI隔离全局设置，未写用户现有凭据；所有自建窗口/进程已关闭，无在途构建句柄。

## 下一步（严格顺序）

1. 完成S4文档/diff检查、提交推送、ls-remote核对和额度查询，完成前不开始S5。
2. S5限定实际Node DLL离线处理→UDP→独立relay→HTTP→CLI四布局与分声道联调。可调查现有Develop/Node/ClassicMixerJackFixture.cpp和其宿主调用点：已有离线JACK边界fixture，不要新增VIRT或伪称真实JACK。复用已实现编码/队列/Service，不扩展产品架构。
3. 必要Release目标及限定回归通过后，写运行部署说明、依赖清单，交付增量覆盖包（非全量包）和独立server/listener包，清单及SHA256核对。包含QtNetwork/WebSockets/TLS实际依赖、新codec COPYΙNG；不纳入无关文件。S5提交推送核对后更新手册、完成目标。

## 未验证与用户决定

不用Computer Use，也不得用其他外部UI自动化绕过。浏览器原生控件/点击播放及人工视觉、听音、安装验收：已跳过（用户指令），不记PASS、不阻塞。Qt程序化测试允许Windows可见实窗、系统字体，每批<=100次显示，严禁offscreen。真实JACK连线、现场音频不中断及物理环绕输出未验证。当前UI仅显示客户端UDP发送计数/源丢帧及控制RTT，不虚构服务端收包率/音频延迟；冻结协议无服务端stats。

## 必须遵守

- 精确任务范围与阶段顺序，达标即结束阶段；人工项不阻塞。全局配置只存地址/发布者/凭据引用，Token仅Credential Manager，工程JSON无Token，开机不自动推流。服务端不依赖WinJACKNexus，音频上行UDP、收听HTTP+参数且audio/*，无接收端GUI、无混音/下混。
- CodeGraph先定位源码；索引遗漏的配置/文档可读文件。minecraft-dev-mcp只限未来实际Minecraft对接，本项目不用。不启动子agent，只有额度<=2%交接阈值才按指令委派gpt-6-luna写手册。
- 仅现有build/release、Release、必要目标；不新建构建树、不ALL_BUILD、不停止现有WinJACKNexus/音频进程。每次构建/测试/打包前台PowerShell 2>&1 | Tee-Object -FilePath build.log，立即检查LASTEXITCODE，失败读log，不能并发覆盖日志。正常权限前台MSBuild使用MSBUILDDISABLENODEREUSE=1，受限MSBuild曾停滞。
- VS18 CMake：D:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe。Python D:/ProgramData/miniconda3/python.exe，Black --workers 1（默认并行曾挂起）；Qt C:/Qt/6.10.3/msvc2022_64，PATH及QT_PLUGIN_PATH按需设；网络测试TEMP/TMP设现有build，localhost需沙箱外执行。使用系统WindowsPowerShell、login:false。Rust target build/bc-target，FFmpeg D:/Program Files/ffmpeg/bin。
- 保留未跟踪用户文件.omc/、midi_tools_node_ref/、themes/Furina/、themes/org.eryth.furina.wjnskin，不暂存。Docs被忽略，精确git add -f。相关文档Docs/广播推流系统实施计划书.md、Docs/Broadcast/{Protocol-v1,Progress}.md、Develop/Node/Node-API.md、Docs/UI-API.md。新增/修改代码格式化及git diff --check。

## 额度

最近可靠接口10080分钟窗口已用20%、剩余80%，重置1794405624；未达到阈值，不猜订阅类型。S4推送后再查。不因额度充足省略完成时手册。
