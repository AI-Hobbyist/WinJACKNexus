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

#include <array>
#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Common {

enum class MeterChannelRole : std::uint8_t {
    unassigned,
    mono,
    frontLeft,
    frontRight,
    frontCenter,
    sideLeft,
    sideRight,
    rearLeft,
    rearRight,
    topFrontLeft,
    topFrontRight,
    topRearLeft,
    topRearRight,
    lfe
};

struct MeterChannel final {
    MeterChannelRole role { MeterChannelRole::unassigned };
    bool lfe {};
};

class MeterChannelLayout final {
public:
    static constexpr std::size_t maximumChannelCount = 16;
    static constexpr double sideChannelWeight = 1.4142135623730950488;

    MeterChannelLayout() noexcept = default;

    explicit MeterChannelLayout(std::size_t channelCount) noexcept
    {
        setChannelCount(channelCount);
    }

    static MeterChannelLayout mono() noexcept
    {
        MeterChannelLayout layout(1);
        layout.setChannel(0, MeterChannelRole::mono);
        return layout;
    }

    static MeterChannelLayout stereo() noexcept
    {
        MeterChannelLayout layout(2);
        layout.setChannel(0, MeterChannelRole::frontLeft);
        layout.setChannel(1, MeterChannelRole::frontRight);
        return layout;
    }

    static MeterChannelLayout fivePointOne() noexcept
    {
        MeterChannelLayout layout(6);
        layout.setChannel(0, MeterChannelRole::frontLeft);
        layout.setChannel(1, MeterChannelRole::frontRight);
        layout.setChannel(2, MeterChannelRole::frontCenter);
        layout.setChannel(3, MeterChannelRole::lfe, true);
        layout.setChannel(4, MeterChannelRole::sideLeft);
        layout.setChannel(5, MeterChannelRole::sideRight);
        return layout;
    }

    static MeterChannelLayout sevenPointOne() noexcept
    {
        MeterChannelLayout layout(8);
        layout.setChannel(0, MeterChannelRole::frontLeft);
        layout.setChannel(1, MeterChannelRole::frontRight);
        layout.setChannel(2, MeterChannelRole::frontCenter);
        layout.setChannel(3, MeterChannelRole::lfe, true);
        layout.setChannel(4, MeterChannelRole::sideLeft);
        layout.setChannel(5, MeterChannelRole::sideRight);
        layout.setChannel(6, MeterChannelRole::rearLeft);
        layout.setChannel(7, MeterChannelRole::rearRight);
        return layout;
    }

    bool setChannelCount(std::size_t channelCount) noexcept
    {
        if (channelCount == 0 || channelCount > maximumChannelCount)
            return false;

        for (std::size_t index = channelCount; index < maximumChannelCount; ++index)
            channels_[index] = {};
        channelCount_ = channelCount;
        return true;
    }

    bool setChannel(std::size_t index, MeterChannelRole role, bool lfe = false) noexcept
    {
        if (index >= channelCount_)
            return false;

        channels_[index] = MeterChannel { role, lfe || role == MeterChannelRole::lfe };
        return true;
    }

    std::size_t channelCount() const noexcept
    {
        return channelCount_;
    }

    const MeterChannel* channel(std::size_t index) const noexcept
    {
        return index < channelCount_ ? &channels_[index] : nullptr;
    }

    bool isLfe(std::size_t index) const noexcept
    {
        const auto* meterChannel = channel(index);
        return meterChannel != nullptr && meterChannel->lfe;
    }

    double measurementWeight(std::size_t index) const noexcept
    {
        const auto* meterChannel = channel(index);
        if (meterChannel == nullptr || meterChannel->lfe)
            return 0.0;

        switch (meterChannel->role) {
        case MeterChannelRole::sideLeft:
        case MeterChannelRole::sideRight:
            return sideChannelWeight;
        default:
            return 1.0;
        }
    }

    bool hasMeasuredChannels() const noexcept
    {
        for (std::size_t index = 0; index < channelCount_; ++index) {
            if (measurementWeight(index) > 0.0)
                return true;
        }
        return false;
    }

private:
    std::array<MeterChannel, maximumChannelCount> channels_ {};
    std::size_t channelCount_ {};
};

}