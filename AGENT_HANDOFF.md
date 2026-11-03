# Agent 交接手册

更新时间：2026-11-03（Asia/Seoul）

## 当前任务与状态

用户已授权实施计划 S0—S5，逐阶段 RED/GREEN、提交、推送及查询额度，最终打包增量替换包。S0 协议冻结、挂接点调查及夹具 RED/GREEN 已完成，当前准备其提交推送检查点，推送确认后才能开始 S1。生产功能尚未实现。

最新方向修订已写入计划：音频上行改为 UDP；听众通过 HTTP GET + 参数请求实时音频，匿名收听，不建立 WebSocket/UDP 播放会话。旧 PCM over WSS 媒体方案撤销。HTTPS/WSS 仅可用于可靠控制、Bearer 发布鉴权和会话协商。新协议的 UDP 字段/会话安全/MTU/重组及 HTTP 响应封装需在 S0 冻结。

用户追加并已写入计划：HTTP 成功头必须是真实标准音频类型，浏览器直接打开 URL 自动出现原生播放控件且能点击收听；支持标准音频和 Opus。计划具体落实为 format=wav（PCM/WAV，audio/wav）及 format=opus（Ogg Opus，audio/ogg; codecs=opus），不以私有 MIME、octet-stream、下载链接、裸 Opus、专用 CLI 或自建网页替代。发送端负责编码与必要重采样，服务端仅选择/封装/转发；Opus 分声道需发送端独立编码。S0 须固定双格式参数和实际浏览器/播放器验证矩阵，未知长度 WAV 的长时直播需验证，不只测短文件。

定位仅使用 CodeGraph；minecraft-dev-mcp 只在未来实际对接 Minecraft 服务端插件/模组时使用，当前项目无此依赖。

用户最新明确要求：广播节点仅 JACK，不实现 VIRT；支持多流和多级分类；输入是广播源，输出是本地实时监听；每轨单总体表、M/S、音量及默认关闭的监听开关；节点部署在 /bc 子路径；统一全局连接；发布需要 Bearer Token，公开收听不需要；完整流及分声道子路径。

## 交付文件

- Docs/广播推流系统实施计划书.md：需求、现有代码调查、音频与监听、全局路径、鉴权、协议建议、GUI 复用、S0—S5 阶段及完成标准。
- Docs/Broadcast/广播发送节点布局参考.png：用户原图的逐字节副本。
- AGENT_HANDOFF.md：本次收尾记录。
- Docs/Broadcast/Protocol-v1.md：冻结的 UDP HMAC 包、发布控制、标准 HTTP WAV/Opus、声道和监听契约。
- Docs/Broadcast/protocol-fixture.json、Develop/Broadcast/ContractTests.py：跨语言协议夹具和 S0 检查。
- Docs/Broadcast/Progress.md：各阶段状态及日志。

按用户实施整份计划的授权，采用计划的 M/S、独立同布局监听输出和全局连接建议。传输遵循最新 UDP/HTTP 更正，不继续旧 WSS 媒体方案。后续先完成 S0，不跳过协议冻结。

## Git 与文件状态

- 分支 codex/broadcast-system；HEAD 66a0387（docs: record 1.0.1 validation and overlay delivery）。
- S0 验证已通过，当前记录随 S0 检查点提交；提交及推送结果请核对 git log 和远端分支，不凭文档猜测。
- 计划书和 Broadcast 文档/图片受既有 /Docs/* 忽略；本阶段显式强制暂存这些交付文件，不修改全局忽略规则。
- 本次跟踪文件修改仅本交接手册；新增计划、素材及 Develop/Broadcast/ContractTests.py。
- 原有未跟踪内容保留：.omc/、midi_tools_node_ref/、themes/Furina/、themes/org.eryth.furina.wjnskin。
- 历史 1.0.1 调音台交付记录可用 git show 66a0387:AGENT_HANDOFF.md 查看，本轮未修改该版本代码或覆盖包。

## 已完成与验证

- 已读 AGENTS.md、原交接、Node/UI API 及相关 Native Fx 契约；优先通过现有 CodeGraph 调查节点、端口和独立窗口入口。
- 确认 Node v4 和 JACK-only 导出适配方式；公共 ConsoleKnob、单柱竖向电平表和共享内容控件可复用。
- 官方资料核对覆盖 Axum、Tokio、RFC 6455 与 RFC 6750，链接已写入计划。
- 原图与副本 SHA-256 一致：31170b64e5500ceafe9dac0c55ef1042d07c11bdc24ca6752cd7e1ef5c1c1a93。
- 文档交付前检查 Git 差异和空白，不把文档检查声称为功能测试。
- 最新额度接口返回 codex 主窗口使用 32%、窗口 10080 分钟，剩余 68%；未达到预警或交接阈值。接口 planType 为 prolite，不据此推断其他订阅；未启动 Luna。
- CodeGraph 确认共享 DragTreeWidget 是拖出源；可复用树展示和共享“移动”操作，不擅自扩展私有视觉控件。Control Deck 恢复通过运行实例 ID 注册表区分复制，可参考避免更改 Node ABI；ApplicationSettings 已有 FPS 刷新绑定。
- S0 测试已更新为 UDP 64 字节头/会话 HMAC/PCM 样本/标准 HTTP 双格式。RED 因缺夹具失败（build/bc-s0-udp-red.log）；补齐新夹具后 GREEN：1 项通过（build/bc-s0-green.log）。这是协议文档/夹具验证，不是网络或浏览器功能 PASS。
- 当前 PATH 的 python 为 Windows Store 别名；使用现有 D:/ProgramData/miniconda3/python.exe。

## 未完成、未验证与失败项

- S0 协议字段及取舍已冻结；S1—S5 未开始。
- 除协议测试草稿外，无生产代码实现、构建、GUI 运行或打包，没有功能 PASS 结论。
- 真实 JACK 连线、硬件播放、公网环境未验证。
- 多级树使用现有展示控件和共享移动操作，不需新控件；复制身份采用 live-ID 注册表；全局配置采用已有 QSettings 命名空间下 broadcast 子组和 Credential Manager，详见协议。具体生产调用点在 S3/S4 实现测试。
- 没有构建失败项；未停止或修改任何正在运行的应用或音频进程。

## 下一步

先确认 S0 提交/推送及远端、查询额度，然后进入 S1：真实独立 Rust 进程的控制鉴权、UDP 音频和 HTTP 双格式响应，每条公共行为先 RED 再 GREEN。S1 通过检查点后才 S2；最终必须完成 S5 增量替换包，不能以协议文档代替系统交付。

## 继续遵守

先读 AGENTS.md 和本手册；有索引先用 CodeGraph。广播节点仅 JACK，Rust 服务端独立且仅转发。UI 优先公共控件和 Theme Token，缺失控件先确认归属，不写私有样式。仅用既有 build 树、Release、必要目标；构建/测试/打包必须前台 PowerShell 经 Tee-Object 写 build.log 并立即检查退出码。Qt 只能 Windows 可见实窗和系统字体，禁用 offscreen，每批最多显示 100 次。不停止现有应用及音频流。仅执行无需 JACK 服务的本地测试，离线证据不冒充硬件端到端。人工验收记录“已跳过（用户指令）”，不算通过。实施阶段验证通过后更新记录、检查差异、提交、推送并确认后才进入下一阶段。保留用户工作区，任务结束更新本手册。
