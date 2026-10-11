# 公开仓库前 S6 最终节点清单

2026-10-11。由现有 CMake 目标与实际 DLL 的 Node v4 metadata 核对，SDK 探针通过 28 个输出：23 个内置模块、3 个 MIDI 重复部署输出及 2 个 SDK 示例。JACK-only 表示子图创建被拒绝；GUI 是声明值，不以创建窗口探测。

| 实际输出路径 | 主图 JACK | 子图 VIRT | 自带 GUI |
| --- | --- | --- | --- |
| nodes/adapter/system_midi_in/mod_system_midi_in.dll | 是 | 否 | 是 |
| nodes/adapter/system_midi_out/mod_system_midi_out.dll | 是 | 否 | 是 |
| nodes/adapter/wasapi_in/mod_wasapi_in.dll | 是 | 否 | 是 |
| nodes/adapter/wasapi_out/mod_wasapi_out.dll | 是 | 否 | 是 |
| nodes/meter/meter_5_1/mod_meter_5_1.dll | 是 | 否 | 是 |
| nodes/meter/meter_7_1/mod_meter_7_1.dll | 是 | 否 | 是 |
| nodes/meter/meter_custom/mod_meter_custom.dll | 是 | 否 | 是 |
| nodes/meter/meter_mono/mod_meter_mono.dll | 是 | 否 | 是 |
| nodes/meter/meter_stereo/mod_meter_stereo.dll | 是 | 否 | 是 |
| nodes/midi_tools/midi_matrix/mod_midi_matrix.dll | 是 | 是 | 是 |
| nodes/midi_tools/midi_spilit/mod_midi_spilit.dll | 是 | 是 | 否 |
| nodes/midi_tools/sf2_loader/mod_sf2_loader.dll | 是 | 否 | 是 |
| nodes/mixer/mod_digital_mixer.dll | 是 | 否 | 是 |
| nodes/native_fx/compressor/mod_compressor.dll | 是 | 是 | 是 |
| nodes/native_fx/deesser/mod_deesser.dll | 是 | 是 | 是 |
| nodes/native_fx/delay/mod_delay.dll | 是 | 是 | 是 |
| nodes/native_fx/gate/mod_gate.dll | 是 | 是 | 是 |
| nodes/native_fx/graphic_eq/mod_graphic_eq.dll | 是 | 是 | 是 |
| nodes/native_fx/limiter/mod_limiter.dll | 是 | 是 | 是 |
| nodes/native_fx/parametric_eq/mod_parametric_eq.dll | 是 | 是 | 是 |
| nodes/native_fx/reverb/mod_reverb.dll | 是 | 是 | 是 |
| nodes/vst/mod_vst_host.dll | 是 | 是 | 是 |
| midi_tools/midi_matrix/mod_midi_matrix.dll | 是 | 是 | 是 |
| midi_tools/midi_spilit/mod_midi_spilit.dll | 是 | 是 | 否 |
| midi_tools/sf2_loader/mod_sf2_loader.dll | 是 | 否 | 是 |
| examples/node/wjn_example_node.dll | 是 | 否 | 否 |
| examples/native_fx/wjn_example_native_fx.dll | 是 | 是 | 是 |
| mod_meterbrigde.dll | 是 | 否 | 是 |

宿主内建：子图为 JACK/VIRT 双能力、自带 GUI 否；Audio In、Audio Out、MIDI In、MIDI Out 都仅 VIRT、自带 GUI 否。子图编辑入口沿用专用图编辑命令，不是模块自定义 GUI。主图外部 JACK 客户端不属于 SDK 模块，不伪造 metadata。

VST 扫描生成/发现的 VST2 x86、VST3 x86、VST2 x64、VST3 x64 条目均继承实际 mod_vst_host.dll 的 JACK/VIRT/GUI 标志及固定模式创建入口。S6 使用既有 .vstinfo/.vst3info schema，通过真实 Host 发现四项，在两种图上下文均可见，VIRT 实际 worker prepare 与五个逻辑端口检查通过；S5/S6 实际四组合的 JACK/VIRT、缓冲和状态独立检查另见日志。

现有输出目录残留 nodes/adapter/mod_adapter.dll、nodes/meter/mod_meterbridge_probe.dll：当前 CMake 已明确取消聚合目标；它们不是当前节点清单，不打包。Root 旧 wjn_example_node.dll 也不替代 examples/node 下的已验证 SDK 示例。旧版本/非法 ABI 在创建入口拒绝，工程记录保留。

重复 MIDI 输出的 DLL 和语言资源已同步，并通过包内字节一致性检查。SDK 示例通过实际加载与 v4 契约检查，SDK 源码和示例不属于应用增量运行包。
