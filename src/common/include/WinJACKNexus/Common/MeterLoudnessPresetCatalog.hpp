#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace WinJACKNexus::Common
{

struct MeterLoudnessPreset final
{
    const char *id;
    const char *displayName;
    double targetIntegratedLufs;
    std::optional<double> alternateTargetHighLufs;
    double maximumTruePeakDbtp;
};

inline constexpr std::array meterLoudnessPresetCatalog{
    MeterLoudnessPreset{"spotify-default", "音乐 - Spotify Default (-14 LUFS)", -14.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"spotify-loud", "音乐 - Spotify Loud (-11 LUFS)", -11.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"spotify-quiet", "音乐 - Spotify Quiet (-19 LUFS)", -19.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"apple-music", "音乐 - Apple Music (-16 LUFS)", -16.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"apple-music-dolby-atmos", "音乐 - Apple Music Dolby Atmos (-18 LUFS)",
                        -18.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"youtube-music", "音乐 - YouTube Music (-14 LUFS)", -14.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"tidal", "音乐 - Tidal (-14 LUFS)", -14.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"amazon-music", "音乐 - Amazon Music (-14 LUFS)", -14.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"deezer", "音乐 - Deezer (-15 LUFS)", -15.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"soundcloud", "音乐 - SoundCloud (建议 -14 LUFS)", -14.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"netease-cloud-music", "音乐 - 网易云音乐 (-14 至 -12 LUFS)", -14.0, -12.0,
                        -1.0},
    MeterLoudnessPreset{"qq-music", "音乐 - QQ 音乐 (-14 至 -12 LUFS)", -14.0, -12.0, -1.0},
    MeterLoudnessPreset{"youtube-main", "视频/社交 - YouTube Main (-14 LUFS)", -14.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"bilibili", "视频/社交 - Bilibili (-14 LUFS)", -14.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"tiktok", "视频/社交 - TikTok / 抖音 (-14 至 -12 LUFS)", -14.0, -12.0,
                        -1.0},
    MeterLoudnessPreset{"instagram-reels", "视频/社交 - Instagram Reels (-14 LUFS)", -14.0,
                        std::nullopt, -1.0},
    MeterLoudnessPreset{"twitch", "视频/社交 - Twitch (-14 至 -12 LUFS)", -14.0, -12.0, -2.0},
    MeterLoudnessPreset{"netflix-dialog-gated", "视频/社交 - Netflix Dialog-Gated (-27 LUFS)",
                        -27.0, std::nullopt, -2.0},
    MeterLoudnessPreset{"disney-hbo-max", "视频/社交 - Disney+ / HBO Max (-24 至 -27 LUFS)", -27.0,
                        -24.0, -2.0},
    MeterLoudnessPreset{"ebu-r128", "广播 - EBU R 128 (-23 LUFS)", -23.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"atsc-a85", "广播 - ATSC A/85 (-24 LKFS)", -24.0, std::nullopt, -2.0},
    MeterLoudnessPreset{"arib-tr-b32", "广播 - ARIB TR-B32 (-24 LUFS)", -24.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"gy-t-262-2012", "广播 - GY/T 262-2012 (-24 LUFS)", -24.0, std::nullopt,
                        -1.0},
    MeterLoudnessPreset{"aes-td1004", "广播 - AES TD1004 (-16 LUFS)", -16.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"aaa-game", "游戏 - 主机/PC 重度游戏 (-24 LUFS)", -24.0, std::nullopt,
                        -1.5},
    MeterLoudnessPreset{"casual-mobile-game", "游戏 - 休闲/手游 (-16 至 -14 LUFS)", -16.0, -14.0,
                        -1.0},
    MeterLoudnessPreset{"vr-ar-spatial", "游戏 - VR / AR 空间音频 (-18 LUFS)", -18.0, std::nullopt,
                        -2.0},
    MeterLoudnessPreset{"streaming-14", "兼容 - 流媒体 -14 LUFS", -14.0, std::nullopt, -1.0},
    MeterLoudnessPreset{"custom", "自定义参考", -23.0, std::nullopt, -1.0}};

inline constexpr const MeterLoudnessPreset *findMeterLoudnessPreset(std::string_view id) noexcept
{
    for (const auto &preset : meterLoudnessPresetCatalog)
    {
        if (id == preset.id)
            return &preset;
    }
    return nullptr;
}

inline constexpr bool isKnownMeterLoudnessPreset(std::string_view id) noexcept
{
    return findMeterLoudnessPreset(id) != nullptr;
}

}