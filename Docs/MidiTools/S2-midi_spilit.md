# midi_spilit S2 完成记录

状态：实现、Release 定向构建、规定离线模型/DLL 与 100% Windows 实窗验证完成。

- 分类 Midi Tools，单类型独立 DLL；固定 MIDI_IN 与 MIDI_OUT_01～16。通道消息按原通道分流，完整系统消息广播；原字节、帧时间和输入顺序不改写。拒绝坏长度/数据/帧/超限 SysEx。
- RT 回调无分配、文件 I/O 或互斥；事件预算 4096、周期帧上限 8192。周期超限/输出失败安排有界 CC64/123/120 释放，失败继续下周期重试，释放失败时暂不启动新音符。改名/关闭前请求回调释放；控制线程最多等 100 ms，服务不可用时无法保证对端接收（真实 JACK 未验证）。
- 只复用公共 ClientNameWidget 侧栏，无独立业务窗口。复用完整固定端口快照和本端/自连接重映射事务；使用实际 JACK 名；失败回滚，部分重连逐项列出，不报完整成功。业务状态不由 backend 替换。
- version=1/clientName JSON 严格校验，未知版本/字段、坏 JSON 和无效名称拒绝且不修改状态；未初始化恢复只修改配置，不加载/启动 JACK。
- 独立四语言 metadata/locate、可选 I18n v1 attach/change/detach，公共侧栏由共享 UI 域热切换；没有新增主题视觉或公开 API。

验证：build/MidiTools-S2-{configure,build,foundation,tests}.log。SplitTests 覆盖所有 16 通道消息类型、系统消息、原字节/时间、无效事件、释放丢包重试、预算溢出、17 端口含自连接/名称冲突/失败回滚/部分恢复模型、真实 DLL 状态恢复/原子拒绝/卸载、真实公共侧栏 Apply/错误与语言切换。FoundationTests 真实加载三个 DLL，不调用已实施节点 init，不依赖 JACK 服务器。

GUI 批次实际显示 1 个可见 Windows 窗口，预算 1/100；QT_QPA_PLATFORM=windows，DPR=1，系统字体 Microsoft YaHei UI。S2-real-window 中四语言原生窗口截图已逐张检查，CJK 正常，没有方块、裁切或布局异常；额外保留韩语拒绝状态截图。无 offscreen 或 QWidget/QImage 重绘作为证据。

人工审校、人工界面/安装验收：已跳过（用户指令）。真实 JACK 客户端打开、实时端口处理及重连未执行；模型和侧栏不等于真实连线验收。这些不阻塞既定离线交付。
