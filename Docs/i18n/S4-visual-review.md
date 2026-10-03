# S4 实窗视觉抽查

日期：2026-10-03。图像来自实际 Windows 原生窗口，系统字体 Microsoft YaHei UI；检查了下列样本，未声称逐张人工检查全部 996 张截图。

| 图像（S4-rendered 下） | 实际观察 |
| --- | --- |
| scale-1/sel-ja-JP.png | 日语 SEL 控件、Sub Mix 页签正常；超长用户标题省略，编辑内容保持原始名称。 |
| scale-2/probe-4-ja-JP.png | 日语自定义探针表单及角色正常；过长角色省略，端口 ID 与用户名称原样保留。 |
| scale-1.5/card-ko-KR-tile-0-0.png | 韩语卡片标签及技术单位正常，预设/标题过长时省略。 |
| scale-2/submix-ja-JP-tile-0-0.png | 日语 Sub Mix 操作与声像说明正常，已保存总线名称原样保留。 |
| scale-1/history-ja-JP-tile-0-0.png、history-ja-JP-tile-1088-0.png | 图表、图例及统计区域正常，完整说明由 tooltip 检查补充。 |
| scale-2/overview-en-US-tile-512-896.png | 英文总览布局与电平/推子区域正常；技术标识保持原样。 |
| scale-2/fx-ko-KR-tile-0-896.png | 韩语 FX 宿主链操作和预设管理正常；内部 PEQ 编辑器仍含中文，归 S5。 |
| scale-1/fx-preset-hot-en-US.png | 热切换后实际显示日语；用户预设名保留，按钮及表单同步。 |
| scale-1/mixer-error-hot-ja-JP.png | 热切换后实际显示英文；诊断显示副本同步。 |
| scale-1/png-chooser-hot-zh-CN.png | 热切换后实际显示英文；用户输入 chart-zh-CN.png 保留，PNG 文件类型及接受按钮同步。系统侧栏的机器名称不翻译。 |

模态文件名中的语言为切换前语言，图中语言为切换后语言。实际 PNG 输出由测试读取验证。窗口在高 DPI 超出桌面时采用同一可见窗口的分区图，屏幕外白色区域不作为完成证据。自动断言补充四语言冷启动、热切换、重开、字形与状态保持；译文语言质量仍需 S6 人工母语/领域审校。
