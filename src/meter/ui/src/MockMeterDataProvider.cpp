#include <WinJACKNexus/Common/ChannelNames.hpp>
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

#include <QDateTime>

#include <algorithm>
#include <array>
#include <cmath>

namespace WinJACKNexus::Meter
{

namespace
{

MeterMetrics metrics(float peak, float rms, float truePeak, float momentary, float shortTerm,
                     float integrated, float lra)
{
    return {peak, rms, truePeak, momentary, shortTerm, integrated, lra};
}

MeterMetrics resetMetrics(float peak, float rms, float truePeak)
{
    MeterMetrics value;
    value.peakDbfs = peak;
    value.rmsDbfs = rms;
    value.truePeakDbtp = truePeak;
    return value;
}

std::vector<MeterChannelView> makeChannels(std::uint16_t count, bool includeLfe,
                                           bool includeMultipleLfe)
{
    static const std::array<std::pair<const char *, const char *>, maximumMeterChannelCount>
        channelNames{std::pair{WinJACKNexus::Common::surroundChannelNames[0], "M+030"},
                     std::pair{WinJACKNexus::Common::surroundChannelNames[1], "M-030"},
                     std::pair{WinJACKNexus::Common::surroundChannelNames[2], "M+000"},
                     std::pair{WinJACKNexus::Common::surroundChannelNames[3], "LFE"},
                     std::pair{WinJACKNexus::Common::surroundChannelNames[4], "M+110"},
                     std::pair{WinJACKNexus::Common::surroundChannelNames[5], "M-110"},
                     std::pair{WinJACKNexus::Common::surroundChannelNames[6], "M+135"},
                     std::pair{WinJACKNexus::Common::surroundChannelNames[7], "M-135"},
                     std::pair{"Ch 9", "M+180"},
                     std::pair{"Ch 10", "M-180"},
                     std::pair{"Ch 11", "U+030"},
                     std::pair{"Ch 12", "U-030"},
                     std::pair{"Ch 13", "U+000"},
                     std::pair{"Ch 14", "T+030"},
                     std::pair{"Ch 15", "T-030"},
                     std::pair{"Ch 16", "T+000"}};

    const auto boundedCount = std::clamp<std::uint16_t>(count, 1, maximumMeterChannelCount);
    std::vector<MeterChannelView> channels;
    channels.reserve(boundedCount);
    for (std::uint16_t index = 0; index < boundedCount; ++index)
    {
        const auto lfe = includeLfe && (index == 3 || (includeMultipleLfe && index == 7));
        channels.push_back(
            {QString::fromStdString(WinJACKNexus::Common::standardChannelName(index, boundedCount)),
             QString::fromLatin1(lfe ? "LFE" : channelNames[index].second), lfe});
    }
    return channels;
}

void appendLongHistory(MeterViewSnapshot &snapshot)
{
    constexpr std::int64_t intervalMilliseconds = 100;
    constexpr std::size_t pointCount = 6000;
    const auto systemTimeAtLatestPoint = QDateTime::currentMSecsSinceEpoch();
    snapshot.history.reserve(pointCount);
    for (std::size_t index = 0; index < pointCount; ++index)
    {
        const auto position = static_cast<float>(index);
        const auto peak =
            -7.0f + std::sin(position * 0.043f) * 2.5f + std::sin(position * 0.17f) * 0.75f;
        const auto rms = peak - 15.5f - std::cos(position * 0.031f) * 1.5f;
        const auto truePeak = peak + 0.8f + std::sin(position * 0.071f) * 0.2f;
        const auto momentary = rms - 1.2f + std::sin(position * 0.019f) * 1.8f;
        const auto shortTerm = momentary - 1.1f + std::cos(position * 0.008f) * 0.8f;
        const auto integrated = -21.0f + std::sin(position * 0.003f) * 0.75f;
        const auto lra = 6.2f + std::sin(position * 0.004f) * 1.3f;
        MeterHistoryPoint point;
        point.elapsedMilliseconds = static_cast<std::int64_t>(index) * intervalMilliseconds;
        point.systemTimeMilliseconds =
            systemTimeAtLatestPoint -
            static_cast<std::int64_t>(pointCount - 1 - index) * intervalMilliseconds;
        point.metrics = metrics(peak, rms, truePeak, momentary, shortTerm, integrated, lra);
        point.lfePeakDbfs = -18.0f + std::sin(position * 0.053f) * 3.0f;
        snapshot.history.push_back(std::move(point));
    }
    snapshot.currentMetrics = snapshot.history.back().metrics;
    snapshot.lfePeakDbfs = snapshot.history.back().lfePeakDbfs;
}

void appendPreviewHistory(MeterViewSnapshot &snapshot, std::uint16_t previewIndex)
{
    constexpr std::int64_t intervalMilliseconds = 100;
    constexpr std::size_t pointCount = 600;
    const auto systemTimeAtLatestPoint = QDateTime::currentMSecsSinceEpoch();
    const auto baseline = snapshot.currentMetrics;
    const auto seed = static_cast<float>(previewIndex) * 0.37F;
    const auto varyingValue = [seed](const std::optional<float> &value, float position,
                                     float amplitude) -> std::optional<float> {
        if (!value.has_value())
            return std::nullopt;
        return *value + std::sin(position * 0.047F + seed) * amplitude;
    };

    snapshot.history.reserve(pointCount);
    for (std::size_t index = 0; index < pointCount; ++index)
    {
        const auto position = static_cast<float>(index);
        MeterHistoryPoint point;
        point.elapsedMilliseconds = static_cast<std::int64_t>(index) * intervalMilliseconds;
        point.systemTimeMilliseconds =
            systemTimeAtLatestPoint -
            static_cast<std::int64_t>(pointCount - 1 - index) * intervalMilliseconds;
        point.metrics.peakDbfs = varyingValue(baseline.peakDbfs, position, 2.4F);
        point.metrics.rmsDbfs = varyingValue(baseline.rmsDbfs, position, 1.7F);
        point.metrics.truePeakDbtp = varyingValue(baseline.truePeakDbtp, position, 2.1F);
        point.metrics.momentaryLufs = varyingValue(baseline.momentaryLufs, position, 1.3F);
        point.metrics.shortTermLufs = varyingValue(baseline.shortTermLufs, position, 0.8F);
        point.metrics.integratedLufs = varyingValue(baseline.integratedLufs, position, 0.3F);
        point.metrics.lraLu = varyingValue(baseline.lraLu, position, 0.6F);
        point.lfePeakDbfs = varyingValue(snapshot.lfePeakDbfs, position, 1.9F);
        point.discontinuity = previewIndex % 5 == 4 && index == pointCount / 2;
        snapshot.history.push_back(std::move(point));
    }
}

}

std::vector<MockMeterScenario> MockMeterDataProvider::availableScenarios() const
{
    return {MockMeterScenario::silence,         MockMeterScenario::normalProgram,
            MockMeterScenario::overload,        MockMeterScenario::monoWithoutLfe,
            MockMeterScenario::surroundWithLfe, MockMeterScenario::multipleLfe,
            MockMeterScenario::renamed,         MockMeterScenario::offline,
            MockMeterScenario::reset,           MockMeterScenario::customChannels,
            MockMeterScenario::longHistory};
}

QString MockMeterDataProvider::scenarioDisplayName(MockMeterScenario scenario)
{
    switch (scenario)
    {
    case MockMeterScenario::silence:
        return QStringLiteral("静音");
    case MockMeterScenario::normalProgram:
        return QStringLiteral("正常节目");
    case MockMeterScenario::overload:
        return QStringLiteral("超限");
    case MockMeterScenario::monoWithoutLfe:
        return QStringLiteral("不含 LFE 的单声道");
    case MockMeterScenario::surroundWithLfe:
        return QStringLiteral("含 LFE 的 5.1");
    case MockMeterScenario::multipleLfe:
        return QStringLiteral("自定义多 LFE");
    case MockMeterScenario::renamed:
        return QStringLiteral("已改名客户端");
    case MockMeterScenario::offline:
        return QStringLiteral("离线");
    case MockMeterScenario::reset:
        return QStringLiteral("重置会话");
    case MockMeterScenario::customChannels:
        return QStringLiteral("自定义 1-16 声道");
    case MockMeterScenario::longHistory:
        return QStringLiteral("长历史");
    }
    return {};
}

void MockMeterDataProvider::setScenario(MockMeterScenario scenario) noexcept
{
    scenario_ = scenario;
    for (auto &state : previewProbeStates_)
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

bool MockMeterDataProvider::dispatch(const MeterViewCommand &command)
{
    const auto index = previewIndexForId(command.instanceId);
    if (!index.has_value())
        return false;

    auto &state = previewProbeStates_[*index];

    switch (command.type)
    {
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
    case MeterViewCommandType::setHistoryViewMode:
        if (!command.historyViewMode.has_value())
            return false;
        state.historyView.mode = *command.historyViewMode;
        return true;
    case MeterViewCommandType::setHistoryTimeWindow:
        if (!command.historyTimeWindowMilliseconds.has_value() ||
            *command.historyTimeWindowMilliseconds <= 0)
            return false;
        state.historyView.timeWindowMilliseconds = std::clamp(
            *command.historyTimeWindowMilliseconds, minimumMeterHistoryWindowMilliseconds,
            maximumMeterHistoryWindowMilliseconds);
        return true;
    case MeterViewCommandType::setHistoryPaused:
        if (!command.enabled.has_value())
            return false;
        state.historyView.paused = *command.enabled;
        return true;
    case MeterViewCommandType::setHistorySeriesVisible: {
        if (!command.enabled.has_value() || command.seriesId.isEmpty())
            return false;
        const auto existing =
            std::find(state.historyView.visibleSeriesIds.cbegin(),
                      state.historyView.visibleSeriesIds.cend(), command.seriesId);
        if (*command.enabled && existing == state.historyView.visibleSeriesIds.cend())
            state.historyView.visibleSeriesIds.push_back(command.seriesId);
        else if (!*command.enabled && existing != state.historyView.visibleSeriesIds.cend())
            state.historyView.visibleSeriesIds.erase(existing);
        return true;
    }
    case MeterViewCommandType::clearHistory:
        state.historyCleared = true;
        return true;
    case MeterViewCommandType::setCustomChannelCount:
        if (!command.channelCount.has_value())
            return false;
        customChannelCount_ =
            std::clamp<std::uint16_t>(*command.channelCount, 1, maximumMeterChannelCount);
        ensureCustomChannels(state);
        return true;
    case MeterViewCommandType::setChannelDisplayName:
        if (scenario_ != MockMeterScenario::customChannels || !command.channelIndex.has_value() ||
            command.textValue.isEmpty())
            return false;
        ensureCustomChannels(state);
        if (*command.channelIndex >= state.customChannels.size())
            return false;
        state.customChannels[*command.channelIndex].displayName = command.textValue;
        return true;
    case MeterViewCommandType::setChannelRole:
        if (scenario_ != MockMeterScenario::customChannels || !command.channelIndex.has_value() ||
            command.textValue.isEmpty())
            return false;
        ensureCustomChannels(state);
        if (*command.channelIndex >= state.customChannels.size())
            return false;
        state.customChannels[*command.channelIndex].speakerRole = command.textValue;
        return true;
    case MeterViewCommandType::setChannelLfe:
        if (scenario_ != MockMeterScenario::customChannels || !command.channelIndex.has_value() ||
            !command.enabled.has_value())
            return false;
        ensureCustomChannels(state);
        if (*command.channelIndex >= state.customChannels.size())
            return false;
        state.customChannels[*command.channelIndex].isLfe = *command.enabled;
        return true;
    case MeterViewCommandType::setGlobalPreset:
        if (command.presetId.isEmpty())
            return false;
        globalSettings_.defaultPresetId = command.presetId;
        return true;
    case MeterViewCommandType::setGlobalSilenceThreshold:
        if (!command.numericValue.has_value() || !std::isfinite(*command.numericValue))
            return false;
        globalSettings_.silenceThresholdDbfs = std::clamp(*command.numericValue, -144.0F, 0.0F);
        return true;
    case MeterViewCommandType::setGlobalSilenceDuration:
        if (!command.numericValue.has_value() || !std::isfinite(*command.numericValue))
            return false;
        globalSettings_.silenceDurationSeconds = std::clamp(*command.numericValue, 0.1F, 60.0F);
        return true;
    case MeterViewCommandType::setGlobalLogRootDirectory:
        if (command.textValue.isEmpty())
            return false;
        globalSettings_.logRootDirectory = command.textValue;
        return true;
    }
    return false;
}

QString MockMeterDataProvider::instanceIdFor(std::uint16_t index)
{
    return QStringLiteral("meter-preview-%1").arg(index + 1);
}

std::optional<std::uint16_t> MockMeterDataProvider::previewIndexForId(
    const QString &instanceId) const
{
    for (std::uint16_t index = 0; index < previewProbeCount_; ++index)
    {
        if (instanceId == instanceIdFor(index))
            return index;
    }
    return std::nullopt;
}

void MockMeterDataProvider::ensureCustomChannels(PreviewProbeState &state)
{
    const auto defaults = makeChannels(customChannelCount_, customChannelCount_ >= 4, false);
    if (state.customChannels.size() == defaults.size())
        return;

    const auto previous = std::move(state.customChannels);
    state.customChannels = defaults;
    const auto copyCount = std::min(previous.size(), state.customChannels.size());
    for (std::size_t index = 0; index < copyCount; ++index)
        state.customChannels[index] = previous[index];
}

MeterViewSnapshot MockMeterDataProvider::makeSnapshot(std::uint16_t previewIndex) const
{
    MeterViewSnapshot snapshot;
    const auto &state = previewProbeStates_[previewIndex];
    snapshot.instanceId = instanceIdFor(previewIndex);
    snapshot.clientName = QStringLiteral("话筒 %1").arg(previewIndex + 1);
    snapshot.nameRevision = 1;
    snapshot.layoutLabel = QStringLiteral("立体声");
    snapshot.presetId = state.presetId;
    snapshot.state = MeterProbeState::active;
    snapshot.automaticResetEnabled = state.automaticResetEnabled;
    snapshot.recordingEnabled = state.recordingEnabled;
    snapshot.channels = makeChannels(2, false, false);
    snapshot.currentMetrics = metrics(-1.2f, -18.5f, -1.0f, -16.8f, -18.1f, -20.3f, 6.4f);
    snapshot.historyView = state.historyView;
    snapshot.globalSettings = globalSettings_;
    snapshot.csvPreview.logRootDirectory = globalSettings_.logRootDirectory;
    snapshot.csvPreview.activeFilePath =
        QStringLiteral("%1/%2-preview.csv")
            .arg(globalSettings_.logRootDirectory, snapshot.instanceId);
    snapshot.csvPreview.statusKind = snapshot.recordingEnabled
                                         ? MeterCsvStatusKind::previewRecording
                                         : MeterCsvStatusKind::previewIdle;
    snapshot.csvPreview.rotationStatus = snapshot.recordingEnabled
                                             ? QStringLiteral("预览记录已启用；轮换间隔：60 分钟")
                                             : QStringLiteral("预览记录空闲；不会写入文件");

    if (previewIndex % 4 == 1)
    {
        snapshot.clientName = QStringLiteral("音乐底");
        snapshot.currentMetrics = metrics(-3.8f, -21.7f, -3.1f, -20.4f, -21.5f, -22.4f, 5.2f);
    }
    else if (previewIndex % 4 == 2)
    {
        snapshot.clientName = QStringLiteral("节目 5.1");
        snapshot.layoutLabel = QStringLiteral("5.1");
        snapshot.channels = makeChannels(6, true, false);
        snapshot.currentMetrics = metrics(-1.8f, -20.4f, -1.3f, -18.0f, -19.1f, -22.7f, 7.0f);
        snapshot.lfePeakDbfs = -6.2f;
    }
    else if (previewIndex % 4 == 3)
    {
        snapshot.clientName = QStringLiteral("旁白");
        snapshot.layoutLabel = QStringLiteral("单声道");
        snapshot.channels = makeChannels(1, false, false);
        snapshot.currentMetrics = metrics(-5.2f, -24.8f, -4.8f, -21.1f, -22.4f, -23.0f, 4.1f);
    }

    switch (scenario_)
    {
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
        snapshot.clientName = QStringLiteral("旁白");
        snapshot.layoutLabel = QStringLiteral("单声道");
        snapshot.channels = makeChannels(1, false, false);
        snapshot.currentMetrics = metrics(-5.2f, -24.8f, -4.8f, -21.1f, -22.4f, -23.0f, 4.1f);
        break;
    case MockMeterScenario::surroundWithLfe:
        snapshot.clientName = QStringLiteral("节目 5.1");
        snapshot.layoutLabel = QStringLiteral("5.1");
        snapshot.channels = makeChannels(6, true, false);
        snapshot.currentMetrics = metrics(-1.8f, -20.4f, -1.3f, -18.0f, -19.1f, -22.7f, 7.0f);
        snapshot.lfePeakDbfs = -6.2f;
        break;
    case MockMeterScenario::multipleLfe:
        snapshot.clientName = QStringLiteral("沉浸式分轨");
        snapshot.layoutLabel = QStringLiteral("自定义 8");
        snapshot.channels = makeChannels(8, true, true);
        snapshot.currentMetrics = metrics(-2.0f, -21.2f, -1.4f, -19.3f, -20.5f, -23.1f, 7.3f);
        snapshot.lfePeakDbfs = -3.4f;
        break;
    case MockMeterScenario::renamed:
        snapshot.clientName = QStringLiteral("节目-2");
        snapshot.nameRevision = 2;
        break;
    case MockMeterScenario::offline:
        snapshot.clientName = QStringLiteral("离线探针");
        snapshot.state = MeterProbeState::offline;
        snapshot.currentMetrics = {};
        snapshot.history.clear();
        break;
    case MockMeterScenario::reset:
        snapshot.currentMetrics = resetMetrics(-14.0f, -31.0f, -13.3f);
        snapshot.history.clear();
        break;
    case MockMeterScenario::customChannels:
        snapshot.clientName = QStringLiteral("自定义总线");
        snapshot.layoutLabel = QStringLiteral("自定义 %1").arg(customChannelCount_);
        snapshot.channels = makeChannels(customChannelCount_, customChannelCount_ >= 4, false);
        if (state.customChannels.size() == snapshot.channels.size())
            snapshot.channels = state.customChannels;
        snapshot.currentMetrics = metrics(-3.6f, -22.6f, -3.1f, -20.0f, -21.4f, -22.9f, 5.8f);
        if (customChannelCount_ >= 4)
            snapshot.lfePeakDbfs = -8.0f;
        snapshot.channelLayoutEditable = true;
        break;
    case MockMeterScenario::longHistory:
        snapshot.clientName = QStringLiteral("长节目混音");
        snapshot.layoutLabel = QStringLiteral("5.1");
        snapshot.channels = makeChannels(6, true, false);
        appendLongHistory(snapshot);
        break;
    }

    if (scenario_ != MockMeterScenario::offline && scenario_ != MockMeterScenario::reset &&
        scenario_ != MockMeterScenario::longHistory)
        appendPreviewHistory(snapshot, previewIndex);
    if (state.resetRequested)
    {
        snapshot.currentMetrics.integratedLufs.reset();
        snapshot.currentMetrics.lraLu.reset();
        snapshot.history.clear();
    }
    if (state.historyCleared)
        snapshot.history.clear();
    return snapshot;
}

}