/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#pragma once

#include <WinJACKNexus/Common/ChannelLayout.hpp>
#include <WinJACKNexus/Common/Export.hpp>
#include <WinJACKNexus/Common/MeterSnapshot.hpp>
#include <WinJACKNexus/Common/RealtimeObject.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace WinJACKNexus::Common
{

struct MeterProbeChannelDescriptor final
{
    std::string name;
    MeterChannelRole role{MeterChannelRole::unassigned};
    bool lfe{};
};

struct MeterProbeDescriptor final
{
    std::string instanceId;
    std::string clientName;
    std::string presetId{"ebu-r128"};
    std::array<MeterProbeChannelDescriptor, MeterChannelLayout::maximumChannelCount> channels{};
    std::size_t channelCount{};
    double sampleRate{48000.0};
    bool channelLayoutEditable{};
    bool followGlobalPreset{true};
    float silenceThresholdDbfs{-70.0F};
};

struct MeterProbeRealtimeFrame final
{
    MeterSnapshot meter;
};

class WINJACK_COMMON_API MeterProbeRealtimeState final
{
  public:
    void publish(const MeterSnapshot &snapshot) noexcept;
    MeterProbeRealtimeFrame frame() const noexcept;

    void setOnline(bool online) noexcept;
    bool isOnline() const noexcept;

    void setAutomaticResetEnabled(bool enabled) noexcept;
    bool automaticResetEnabled() const noexcept;

    void setSilenceResetConfiguration(float thresholdDbfs, float durationSeconds) noexcept;
    float silenceThresholdDbfs() const noexcept;
    float silenceDurationSeconds() const noexcept;

    void requestLoudnessReset() noexcept;
    bool consumeLoudnessResetRequest(std::uint64_t &observedGeneration) noexcept;

    void notifyLoudnessReset() noexcept;
    std::uint64_t loudnessResetGeneration() const noexcept;

  private:
    mutable RealtimeObject<MeterProbeRealtimeFrame> frame_;
    std::atomic_bool online_{false};
    std::atomic_bool automaticResetEnabled_{true};
    std::atomic<float> silenceThresholdDbfs_{-70.0F};
    std::atomic<float> silenceDurationSeconds_{3.0F};
    std::atomic_uint64_t resetGeneration_{0};
    std::atomic_uint64_t completedResetGeneration_{0};
};

struct MeterProbeRegistration final
{
    MeterProbeDescriptor descriptor;
    std::shared_ptr<MeterProbeRealtimeState> realtimeState;
    std::uint64_t nameRevision{};
};

WINJACK_COMMON_API bool registerMeterProbe(
    MeterProbeDescriptor descriptor,
    std::shared_ptr<MeterProbeRealtimeState> realtimeState) noexcept;
WINJACK_COMMON_API void unregisterMeterProbe(
    std::string_view instanceId,
    const std::shared_ptr<MeterProbeRealtimeState> &realtimeState) noexcept;
WINJACK_COMMON_API std::vector<MeterProbeRegistration> meterProbeRegistrations();

}