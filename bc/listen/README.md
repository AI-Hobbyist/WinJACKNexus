# bc-listen

匿名 HTTP 直播收听工具，支持 PCM16 WAV 与 Ogg Opus，无需 JACK 或发布 Token。
现有 CMake 树显式构建目标为 `wjn_bc_listen`（Release）。也可使用 Cargo：

```powershell
cargo build --manifest-path bc/Cargo.toml --package bc-listen --release --locked --target-dir build/bc-target
```

Windows 原生 Opus 依赖使用 VS18 CMake；构建/测试遵守根 AGENTS 的前台日志规则。

```powershell
build/bc-target/release/bc-listen.exe --list-devices
build/bc-target/release/bc-listen.exe --url 'https://host:9443/bc/listen?publisher=station&stream=news&channel=all&format=opus' --reconnect
```

- `--device ID`：选择枚举结果中的完整 ID；省略时使用系统默认设备。不存在则报错。
- `--frames N`：限定当前订阅的帧数；省略时持续读取。
- `--check`：解码检查，不开设备；JSON 报告布局、帧数、峰值及 Opus PLC 帧数。
- `--pcm-out FILE`：仅用于检查，导出解码后的交错 float32 little-endian 样本；每次订阅覆盖文件。
- `--reconnect`：网络断连、响应结束或 503 后按 1/2/4/8/16/30 秒退避重订阅，重新读取音频头并创建设备流。其他 HTTP、格式和设备错误退出。检查帧数和报告不跨不同订阅合并。Ctrl+C 停止。

URL 的 publisher/stream 必填，channel 默认 all，format 默认 opus；format=wav 选择 PCM16。
channel 可为 M 或 L/R/C/LFE/Ls/Rs/Lrb/Rrb，依实际布局选择。不跳过 HTTPS 证书校验。

保留原声道数/采样率，无自动 downmix 或重采样；设备须支持该布局/速率的 float32 输出。
设备/WAV 使用 Windows 顺序：5.1 为 L,R,C,LFE,Ls,Rs；7.1 为 L,R,C,LFE,Lrb,Rrb,Ls,Rs。
队列上限 100ms，预缓冲目标 40ms；缺数据静音，设备持续不消费则报错。

浏览器原生控件、人工听音、人工界面验收按用户指令跳过，未记 PASS。
设备回调测试不替代真实环绕声硬件或浏览器播放证据。
