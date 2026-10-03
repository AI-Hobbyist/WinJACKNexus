# S5 Windows 实窗视觉抽查

2026-10-03，验收进行中。当前截图来自系统字体、可见 Windows 实窗的桌面像素读取；未使用 offscreen。旧 QScreen 抓取记录仅保留历史用途。旧大矩阵已停止，当前改为最多 100 次显示的独立批次，本记录不能作为 S5 完成声明。

最新已查看 `20261003T143902908Z-scale-2-graphic_eq_limiter_delay_parametric_eq_reverb-en-US/graphic_eq-load-dialog-en-US-0-0.png` 与 `20261003T143952477Z-scale-2-gate_graphic_eq_deesser_compressor_limiter_delay_parametric_eq_reverb-zh-CN/graphic_eq-load-dialog-zh-CN-0-0.png`：200% 冷启动分别显示 Size 与“大小”，加载/取消及 EQ 预设过滤可读，含日文/韩文的文件名草稿及选区保留；截图为实际测试对话框，无黑图或其他应用画面。原生目录项按列宽省略，未将目录名称当作待翻译 UI。

最新纠正：旧桌面抓取在一张 150% EQ 截图中读到了 LMMS 界面，Qt visible/exposed 本身不足以证明截图归属。旧协议的截图记录仅保留历史用途，不计入最终视觉覆盖。新协议使用测试窗口临时置顶、HWND 客户区物理坐标、五点窗口归属检查与实际桌面像素读取；抓取后恢复原置顶状态。其余应用及音频进程保持运行。

新协议已实际查看 `20261003T135344830Z-scale-1.5-graphic_eq-zh-CN/` 的加载对话框、预设菜单与数字草稿：对应 EQ 内容可读，无其他应用画面，中文“大小”正确，数字草稿 `1.` 的选区保留。已查看 `20261003T135552421Z-scale-2-graphic_eq-hot/graphic_eq-load-dialog-zh-CN-0-0.png`，200% 下同样正确显示“大小”、打开/取消及完整文件名草稿。三个初步 EQ 补验批次仅存本地诊断，未发布误抓到的 LMMS 画面。

已查看新协议 `20261003T142123219Z-scale-2-parametric_eq-hot/parametric_eq-node-zh-CN-0-0.png`：200% 首块的作用声道、模块名称、说明、增益轴及频谱图例可读，无其他应用画面。窗口超过桌面宽高，右侧与下方内容由同批其余分块覆盖，首块边缘的截断属于分块边界，不将首块当作整窗证据。

旧 `S5-rendered` 中已发现原生菜单黑图，不能作为菜单视觉完成证据。新 `S5-bounded-rendered` 使用可见桌面区域抓取，保持原生弹窗位置及可见性；截图前恢复文件名对应语言。已实际查看：150% Gate Japanese 预设菜单显示三条日文预设；200% Gate Japanese 操作菜单显示“デフォルトに戻す”；200% EQ Japanese 加载对话框显示日文文件类型、打开/取消按钮及保留的日文/韩文文件名草稿。后两张位于 `20261003T125345493Z-scale-2-gate_graphic_eq-hot/`，未见黑图或文字覆盖；截断的原生目录表项仍按 Qt 原生布局显示省略号。

已查看 `20261003T125815820Z-scale-2-deesser_compressor-hot/compressor-node-ja-JP-0-0.png`：200% 首块中日文声道、模块名、参数及曲线可读，副标题与轴说明按固定区域省略，没有覆盖电源开关或旋钮数值；大窗口其余区域由同批分块截图记录，未将首块视为整窗证据。

200% 后续抽查：`20261003T130314264Z-scale-2-limiter_delay-hot/limiter-node-ko-KR-0-0.png` 及 `-476-0.png` 合并覆盖上下分块，韩文输出上限、未使用字段“미사용”和模式可读；历史括号 Lookahead 保留，不能解读为已实现前视 DSP。`20261003T130429963Z-scale-2-parametric_eq_reverb-hot/reverb-node-ja-JP-0-0.png` 首块的房间类型及参数可读，长说明按区域省略，无覆盖旋钮数值；其余内容仍以同批分块记录为准。

200% 简体中文冷启动抽查：`20261003T131057766Z-scale-2-gate_graphic_eq_deesser_compressor_limiter_delay_parametric_eq_reverb-zh-CN/gate-preset-menu-zh-CN-0-0.png` 显示默认/紧门限/柔和门限，无黑图；同目录 `graphic_eq-load-dialog-zh-CN-0-0.png` 显示中文打开/取消和 EQ 预设筛选，日文/韩文文件名草稿及选区保留，原生表格按可用宽度省略。文件大小列显示“体积”，已列入 S6 术语审校，尚未认定该译文通过。

用户随后明确要求改成“大小”。已查看修正版 `20261003T132219801Z-scale-2-graphic_eq-zh-CN/graphic_eq-load-dialog-zh-CN-0-0.png`：列标题显示“大小”，中文打开/取消及筛选保持，文件名草稿和选区仍保留。该意见仅作为两条中文文件大小译文的确认，其他术语与语言仍待审校。

## 已查看的截图

| 缩放 | 实际截图 | 观察 |
| --- | --- | --- |
| 100% | `S5-rendered/scale-1/hot/gate-node-en-US-0-0.png` | 英文标题、声道、六个参数和侧链说明可读，无相互覆盖 |
| 100% | `S5-rendered/scale-1/hot/graphic_eq-node-ko-KR-0-0.png` | 韩文字形正常，31 段频率与技术单位保留；窄操作说明显示省略号 |
| 100% | `S5-rendered/scale-1/hot/deesser-node-en-US-0-0.png` | 副标题宽度修复后与电源开关分离；中心频率说明省略但不越界 |
| 100% | `S5-rendered/scale-1/hot/compressor-node-ja-JP-0-0.png` | 日文字形正常，固定宽度的副标题、预设/侧链标签省略，参数数值保持 |
| 100% | `S5-rendered/scale-1/hot/limiter-node-ko-KR-0-0.png` | 最终文案修正后再次查看：旧 Lookahead 字段主标签明确显示 미사용（未使用），括号仍标识历史字段；其余韩文、曲线和旋钮无相互覆盖 |
| 100% | `S5-rendered/scale-1/hot/delay-node-ja-JP-0-0.png` | 日文字形正常；声道说明、滤波器/模式等窄区域省略，未覆盖数值控件 |
| 100% | `S5-rendered/scale-1/hot/parametric_eq-node-zh-CN-0-0.png` | 中文频段、曲线图例、滤波器、分析设置均可读；技术频率/Q/单位保留 |
| 100% | `S5-rendered/scale-1/hot/reverb-node-zh-CN-0-0.png` | 中文房间类型、旋钮和分区说明可读，无缺字 |
| 150% | `S5-rendered/scale-1.5/hot/compressor-node-ja-JP-0-0.png` | 日文参数及曲线标签可读，长标签按区域省略 |
| 150% | `S5-rendered/scale-1.5/hot/limiter-node-ko-KR-0-0.png` | 韩文参数、English 对照及曲线可读；模式和开关无相互覆盖 |
| 150% | `S5-rendered/scale-1.5/hot/graphic_eq-load-dialog-ja-JP-0-0.png` | 文件名中的日文/韩文均可读；文件名、目录及 JSON glob 保留。此文件名中的语言标记是测试起始语言，截图在热切换到 English 后拍摄，不能当作日文最终按钮截图 |
| 150% | `S5-rendered/scale-1.5/hot/fx-host-gate-ko-KR-0-0.png` | 宿主及实际内嵌 Gate 均显示韩文；大窗口按可见桌面分块，此图为首块 |

## 修复与尚待验证

De-Esser 英文副标题原先覆盖电源开关，已收紧文字矩形并在实际截图确认。150% tooltip 命中失败已定位为 combinedTransform 包含设备像素缩放；公共控件已转换回逻辑坐标，最小实窗检查通过。测试还等待原生 Qt tooltip 上一轮延迟隐藏结束，保持实际窗口、系统字体和原生动画。

完整三档矩阵、200% 截图、四语言冷启动及回归尚待完成。省略行为允许且不得改写原文，全文提示及热刷新由实际调用点测试验证；本视觉抽查不代替该检查。译文自然度、Gate/Limiter/De-Esser 等专业词语和括号 English 参考形式仍待 S6 用户审校。
