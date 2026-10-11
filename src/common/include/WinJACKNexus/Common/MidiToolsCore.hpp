// Internal MIDI Tools core. No Qt, I/O, allocation or locks in event validation.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <span>
namespace WinJACKNexus::Common::MidiTools {
inline constexpr unsigned channelCount = 16;
inline constexpr unsigned maxEvents = 4096;
inline constexpr unsigned maxSysexBytes = 4096;
inline constexpr unsigned maxFrames = 8192;
inline constexpr unsigned maxResources = 32;
inline constexpr unsigned maxGenerations = 4;
inline constexpr unsigned maxVoicesPerChannel = 64;
inline constexpr unsigned synthBlockFrames = 256;
inline constexpr uint64_t maxPreparedSampleBytes = 512ull * 1024 * 1024;
struct EventView { uint32_t frame {}; std::span<const uint8_t> bytes; };
inline bool validMessage(std::span<const uint8_t> b) noexcept {
    if (b.empty() || b[0] < 0x80 || b.size() > maxSysexBytes) return false;
    const auto status = b[0];
    if (status == 0xf0) {
        if (b.size() < 2 || b.back() != 0xf7) return false;
        for (unsigned i = 1; i + 1 < b.size(); ++i) if (b[i] >= 0x80) return false;
        return true;
    }
    unsigned length {};
    if (status < 0xf0) length = (status >> 4 == 0xc || status >> 4 == 0xd) ? 2 : 3;
    else switch (status) {
    case 0xf1: case 0xf3: length = 2; break;
    case 0xf2: length = 3; break;
    case 0xf6: case 0xf8: case 0xfa: case 0xfb: case 0xfc: case 0xfe: case 0xff: length = 1; break;
    default: return false;
    }
    if (b.size() != length) return false;
    for (unsigned i = 1; i < length; ++i) if (b[i] >= 0x80) return false;
    return true;
}
inline bool validEvent(EventView event, uint32_t frames) noexcept {
    return frames <= maxFrames && event.frame < frames && validMessage(event.bytes);
}
inline uint16_t splitTargets(std::span<const uint8_t> bytes) noexcept {
    if (!validMessage(bytes)) return 0;
    return bytes[0] < 0xf0 ? static_cast<uint16_t>(1u << (bytes[0] & 15)) : 0xffff;
}
inline bool releaseMessage(std::span<const uint8_t> b) noexcept {
    if (!validMessage(b) || b[0] >= 0xf0) return false;
    const auto type = b[0] >> 4;
    return type == 8 || (type == 9 && b[2] == 0) ||
        (type == 11 && ((b[1] == 64 && b[2] < 64) || b[1] == 120 || b[1] == 121 || b[1] == 123));
}
}
