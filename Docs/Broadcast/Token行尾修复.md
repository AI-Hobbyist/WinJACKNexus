# 从配置文件复制 Token 的行尾修复

日期：2026-11-07。

用户从 `build/Remote-Audio-Config-20261107.zip` 的 `publish.token` 复制凭据，保存时报 `Invalid broadcast credential target or token`。检查包内文件（未输出正文）发现其为64字节Token加CRLF，共66字节，无BOM或NUL。

Rust服务端读取Token文件时调用 `trim()`，旧客户端保存凭据时却拒绝首尾换行。隔离测试用虚构Token加CRLF复现相同保存失败。修复后 `Credentials::write` 先去除首尾ASCII空白，再进行现有校验；中间的换行/NUL、空白正文和超长正文仍被拒绝，失败不覆盖已存凭据。Token只写Windows凭据管理器，不写入工程/普通设置或日志。

## 验证

- RED：`build/bc-token-crlf-red.log`，CRLF凭据保存失败。
- Release必要构建：`build/bc-token-crlf-final-build.log`。
- GREEN：`build/bc-token-crlf-final-tests.log`，凭据与广播GUI测试2/2通过，3.40秒。使用临时设置及唯一测试凭据，未读写用户实际凭据；GUI为正常Windows Qt实窗，无offscreen/Computer Use。
- 匿名网络检查：`https://wjn.acgnai.top/bc/publish?publisher=diagnostic` 返回401和 `unauthorized`，`/bc/listen` 返回400和 `invalid_publisher`；DNS/HTTPS及这两个路由的反代可达。`/bc` 本身返回404不表示控制端点不可用。
- 带真实Token的远端检查首次被自动审批拒绝，未执行；用户随后明确授权后重新执行。对指定域名 `/bc/publish` 的非WebSocket请求返回400和 `websocket_required`，证明去掉行尾的包内Token通过认证。只输出状态与协议错误码，未显示Token、建立会话或发布流。真实WebSocket升级/UDP音频端到端尚未验证。

## 替换与使用

保存工程并正常退出程序后，备份主程序目录的 `broadcast_runtime.dll`，用修复增量包中的同名DLL覆盖，再启动、重新粘贴Token并保存服务器。服务器地址仍填 `https://wjn.acgnai.top/bc`。

另提供原配置包的无行尾副本，唯一内容变更是去掉 `publish.token` 的CRLF；Token正文不变，无需轮换服务端Token。保留原包，其他配置逐字节保持一致。这是用户本地配置包，不进入Git或公共发布。

未自动替换安装目录，未启动/停止用户应用或音频进程；现场安装、JACK/听音验收按用户指令SKIPPED，不记PASS。
