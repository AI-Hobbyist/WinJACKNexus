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

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace WinJACKNexus::Meter
{

inline constexpr std::uint16_t maximumMeterChannelCount = 16;
inline constexpr std::int64_t minimumMeterHistoryWindowMilliseconds = 10'000;
inline constexpr std::int64_t defaultMeterHistoryWindowMilliseconds = 30'000;
inline constexpr std::int64_t maximumMeterHistoryWindowMilliseconds = 3'600'000;

enum class MeterProbeState : std::uint8_t
{
    silent,
    active,
    overload,
    offline
};

enum class MeterHistoryViewMode : std::uint8_t
{
    realtimeScroll,
    scanning
};

enum class MeterViewCommandType : std::uint8_t
{
    resetLoudness,
    setAutomaticReset,
    setRecording,
    setPreset,
    openHistory,
    selectInPatchbay,
    setHistoryViewMode,
    setHistoryTimeWindow,
    setHistoryPaused,
    setHistorySeriesVisible,
    clearHistory,
    setCustomChannelCount,
    setChannelDisplayName,
    setChannelRole,
    setChannelLfe,
    setGlobalPreset,
    setGlobalSilenceThreshold,
    setGlobalSilenceDuration,
    setGlobalLogRootDirectory
};

struct MeterViewCommand final
{
    MeterViewCommandType type = MeterViewCommandType::resetLoudness;
    QString instanceId;
    std::optional<bool> enabled;
    QString presetId;
    std::optional<MeterHistoryViewMode> historyViewMode;
    std::optional<std::int64_t> historyTimeWindowMilliseconds;
    QString seriesId;
    std::optional<std::uint16_t> channelIndex;
    std::optional<std::uint16_t> channelCount;
    QString textValue;
    std::optional<float> numericValue;
};

struct MeterChannelView final
{
    QString displayName;
    QString speakerRole;
    bool isLfe = false;

    bool operator==(const MeterChannelView &) const = default;
};

struct MeterMetrics final
{
    std::optional<float> peakDbfs;
    std::optional<float> rmsDbfs;
    std::optional<float> truePeakDbtp;
    std::optional<float> momentaryLufs;
    std::optional<float> shortTermLufs;
    std::optional<float> integratedLufs;
    std::optional<float> lraLu;

    bool operator==(const MeterMetrics &) const = default;
};

struct MeterHistoryPoint final
{
    std::int64_t elapsedMilliseconds = 0;
    std::optional<std::int64_t> systemTimeMilliseconds;
    MeterMetrics metrics;
    std::optional<float> lfePeakDbfs;
    bool discontinuity = false;

    bool operator==(const MeterHistoryPoint &) const = default;
};

struct MeterHistoryViewSettings final
{
    MeterHistoryViewMode mode = MeterHistoryViewMode::realtimeScroll;
    std::int64_t timeWindowMilliseconds = defaultMeterHistoryWindowMilliseconds;
    bool paused = false;
    std::vector<QString> visibleSeriesIds;

    bool operator==(const MeterHistoryViewSettings &) const = default;
};

enum class MeterCsvStatusKind
{
    unspecified,
    waiting,
    stopped,
    recording,
    queueFull,
    writerClosed,
    sessionClosed,
    noDirectory,
    directoryFailure,
    openFailure,
    writeFailure,
    previewRecording,
    previewIdle
};

struct MeterCsvPreviewState final
{
    QString logRootDirectory;
    QString activeFilePath;
    QString rotationStatus;
    MeterCsvStatusKind statusKind = MeterCsvStatusKind::unspecified;
    QString statusDetail;
    std::size_t droppedRecordCount = 0;

    bool operator==(const MeterCsvPreviewState &) const = default;
};

struct MeterGlobalSettingsView final
{
    QString defaultPresetId = QStringLiteral("ebu-r128");
    float silenceThresholdDbfs = -70.0F;
    float silenceDurationSeconds = 3.0F;
    QString logRootDirectory;

    bool operator==(const MeterGlobalSettingsView &) const = default;
};

struct MeterViewSnapshot final
{
    QString instanceId;
    QString clientName;
    std::uint64_t nameRevision = 0;
    QString layoutLabel;
    QString presetId;
    MeterProbeState state = MeterProbeState::offline;
    bool automaticResetEnabled = true;
    bool recordingEnabled = false;
    std::vector<MeterChannelView> channels;
    MeterMetrics currentMetrics;
    std::optional<float> lfePeakDbfs;
    std::vector<MeterHistoryPoint> history;
    std::optional<std::int64_t> latestHistoryElapsedMilliseconds;
    MeterHistoryViewSettings historyView;
    MeterCsvPreviewState csvPreview;
    MeterGlobalSettingsView globalSettings;
    bool channelLayoutEditable = false;
};

}