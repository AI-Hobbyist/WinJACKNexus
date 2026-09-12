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
#include <WinJACKNexus/Common/MeterProbeRegistry.hpp>
#include <WinJACKNexus/Meter/MeterCsvWriter.hpp>
#include <WinJACKNexus/Meter/LiveMeterDataProvider.hpp>

#include <QDateTime>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <memory>

namespace {

using WinJACKNexus::Common::MeterChannelRole;
using WinJACKNexus::Common::MeterProbeDescriptor;
using WinJACKNexus::Common::MeterProbeRealtimeState;
using WinJACKNexus::Common::MeterSnapshot;
using WinJACKNexus::Common::MeterValue;
using WinJACKNexus::Meter::LiveMeterDataProvider;
using WinJACKNexus::Meter::MeterProbeState;
using WinJACKNexus::Meter::MeterViewCommand;
using WinJACKNexus::Meter::MeterViewCommandType;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool closeEnough(const std::optional<float>& value, float expected)
{
    return value.has_value() && std::abs(*value - expected) < 0.001F;
}

MeterProbeDescriptor descriptor()
{
    MeterProbeDescriptor value;
    value.instanceId = "live-provider-test";
    value.clientName = "Live Probe";
    value.channelCount = 6;
    value.sampleRate = 48000.0;
    value.silenceThresholdDbfs = -70.0F;
    value.channels[0] = { "L", MeterChannelRole::frontLeft, false };
    value.channels[1] = { "R", MeterChannelRole::frontRight, false };
    value.channels[2] = { "C", MeterChannelRole::frontCenter, false };
    value.channels[3] = { "LFE", MeterChannelRole::lfe, true };
    value.channels[4] = { "Ls", MeterChannelRole::sideLeft, false };
    value.channels[5] = { "Rs", MeterChannelRole::sideRight, false };
    return value;
}

MeterSnapshot frame(std::uint64_t processedFrames)
{
    return {
        MeterValue { -1.5, true },
        MeterValue { -18.5, true },
        MeterValue { -0.5, true },
        MeterValue { -7.5, true },
        MeterValue { -16.5, true },
        MeterValue { -17.5, true },
        MeterValue { -18.5, true },
        MeterValue { 5.5, true },
        processedFrames,
        0
    };
}

bool testRegistryFramesDriveLiveSnapshot()
{
    QSettings settings(QStringLiteral("WinJACKNexus"), QStringLiteral("Meterbridge"));
    const auto previousPreset = settings.value(QStringLiteral("defaultPresetId"));
    const auto previousThreshold = settings.value(QStringLiteral("silenceThresholdDbfs"));
    const auto previousDuration = settings.value(QStringLiteral("silenceDurationSeconds"));
    const auto previousLogRoot = settings.value(QStringLiteral("logRootDirectory"));
    auto realtimeState = std::make_shared<MeterProbeRealtimeState>();
    auto registeredDescriptor = descriptor();
    bool passed = check(WinJACKNexus::Common::registerMeterProbe(registeredDescriptor, realtimeState),
                        "live test Probe registers in Common");
    realtimeState->setOnline(true);
    realtimeState->publish(frame(4800));

    LiveMeterDataProvider provider;
    const auto timeBeforeSnapshot = QDateTime::currentMSecsSinceEpoch();
    auto snapshots = provider.snapshots();
    const auto timeAfterSnapshot = QDateTime::currentMSecsSinceEpoch();
    passed &= check(snapshots.size() == 1
                        && snapshots.front().instanceId == QStringLiteral("live-provider-test")
                        && snapshots.front().clientName == QStringLiteral("Live Probe")
                        && snapshots.front().nameRevision == 1
                        && snapshots.front().channels.size() == 6,
                    "live provider exposes registered identity, revision and channel layout");
    const auto& snapshot = snapshots.front();
    passed &= check(closeEnough(snapshot.currentMetrics.peakDbfs, -1.5F)
                        && closeEnough(snapshot.currentMetrics.rmsDbfs, -18.5F)
                        && closeEnough(snapshot.currentMetrics.truePeakDbtp, -0.5F)
                        && closeEnough(snapshot.currentMetrics.momentaryLufs, -16.5F)
                        && closeEnough(snapshot.currentMetrics.shortTermLufs, -17.5F)
                        && closeEnough(snapshot.currentMetrics.integratedLufs, -18.5F)
                        && closeEnough(snapshot.currentMetrics.lraLu, 5.5F),
                    "live provider maps all current Common meter values");
    passed &= check(closeEnough(snapshot.lfePeakDbfs, -7.5F)
                        && snapshot.channels[3].isLfe
                        && snapshot.history.size() == 1
                        && snapshot.history.front().elapsedMilliseconds == 0
                        && snapshot.history.front().systemTimeMilliseconds.has_value()
                        && *snapshot.history.front().systemTimeMilliseconds >= timeBeforeSnapshot
                        && *snapshot.history.front().systemTimeMilliseconds <= timeAfterSnapshot
                        && closeEnough(snapshot.history.front().lfePeakDbfs, -7.5F),
                    "live provider preserves Common LFE peak and timestamps the first history point with system time");

    snapshots = provider.snapshots();
    passed &= check(snapshots.front().history.size() == 1,
                    "repeated UI reads do not oversample unchanged realtime history");

    realtimeState->publish(frame(7200));
    snapshots = provider.snapshots();
    passed &= check(snapshots.front().history.size() == 1,
                    "realtime frames below the 100 ms interval do not oversample history");

    realtimeState->publish(frame(9600));
    snapshots = provider.snapshots();
    passed &= check(snapshots.front().history.size() == 2
                        && snapshots.front().history.back().elapsedMilliseconds == 100,
                    "new realtime frames append 10 Hz history points using the Probe sample rate");

    realtimeState->publish(frame(10400));
    const auto lightweightSnapshots = provider.snapshots(false);
    snapshots = provider.snapshots();
    passed &= check(lightweightSnapshots.front().history.empty()
                        && lightweightSnapshots.front().latestHistoryElapsedMilliseconds == 100
                        && snapshots.front().history.size() == 2
                        && snapshots.front().history.back().elapsedMilliseconds == 100,
                    "live UI refreshes expose the latest 100 ms history slot without copying history into every card frame");

    MeterViewCommand command;
    command.instanceId = QStringLiteral("live-provider-test");
    command.type = MeterViewCommandType::setAutomaticReset;
    command.enabled = false;
    passed &= check(provider.dispatch(command) && !realtimeState->automaticResetEnabled(),
                    "automatic-reset commands reach the Probe realtime state");

    command.type = MeterViewCommandType::setRecording;
    command.enabled = true;
    passed &= check(provider.dispatch(command) && provider.snapshots().front().recordingEnabled,
                    "recording commands update the live session state");

    command.type = MeterViewCommandType::setPreset;
    command.enabled.reset();
    command.presetId = QStringLiteral("atsc-a85");
    passed &= check(provider.dispatch(command)
                        && provider.snapshots().front().presetId == QStringLiteral("atsc-a85"),
                    "individual preset changes are retained by the live session state");

    command.type = MeterViewCommandType::setGlobalPreset;
    command.presetId = QStringLiteral("spotify-default");
    passed &= check(provider.dispatch(command)
                        && provider.snapshots().front().presetId == QStringLiteral("atsc-a85"),
                    "changing the default preset does not overwrite an existing card preset");

    auto newDescriptor = descriptor();
    newDescriptor.instanceId = "live-provider-new-card";
    newDescriptor.clientName = "New Live Probe";
    newDescriptor.followGlobalPreset = true;
    auto newRealtimeState = std::make_shared<MeterProbeRealtimeState>();
    newRealtimeState->setOnline(true);
    newRealtimeState->publish(frame(4800));
    passed &= check(WinJACKNexus::Common::registerMeterProbe(newDescriptor, newRealtimeState),
                    "new live Probe registers for default-preset initialization");
    const auto newSnapshots = provider.snapshots();
    const auto newSnapshot = std::find_if(newSnapshots.cbegin(), newSnapshots.cend(), [](const auto& value) {
        return value.instanceId == QStringLiteral("live-provider-new-card");
    });
    passed &= check(newSnapshot != newSnapshots.cend()
                        && newSnapshot->presetId == QStringLiteral("spotify-default"),
                    "a new card adopts the current default preset exactly once");
    WinJACKNexus::Common::unregisterMeterProbe(newDescriptor.instanceId, newRealtimeState);

    command.type = MeterViewCommandType::setGlobalSilenceThreshold;
    command.numericValue = -61.0F;
    command.presetId.clear();
    passed &= check(provider.dispatch(command), "global silence-threshold commands are accepted");
    command.type = MeterViewCommandType::setGlobalSilenceDuration;
    command.numericValue = 4.0F;
    passed &= check(provider.dispatch(command), "global silence-duration commands are accepted");
    const auto appliedToRealtimeStateImmediately = closeEnough(std::optional<float>(realtimeState->silenceThresholdDbfs()), -61.0F)
        && closeEnough(std::optional<float>(realtimeState->silenceDurationSeconds()), 4.0F);
    snapshots = provider.snapshots();
    passed &= check(appliedToRealtimeStateImmediately
                        && closeEnough(std::optional<float>(snapshots.front().globalSettings.silenceThresholdDbfs), -61.0F)
                        && closeEnough(std::optional<float>(snapshots.front().globalSettings.silenceDurationSeconds), 4.0F),
                    "global silence settings are persisted and propagated through Probe atomics immediately");

    command.type = MeterViewCommandType::setHistoryViewMode;
    command.historyViewMode = WinJACKNexus::Meter::MeterHistoryViewMode::scanning;
    command.numericValue.reset();
    passed &= check(provider.dispatch(command), "history view mode is saved for project persistence");
    command.type = MeterViewCommandType::setHistoryTimeWindow;
    command.historyTimeWindowMilliseconds = 300000;
    passed &= check(provider.dispatch(command), "history time window is saved for project persistence");
    const auto projectMeters = provider.projectMeters();
    LiveMeterDataProvider restoredProvider;
    restoredProvider.restoreProjectMeters(projectMeters);
    const auto restoredSnapshots = restoredProvider.snapshots();
    passed &= check(restoredSnapshots.size() == 1
                        && restoredSnapshots.front().presetId == QStringLiteral("atsc-a85")
                        && restoredSnapshots.front().recordingEnabled
                        && restoredSnapshots.front().historyView.mode == WinJACKNexus::Meter::MeterHistoryViewMode::scanning
                        && restoredSnapshots.front().historyView.timeWindowMilliseconds == 300000
                        && !realtimeState->automaticResetEnabled(),
                    "meters[] restores per-Probe preset, recording, history settings and automatic reset by instance id");
    auto unknownPresetMeters = projectMeters;
    auto unknownPresetMeter = unknownPresetMeters.at(0).toObject();
    unknownPresetMeter.insert(QStringLiteral("presetId"), QStringLiteral("future-platform-preset"));
    unknownPresetMeters.replace(0, unknownPresetMeter);
    LiveMeterDataProvider unknownPresetProvider;
    unknownPresetProvider.restoreProjectMeters(unknownPresetMeters);
    const auto unknownPresetSnapshots = unknownPresetProvider.snapshots();
    passed &= check(unknownPresetSnapshots.size() == 1
                        && unknownPresetSnapshots.front().presetId == unknownPresetSnapshots.front().globalSettings.defaultPresetId
                        && unknownPresetProvider.projectMeters().at(0).toObject().value(QStringLiteral("presetId")).toString()
                            == QStringLiteral("future-platform-preset"),
                    "unknown project presets fall back to the global default while preserving their original id");

    command.type = MeterViewCommandType::resetLoudness;
        command.presetId.clear();
    std::uint64_t resetGeneration = 0;
    passed &= check(provider.dispatch(command) && realtimeState->consumeLoudnessResetRequest(resetGeneration),
                    "reset commands reach the Probe callback through an atomic request");

    registeredDescriptor.clientName = "Live Probe Renamed";
    passed &= check(WinJACKNexus::Common::registerMeterProbe(registeredDescriptor, realtimeState),
                    "same Probe can update its registry descriptor");
    snapshots = provider.snapshots();
    passed &= check(snapshots.front().clientName == QStringLiteral("Live Probe Renamed")
                        && snapshots.front().nameRevision == 2,
                    "client renames retain stable identity and increment the revision");

    realtimeState->setOnline(false);
    snapshots = provider.snapshots();
    passed &= check(snapshots.front().state == MeterProbeState::offline
                        && !snapshots.front().currentMetrics.peakDbfs.has_value()
                        && !snapshots.front().lfePeakDbfs.has_value(),
                    "offline Probes do not surface stale realtime measurements");

    WinJACKNexus::Common::unregisterMeterProbe(registeredDescriptor.instanceId, realtimeState);
    passed &= check(provider.snapshots().empty(), "unregistered Probes are removed from the bridge collection");
    const auto restoreSetting = [&settings](const QString& key, const QVariant& previous) {
        if (previous.isValid())
            settings.setValue(key, previous);
        else
            settings.remove(key);
    };
    restoreSetting(QStringLiteral("defaultPresetId"), previousPreset);
    restoreSetting(QStringLiteral("silenceThresholdDbfs"), previousThreshold);
    restoreSetting(QStringLiteral("silenceDurationSeconds"), previousDuration);
    restoreSetting(QStringLiteral("logRootDirectory"), previousLogRoot);
    return passed;
}

bool testCsvWriterRotatesAndSanitizesPaths()
{
    QTemporaryDir directory;
    bool passed = check(directory.isValid(), "temporary CSV directory is available");
    if (!passed)
        return false;

    WinJACKNexus::Meter::MeterCsvWriter writer;
    WinJACKNexus::Meter::MeterCsvWriter::Record record;
    record.instanceId = QStringLiteral("csv-test");
    record.clientName = QStringLiteral("Program: 1. ");
    record.logRootDirectory = directory.path();
    record.point.elapsedMilliseconds = 100;
    record.point.systemTimeMilliseconds = QDateTime::currentMSecsSinceEpoch();
    record.point.metrics.peakDbfs = -1.0F;
    writer.enqueue(record);
    passed &= check(writer.waitForIdle(2000), "CSV writer drains its bounded queue");
    const auto firstStatus = writer.status(record.instanceId);
    passed &= check(!firstStatus.activeFilePath.isEmpty()
                        && QFile::exists(firstStatus.activeFilePath)
                        && firstStatus.activeFilePath.contains(QStringLiteral("Program_ 1")),
                    "CSV writer sanitizes client directory names and creates a session file");

    QFile firstFile(firstStatus.activeFilePath);
    passed &= check(firstFile.open(QIODevice::ReadOnly)
                        && QString::fromUtf8(firstFile.readAll()).startsWith(QStringLiteral("localTime,elapsedMilliseconds")),
                    "CSV writer emits stable UTF-8 column headers");
    firstFile.close();

    writer.closeSession(record.instanceId);
    passed &= check(writer.waitForIdle(2000), "CSV writer closes a session before rotation");
    writer.enqueue(record);
    passed &= check(writer.waitForIdle(2000), "CSV writer writes the rotated session");
    const auto secondStatus = writer.status(record.instanceId);
    passed &= check(!secondStatus.activeFilePath.isEmpty()
                        && secondStatus.activeFilePath != firstStatus.activeFilePath,
                    "CSV writer avoids overwriting a same-timestamp session file");

    writer.closeSession(record.instanceId);
    record.logRootDirectory.clear();
    writer.enqueue(record);
    passed &= check(writer.waitForIdle(2000)
                        && writer.status(record.instanceId).rotationStatus.contains(QStringLiteral("未设置日志保存根目录")),
                    "CSV writer reports an actionable missing-log-root error");
    return passed;
}

}

int main()
{
    if (!testRegistryFramesDriveLiveSnapshot() || !testCsvWriterRotatesAndSanitizesPaths())
        return 1;
    std::puts("Live Meter data provider tests passed");
    return 0;
}