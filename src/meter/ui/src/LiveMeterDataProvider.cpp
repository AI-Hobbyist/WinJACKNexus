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
#include <WinJACKNexus/Meter/LiveMeterDataProvider.hpp>
#include <WinJACKNexus/Meter/MeterCsvWriter.hpp>

#include <WinJACKNexus/Common/MeterLoudnessPresetCatalog.hpp>
#include <WinJACKNexus/Common/MeterCustomLoudnessPresets.hpp>
#include <WinJACKNexus/Common/MeterProbeRegistry.hpp>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>

namespace WinJACKNexus::Meter
{

namespace
{

constexpr std::size_t maximumHistoryPointCount = 6000;
constexpr std::uint64_t historySampleRate = 10;

QString normalizedLogRootDirectory(const QString &path)
{
    const auto trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath()));
}

bool isValidSilenceThreshold(float value)
{
    return std::isfinite(value) && value >= -144.0F && value <= 0.0F;
}

bool isValidSilenceDuration(float value)
{
    return std::isfinite(value) && value >= 0.1F && value <= 60.0F;
}

std::optional<float> viewValue(const WinJACKNexus::Common::MeterValue &value)
{
    if (!value.valid || !std::isfinite(value.value))
        return std::nullopt;
    return static_cast<float>(value.value);
}

MeterMetrics viewMetrics(const WinJACKNexus::Common::MeterSnapshot &snapshot)
{
    return {viewValue(snapshot.samplePeakDbfs), viewValue(snapshot.rmsDbfs),
            viewValue(snapshot.truePeakDbtp),   viewValue(snapshot.momentaryLufs),
            viewValue(snapshot.shortTermLufs),  viewValue(snapshot.integratedLufs),
            viewValue(snapshot.loudnessRangeLu)};
}

QString speakerRoleLabel(WinJACKNexus::Common::MeterChannelRole role)
{
    using WinJACKNexus::Common::MeterChannelRole;
    switch (role)
    {
    case MeterChannelRole::mono:
    case MeterChannelRole::frontCenter:
        return QStringLiteral("M+000");
    case MeterChannelRole::frontLeft:
        return QStringLiteral("M+030");
    case MeterChannelRole::frontRight:
        return QStringLiteral("M-030");
    case MeterChannelRole::sideLeft:
        return QStringLiteral("M+110");
    case MeterChannelRole::sideRight:
        return QStringLiteral("M-110");
    case MeterChannelRole::rearLeft:
        return QStringLiteral("M+135");
    case MeterChannelRole::rearRight:
        return QStringLiteral("M-135");
    case MeterChannelRole::topFrontLeft:
        return QStringLiteral("U+030");
    case MeterChannelRole::topFrontRight:
        return QStringLiteral("U-030");
    case MeterChannelRole::topRearLeft:
        return QStringLiteral("T+030");
    case MeterChannelRole::topRearRight:
        return QStringLiteral("T-030");
    case MeterChannelRole::lfe:
        return QStringLiteral("LFE");
    case MeterChannelRole::unassigned:
        return QStringLiteral("未分配");
    }
    return QStringLiteral("未分配");
}

QString layoutLabel(const WinJACKNexus::Common::MeterProbeDescriptor &descriptor)
{
    if (descriptor.channelLayoutEditable)
        return QStringLiteral("自定义 %1").arg(static_cast<int>(descriptor.channelCount));
    switch (descriptor.channelCount)
    {
    case 1:
        return QStringLiteral("单声道");
    case 2:
        return QStringLiteral("立体声");
    case 6:
        return QStringLiteral("5.1");
    case 8:
        return QStringLiteral("7.1");
    default:
        return QStringLiteral("多声道 %1").arg(static_cast<int>(descriptor.channelCount));
    }
}

MeterProbeState probeState(const WinJACKNexus::Common::MeterProbeRegistration &registration,
                           const WinJACKNexus::Common::MeterSnapshot &snapshot)
{
    if (registration.realtimeState == nullptr || !registration.realtimeState->isOnline())
        return MeterProbeState::offline;
    if (snapshot.truePeakDbtp.valid && snapshot.truePeakDbtp.value > 0.0)
        return MeterProbeState::overload;
    if (snapshot.samplePeakDbfs.valid &&
        snapshot.samplePeakDbfs.value <= registration.descriptor.silenceThresholdDbfs)
        return MeterProbeState::silent;
    return MeterProbeState::active;
}

bool containsId(const std::vector<QString> &ids, const QString &id)
{
    return std::find(ids.cbegin(), ids.cend(), id) != ids.cend();
}

}

LiveMeterDataProvider::LiveMeterDataProvider()
    : globalSettings_(loadGlobalSettings()), csvWriter_(std::make_unique<MeterCsvWriter>())
{
}

LiveMeterDataProvider::~LiveMeterDataProvider() = default;

MeterGlobalSettingsView LiveMeterDataProvider::loadGlobalSettings()
{
    QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
    MeterGlobalSettingsView result;
    result.defaultPresetId =
        settings.value(QStringLiteral("defaultPresetId"), result.defaultPresetId)
            .toString()
            .trimmed();
    if (result.defaultPresetId.isEmpty())
        result.defaultPresetId = QStringLiteral("ebu-r128");

    const auto threshold =
        settings.value(QStringLiteral("silenceThresholdDbfs"), result.silenceThresholdDbfs)
            .toFloat();
    if (isValidSilenceThreshold(threshold))
        result.silenceThresholdDbfs = threshold;

    const auto duration =
        settings.value(QStringLiteral("silenceDurationSeconds"), result.silenceDurationSeconds)
            .toFloat();
    if (isValidSilenceDuration(duration))
        result.silenceDurationSeconds = duration;

    result.logRootDirectory = normalizedLogRootDirectory(
        settings.value(QStringLiteral("logRootDirectory"), result.logRootDirectory).toString());
    return result;
}

void LiveMeterDataProvider::saveGlobalSettings(const MeterGlobalSettingsView &settings)
{
    QSettings storage(QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
    storage.setValue(QStringLiteral("defaultPresetId"), settings.defaultPresetId);
    storage.setValue(QStringLiteral("silenceThresholdDbfs"), settings.silenceThresholdDbfs);
    storage.setValue(QStringLiteral("silenceDurationSeconds"), settings.silenceDurationSeconds);
    storage.setValue(QStringLiteral("logRootDirectory"), settings.logRootDirectory);
}

void LiveMeterDataProvider::reloadGlobalSettings()
{
    const auto registrations = WinJACKNexus::Common::meterProbeRegistrations();
    std::lock_guard lock(mutex_);
    globalSettings_ = loadGlobalSettings();
    for (const auto &registration : registrations)
    {
        if (registration.realtimeState != nullptr)
        {
            registration.realtimeState->setSilenceResetConfiguration(
                globalSettings_.silenceThresholdDbfs, globalSettings_.silenceDurationSeconds);
        }
    }
}

QJsonArray LiveMeterDataProvider::projectMeters() const
{
    const auto registrations = WinJACKNexus::Common::meterProbeRegistrations();
    std::lock_guard lock(mutex_);
    QJsonArray meters;
    for (const auto &registration : registrations)
    {
        if (registration.realtimeState == nullptr)
            continue;
        const auto &descriptor = registration.descriptor;
        auto &state = stateFor(descriptor.instanceId);
        if (!state.configurationInitialized)
        {
            state.presetId = descriptor.followGlobalPreset
                                 ? globalSettings_.defaultPresetId
                                 : QString::fromStdString(descriptor.presetId).trimmed();
            if (state.presetId.isEmpty())
                state.presetId = globalSettings_.defaultPresetId;
            state.configurationInitialized = true;
        }
        QJsonArray channels;
        for (std::size_t index = 0; index < descriptor.channelCount; ++index)
        {
            const auto &channel = descriptor.channels[index];
            channels.append(
                QJsonObject{{QStringLiteral("name"), QString::fromStdString(channel.name)},
                            {QStringLiteral("role"), speakerRoleLabel(channel.role)},
                            {QStringLiteral("lfe"), channel.lfe}});
        }
        QJsonArray visibleSeries;
        for (const auto &seriesId : state.historyView.visibleSeriesIds)
            visibleSeries.append(seriesId);
        meters.append(QJsonObject{
            {QStringLiteral("instanceId"), QString::fromStdString(descriptor.instanceId)},
            {QStringLiteral("clientName"), QString::fromStdString(descriptor.clientName)},
            {QStringLiteral("layout"), layoutLabel(descriptor)},
            {QStringLiteral("channels"), channels},
            {QStringLiteral("presetId"), state.preservedUnknownPresetId.isEmpty()
                                             ? state.presetId
                                             : state.preservedUnknownPresetId},
            {QStringLiteral("autoReset"), registration.realtimeState->automaticResetEnabled()},
            {QStringLiteral("silenceThresholdDbfs"),
             registration.realtimeState->silenceThresholdDbfs()},
            {QStringLiteral("silenceDurationSeconds"),
             registration.realtimeState->silenceDurationSeconds()},
            {QStringLiteral("history"),
             QJsonObject{{QStringLiteral("windowSeconds"),
                          static_cast<double>(state.historyView.timeWindowMilliseconds) / 1000.0},
                         {QStringLiteral("viewMode"),
                          state.historyView.mode == MeterHistoryViewMode::scanning
                              ? QStringLiteral("scanning")
                              : QStringLiteral("scrolling")},
                         {QStringLiteral("paused"), state.historyView.paused},
                         {QStringLiteral("visibleSeries"), visibleSeries},
                         {QStringLiteral("realtimeCsv"), state.recordingEnabled}}}});
    }
    return meters;
}

void LiveMeterDataProvider::restoreProjectMeters(const QJsonArray &meters)
{
    const auto registrations = WinJACKNexus::Common::meterProbeRegistrations();
    std::lock_guard lock(mutex_);
    for (const auto &[instanceId, state] : probeStates_)
    {
        if (state.recordingEnabled)
            csvWriter_->closeSession(QString::fromStdString(instanceId));
    }
    probeStates_.clear();
    for (const auto &value : meters)
    {
        if (!value.isObject())
            continue;
        const auto meter = value.toObject();
        const auto instanceId = meter.value(QStringLiteral("instanceId")).toString().toStdString();
        if (instanceId.empty())
            continue;
        auto &state = stateFor(instanceId);
        const auto presetId = meter.value(QStringLiteral("presetId")).toString().trimmed();
        const auto knownPreset =
            WinJACKNexus::Common::isKnownMeterLoudnessPreset(presetId.toStdString()) ||
            WinJACKNexus::Common::findCustomMeterLoudnessPreset(presetId).has_value();
        state.preservedUnknownPresetId = presetId.isEmpty() || knownPreset ? QString() : presetId;
        state.presetId =
            presetId.isEmpty() || !knownPreset ? globalSettings_.defaultPresetId : presetId;
        state.configurationInitialized = true;
        const auto history = meter.value(QStringLiteral("history")).toObject();
        const auto windowSeconds =
            history.value(QStringLiteral("windowSeconds"))
                .toDouble(static_cast<double>(defaultMeterHistoryWindowMilliseconds) / 1000.0);
        if (std::isfinite(windowSeconds) && windowSeconds > 0.0)
            state.historyView.timeWindowMilliseconds = std::clamp<std::int64_t>(
                static_cast<std::int64_t>(std::llround(windowSeconds * 1000.0)),
                minimumMeterHistoryWindowMilliseconds, maximumMeterHistoryWindowMilliseconds);
        state.historyView.mode =
            history.value(QStringLiteral("viewMode")).toString() == QStringLiteral("scanning")
                ? MeterHistoryViewMode::scanning
                : MeterHistoryViewMode::realtimeScroll;
        state.historyView.paused = history.value(QStringLiteral("paused")).toBool(false);
        state.historyView.visibleSeriesIds.clear();
        for (const auto &seriesValue : history.value(QStringLiteral("visibleSeries")).toArray())
        {
            const auto seriesId = seriesValue.toString();
            if (!seriesId.isEmpty() && !containsId(state.historyView.visibleSeriesIds, seriesId))
                state.historyView.visibleSeriesIds.push_back(seriesId);
        }
        if (state.historyView.visibleSeriesIds.empty())
        {
            state.historyView.visibleSeriesIds = {
                QStringLiteral("peak"),       QStringLiteral("rms"),
                QStringLiteral("lfe"),        QStringLiteral("true-peak"),
                QStringLiteral("momentary"),  QStringLiteral("short-term"),
                QStringLiteral("integrated"), QStringLiteral("lra")};
        }
        state.recordingEnabled = history.value(QStringLiteral("realtimeCsv")).toBool(false);

        const auto registration = std::find_if(
            registrations.cbegin(), registrations.cend(), [&instanceId](const auto &candidate) {
                return candidate.descriptor.instanceId == instanceId;
            });
        if (registration != registrations.cend() && registration->realtimeState != nullptr)
        {
            registration->realtimeState->setAutomaticResetEnabled(
                meter.value(QStringLiteral("autoReset")).toBool(true));
            const auto threshold =
                static_cast<float>(meter.value(QStringLiteral("silenceThresholdDbfs"))
                                       .toDouble(globalSettings_.silenceThresholdDbfs));
            const auto duration =
                static_cast<float>(meter.value(QStringLiteral("silenceDurationSeconds"))
                                       .toDouble(globalSettings_.silenceDurationSeconds));
            if (isValidSilenceThreshold(threshold) && isValidSilenceDuration(duration))
                registration->realtimeState->setSilenceResetConfiguration(threshold, duration);
        }
    }
}

LiveMeterDataProvider::ProbeViewState &LiveMeterDataProvider::stateFor(
    const std::string &instanceId) const
{
    auto [iterator, inserted] = probeStates_.try_emplace(instanceId);
    if (inserted)
        iterator->second.presetId = globalSettings_.defaultPresetId;
    return iterator->second;
}

std::vector<MeterViewSnapshot> LiveMeterDataProvider::snapshots() const
{
    return snapshots(true);
}

std::vector<MeterViewSnapshot> LiveMeterDataProvider::snapshots(bool includeHistory) const
{
    const auto registrations = WinJACKNexus::Common::meterProbeRegistrations();
    std::lock_guard lock(mutex_);

    std::unordered_set<std::string> activeIds;
    activeIds.reserve(registrations.size());
    std::vector<MeterViewSnapshot> result;
    result.reserve(registrations.size());
    for (const auto &registration : registrations)
    {
        if (registration.realtimeState == nullptr)
            continue;

        const auto &descriptor = registration.descriptor;
        activeIds.insert(descriptor.instanceId);
        auto &state = stateFor(descriptor.instanceId);
        if (!state.configurationInitialized)
        {
            state.presetId = descriptor.followGlobalPreset
                                 ? globalSettings_.defaultPresetId
                                 : QString::fromStdString(descriptor.presetId).trimmed();
            if (state.presetId.isEmpty())
                state.presetId = globalSettings_.defaultPresetId;
            state.configurationInitialized = true;
        }
        registration.realtimeState->setSilenceResetConfiguration(
            globalSettings_.silenceThresholdDbfs, globalSettings_.silenceDurationSeconds);
        const auto loudnessResetGeneration = registration.realtimeState->loudnessResetGeneration();
        if (loudnessResetGeneration != state.observedLoudnessResetGeneration)
        {
            state.observedLoudnessResetGeneration = loudnessResetGeneration;
            state.history.clear();
            state.hasFrame = false;
            if (state.recordingEnabled)
                csvWriter_->closeSession(QString::fromStdString(descriptor.instanceId));
        }
        const auto realtimeFrame = registration.realtimeState->frame();
        const auto isOnline = registration.realtimeState->isOnline();
        const auto processedFrames = realtimeFrame.meter.processedFrames;
        const auto sampleRate = descriptor.sampleRate > 0.0 ? descriptor.sampleRate : 48000.0;
        const auto historyFrameInterval = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::llround(sampleRate / historySampleRate)));
        if (isOnline && (!state.hasFrame || processedFrames != state.lastProcessedFrames))
        {
            const auto resetDetected =
                state.hasFrame && processedFrames < state.lastProcessedFrames;
            if (!state.hasFrame || resetDetected)
            {
                state.history.clear();
                state.firstProcessedFrames = processedFrames;
                state.lastHistoryProcessedFrames = processedFrames;
            }

            if (!state.hasFrame || resetDetected ||
                processedFrames - state.lastHistoryProcessedFrames >= historyFrameInterval)
            {
                MeterHistoryPoint point;
                const auto elapsedFrames = processedFrames >= state.firstProcessedFrames
                                               ? processedFrames - state.firstProcessedFrames
                                               : 0;
                point.elapsedMilliseconds = static_cast<std::int64_t>(
                    std::llround(static_cast<double>(elapsedFrames) * 1000.0 / sampleRate));
                point.systemTimeMilliseconds = QDateTime::currentMSecsSinceEpoch();
                point.metrics = viewMetrics(realtimeFrame.meter);
                point.lfePeakDbfs = viewValue(realtimeFrame.meter.lfePeakDbfs);
                point.discontinuity = resetDetected;
                state.history.push_back(std::move(point));
                if (state.recordingEnabled)
                {
                    csvWriter_->enqueue({QString::fromStdString(descriptor.instanceId),
                                         QString::fromStdString(descriptor.clientName),
                                         globalSettings_.logRootDirectory, state.history.back()});
                }
                if (state.history.size() > maximumHistoryPointCount)
                    state.history.erase(state.history.begin(),
                                        state.history.begin() +
                                            (state.history.size() - maximumHistoryPointCount));
                state.lastHistoryProcessedFrames = processedFrames;
            }
            state.lastProcessedFrames = processedFrames;
            state.hasFrame = true;
        }

        MeterViewSnapshot snapshot;
        snapshot.instanceId = QString::fromStdString(descriptor.instanceId);
        snapshot.clientName = QString::fromStdString(descriptor.clientName);
        snapshot.nameRevision = registration.nameRevision;
        snapshot.layoutLabel = layoutLabel(descriptor);
        snapshot.presetId = state.presetId;
        snapshot.state = probeState(registration, realtimeFrame.meter);
        snapshot.automaticResetEnabled = registration.realtimeState->automaticResetEnabled();
        snapshot.recordingEnabled = state.recordingEnabled;
        snapshot.currentMetrics = isOnline ? viewMetrics(realtimeFrame.meter) : MeterMetrics{};
        snapshot.lfePeakDbfs = isOnline ? viewValue(realtimeFrame.meter.lfePeakDbfs) : std::nullopt;
        if (includeHistory)
            snapshot.history = state.history;
        if (!state.history.empty())
            snapshot.latestHistoryElapsedMilliseconds = state.history.back().elapsedMilliseconds;
        snapshot.historyView = state.historyView;
        snapshot.globalSettings = globalSettings_;
        const auto csvStatus = csvWriter_->status(snapshot.instanceId);
        snapshot.csvPreview.logRootDirectory = globalSettings_.logRootDirectory;
        snapshot.csvPreview.activeFilePath = csvStatus.activeFilePath;
        snapshot.csvPreview.rotationStatus =
            state.recordingEnabled
                ? (csvStatus.rotationStatus.isEmpty() ? QStringLiteral("CSV 等待第一条记录")
                                                      : csvStatus.rotationStatus)
                : QStringLiteral("CSV 记录已停止");
        snapshot.csvPreview.statusKind = !state.recordingEnabled ? MeterCsvStatusKind::stopped
                                         : csvStatus.rotationStatus.isEmpty()
                                             ? MeterCsvStatusKind::waiting
                                             : csvStatus.statusKind;
        snapshot.csvPreview.statusDetail = csvStatus.statusDetail;
        snapshot.csvPreview.droppedRecordCount = csvStatus.droppedRecordCount;
        snapshot.channelLayoutEditable = false;
        snapshot.channels.reserve(descriptor.channelCount);
        for (std::size_t index = 0; index < descriptor.channelCount; ++index)
        {
            const auto &channel = descriptor.channels[index];
            snapshot.channels.push_back(
                {QString::fromStdString(channel.name),
                 channel.lfe ? QStringLiteral("LFE") : speakerRoleLabel(channel.role),
                 channel.lfe});
        }
        result.push_back(std::move(snapshot));
    }

    std::erase_if(probeStates_,
                  [&activeIds](const auto &item) { return !activeIds.contains(item.first); });
    return result;
}

bool LiveMeterDataProvider::dispatchGlobalSetting(const MeterViewCommand &command)
{
    const auto registrations = WinJACKNexus::Common::meterProbeRegistrations();
    std::lock_guard lock(mutex_);
    auto updated = globalSettings_;
    switch (command.type)
    {
    case MeterViewCommandType::setGlobalPreset:
        updated.defaultPresetId = command.presetId.trimmed();
        if (updated.defaultPresetId.isEmpty())
            return false;
        break;
    case MeterViewCommandType::setGlobalSilenceThreshold:
        if (!command.numericValue.has_value() || !isValidSilenceThreshold(*command.numericValue))
            return false;
        updated.silenceThresholdDbfs = *command.numericValue;
        break;
    case MeterViewCommandType::setGlobalSilenceDuration:
        if (!command.numericValue.has_value() || !isValidSilenceDuration(*command.numericValue))
            return false;
        updated.silenceDurationSeconds = *command.numericValue;
        break;
    case MeterViewCommandType::setGlobalLogRootDirectory:
        updated.logRootDirectory = normalizedLogRootDirectory(command.textValue);
        break;
    default:
        return false;
    }
    globalSettings_ = std::move(updated);
    for (const auto &registration : registrations)
    {
        if (registration.realtimeState != nullptr)
        {
            registration.realtimeState->setSilenceResetConfiguration(
                globalSettings_.silenceThresholdDbfs, globalSettings_.silenceDurationSeconds);
        }
    }
    saveGlobalSettings(globalSettings_);
    if (command.type == MeterViewCommandType::setGlobalLogRootDirectory)
    {
        for (const auto &[instanceId, state] : probeStates_)
        {
            if (state.recordingEnabled)
                csvWriter_->closeSession(QString::fromStdString(instanceId));
        }
    }
    return true;
}

bool LiveMeterDataProvider::dispatch(const MeterViewCommand &command)
{
    switch (command.type)
    {
    case MeterViewCommandType::setGlobalPreset:
    case MeterViewCommandType::setGlobalSilenceThreshold:
    case MeterViewCommandType::setGlobalSilenceDuration:
    case MeterViewCommandType::setGlobalLogRootDirectory:
        return dispatchGlobalSetting(command);
    default:
        break;
    }

    const auto registrations = WinJACKNexus::Common::meterProbeRegistrations();
    const auto registration = std::find_if(
        registrations.cbegin(), registrations.cend(), [&command](const auto &candidate) {
            return QString::fromStdString(candidate.descriptor.instanceId) == command.instanceId;
        });
    if (registration == registrations.cend() || registration->realtimeState == nullptr)
        return false;

    std::lock_guard lock(mutex_);
    auto &state = stateFor(registration->descriptor.instanceId);
    switch (command.type)
    {
    case MeterViewCommandType::resetLoudness:
        registration->realtimeState->requestLoudnessReset();
        return true;
    case MeterViewCommandType::setAutomaticReset:
        if (!command.enabled.has_value())
            return false;
        registration->realtimeState->setAutomaticResetEnabled(*command.enabled);
        return true;
    case MeterViewCommandType::setRecording:
        if (!command.enabled.has_value())
            return false;
        state.recordingEnabled = *command.enabled;
        if (!state.recordingEnabled)
            csvWriter_->closeSession(command.instanceId);
        return true;
    case MeterViewCommandType::setPreset:
        if (command.presetId.trimmed().isEmpty())
            return false;
        state.presetId = command.presetId.trimmed();
        state.preservedUnknownPresetId.clear();
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
    case MeterViewCommandType::setHistorySeriesVisible:
        if (!command.enabled.has_value() || command.seriesId.isEmpty())
            return false;
        if (*command.enabled && !containsId(state.historyView.visibleSeriesIds, command.seriesId))
            state.historyView.visibleSeriesIds.push_back(command.seriesId);
        else if (!*command.enabled)
            std::erase(state.historyView.visibleSeriesIds, command.seriesId);
        return true;
    case MeterViewCommandType::clearHistory:
        state.history.clear();
        return true;
    case MeterViewCommandType::setCustomChannelCount:
    case MeterViewCommandType::setChannelDisplayName:
    case MeterViewCommandType::setChannelRole:
    case MeterViewCommandType::setChannelLfe:
        return false;
    }
    return false;
}

}