# S6 探测错误显示协议

2026-10-04。内部 IPC 的增量显示字段，不改变 Node ABI 或插件信息文件格式。

失败结果保留 `ok=false`、`skippable` 和 `error`；部分探测成功保留 `ok=true`、`classes` 和可选 `probe_error`。项目自身错误的原文字段提供完整 English 回退，并附加同名前缀的 `_key` 和 `_args`。例如：

```json
{"ok":false,"skippable":false,"error":"LoadLibraryExW failed","error_key":"probe.loadLibrary","error_args":[]}
```

参数保持原始插件名称或 class ID，不作为翻译内容。VST3 SDK 返回的原始诊断只填写原文字段，不伪造项目 key。旧消费端可忽略新增字段；新消费端遇到旧探测器、未知 key 或参数数量/类型不合规时，使用原文字段。Scanner 白名单中的十个 key 调用请求内 `scannerText`，无翻译服务时回退 English。原 `ok`、`skippable` 和 `probe_error` 判定继续决定扫描状态，译文不参与业务判断。

构建证据：`S6-probe-build.log`。Probe32/Probe64 各一次离线缺失 DLL 检查：`S6-probe-tests.log`、`S6-probe-verification.json`。该结果只证明 VST2 加载失败分支的真实 Release IPC；其他运行分支及 Scanner 实际译文显示未验证，未重复其 UI 测试。

四语言草稿和占位符/部署文件核对：`S6-scanner-strings.tsv`、`S6-scanner-verification.json`。人工审校仍待用户意见。
