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

#include <WinJACKNexus/Meter/MeterViewDataProvider.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace WinJACKNexus::Meter
{

enum class MockMeterScenario : std::uint8_t
{
    silence,
    normalProgram,
    overload,
    monoWithoutLfe,
    surroundWithLfe,
    multipleLfe,
    renamed,
    offline,
    reset,
    customChannels,
    longHistory
};

class MockMeterDataProvider final : public MeterViewDataProvider
{
  public:
    MockMeterDataProvider() = default;

    std::vector<MockMeterScenario> availableScenarios() const;
    static QString scenarioDisplayName(MockMeterScenario scenario);

    void setScenario(MockMeterScenario scenario) noexcept;
    MockMeterScenario scenario() const noexcept;
    void setCustomChannelCount(std::uint16_t count) noexcept;
    std::uint16_t customChannelCount() const noexcept;
    void setPreviewProbeCount(std::uint16_t count) noexcept;
    std::uint16_t previewProbeCount() const noexcept;

    std::vector<MeterViewSnapshot> snapshots() const override;
    bool dispatch(const MeterViewCommand &command) override;

  private:
    struct PreviewProbeState final
    {
        bool automaticResetEnabled = true;
        bool recordingEnabled = false;
        bool resetRequested = false;
        bool historyCleared = false;
        QString presetId = QStringLiteral("ebu-r128");
        MeterHistoryViewSettings historyView{
            MeterHistoryViewMode::realtimeScroll,
            defaultMeterHistoryWindowMilliseconds,
            false,
            {QStringLiteral("peak"), QStringLiteral("rms"), QStringLiteral("lfe"),
             QStringLiteral("true-peak"), QStringLiteral("momentary"), QStringLiteral("short-term"),
             QStringLiteral("integrated"), QStringLiteral("lra")}};
        std::vector<MeterChannelView> customChannels;
    };

    static QString instanceIdFor(std::uint16_t index);
    std::optional<std::uint16_t> previewIndexForId(const QString &instanceId) const;
    void ensureCustomChannels(PreviewProbeState &state);
    MeterViewSnapshot makeSnapshot(std::uint16_t previewIndex) const;

    MockMeterScenario scenario_ = MockMeterScenario::normalProgram;
    std::uint16_t customChannelCount_ = 2;
    std::uint16_t previewProbeCount_ = 2;
    std::array<PreviewProbeState, maximumMeterChannelCount> previewProbeStates_;
    MeterGlobalSettingsView globalSettings_;
};

}