# S0 术语与语义基线

2026-10-03。English 是后续语言包基线；下表是词汇建议，不是母语/专业人工审校通过记录。日语、韩语译文沿用计划书示例时也仍为待审校。

| 语义 key 后缀 | 简体中文 | English | 日本語（待审校） | 한국어（待审校） | 语义与边界 |
| --- | --- | --- | --- | --- | --- |
| view.patchbay | 接线盘 | Patchbay | パッチベイ | 패치베이 | 路由界面 |
| view.mixer | 调音台 | Mixer | ミキサー | 믹서 | 不作为模块身份 |
| view.meterbridge | 电平表桥 | Meter bridge | メーターブリッジ | 미터 브리지 | 测量显示 |
| control.inputGain | 增益 | Gain | ゲイン | 게인 | 输入增益，区别推子 |
| control.faderVolume | 音量 | Volume | 音量 | 음량 | 推子音量 |
| control.threshold | 阈值 | Threshold | スレッショルド | 임계값 | 按各效果检测语义，单位不译 |
| control.ratio | 比率 | Ratio | レシオ | 비율 | 压缩/扩展比，方向按效果说明 |
| control.knee | 拐点 | Knee | ニー | 니 | Soft Knee 为软拐点宽度 |
| control.attack | 启动时间 | Attack time | アタックタイム | 어택 타임 | 动态响应时间，ms |
| control.release | 释放时间 | Release time | リリースタイム | 릴리스 타임 | 不译为发行；Release 构建保留 |
| control.holdTime | 保持时间 | Hold time | ホールドタイム | 홀드 시간 | 时间参数；不适用于当前 Gate holdDb |
| control.hysteresis | 滞回 | Hysteresis | ヒステリシス | 히스테리시스 | Gate 开关门限差，dB，见身份风险文档 |
| control.floor | 衰减下限 | Floor | 減衰下限 | 감쇠 하한 | Gate 最大衰减下限，不能误称测得底噪 |
| control.makeupGain | 补偿增益 | Makeup gain | メイクアップゲイン | 메이크업 게인 | 动态处理输出增益 |
| control.lookahead | 前视 | Lookahead | ルックアヘッド | 룩어헤드 | 真实时间前视；当前 Limiter 字段未实现，另记录 |
| control.sidechain | 侧链 | Sidechain | サイドチェーン | 사이드체인 | 检测路径，区别音频输出滤波 |
| control.band | 频带 | Band | バンド | 밴드 | EQ/De-Esser |
| control.frequency | 频率 | Frequency | 周波数 | 주파수 | Hz/kHz 原样 |
| control.q | Q 值 | Q factor | Q 値 | Q 값 | Q 标识保留，解释翻译 |
| control.bypass | 旁路 | Bypass | バイパス | 바이패스 | 旁路状态，不等于删除模块 |
| control.all | 全部 | All | すべて | 전체 | 编辑结果广播到八声道，未编辑参数独立 |
| control.preDelay | 预延迟 | Pre-delay | プリディレイ | 프리딜레이 | 混响前延迟 |
| control.decay | 衰减时间 | Decay time | ディケイタイム | 감쇠 시간 | 具体混响参数单位核对 |
| control.wet | 湿声 | Wet | ウェット | 웨트 | 处理信号，按控件区分电平与比例 |
| control.dry | 干声 | Dry | ドライ | 드라이 | 原始信号，按控件区分电平与比例 |
| control.feedback | 反馈 | Feedback | フィードバック | 피드백 | 延迟回授比例 |
| output.monitor | 监听 | Monitor | モニター | 모니터링 | 监听输出 |
| output.master | 主输出 | Master output | マスター出力 | 마스터 출력 | MAIN 不被其他通道 Solo 静音 |
| listen.pfl.help | 输入增益后、FX/静音/推子前监听 | Monitor after input gain, before FX, mute and fader. | 待翻译审校 | 待翻译审校 | DigitalMixerProcessor.cpp:516–517 |
| listen.afl.help | 推子后监听，受静音与推子影响 | Monitor after the fader, affected by mute and fader. | 待翻译审校 | 待翻译审校 | 取 post；按钮 AFL 原样 |
| listen.sip.help | 就地独奏，会改变混音输出 | Solo in place; changes the mix output. | 待翻译审校 | 待翻译审校 | 破坏性监听，MAIN 例外 |
| route.subMix | 分组混音 | Sub Mix | サブミックス | 서브 믹스 | 区别 AUX 发送 |
| meter.peakHold | 峰值保持 | Peak hold | ピークホールド | 피크 홀드 | 区别动态处理 Hold |
| meter.clipping | 削波 | Clipping | クリッピング | 클리핑 | 超限状态 |
| meter.truePeak | 真峰值 | True peak | トゥルーピーク | 트루 피크 | dBTP 原样 |
| meter.momentary | 瞬时响度 | Momentary loudness | モーメンタリーラウドネス | 순간 라우드니스 | 响度时间窗口 |
| meter.shortTerm | 短时响度 | Short-term loudness | ショートタームラウドネス | 단기 라우드니스 | 区别瞬时 |
| meter.integrated | 综合响度 | Integrated loudness | 統合ラウドネス | 통합 라우드니스 | 累计测量 |
| meter.loudnessRange | 响度范围 | Loudness range | ラウドネスレンジ | 라우드니스 범위 | LRA 标识与说明分别处理 |

## key、参数与审校规则

key 按所属模块域加语义后缀，例如 `nativefx.gate / control.hysteresis`。同词不同含义分 key；不同控件若语义和占位参数一致可共享。源代码扫描的 `s0.<hash>` 是证据 ID，不能当作公开语义 key。

抽取时记录原始 `%1/%2` 等占位符；正式 formatter 的 `{name}/{count}/{path}` 转换在 S1 冻结之后实施。English one/other；中日韩 other。HTML 用户参数须在调用点转义。音频回调不查翻译。

控件宽度和换行约束在逐控件 key 清单中记录：原生菜单自动度量；共享布局按实际字体计算；自绘固定 rect 必须保留 rect/hit rect 证据，四语言高 DPI 测量与省略 tooltip 在实施阶段验证。S0 不凭源字符串长度宣称排版通过。
