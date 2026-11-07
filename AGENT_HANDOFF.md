# Agent 交接手册

更新：2026-11-07（Asia/Seoul）。目标已完成：计划书S0—S5均完成，逐阶段RED/GREEN、提交推送/远端核对及额度查询已执行，三个交付包已生成并验证。

## 当前检查点

分支broadcast-system，跟踪origin/broadcast-system，禁止codex/前缀。S0 72aec55、S1 05da24f、S2 723273d、S3 85808b05a6f73fafdffe98cc9d8add4dd96f69c4均已提交推送，S3远端一致。S4已提交推送4a2008f62c24e459d0293ea0ea717ff4d867653b，远端核对一致。S5限定联调、自动验证及三个交付包已完成，提交a3fe7cebc4ab5fbdbe8f3a29e3a009cf0227333a已推送origin/broadcast-system，ls-remote与本地一致。本文后续收尾提交仅记录该已核实结果。

## 后续任务：配置文件、可选TLS、README/Wiki及重新打包（2026-11-07）

- 用户先要求本地TLS证书，后明确为NGINX反代将TLS改为可选；本次不生成生产证书。服务端未配置证书/key即HTTP/WS，同时配置则HTTPS/WSS，仅一项报错；既有insecure-loopback仍限制双回环绑定。UDP上行仍为会话/HMAC保护、未加密，不经过HTTP反代。
- 新增bc/server/src/config.rs与--config JSON，全部六项配置均支持；CLI覆盖JSON且不受顺序影响，JSON文件路径相对于配置文件目录，CLI路径相对于cwd；未知字段/重复参数/缺值拒绝。无新Cargo依赖。新增server.example.json及nginx.example.conf，服务端Action产物附两示例。
- 验证：原生Release及Action同target Windows x64构建通过，3项Rust单元测试通过；ConfigTests/RelayTests/SessionTests共23项真实进程/socket回归通过6.999秒，含无TLS HTTP/WS、配置TLS HTTPS/WSS、相对路径与CLI覆盖。实际节点DLL12项回归通过，主程序增量所需mod_broadcast Release构建通过。日志build/bc-config-{build,rust-tests,process-tests,network-regression,actionlint,action-windows-build,action-windows-tests,client-release-build,client-module-tests}.log。MSBuild因测试TEMP指向build出现MSB8029提示，构建成功，不改变应用或音频进程。
- 已更新四语言主README、服务端README、Protocol-v1和运行说明。服务端定位为独立远程音频公共后端，目前实现广播，其他远程音频能力仅为后续规划，未扩展实现或重命名程序。Wiki新增Broadcast-System页及Home/Features/User-Guide/_Sidebar导航；本地正文Docs/Wiki/Broadcast-System.md，待发布。
- Wiki使用Git独立仓库https://github.com/AI-Hobbyist/WinJACKNexus.wiki.git，原master/HEAD为29e43df3aa52a7dea0b6e8937ac2835b7dff79c3。仅在现有build下建立bare元数据build/broadcast-wiki.git，不创建代码工作区；五页草稿build/bc-wiki-pages，保留既有页面和行尾。用户已明确授权更新Wiki，可正常快进push，禁止force覆盖。
- 当前源代码与测试已完成；后续仅需按用户要求生成主程序增量包、服务端程序/通用配置部署包，使用package_broadcast.py --component client|server --label 20261107-Config避免覆盖旧S5哈希包。本地复制部署所需发布Token只能生成到build下私有部署目录，绝不入Git/公共Action。Linux二进制由对应Action取得，本机不冒称已构建Linux。实际NGINX/人工安装按指令跳过，不阻塞。

## 后续任务：独立服务端 GitHub Action（2026-11-07）

- 新增.github/workflows/broadcast-server.yml，手动workflow_dispatch，独立于桌面release.yml。原生runner矩阵Windows x64/MSVC、Linux x64/GNU、Linux ARM64/aarch64/GNU；ARM64已由用户明确确认，不含ARMv7。仅构建/测试bc-server，不安装Qt/JACK/收听端依赖。产物Windows ZIP、Linux tar.gz，附README/LICENSE/Cargo.lock/build-info.json及SHA-256，失败也上传日志。
- actionlint与git diff --check通过；按同一目标/锁文件本地Windows Release构建成功，Cargo test执行成功但当前Rust crate为0项测试，不冒称运行了功能单元测试。复用workflow原文打包步骤实测ZIP，五文件清单、CRC、target元数据、SHA-256通过。日志build/bc-server-action-{lint,windows-build,windows-tests,windows-package,package-verification}.log。
- Linux x64/ARM64和GitHub托管执行尚未实际运行；不将静态验证视为跨平台构建PASS。新Action为手动触发，按GitHub规则需先进入默认分支后才能在Actions UI中Run workflow。本次不合并默认分支、不触发云构建。bc/server/README.md已记录平台、产物与操作说明。没有未完成实现；提交推送状态以本记录所属提交及远端核对为准。

## 已实现和验证

- Rust relay bc/server独立于Qt/JACK，WSS Bearer控制+认证分片UDP上行，匿名HTTP GET参数返回WAV PCM16及Ogg Opus，服务器不转码。bc/listen实际CLI解码、CPAL设备播放、全/分声道、重连及检查导出。Protocol-v1固定。
- JACK-only节点nodes/bc/mod_broadcast.dll，1/2/6/8输入及同布局监听输出，多流、多级分类、稳定UUID端口、克隆身份保护、工程恢复事务。Listen/S不持久化且恢复关闭，M广播+监听，S仅本实例监听；每轨max绝对样本总体峰值。
- AudioStream预分配100ms SPSC队列、5ms增益平滑、generation隔离；节点callback只做音频/有界队列，无编码/网络/等待锁。源率改变由原子通知和控制线程重建。broadcast_runtime.dll进程全局Service/QThread拥有所有网络、Encoder/重采样和UDP组件；停止/断网保留本地监听，最后owner释放join自身worker。连续性/100ms预算失败明确停止发布要求重启，不无界追赶。
- 官方libopus1.6.1/libsamplerate0.2.2固定源码SHA256，静态构建在现有build/release/_deps，SRC_SINC_MEDIUM_QUALITY。S5需附两库COPYING。137帧输入/四布局/多采样率/抗混叠真实编码通过，96k源30kHz下采样稳态峰值0.00000522。
- S3最终Release build/bc-s3-final-build.log，加断网probe最后增量bc-s3-service-fault-build.log。C++5/5（71断言）及Python39/39（50.807秒）通过，bc-s3-final-cpp-tests.log、bc-s3-final-network-tests.log。真实共享线程四布局完整和所有分声道双格式、两个owner同会话、停止重启/移除/断网本地输出继续。实际DLL工厂状态与Common/worker音频分别验证，不冒称真实JACK或实际Node DLL音频回调端到端。
- S4 Window.hpp/cpp独立窗口，左侧共享DragTreeWidget+父分类/名字/布局事务编辑，右侧每轨GroupPanel/单列FxLevelMeterWidget/ConsoleKnob/M/S/Listen。顶端全局服务器/发布者/Token保存及Start/Stop；保存停止全局发布，空Token保留旧凭据。匿名完整或分声道URL复制。关闭隐藏不销毁节点。共享global Theme Token，无业务QSS/硬编码颜色；四语言Node i18n v2域broadcast、locate目录；刷新遵循全局FPS。
- S4 RED真实DLL未声明GUI：bc-s4-red.log。最终Release bc-s4-final-build.log；C++6/6通过2.29秒，含实际Windows GUI95检查；Service网络4/4通过21.360秒。日志bc-s4-final-cpp-tests.log、bc-s4-final-service-tests.log。GUI3次顶层显示；windows、真实HWND可见/exposed、系统Microsoft YaHei UI、96DPI/1.00、CJK glyph无缺失；截图build/bc-s4-window.png。完整GUI输出build/release/Testing/Temporary/LastTest.log。测试通过临时INI隔离全局设置，未写用户现有凭据；所有自建窗口/进程已关闭，无在途构建句柄。

## S5最终状态与收尾

- 实际Node DLL回调由显式离线JACK fixture驱动（复用现有SubGraphJackFixture.cpp，不改生产API、不增加VIRT），4流/34端口。四布局完整/所有分声道经真实UDP/Rust/HTTP/CLI双格式核验，WAV证明一次-6.0206dB增益，默认全部监听输出全零，销毁后端口和客户端清零。不是实际JACK服务/硬件验证。
- 最终Release build/bc-s5-final-build.log，加移除TLS诊断后的bc-s5-tls-final-build.log。C++6/6通过2.54秒（GUI仍95检查/3次显示）；Python41/41通过69.141秒。bc-s5-final-cpp-tests.log、bc-s5-final-network-tests.log，无在途句柄。
- S5隔离部署TLS实际使用包内Schannel，Qt插件目录及PATH不借用SDK；拒绝未信任证书、仅进程信任临时CA后成功。既有环境实际OpenSSL；临时测试CA改用setCaCertificates的封闭信任集，证书校验保持，没有系统CA安装或生产策略变化。额外证书插件实验撤销，临时诊断已移除。RelayTests只扩展TLS夹具函数，已撤回其余无关Black格式差异。
- 前50ms Opus峰值不适合作精确幅值证据；250ms样本后100ms稳态以同一误差限通过，未修改生产编码/增益。WAV精确样本与独立声道频率继续验证。
- 交付文件：build/Broadcast-Client-Overlay-20261107.zip（2693206字节）、Broadcast-Server-20261107.zip（1799157）、Broadcast-Listener-20261107.zip（1727845），均有.zip.sha256。精确清单/x64 PE/JSON/CRC/逐文件及ZIP SHA-256通过，build/bc-s5-package.log。哈希详见Docs/Broadcast/S5交付记录.md，运行说明Docs/Broadcast/运行与替换说明.md。客户端是兼容安装增量包，不是完整包；Rust两包独立，不依赖Qt/JACK。未终止应用或覆盖运行目录。
- 没有剩余产品实施或自动验证任务，不新增阶段。最终交付复核build/bc-s5-delivery-verification.log再次确认三包清单/CRC/哈希一致；运行源码自S4检查点未变，包内manifest记录该源码检查点。没有部署到用户运行目录，无需等待人工验收。

## 未验证与用户决定

不用Computer Use，也不得用其他外部UI自动化绕过。浏览器原生控件/点击播放及人工视觉、听音、安装验收：已跳过（用户指令），不记PASS、不阻塞。Qt程序化测试允许Windows可见实窗、系统字体，每批<=100次显示，严禁offscreen。真实JACK连线、现场音频不中断及物理环绕输出未验证。当前UI仅显示客户端UDP发送计数/源丢帧及控制RTT，不虚构服务端收包率/音频延迟；冻结协议无服务端stats。

## 必须遵守

- 精确任务范围与阶段顺序，达标即结束阶段；人工项不阻塞。全局配置只存地址/发布者/凭据引用，Token仅Credential Manager，工程JSON无Token，开机不自动推流。服务端不依赖WinJACKNexus，音频上行UDP、收听HTTP+参数且audio/*，无接收端GUI、无混音/下混。
- CodeGraph先定位源码；索引遗漏的配置/文档可读文件。minecraft-dev-mcp只限未来实际Minecraft对接，本项目不用。不启动子agent，只有额度<=2%交接阈值才按指令委派gpt-6-luna写手册。
- 仅现有build/release、Release、必要目标；不新建构建树、不ALL_BUILD、不停止现有WinJACKNexus/音频进程。每次构建/测试/打包前台PowerShell 2>&1 | Tee-Object -FilePath build.log，立即检查LASTEXITCODE，失败读log，不能并发覆盖日志。正常权限前台MSBuild使用MSBUILDDISABLENODEREUSE=1，受限MSBuild曾停滞。
- VS18 CMake：D:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe。Python D:/ProgramData/miniconda3/python.exe，Black --workers 1（默认并行曾挂起）；Qt C:/Qt/6.10.3/msvc2022_64，PATH及QT_PLUGIN_PATH按需设；网络测试TEMP/TMP设现有build，localhost需沙箱外执行。使用系统WindowsPowerShell、login:false。Rust target build/bc-target，FFmpeg D:/Program Files/ffmpeg/bin。
- 保留未跟踪用户文件.omc/、midi_tools_node_ref/、themes/Furina/、themes/org.eryth.furina.wjnskin，不暂存。Docs被忽略，精确git add -f。相关文档Docs/广播推流系统实施计划书.md、Docs/Broadcast/{Protocol-v1,Progress}.md、Develop/Node/Node-API.md、Docs/UI-API.md。新增/修改代码格式化及git diff --check。

## 额度

最近可靠接口10080分钟窗口已用21%、剩余79%，重置1794405624；未达到阈值，不猜订阅类型。S5推送后已查询。不因额度充足省略完成时手册。

## 2026-11-07 LAN & Remote Audio follow-up checkpoint

- Current branch: broadcast-system, upstream origin/broadcast-system. Config/optional TLS checkpoint d27722f is pushed. New runtime folder is nodes/lan_remote_audio, category is exactly LAN & Remote Audio. Source src/bc, mod_broadcast.dll, stable IDs and /bc endpoints are unchanged.
- Host maps legacy nodes/bc/mod_broadcast.dll project paths when the new DLL exists, suppresses old discovery duplicates, and escapes literal ampersands in category menus. Overlay must include mod_patchbay.dll.
- Release build passed for mod_broadcast, mod_patchbay, module/gui tests and node probe. Module test: 13 checks, including relocated DLL/category. Real Windows Qt GUI: 95 checks, 3 top-level shows; ctest 2/2 passed in 2.52 s. Logs build/bc-lan-remote-{build,host-build,tests}.log. Legacy saved-project migration was statically inspected, not exercised in a running project. Manual install/listening/NGINX/hardware acceptance SKIPPED per user instruction.
- Main README in four languages, Node API, runtime instructions and Wiki source updated. Actual Wiki master 31054495d649439cfd6e8f396d1340ccfc79f04f pushed and remote verified. No Computer Use.
- Next minimal work: generate and verify final labeled incremental client/server/config ZIPs, update this handoff with results, commit/push delivery record. Do not overwrite old S5 ZIPs. Private generated token stays only under ignored build; never add it to Git or public Actions.

## Final delivery checkpoint (2026-11-07)

- All requested implementation and autonomous validation completed. Source commit 4f181ef2fe4e368251d034aab0bf25a602103007 pushed to origin/broadcast-system and remote hash verified. This final record is committed/pushed separately; package source checkpoint remains 4f181ef.
- Client incremental overlay: build/Broadcast-Client-Overlay-20261107-Config.zip, 3,047,643 bytes, SHA-256 3a04eec91abeea0323e3636d85cc64389d23ae0c8868ab9ce125188562e54a27. 18 payload files plus manifest; includes mod_patchbay.dll and nodes/lan_remote_audio/mod_broadcast.dll/catalogs. Exact file set, x64 PE, JSON, CRC and per-file SHA-256 verified. No full application rebuild/package claim.
- Windows standalone server: build/Broadcast-Server-20261107-Config.zip, 1,813,517 bytes, SHA-256 f88aef63aa98fb8edf2fc2ee2f57b584f92f63c4f5b109dc3f2dfcc115ca906d. Seven payload files plus manifest, verified. Paired private deployment config: build/Remote-Audio-Config-20261107.zip; random publish.token generated only under ignored build and never logged/committed. Keep this configuration archive private. Copy its files beside the platform-specific server binary; Linux binaries come from the corresponding Action. No production TLS certificate generated following user's optional-TLS/NGINX revision.
- Extracted Windows program plus actual private config smoke passed: relative token lookup from another cwd, CLI ephemeral port overrides, HTTP route and authenticated WS. Test process cleaned up. Log build/bc-config-deployment-smoke.log. ZIP verification logs build/bc-config-final-{client,server}-package.log and bc-config-private-package.log.
- Current Action Windows packaging replay passed with all seven files (including JSON/NGINX examples), target metadata, CRC and SHA-256: build/bc-config-action-package-verification.log. Actual Linux x64/ARM64 and GitHub-hosted runs NOT executed. NGINX not installed locally; actual deployment/manual install/browser playback/listening/hardware acceptance SKIPPED (user instruction), never PASS. No actual user project migration run; path alias and discovery suppression inspected statically.
- Actual Wiki master is 31054495d649439cfd6e8f396d1340ccfc79f04f, push verified. Main README and Node API updated. Prior config tests 23/23, Rust 3/3, and this checkpoint module/real-window GUI 2/2 remain passing. No remaining implementation or active processes from this task. Preserve unrelated untracked .omc/, midi_tools_node_ref/, themes/Furina/, themes/org.eryth.furina.wjnskin. No quota handoff: reliable usage was 22% used in 10080-minute pool.

## Default broadcast stream bug fix (2026-11-07)

- User reported dropping a broadcast node showed nothing in D:/Program Files/WinJACKNexus. Installed mod_patchbay.dll, mod_broadcast.dll and broadcast_runtime.dll hashes matched the Config overlay. Real log: C:/Users/eryth/AppData/Local/WinJACKNexus/logs/WinJACKNexus-20261107-084150-241-8116.log. Separate exit-time access violation dump 084352-037-8116.dmp identifies Qt6Core+0xf1dd6; not attributed to this issue or fixed here, retain as follow-up.
- Deterministic RED through actual LoadedModuleNode::create with offline JACK fixture: initialized default node had zero streams/ports, so port-based Snapshot::capture omitted its client; build/bc-empty-host-red.log. Initial host empty-node synthesis experiment was withdrawn completely after user specified a default reserved stream. No production host changes remain.
- Final behavior: new BroadcastNode has one stereo Stream 1, 2 source inputs + 2 monitor outputs, Listen/Solo off. At least one stream must remain: disabled Delete action plus handler and Node::apply guard. Valid legacy empty saved state normalizes to a default stereo stream; nonempty state/IDs preserved. Default UUID participates in existing live identity registry.
- Release targets passed. ctest broadcast_host/module/gui: 3/3 in 4.05 seconds; actual DLL + production host loader and snapshot, real Windows canvas/standalone GUI, existing GUI checks expanded for default, legacy recovery and final-stream deletion guard. System fonts/windows backend; no actual JACK server, no Computer Use. Logs build/bc-default-stream-{build,final-build,tests}.log. Initial missing C++ headers/namespacing in experiment failed compilation and were corrected before final pass. Manual actual hardware/listening acceptance SKIPPED per user instruction.
- Source, Node API and runtime instructions ready. Next atomic delivery: commit/push, generate a mod_broadcast-only fix ZIP with SHA-256; if application is not running, back up and replace installed nodes/lan_remote_audio/mod_broadcast.dll. Do not stop user application/audio or launch it. Preserve prior archives and unrelated untracked files.

## Default stream delivery completed (2026-11-07)

- Source commit 30189d9326f7be8cef95979ce5abbcdf34712922 is pushed and verified on origin/broadcast-system. No production mod_patchbay source change remains; experimental host synthesis was fully restored to HEAD.
- Fix package build/Broadcast-Default-Stream-Fix-20261107.zip: 86,450 bytes, SHA-256 d6ba416ebd9273100c6f9c1d2b62e50168ef3e0e2048020c2ee3cd301a0d406e. Only changed nodes/lan_remote_audio/mod_broadcast.dll plus runtime instructions and manifest; exact contents, x64 PE, CRC and hashes verified. Original Config ZIP preserved. Log build/bc-default-stream-package.log.
- Installed the verified DLL directly to D:/Program Files/WinJACKNexus/nodes/lan_remote_audio/mod_broadcast.dll after confirming WinJACKNexus was not running. Backup build/bc-default-stream-installed-backup/mod_broadcast.dll preserved. Installed/source SHA-256 matched. Log build/bc-default-stream-install.log. Did not launch or stop application/audio processes. Real installed application interaction remains manual and SKIPPED per user instruction; automated real-window host/module/GUI validation passed as recorded above.
- No remaining implementation or autonomous delivery work. User can launch the installed application normally; a new node now has one stereo stream and the last stream cannot be deleted. Separate exit-time Qt6Core dump remains follow-up, not claimed fixed. This closing record is committed/pushed separately from source checkpoint.

## Compact broadcast strips and descriptive JACK names (2026-11-07)

- User requested ports named category-subcategory-stream, no black control backgrounds, shared button M/S/monitor, thinner meters/strips, centered/elided names/categories, and silence as negative infinity. Implemented in src/common/src/BroadcastState.cpp and src/bc/{Node,Window}.cpp. Ports use category-path-stream-in/out-speaker; duplicate/overlong prefixes get a UUID suffix within JACK limits. UUID/direction/role keys retain existing port objects during rename; temporary rename staging supports name swaps and frees removed port names. Network stream UUIDs/endpoints unchanged.
- UI uses existing shared ActionButton (checkable), TextLabel elision/full tooltip, ConsoleKnob and bindMidiToolsTheme passive surfaces. Strips 124 px (previously180), meter widget64 px with narrow single overall bar. Shared FxLevelMeterWidget adds opt-in setReadoutBelow(bool), default false with no new data members; existing horizontal/two-row layouts unchanged. Broadcast opts in and passes negative infinity for zero peak (existing floor readout shows -infinity). New API and call-site behavior documented in Docs/UI-API.md and Develop/Node/Node-API.md.
- Release affected targets built. Final ctest host/gui/state/module 4/4 passed4.47s; GUI117 checks,3 shows plus bounded2-show host real-window test, windows backend/system fonts. Actual category/name edit retained JACK port handle in offline fixture. Long Unicode/duplicate path naming covered. No real JACK server used. build/bc-compact-silence-{build,tests}.log; earlier naming pass build/bc-compact-names-{build,tests}.log.
- Real node DLL callback + real UDP/HTTP four-layout/all-channel network test1/1 passed18.099s, build/bc-compact-names-node-network.log. Shared UI-only readout follow-up was then rebuilt and ctest passed; no additional audio logic edits after the network pass. Actual stable Windows screenshot build/bc-compact-window.png inspected: black rectangles gone, centered elided names, shared buttons, thin bars, fully visible -infinity readout below. Manual actual installed application/JACK/listening acceptance SKIPPED per user instruction.
- Dependency exports verified against installed directory with replacement mod_broadcast.dll, broadcast_runtime.dll, ui.dll: build/bc-compact-dependency-check.log. Standard client packager now includes ui.dll due to the added exported method. No new controls, theme colors, fonts or private styles. User must reconnect old UUID-named JACK connections and save projects; current rename keeps port objects but old saved paths changed intentionally.
- Next atomic delivery: commit/push source, package the three changed DLLs plus instructions/license in a new compact-names overlay, preserve old ZIPs, replace installed files only if WinJACKNexus is not running, record final package/install/push status. Preserve unrelated untracked files. No Computer Use, no user process stopped or started.

## Compact/names delivery completed (2026-11-07)

- Source checkpoint 5a947251fb6e0221fe458741cfde2a1717d0a006 pushed and remote verified on broadcast-system. Final package build/Broadcast-Compact-Names-20261107.zip: 2,715,517 bytes; SHA-256 d87615c3f57dfb00ebb932a19414f5b3c741b568a2fac0c1974714a0b1505165. Five payload files plus manifest: mod_broadcast.dll at nodes/lan_remote_audio/, broadcast_runtime.dll, ui.dll, runtime instructions and project license. x64 PE/exact contents/CRC/per-file and archive hashes passed; build/bc-compact-package.log. Prior packages preserved.
- Verified WinJACKNexus was not running, backed up the three installed DLLs to build/bc-compact-installed-backup/ with relative paths, replaced them under D:/Program Files/WinJACKNexus, and matched SHA-256 against built replacements. build/bc-compact-install.log. No user application/audio process stopped or launched. No remaining implementation/autonomous delivery work; only manual installed-app/JACK/listening acceptance remains SKIPPED per user instruction.
- Old saved UUID-based JACK port connections intentionally changed names: user needs to reconnect affected broadcast ports and save the project. This is the project update step, not a blocked task. Current stream/category edits retain native port handles as verified by offline host regression. Separate previously recorded exit-time Qt6Core dump remains follow-up, not claimed fixed.
- Final GUI evidence:117 checks/3 shows, host bounded real Windows canvas + editor, 4/4 ctest4.47s and audio network1/1. Screenshot build/bc-compact-window.png inspected with no black rectangles, centered elision, thin meters, buttons and uncropped -infinity readout. Latest reliable usage23% used/10080-minute window; no quota handoff. Closing record commit/push is separate from source checkpoint.

## Control Deck meter variants checkpoint (2026-11-07)

- Stage 1 implemented legacy mono plus new peak_stereo/peak_5_1/peak_7_1 DLLs, input-only JACK/VIRT, independent atomic peaks, existing channel order L/R/C/LFE/Ls/Rs/Lrb/Rrb. New four-language catalogs. Endpoint ABI unchanged: contiguous atomic array length determined by new domain names; legacy domain remains1. Control Deck uses existing shared meters per channel with horizontal/vertical grouping and outside readouts. Shared setReadoutOutside(bool), defaultfalse, uses right-hand horizontal or bottom vertical text; public global text token supports light/dark surfaces. Docs/UI-API.md and Node-API.md updated.
- Release build passed. New direct wjn_control_deck_peak_tests.exe passed all four layouts, input ports, distinct channel levels, both orientations/outside-readout flag and reset with real Windows/system fonts; no JACK server. Four editor show calls, below100. Root CTest currently did not enumerate src/control_deck tests, so executed new test directly, not claimed as CTest pass. Broadcast GUI CTest1/1 passed2.61s final. Logs build/control-deck-peak-{final-build,final-tests,broadcast-regression}.log. 7.1 horizontal/vertical captures build/control-deck-peak-7.1-*.png; final vertical inspected with visible colored levels and readable external numbers.
- Next checkpoint actions: commit/push Stage1, package control_deck.dll/ui.dll + four peak modules/catalogs, preserve older archives. Then Stage2 user-approved new oscilloscope/spectrogram/polar widgets: 1/2/6/8-channel variants, combined/separate oscilloscope modes with per-channel colors, colored spectrogram with linear/log and combined/separate modes, polar channel-direction/energy distribution (NOT stereo vectorscope). User explicitly chose shared public UI library implementations, composed by Control Deck. Organize node folders by function into nested categories; write Control Deck specification. Do not begin Stage2 production edits until Stage1 commit/push verified. Manual checks skipped, no Computer Use.
