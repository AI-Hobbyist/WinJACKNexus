# 广播与独立远程音频服务端

## 当前能力与后续定位

独立服务端是WinJACKNexus远程音频功能的公共后端，目前已实现广播发布与转发。后续计划扩展其他涉及远程音频的功能，这些能力尚未实现，不作为当前可用功能宣传。当前可执行文件名仍为`bc-server`。

客户端是Windows/JACK-only节点，服务端则是独立Rust程序，可脱离WinJACKNexus、Qt、JACK及音频设备运行。独立 **Build standalone broadcast server** Action提供Windows x64、Linux x64、Linux ARM64构建；以实际Actions运行确认各平台结果。新工作流需先进入默认分支，才可从Actions页面手动触发。

## 客户端使用

1. 添加`nodes/lan_remote_audio/`中的Broadcast Sender节点并打开独立窗口。
2. 建立分类或多级子分类，添加Mono、Stereo、5.1或7.1流，设置名称。
3. 将要广播的源连接到该流输入。每条流有同布局的本地监听输出，Listen默认关闭。
4. 填写全局服务器地址、发布者ID、Token，保存服务器后显式开始推流。所有广播窗口共用一组连接配置，Token仅存Windows Credential Manager，不写工程。
5. 每轨只有一个总体峰值电平表；增益平滑作用一次，M静音广播及监听，S仅影响本节点监听。关闭窗口不停止处理或推流。
6. 选择流、完整流`all`或分声道以及WAV/Opus，复制匿名收听地址。

## 复制部署与JSON配置

下载对应平台的服务端程序，将`server.example.json`复制为`server.json`，在同目录放入自己的UTF-8 `publish.token`。服务器无需Python、Qt或JACK。

```json
{
  "bind": "127.0.0.1:9443",
  "udp_bind": "0.0.0.0:9444",
  "publish_token_file": "publish.token",
  "tls_cert": null,
  "tls_key": null,
  "insecure_loopback": false
}
```

Linux启动：

```sh
./bc-server --config ./server.json
```

Windows启动：

```powershell
& ./bc-server.exe --config ./server.json
```

所有原有启动参数均可写入JSON。显式命令行参数优先覆盖配置，不受参数顺序影响；JSON中的文件路径按配置文件目录解析，CLI中的文件路径仍按启动目录解析。`publish_token_file`必填，JSON未知字段、CLI重复参数和缺失值均报错。

未指定时，HTTP和UDP绑定分别为`0.0.0.0:9443`和`0.0.0.0:9444`。旧`--insecure-loopback`开发开关仍只允许回环绑定，也可用`true`/`false`显式覆盖JSON值。普通无证书HTTP/WS模式不需要这个开发开关。

## 可选TLS与NGINX

- 证书和私钥都省略或为null：服务端提供HTTP/WS，适合作为NGINX后端。
- 同时配置`tls_cert`和`tls_key`：服务端直接提供HTTPS/WSS。
- 只配置一个，或提供了无效证书文件：启动失败，不静默退回HTTP。

NGINX终止外部TLS时，客户端仍连接公开的`https://stream.example.com/bc`。将服务端随包`nginx.example.conf`的map/server块适配到现有NGINX http配置，修改域名及NGINX已有证书路径。该示例保留`/bc/`路径、转发WebSocket Upgrade，并关闭实时音频响应缓冲。

**UDP 9444必须独立到达与控制连接相同的公网主机，不能通过HTTP反代转发。** 同机部署时NGINX监听TCP 443，relay监听本机TCP 9443和对外UDP 9444；NGINX异机时另配UDP转发。当前UDP负载由会话鉴权和HMAC保护完整性，并未被TLS加密。TLS只保护HTTPS收听和WSS控制链路。

NGINX官方说明：[WebSocket代理](https://nginx.org/en/docs/http/websocket.html)、[响应缓冲](https://nginx.org/en/docs/http/ngx_http_proxy_module.html#proxy_buffering)。本次未运行实际NGINX部署验收。

## 收听

```text
https://stream.example.com/bc/listen?publisher=station&stream=流UUID&channel=all&format=opus
```

`publisher`、`stream`必填；`channel`默认all，可选当前布局中的M或L/R/C/LFE/Ls/Rs/Lrb/Rrb；`format`默认opus，也可为wav。响应分别为`audio/ogg; codecs=opus`或`audio/wav`，不是JSON或自定义网页。听众不需要Token；只有发布控制需要Bearer鉴权。服务端不解码、编码或重采样。

标准播放器或独立`bc-listen`均可使用该URL。浏览器直接打开时显示原生播放控件是兼容目标；浏览器人工播放、人工听音/安装及真实JACK/环绕声硬件验收未记为通过。

## 说明与源码

- [服务端参数与构建说明](https://github.com/AI-Hobbyist/WinJACKNexus/blob/broadcast-system/bc/server/README.md)
- [JSON示例](https://github.com/AI-Hobbyist/WinJACKNexus/blob/broadcast-system/bc/server/server.example.json)
- [NGINX示例](https://github.com/AI-Hobbyist/WinJACKNexus/blob/broadcast-system/bc/server/nginx.example.conf)
- [独立收听工具](https://github.com/AI-Hobbyist/WinJACKNexus/blob/broadcast-system/bc/listen/README.md)
- [主仓库](https://github.com/AI-Hobbyist/WinJACKNexus)

远程音频节点统一归类为 `LAN & Remote Audio`，安装目录为 `nodes/lan_remote_audio/`。旧工程中的 `nodes/bc/mod_broadcast.dll` 在新模块存在时自动迁移；覆盖安装遗留的旧模块不重复列出。网络端点继续使用 `/bc`。
