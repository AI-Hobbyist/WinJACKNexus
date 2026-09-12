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

#include <QJsonArray>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace WinJACKNexus::Meter {

class MeterCsvWriter;

class LiveMeterDataProvider final : public MeterViewDataProvider {
public:
    LiveMeterDataProvider();
    ~LiveMeterDataProvider() override;

    std::vector<MeterViewSnapshot> snapshots() const override;
    std::vector<MeterViewSnapshot> snapshots(bool includeHistory) const override;
    bool dispatch(const MeterViewCommand& command) override;
    void reloadGlobalSettings();
    QJsonArray projectMeters() const;
    void restoreProjectMeters(const QJsonArray& meters);

private:
    struct ProbeViewState final {
        MeterHistoryViewSettings historyView {
            MeterHistoryViewMode::realtimeScroll,
            defaultMeterHistoryWindowMilliseconds,
            false,
            {
                QStringLiteral("peak"),
                QStringLiteral("rms"),
                QStringLiteral("lfe"),
                QStringLiteral("true-peak"),
                QStringLiteral("momentary"),
                QStringLiteral("short-term"),
                QStringLiteral("integrated"),
                QStringLiteral("lra")
            }
        };
        std::vector<MeterHistoryPoint> history;
        QString presetId = QStringLiteral("ebu-r128");
        QString preservedUnknownPresetId;
        std::uint64_t firstProcessedFrames {};
        std::uint64_t lastProcessedFrames {};
        std::uint64_t lastHistoryProcessedFrames {};
        bool hasFrame {};
        bool recordingEnabled {};
        bool configurationInitialized {};
        std::uint64_t observedLoudnessResetGeneration {};
    };

    ProbeViewState& stateFor(const std::string& instanceId) const;
    static MeterGlobalSettingsView loadGlobalSettings();
    static void saveGlobalSettings(const MeterGlobalSettingsView& settings);
    bool dispatchGlobalSetting(const MeterViewCommand& command);

    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, ProbeViewState> probeStates_;
    MeterGlobalSettingsView globalSettings_;
    std::unique_ptr<MeterCsvWriter> csvWriter_;
};

}