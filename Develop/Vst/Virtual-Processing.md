# VST Node 双模式与内部块协议 v1

本契约只扩展既有 VST Host/VSTProcess 的子图执行边界。Node metadata v4 声明 JACK_NODE、VIRT_NODE 与自带 GUI；扫描生成的 .vstinfo/.vst3info 条目继承 Host 的能力，插件路径、位数、class/shell ID、实例 ID 和既有状态格式保持。

JACK 实例仅由对应位数工作进程的 JACK 回调处理，拒绝 VIRT prepare。VIRT 实例拒绝 INode::init，prepare 使用根提供的采样率和最大块长；工作进程通过 `--virtual-blocks-v1 <mapping>` 接入共享缓冲，不加载或调用 JACK。32 位插件的装载和 class/shell 选择交给 32 位工作进程；保存的 ID 在两种模式中一致。

共享协议在 `VstBlockIpc.hpp` 定义，固定宽度字段、8 字节对齐与无指针事件偏移支持 x86/x64。magic、protocol=1、headerSize、maxFrames 均验证；控制 status 返回 virtualProtocol=1，Host 拒绝未声明此版本的旧工作进程。prepare 最大 65536 帧、每方向最多 64 个音频通道及最多 64 个 MIDI 输入，实际逻辑端口从已加载插件取得；音频 ID 顺序为输入、输出，再 MIDI 输入。既有工作进程不支持 MIDI 输出，此次不新增该功能。

一个实例只拥有一个预分配块。状态严格为 free → pending → processing → complete → free，以 Windows Interlocked 发布/获取；主回调先消费匹配帧数和 generation 的上一已完成块，再提交本块。首块或工作进程未就绪时输出静音；拥塞丢弃当前提交，不等待、不分配、不装载文件/DLL。该异步通路具有至少一块的延迟，工作进程调度迟滞可能增加静音块；本次不增加跨节点延迟补偿。MIDI 在缓冲内复制，最多 1024 事件/65536 字节，容量不足丢弃新事件，稳定按帧偏移及原顺序处理。根 samplePosition 原样送入 VST3 ProcessContext 与 VST2 GetTime，采样率和 prepare 块长来自根。

控制侧 reset 更新 generation，废弃旧输出并重新 prepare 插件；stop 在宿主停止引用后退出该实例工作进程、释放映射和上下文。关闭设置窗口或插件编辑器不停止实例。控制命令与 VIRT DSP 在工作进程内部互斥，主音频回调不取得该锁。管道 I/O 带超时和取消，失败清理仍只终止该实例拥有的工作进程。崩溃沿用既有隔离 bypass，复用映射地址以避免实时侧悬空指针；替换插件/时钟必须在宿主已暂停该实例处理的控制阶段完成。

可选只读诊断导出 `uint64_t wjn_vst_virtual_counter(INode*, uint32_t index)`：0=未就绪丢弃的音频提交数，1=MIDI 丢弃事件数，2=已提交块序号；生命周期内累计。模块销毁使用原 wjn_destroy_node，函数表不承担 DLL 租约。

离线夹具只放在测试输出中；JACK 替身仅部署到测试临时目录，禁止放入交付包或覆盖系统/用户 JACK DLL。最终验证结果与未验证影响见 S5 阶段记录。
