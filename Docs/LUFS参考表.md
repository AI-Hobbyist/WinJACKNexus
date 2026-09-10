# 全球主流平台及行业标准响度（LUFS）参考表

> **说明**：
> 1. **Integrated LUFS**：整体集成响度（全曲/全视频的平均响度）。低于该值的音频会被平台适度拉高或保持，高于该值的音频会被平台强制下拉（Normalization）以防爆音或响度战争。
> 2. **dBTP (True Peak)**：真实峰值，建议预留绝对安全余量以防止 DAC 解码与 Lossy 编码（如 AAC/MP3）转码时产生数字间峰值失真。

---

### 一、 音乐流媒体平台 (Music Streaming Services)

| 平台名称 | 目标集成响度 (Integrated LUFS) | 允许最大真实峰值 (Max True Peak) | 响度惩罚/处理机制 (Normalization Policy) |
| :--- | :--- | :--- | :--- |
| **Spotify** | **-14 LUFS** (默认) | **-1.0 dBTP** | 超出强制限幅；提供 Loud (-11 LUFS) / Quiet (-19 LUFS) 用户选项 |
| **Apple Music** | **-16 LUFS** | **-1.0 dBTP** | 开启 Sound Check 后生效；Dolby Atmos 空间音频强制要求 **-18 LUFS** |
| **YouTube Music** | **-14 LUFS** | **-1.0 dBTP** | 仅对高响度音频做下压（Normalization），低于目标值不拉高 |
| **Tidal** | **-14 LUFS** | **-1.0 dBTP** | 默认开启响度匹配，高于 -14 LUFS 自动下压 |
| **Amazon Music** | **-14 LUFS** | **-1.0 dBTP** | 自动下压超出部分，建议 True Peak 留足余量 |
| **Deezer** | **-15 LUFS** | **-1.0 dBTP** | 自动下压高响度音频 |
| **SoundCloud** | **-14 LUFS** (无强制限幅) | **-1.0 dBTP** | **不自动压限**，但推荐 -14 LUFS 以防转码 MP3/AAC 时 Clipping 爆音 |
| **网易云音乐 (NetEase)**| **-14 LUFS ~ -12 LUFS** | **-1.0 dBTP** | 移动端默认开启响度平衡，高响度曲目会被自动压低 |
| **QQ音乐 (QQ Music)** | **-14 LUFS ~ -12 LUFS** | **-1.0 dBTP** | 开启“音量均衡”后自动匹配，建议保留足够的动态范围 |

---

### 二、 视频与社交媒体平台 (Video & Social Media)

| 平台名称 | 目标集成响度 (Integrated LUFS) | 允许最大真实峰值 (Max True Peak) | 说明与建议 |
| :--- | :--- | :--- | :--- |
| **YouTube (Main)** | **-14 LUFS** | **-1.0 dBTP** | **只降不升**。高于 -14 LUFS 会被右键“详细统计信息”显示 Volume 缩减 % |
| **Bilibili (哔哩哔哩)** | **-14 LUFS** | **-1.0 dBTP** | 网页端与 App 均已引入响度均衡，建议视频总音轨控制在 -14 LUFS 左右 |
| **TikTok / 抖音** | **-14 LUFS ~ -12 LUFS** | **-1.0 dBTP** | 移动端短视频环境干扰大，语音/人声推至 -12 LUFS 依然能保持极佳清晰度 |
| **Instagram (Reels)** | **-14 LUFS** | **-1.0 dBTP** | 超过会自动做Peak/Loudness压缩，过高会导致低音抽搐 |
| **Twitch (直播)** | **-14 LUFS ~ -12 LUFS** | **-2.0 dBTP** | 实时直播流，建议人声核心处在 -14 LUFS，预留 -2 dBTP 容错率防止实时爆音 |
| **Netflix (网飞)** | **-27 LUFS (±1 LU)** | **-2.0 dBTP** | 极其严苛的对话响度标准（Dialog-Gated），针对 5.1 / 7.1 电影混音 |
| **Disney+ / HBO Max**| **-24 LUFS ~ -27 LUFS** | **-2.0 dBTP** | 广电/影院级高动态范围标准 |

---

### 三、 广电与国际工业标准 (Broadcast & Cinema Standards)

| 规范名称 | 适用领域 / 地区 | 目标集成响度 (Integrated LUFS/LKFS) | 最大 True Peak |
| :--- | :--- | :--- | :--- |
| **EBU R128** | 欧洲广播电视 / 广电标准 | **-23.0 LUFS** (±0.5 LU) | **-1.0 dBTP** |
| **ATSC A/85** | 美国电视广播 (CALM Act) | **-24.0 LKFS** (±1.0 LKFS) | **-2.0 dBTP** |
| **ARIB TR-B32** | 日本广播协会 (NHK / 日本广电) | **-24.0 LUFS** | **-1.0 dBTP** |
| **GY/T 262-2012** | 中国国家广播电影电视总局规范 | **-24.0 LUFS** | **-1.0 dBTP** |
| **AES TD1004** | 互联网音频流媒体通用建议 | **-16.0 LUFS** | **-1.0 dBTP** |

---

### 四、 游戏与交互式音频 (Games & Interactive Audio)

| 游戏类型 / 平台 | 目标集成响度 (Integrated LUFS) | 允许最大真实峰值 (Max True Peak) | 说明 (PS5 / Xbox 推荐) |
| :--- | :--- | :--- | :--- |
| **主机/PC 重度游戏 (3A)**| **-24 LUFS** (Master) | **-1.5 dBTP** | 留足大动态范围（如爆炸、枪械与宏大交响乐） |
| **休闲/手游 (Mobile)** | **-16 LUFS ~ -14 LUFS** | **-1.0 dBTP** | 针对手机扬声器优化，动态范围较窄，确保环境噪音下能听清 |
| **VR / AR 空间音频** | **-18 LUFS** | **-2.0 dBTP** | 避免头部转动时声场定位变化导致局部通道 Clipping |