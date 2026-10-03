/* SPDX-License-Identifier: AGPL-3.0-only */
#pragma once
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/Common/MeterLoudnessPresetCatalog.hpp>
#include <WinJACKNexus/Meter/MeterViewSnapshot.hpp>
#include <cstring>

namespace WinJACKNexus::Meter::Mod::Localized {
inline QString presetName(const Common::MeterLoudnessPreset& preset) {
    if (std::strcmp(preset.id, "spotify-default") == 0) return UI::localizedText("meterbridge", "preset.spotify-default", "Music - Spotify Default (-14 LUFS)");
    if (std::strcmp(preset.id, "spotify-loud") == 0) return UI::localizedText("meterbridge", "preset.spotify-loud", "Music - Spotify Loud (-11 LUFS)");
    if (std::strcmp(preset.id, "spotify-quiet") == 0) return UI::localizedText("meterbridge", "preset.spotify-quiet", "Music - Spotify Quiet (-19 LUFS)");
    if (std::strcmp(preset.id, "apple-music") == 0) return UI::localizedText("meterbridge", "preset.apple-music", "Music - Apple Music (-16 LUFS)");
    if (std::strcmp(preset.id, "apple-music-dolby-atmos") == 0) return UI::localizedText("meterbridge", "preset.apple-music-dolby-atmos", "Music - Apple Music Dolby Atmos (-18 LUFS)");
    if (std::strcmp(preset.id, "youtube-music") == 0) return UI::localizedText("meterbridge", "preset.youtube-music", "Music - YouTube Music (-14 LUFS)");
    if (std::strcmp(preset.id, "tidal") == 0) return UI::localizedText("meterbridge", "preset.tidal", "Music - Tidal (-14 LUFS)");
    if (std::strcmp(preset.id, "amazon-music") == 0) return UI::localizedText("meterbridge", "preset.amazon-music", "Music - Amazon Music (-14 LUFS)");
    if (std::strcmp(preset.id, "deezer") == 0) return UI::localizedText("meterbridge", "preset.deezer", "Music - Deezer (-15 LUFS)");
    if (std::strcmp(preset.id, "soundcloud") == 0) return UI::localizedText("meterbridge", "preset.soundcloud", "Music - SoundCloud (suggested -14 LUFS)");
    if (std::strcmp(preset.id, "netease-cloud-music") == 0) return UI::localizedText("meterbridge", "preset.netease-cloud-music", "Music - NetEase Cloud Music (-14 to -12 LUFS)");
    if (std::strcmp(preset.id, "qq-music") == 0) return UI::localizedText("meterbridge", "preset.qq-music", "Music - QQ Music (-14 to -12 LUFS)");
    if (std::strcmp(preset.id, "youtube-main") == 0) return UI::localizedText("meterbridge", "preset.youtube-main", "Video/social - YouTube Main (-14 LUFS)");
    if (std::strcmp(preset.id, "bilibili") == 0) return UI::localizedText("meterbridge", "preset.bilibili", "Video/social - Bilibili (-14 LUFS)");
    if (std::strcmp(preset.id, "tiktok") == 0) return UI::localizedText("meterbridge", "preset.tiktok", "Video/social - TikTok (-14 to -12 LUFS)");
    if (std::strcmp(preset.id, "instagram-reels") == 0) return UI::localizedText("meterbridge", "preset.instagram-reels", "Video/social - Instagram Reels (-14 LUFS)");
    if (std::strcmp(preset.id, "twitch") == 0) return UI::localizedText("meterbridge", "preset.twitch", "Video/social - Twitch (-14 to -12 LUFS)");
    if (std::strcmp(preset.id, "netflix-dialog-gated") == 0) return UI::localizedText("meterbridge", "preset.netflix-dialog-gated", "Video/social - Netflix Dialog-Gated (-27 LUFS)");
    if (std::strcmp(preset.id, "disney-hbo-max") == 0) return UI::localizedText("meterbridge", "preset.disney-hbo-max", "Video/social - Disney+ / HBO Max (-24 to -27 LUFS)");
    if (std::strcmp(preset.id, "ebu-r128") == 0) return UI::localizedText("meterbridge", "preset.ebu-r128", "Broadcast - EBU R 128 (-23 LUFS)");
    if (std::strcmp(preset.id, "atsc-a85") == 0) return UI::localizedText("meterbridge", "preset.atsc-a85", "Broadcast - ATSC A/85 (-24 LKFS)");
    if (std::strcmp(preset.id, "arib-tr-b32") == 0) return UI::localizedText("meterbridge", "preset.arib-tr-b32", "Broadcast - ARIB TR-B32 (-24 LUFS)");
    if (std::strcmp(preset.id, "gy-t-262-2012") == 0) return UI::localizedText("meterbridge", "preset.gy-t-262-2012", "Broadcast - GY/T 262-2012 (-24 LUFS)");
    if (std::strcmp(preset.id, "aes-td1004") == 0) return UI::localizedText("meterbridge", "preset.aes-td1004", "Broadcast - AES TD1004 (-16 LUFS)");
    if (std::strcmp(preset.id, "aaa-game") == 0) return UI::localizedText("meterbridge", "preset.aaa-game", "Games - Console/PC AAA (-24 LUFS)");
    if (std::strcmp(preset.id, "casual-mobile-game") == 0) return UI::localizedText("meterbridge", "preset.casual-mobile-game", "Games - Casual/mobile (-16 to -14 LUFS)");
    if (std::strcmp(preset.id, "vr-ar-spatial") == 0) return UI::localizedText("meterbridge", "preset.vr-ar-spatial", "Games - VR / AR spatial audio (-18 LUFS)");
    if (std::strcmp(preset.id, "streaming-14") == 0) return UI::localizedText("meterbridge", "preset.streaming-14", "Compatibility - Streaming -14 LUFS");
    if (std::strcmp(preset.id, "custom") == 0) return UI::localizedText("meterbridge", "preset.custom", "Custom reference");
    return QString::fromUtf8(preset.displayName);
}
inline QString layoutName(const QString& fallback, std::size_t count, bool custom) {
    if (count == 0) return fallback;
    if (custom) return UI::localizedText("meterbridge", "layout.custom", "Custom %1").arg(count);
    switch (count) {
    case 1: return UI::localizedText("meterbridge", "layout.mono", "Mono");
    case 2: return UI::localizedText("meterbridge", "layout.stereo", "Stereo");
    case 6: return QStringLiteral("5.1");
    case 8: return QStringLiteral("7.1");
    default: return UI::localizedText("meterbridge", "layout.multichannel", "Multichannel %1").arg(count);
    }
}
inline QString layoutName(const MeterViewSnapshot& snapshot) {
    return layoutName(snapshot.layoutLabel, snapshot.channels.size(), snapshot.channelLayoutEditable);
}
}
