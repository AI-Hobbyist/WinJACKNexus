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
#include <WinJACKNexus/Meter/MockMeterDataProvider.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::Meter {

namespace {

MeterMetrics metrics(float peak,
                      float rms,
                      float truePeak,
                      float momentary,
                      float shortTerm,
                      float integrated,
                      float lra)
{
    return {
        peak,
        rms,
        truePeak,
        momentary,
        shortTerm,
        integrated,
        lra
    };
}

MeterMetrics resetMetrics(float peak, float rms, float truePeak)
{
    MeterMetrics value;
    value.peakDbfs = peak;
    value.rmsDbfs = rms;
    value.truePeakDbtp = truePeak;
    return value;
}

std::vector<MeterChannelView> makeChannels(std::uint16_t count, bool includeLfe, bool includeMultipleLfe)
{
    static const std::array<std::pair<const char*, const char*>, maximumMeterChannelCount> channelNames {
        std::pair { "L", "M+030" },
        std::pair { "R", "M-030" },
        std::pair { "C", "M+000" },
        std::pair { "LFE", "LFE" },
        std::pair { "Ls", "M+110" },
        std::pair { "Rs", "M-110" },
        std::pair { "Lb", "M+135" },
        std::pair { "Rb", "M-135" },
        std::pair { "Ch 9", "M+180" },
        std::pair { "Ch 10", "M-180" },
        std::pair { "Ch 11", "U+030" },
        std::pair { "Ch 12", "U-030" },
        std::pair { "Ch 13", "U+000" },
        std::pair { "Ch 14", "T+030" },
        std::pair { "Ch 15", "T-030" },
        std::pair { "Ch 16", "T+000" }
    };

    const auto boundedCount = std::clamp<std::uint16_t>(count, 1, maximumMeterChannelCount);
    std::vector<MeterChannelView> channels;
    channels.reserve(boundedCount);
    for (std::uint16_t index = 0; index < boundedCount; ++index) {
        const auto lfe = includeLfe && (index == 3 || (includeMultipleLfe && index == 7));
        channels.push_back({
            QString::fromLatin1(channelNames[index].first),
            QString::fromLatin1(lfe ? "LFE" : channelNames[index].second),
            lfe
        });
    }
    return channels;
}

void appendLongHistory(MeterViewSnapshot& snapshot)
{
    constexpr std::int64_t intervalMilliseconds = 100;
    constexpr std::size_t pointCount = 6000;
    snapshot.history.reserve(pointCount);
    for (std::size_t index = 0; index < pointCount; ++index) {
        const auto position = static_cast<float>(index);
        const auto peak = -7.0f + std::sin(position * 0.043f) * 2.5f + std::sin(position * 0.17f) * 0.75f;
        const auto rms = peak - 15.5f - std::cos(position * 0.031f) * 1.5f;
        const auto truePeak = peak + 0.8f + std::sin(position * 0.071f) * 0.2f;
        const auto momentary = rms - 1.2f + std::sin(position * 0.019f) * 1.8f;
        const auto shortTerm = momentary - 1.1f + std::cos(position * 0.008f) * 0.8f;
        const auto integrated = -21.0f + std::sin(position * 0.003f) * 0.75f;
        const auto lra = 6.2f + std::sin(position * 0.004f) * 1.3f;
        MeterHistoryPoint point;
        point.elapsedMilliseconds = static_cast<std::int64_t>(index) * intervalMilliseconds;
        point.metrics = metrics(peak, rms, truePeak, momentary, shortTerm, integrated, lra);
        point.lfePeakDbfs = -18.0f + std::sin(position * 0.053f) * 3.0f;
        snapshot.history.push_back(std::move(point));
    }
    snapshot.currentMetrics = snapshot.history.back().metrics;
    snapshot.lfePeakDbfs = snapshot.history.back().lfePeakDbfs;
}

}

std::vector<MockMeterScenario> MockMeterDataProvider::availableScenarios() const
{
    return {
        MockMeterScenario::silence,
        MockMeterScenario::normalProgram,
        MockMeterScenario::overload,
        MockMeterScenario::monoWithoutLfe,
        MockMeterScenario::surroundWithLfe,
        MockMeterScenario::multipleLfe,
        MockMeterScenario::renamed,
        MockMeterScenario::offline,
        MockMeterScenario::reset,
        MockMeterScenario::customChannels,
        MockMeterScenario::longHistory
    };
}

QString MockMeterDataProvider::scenarioDisplayName(MockMeterScenario scenario)
{
    switch (scenario) {
    case MockMeterScenario::silence:
        return QStringLiteral("Silence");
    case MockMeterScenario::normalProgram:
        return QStringLiteral("Normal program");
    case MockMeterScenario::overload:
        return QStringLiteral("Over limit");
    case MockMeterScenario::monoWithoutLfe:
        return QStringLiteral("Mono without LFE");
    case MockMeterScenario::surroundWithLfe:
        return QStringLiteral("5.1 with LFE");
    case MockMeterScenario::multipleLfe:
        return QStringLiteral("Custom multiple LFE");
    case MockMeterScenario::renamed:
        return QStringLiteral("Renamed client");
    case MockMeterScenario::offline:
        return QStringLiteral("Offline");
    case MockMeterScenario::reset:
        return QStringLiteral("Reset session");
    case MockMeterScenario::customChannels:
        return QStringLiteral("Custom 1-16 channels");
    case MockMeterScenario::longHistory:
        return QStringLiteral("Long history");
    }
    return {};
}

void MockMeterDataProvider::setScenario(MockMeterScenario scenario) noexcept
{
    scenario_ = scenario;
    for (auto& state : previewProbeStates_)
        state.resetRequested = false;
}

MockMeterScenario MockMeterDataProvider::scenario() const noexcept
{
    return scenario_;
}

void MockMeterDataProvider::setCustomChannelCount(std::uint16_t count) noexcept
{
    customChannelCount_ = std::clamp<std::uint16_t>(count, 1, maximumMeterChannelCount);
}

std::uint16_t MockMeterDataProvider::customChannelCount() const noexcept
{
    return customChannelCount_;
}

void MockMeterDataProvider::setPreviewProbeCount(std::uint16_t count) noexcept
{
    previewProbeCount_ = std::clamp<std::uint16_t>(count, 0, maximumMeterChannelCount);
}

std::uint16_t MockMeterDataProvider::previewProbeCount() const noexcept
{
    return previewProbeCount_;
}

std::vector<MeterViewSnapshot> MockMeterDataProvider::snapshots() const
{
    std::vector<MeterViewSnapshot> result;
    result.reserve(previewProbeCount_);
    for (std::uint16_t index = 0; index < previewProbeCount_; ++index)
        result.push_back(makeSnapshot(index));
    return result;
}

bool MockMeterDataProvider::dispatch(const MeterViewCommand& command)
{
    const auto index = previewIndexForId(command.instanceId);
    if (!index.has_value())
        return false;

    auto& state = previewProbeStates_[*index];

    switch (command.type) {
    case MeterViewCommandType::resetLoudness:
        state.resetRequested = true;
        return true;
    case MeterViewCommandType::setAutomaticReset:
        if (!command.enabled.has_value())
            return false;
        state.automaticResetEnabled = *command.enabled;
        return true;
    case MeterViewCommandType::setRecording:
        if (!command.enabled.has_value())
            return false;
        state.recordingEnabled = *command.enabled;
        return true;
    case MeterViewCommandType::setPreset:
        if (command.presetId.isEmpty())
            return false;
        state.presetId = command.presetId;
        return true;
    case MeterViewCommandType::openHistory:
    case MeterViewCommandType::selectInPatchbay:
        return true;
    }
    return false;
}

QString MockMeterDataProvider::instanceIdFor(std::uint16_t index)
{
    return QStringLiteral("meter-preview-%1").arg(index + 1);
}

std::optional<std::uint16_t> MockMeterDataProvider::previewIndexForId(const QString& instanceId) const
{
    for (std::uint16_t index = 0; index < previewProbeCount_; ++index) {
        if (instanceId == instanceIdFor(index))
            return index;
    }
    return std::nullopt;
}

MeterViewSnapshot MockMeterDataProvider::makeSnapshot(std::uint16_t previewIndex) const
{
    MeterViewSnapshot snapshot;
    snapshot.instanceId = instanceIdFor(previewIndex);
    snapshot.clientName = QStringLiteral("Mic %1").arg(previewIndex + 1);
    snapshot.nameRevision = 1;
    snapshot.layoutLabel = QStringLiteral("Stereo");
    snapshot.presetId = previewProbeStates_[previewIndex].presetId;
    snapshot.state = MeterProbeState::active;
    snapshot.automaticResetEnabled = previewProbeStates_[previewIndex].automaticResetEnabled;
    snapshot.recordingEnabled = previewProbeStates_[previewIndex].recordingEnabled;
    snapshot.channels = makeChannels(2, false, false);
    snapshot.currentMetrics = metrics(-1.2f, -18.5f, -1.0f, -16.8f, -18.1f, -20.3f, 6.4f);

    if (previewIndex % 4 == 1) {
        snapshot.clientName = QStringLiteral("Music bed");
        snapshot.currentMetrics = metrics(-3.8f, -21.7f, -3.1f, -20.4f, -21.5f, -22.4f, 5.2f);
    } else if (previewIndex % 4 == 2) {
        snapshot.clientName = QStringLiteral("Program 5.1");
        snapshot.layoutLabel = QStringLiteral("5.1");
        snapshot.channels = makeChannels(6, true, false);
        snapshot.currentMetrics = metrics(-1.8f, -20.4f, -1.3f, -18.0f, -19.1f, -22.7f, 7.0f);
        snapshot.lfePeakDbfs = -6.2f;
    } else if (previewIndex % 4 == 3) {
        snapshot.clientName = QStringLiteral("Narration");
        snapshot.layoutLabel = QStringLiteral("Mono");
        snapshot.channels = makeChannels(1, false, false);
        snapshot.currentMetrics = metrics(-5.2f, -24.8f, -4.8f, -21.1f, -22.4f, -23.0f, 4.1f);
    }

    switch (scenario_) {
    case MockMeterScenario::silence:
        snapshot.state = MeterProbeState::silent;
        snapshot.currentMetrics = resetMetrics(-96.0f, -102.0f, -95.5f);
        break;
    case MockMeterScenario::normalProgram:
        break;
    case MockMeterScenario::overload:
        snapshot.state = MeterProbeState::overload;
        snapshot.currentMetrics = metrics(0.4f, -11.5f, 1.1f, -12.8f, -14.2f, -17.4f, 8.7f);
        break;
    case MockMeterScenario::monoWithoutLfe:
        snapshot.clientName = QStringLiteral("Narration");
        snapshot.layoutLabel = QStringLiteral("Mono");
        snapshot.channels = makeChannels(1, false, false);
        snapshot.currentMetrics = metrics(-5.2f, -24.8f, -4.8f, -21.1f, -22.4f, -23.0f, 4.1f);
        break;
    case MockMeterScenario::surroundWithLfe:
        snapshot.clientName = QStringLiteral("Program 5.1");
        snapshot.layoutLabel = QStringLiteral("5.1");
        snapshot.channels = makeChannels(6, true, false);
        snapshot.currentMetrics = metrics(-1.8f, -20.4f, -1.3f, -18.0f, -19.1f, -22.7f, 7.0f);
        snapshot.lfePeakDbfs = -6.2f;
        break;
    case MockMeterScenario::multipleLfe:
        snapshot.clientName = QStringLiteral("Immersive stem");
        snapshot.layoutLabel = QStringLiteral("Custom 8");
        snapshot.channels = makeChannels(8, true, true);
        snapshot.currentMetrics = metrics(-2.0f, -21.2f, -1.4f, -19.3f, -20.5f, -23.1f, 7.3f);
        snapshot.lfePeakDbfs = -3.4f;
        break;
    case MockMeterScenario::renamed:
        snapshot.clientName = QStringLiteral("Program-2");
        snapshot.nameRevision = 2;
        break;
    case MockMeterScenario::offline:
        snapshot.clientName = QStringLiteral("Offline probe");
        snapshot.state = MeterProbeState::offline;
        snapshot.currentMetrics = {};
        snapshot.history.clear();
        break;
    case MockMeterScenario::reset:
        snapshot.currentMetrics = resetMetrics(-14.0f, -31.0f, -13.3f);
        snapshot.history.clear();
        break;
    case MockMeterScenario::customChannels:
        snapshot.clientName = QStringLiteral("Custom bus");
        snapshot.layoutLabel = QStringLiteral("Custom %1").arg(customChannelCount_);
        snapshot.channels = makeChannels(customChannelCount_, customChannelCount_ >= 4, false);
        snapshot.currentMetrics = metrics(-3.6f, -22.6f, -3.1f, -20.0f, -21.4f, -22.9f, 5.8f);
        if (customChannelCount_ >= 4)
            snapshot.lfePeakDbfs = -8.0f;
        break;
    case MockMeterScenario::longHistory:
        snapshot.clientName = QStringLiteral("Long-form mix");
        snapshot.layoutLabel = QStringLiteral("5.1");
        snapshot.channels = makeChannels(6, true, false);
        appendLongHistory(snapshot);
        break;
    }

    if (previewProbeStates_[previewIndex].resetRequested) {
        snapshot.currentMetrics.integratedLufs.reset();
        snapshot.currentMetrics.lraLu.reset();
        snapshot.history.clear();
    }
    return snapshot;
}

}