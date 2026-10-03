# S5 Windows 实窗视觉抽查

2026-10-03，验收进行中。截图来自系统字体、Windows 原生窗口的 QScreen 抓取；未使用 offscreen。旧大矩阵已停止，当前改为最多 100 次显示的独立批次，本记录不能作为 S5 完成声明。

旧 `S5-rendered` 中已发现原生菜单黑图，不能作为菜单视觉完成证据。新 `S5-bounded-rendered` 使用可见桌面区域抓取，保持原生弹窗位置及可见性；截图前恢复文件名对应语言。已实际查看：150% Gate Japanese 预设菜单显示三条日文预设；200% Gate Japanese 操作菜单显示“デフォルトに戻す”；200% EQ Japanese 加载对话框显示日文文件类型、打开/取消按钮及保留的日文/韩文文件名草稿。后两张位于 `20261003T125345493Z-scale-2-gate_graphic_eq-hot/`，未见黑图或文字覆盖；截断的原生目录表项仍按 Qt 原生布局显示省略号。

已查看 `20261003T125815820Z-scale-2-deesser_compressor-hot/compressor-node-ja-JP-0-0.png`：200% 首块中日文声道、模块名、参数及曲线可读，副标题与轴说明按固定区域省略，没有覆盖电源开关或旋钮数值；大窗口其余区域由同批分块截图记录，未将首块视为整窗证据。

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
