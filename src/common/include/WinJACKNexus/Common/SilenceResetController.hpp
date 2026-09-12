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

#include <WinJACKNexus/Common/MeterSnapshot.hpp>

#include <cmath>

namespace WinJACKNexus::Common {

enum class SilenceResetState {
    active,
    pendingSilence,
    resetArmed
};

class SilenceResetController final {
public:
    SilenceResetController(double thresholdDbfs = -70.0,
                           double durationSeconds = 3.0) noexcept
    {
        configure(thresholdDbfs, durationSeconds);
    }

    bool configure(double thresholdDbfs, double durationSeconds) noexcept
    {
        if (!std::isfinite(thresholdDbfs) || !std::isfinite(durationSeconds) || durationSeconds < 0.0)
            return false;

        thresholdDbfs_ = thresholdDbfs;
        durationSeconds_ = durationSeconds;
        reset();
        return true;
    }

    void reset() noexcept
    {
        state_ = SilenceResetState::active;
        silentSeconds_ = 0.0;
    }

    bool update(const MeterValue& peakDbfs, double elapsedSeconds) noexcept
    {
        const auto active = peakDbfs.valid
            && std::isfinite(peakDbfs.value)
            && peakDbfs.value > thresholdDbfs_;
        const auto elapsed = std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0
            ? elapsedSeconds
            : 0.0;

        if (active) {
            const auto shouldReset = state_ == SilenceResetState::resetArmed;
            state_ = SilenceResetState::active;
            silentSeconds_ = 0.0;
            return shouldReset;
        }

        if (state_ == SilenceResetState::active)
            state_ = SilenceResetState::pendingSilence;
        if (state_ == SilenceResetState::pendingSilence) {
            silentSeconds_ += elapsed;
            if (silentSeconds_ >= durationSeconds_)
                state_ = SilenceResetState::resetArmed;
        }
        return false;
    }

    SilenceResetState state() const noexcept
    {
        return state_;
    }

    double silentSeconds() const noexcept
    {
        return silentSeconds_;
    }

private:
    double thresholdDbfs_ { -70.0 };
    double durationSeconds_ { 3.0 };
    double silentSeconds_ {};
    SilenceResetState state_ { SilenceResetState::active };
};

}