# Agent 交接手册

更新时间：2026-11-04（Asia/Seoul）

## 目标与当前阶段

执行 Docs/广播推流系统实施计划书.md 的 S0—S5，每阶段 RED/GREEN、验证、提交、推送并核对远端，查额度，最终交付增量替换包。目标仍未完成。

S0 已完成，提交 72aec5521599ce033795b734e062b077eac7e09f，已推送 origin/codex/broadcast-system 并核对哈希一致。
S1 已完成实现及自动验证，当前进入提交推送检查点；当前记录随 S1 提交，后续先核对 git log/远端确认检查点，再开始 S2。S2—S5 尚未开始，不将服务器完成算作整个系统交付。

## 最新有效需求

- 广播节点仅 JACK，不支持 VIRT；多流、多级分类，输入为源，每轨同布局监听输出，监听默认关闭。M 影响广播与监听，S 只作用于该实例本地监听。
- 每轨单总体峰值表、M/S、旋钮、监听开关；独立 GUI，公共控件及 Theme Token，统一 FPS。
- 节点部署 nodes/bc；进程全局服务器配置与共享会话。
- 发送音频用 UDP，上行发布控制校验 Bearer Token；匿名 HTTP GET + publisher/stream/channel/format 参数收听。
- WAV/PCM 和 Ogg Opus 双格式，真实 audio/wav 或 audio/ogg; codecs=opus。浏览器直接导航显示原生音频控件并可点击收听，不以专用 CLI、网页、私有 MIME 或下载响应替代。
- Opus 编码及单声道变体由发送端负责；Rust 服务端独立、只做注册、重组、容器及字节转发，不解码/转码、不依赖 JACK/Qt。
- 当前定位经常使用 CodeGraph。minecraft-dev-mcp 只适用于未来真正对接 Minecraft 服务端插件/模组，当前不用。

## 文件及 Git

分支 codex/broadcast-system；S0 基线 72aec55。S1 修改 CMakeLists.txt、新增 bc/CMakeLists.txt、bc/Cargo.toml/lock、bc/server/**、Develop/Broadcast/RelayTests.py、OpusFixtures.json，更新契约测试格式、计划、Progress 及本手册。S1 提交哈希以 git log 为准，检查远端相同后继续。

计划和 Docs/Broadcast 下文件受原 /Docs/* 忽略规则影响，但 S0 已显式加入版本控制；后续新增该目录文件仍需显式暂存。未修改全局忽略规则。

原有未跟踪内容保留且不暂存：.omc/、midi_tools_node_ref/、themes/Furina/、themes/org.eryth.furina.wjnskin。历史调音台 1.0.1 交付可查 git show 66a0387:AGENT_HANDOFF.md，本轮未修改其业务代码和包。

## 已验证证据

- S0 新 UDP 协议夹具 RED→GREEN；Docs/Broadcast/Protocol-v1.md 为有效冻结契约，旧 56 字节/WSS 音频草稿撤销。
- S1 现有 build/release 的 wjn_bc_server 目标 Release 构建成功（build/bc-s1-cmake-build-green.log），无 ALL_BUILD。
- 最终契约 1 项通过（build/bc-s1-contract-final.log），真实服务进程网络测试 13 项通过（build/bc-s1-final.log，5.815 秒）。测试启动/回收自己的 relay，不要求 JACK。
- 覆盖鉴权/会话冲突、UDP HMAC/重放/旧代次/迟到、分包倒序和重复、四布局 WAV 双听众及声道顺序、所有 Opus 布局/声道编码包不变、Ogg CRC及时间戳、HTTP 参数/离线、慢听众不拖住其他流、证书校验 HTTPS/WSS。
- RED 修复：乱序时后位置抢先输出；无 requestId 仍执行删除；重发布后旧 offline 记录使删除仍报 503。各 red/green 日志保存于 build/bc-s1-*。
- TLS 初次验证缺启动地址而 RED，补齐实际端口输出后通过；测试临时证书仅在测试内信任，没有安装系统证书。
- 现有 FFmpeg 8.1.2/libopus 生成并保存真实 1/2/6/8 声道 Opus 测试夹具，JSON 记录来源参数；FFmpeg 是测试工具，不是 relay 运行依赖。
- Rust cargo fmt、Python Black（110 列），交付前检查 git diff --check。

## 失败处理及未验证边界

首次 CMake locked 构建因移除直接依赖后 Cargo.lock 未同步失败（bc-s1-cmake-build.log）；offline Release 同步后 locked CMake 构建通过。配置有既有 FluidSynth/sfizz/Vulkan 提示，未扩展处理。

S1 证明真实 UDP/HTTP 字节及容器转发，尚未验证浏览器原生播放器、实际解码播放、长期 WAV 直播、CLI、生产 JACK 节点及 GUI。它们是 S2—S5 的工作，不能报告 PASS。真实 JACK/硬件连线未执行，人工听音/审校/安装验收已跳过（用户指令），不代表通过。没有增量包。

## 下一步

1. 确认 S1 提交推送成功并查额度。
2. S2 按真实 HTTP URL 做最小 CLI 标准 WAV/Opus 接收和播放；先 RED，再实现；验证四布局/分声道及设备布局不支持报错。
3. 使用真实可见浏览器，直接导航 URL，核对原生控件、点击播放和时间推进、暂停恢复；不能做自建网页替代。Safari 环境缺失则记录 MANUAL/PENDING，不虚构结果。
4. S2 达到既定门槛后更新记录、提交推送再开始 S3；最终 S5 完成实际链路及增量包。

## 工具与额度

Python 使用 D:/ProgramData/miniconda3/python.exe，PATH 中 python 可能是 Windows Store 别名。Cargo/Rust 1.95 可用。已有 FFmpeg 位于 D:/Program Files/ffmpeg/bin。不要新装数据库或建立额外工作区/构建树。

最新可靠额度读数：codex 10080 分钟窗口已用 32%，剩余 68%，没有达到预警/交接阈值。未启动 Luna。阶段检查点后继续查询；仅额度交接阈值触发时把手册编写交给 gpt-6-luna，先安全完成原子操作并核对事实。

## 强制约束

先读 AGENTS.md、本手册及计划，CodeGraph 优先；范围严格按计划。仅现有 build、Release、必要目标；所有构建/测试/打包前台 PowerShell 经 2>&1 | Tee-Object -FilePath build.log，并立即检查 LASTEXITCODE，失败读日志。不要让并发命令写同一个 build.log。

Qt 禁 offscreen，只可见 Windows 实窗、系统字体、每批显示不超过 100 次，遵循应用 FPS。不停止任何现有 WinJACKNexus/音频进程；输出占用就报告文件。公共 UI/Theme Token 优先。仅无 JACK 服务测试，离线证据不能当现场端到端。每阶段检查点未完成不可开始下一阶段，完成任务时更新本手册。
