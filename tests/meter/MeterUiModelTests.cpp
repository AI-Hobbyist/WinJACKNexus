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
#include <cstdio>

namespace {

using WinJACKNexus::Meter::MeterProbeState;
using WinJACKNexus::Meter::MeterViewCommand;
using WinJACKNexus::Meter::MeterViewCommandType;
using WinJACKNexus::Meter::MockMeterDataProvider;
using WinJACKNexus::Meter::MockMeterScenario;

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

WinJACKNexus::Meter::MeterViewSnapshot singleSnapshot(const MockMeterDataProvider& provider)
{
    return provider.snapshots().front();
}

bool testScenarioCoverage()
{
    MockMeterDataProvider provider;
    bool passed = true;
    const auto scenarios = provider.availableScenarios();
    passed &= check(scenarios.size() == 11, "mock provider exposes every UI-1 scenario");
    passed &= check(std::find(scenarios.cbegin(), scenarios.cend(), MockMeterScenario::longHistory) != scenarios.cend(),
                    "mock provider exposes the long-history scenario");

    provider.setScenario(MockMeterScenario::normalProgram);
    const auto& normal = singleSnapshot(provider);
    passed &= check(normal.instanceId == QStringLiteral("meter-preview-1") && normal.nameRevision == 1,
                    "normal program exposes stable identity and name revision");
    passed &= check(normal.lfePeakDbfs == std::nullopt && normal.channels.size() == 2,
                    "normal program has no LFE and remains stereo");

    provider.setScenario(MockMeterScenario::silence);
    passed &= check(singleSnapshot(provider).state == MeterProbeState::silent,
                    "silence scenario is explicitly marked silent");

    provider.setScenario(MockMeterScenario::overload);
    const auto& overload = singleSnapshot(provider);
    passed &= check(overload.state == MeterProbeState::overload
                        && overload.currentMetrics.truePeakDbtp.has_value()
                        && *overload.currentMetrics.truePeakDbtp > 0.0f,
                    "over-limit scenario contains a positive true peak");

    provider.setScenario(MockMeterScenario::monoWithoutLfe);
    passed &= check(singleSnapshot(provider).channels.size() == 1 && !singleSnapshot(provider).lfePeakDbfs.has_value(),
                    "mono scenario contains one non-LFE channel");

    provider.setScenario(MockMeterScenario::surroundWithLfe);
    const auto& surround = singleSnapshot(provider);
    passed &= check(surround.channels.size() == 6 && surround.lfePeakDbfs.has_value()
                        && std::count_if(surround.channels.cbegin(), surround.channels.cend(), [](const auto& channel) {
                               return channel.isLfe;
                           }) == 1,
                    "5.1 scenario exposes a distinct LFE channel and value");

    provider.setScenario(MockMeterScenario::multipleLfe);
    const auto& multiLfe = singleSnapshot(provider);
    passed &= check(std::count_if(multiLfe.channels.cbegin(), multiLfe.channels.cend(), [](const auto& channel) {
                        return channel.isLfe;
                    }) == 2,
                    "multiple-LFE scenario marks both diagnostic channels");

    provider.setScenario(MockMeterScenario::renamed);
    const auto& renamed = singleSnapshot(provider);
    passed &= check(renamed.instanceId == normal.instanceId && renamed.clientName == QStringLiteral("Program-2")
                        && renamed.nameRevision > normal.nameRevision,
                    "rename changes the visible client name without changing identity");

    provider.setScenario(MockMeterScenario::offline);
    const auto& offline = singleSnapshot(provider);
    passed &= check(offline.state == MeterProbeState::offline && !offline.currentMetrics.peakDbfs.has_value(),
                    "offline scenario does not present stale measurements");

    provider.setScenario(MockMeterScenario::reset);
    const auto& reset = singleSnapshot(provider);
    passed &= check(!reset.currentMetrics.integratedLufs.has_value() && reset.history.empty(),
                    "reset scenario clears session metrics and history");
    return passed;
}

bool testCustomChannelRangeAndHistory()
{
    MockMeterDataProvider provider;
    provider.setScenario(MockMeterScenario::customChannels);
    bool passed = true;
    for (std::uint16_t count = 1; count <= 16; ++count) {
        provider.setCustomChannelCount(count);
        const auto& snapshot = singleSnapshot(provider);
        passed &= check(snapshot.channels.size() == count,
                        "custom scenario supports the requested channel count");
    }
    provider.setScenario(MockMeterScenario::longHistory);
    const auto firstHistory = provider.snapshots().front().history;
    const auto secondHistory = provider.snapshots().front().history;
    passed &= check(firstHistory.size() == 6000 && firstHistory == secondHistory,
                    "long-history samples are deterministic and retain ten minutes at 10 Hz");
    return passed;
}

bool testCommandInterface()
{
    MockMeterDataProvider provider;
    MeterViewCommand command;
    command.instanceId = QStringLiteral("meter-preview-1");
    command.type = MeterViewCommandType::setRecording;
    command.enabled = true;
    bool passed = check(provider.dispatch(command) && singleSnapshot(provider).recordingEnabled,
                        "recording command updates mock UI state");

    command.type = MeterViewCommandType::setAutomaticReset;
    command.enabled = false;
    passed &= check(provider.dispatch(command) && !singleSnapshot(provider).automaticResetEnabled,
                    "automatic-reset command updates mock UI state");

    command.type = MeterViewCommandType::resetLoudness;
    command.enabled.reset();
    passed &= check(provider.dispatch(command) && !singleSnapshot(provider).currentMetrics.integratedLufs.has_value(),
                    "reset command clears the session measurement surface");

    command.instanceId = QStringLiteral("another-instance");
    passed &= check(!provider.dispatch(command), "commands reject an unknown stable instance id");
    return passed;
}

bool testPreviewProbeLifecycle()
{
    MockMeterDataProvider provider;
    provider.setPreviewProbeCount(3);
    const auto snapshots = provider.snapshots();
    bool passed = true;
    passed &= check(snapshots.size() == 3 && snapshots[0].instanceId == QStringLiteral("meter-preview-1")
                        && snapshots[1].instanceId == QStringLiteral("meter-preview-2")
                        && snapshots[2].instanceId == QStringLiteral("meter-preview-3"),
                    "preview provider exposes distinct stable identities for multiple probes");

    MeterViewCommand command;
    command.instanceId = QStringLiteral("meter-preview-2");
    command.type = MeterViewCommandType::setPreset;
    command.presetId = QStringLiteral("atsc-a85");
    passed &= check(provider.dispatch(command) && provider.snapshots()[1].presetId == QStringLiteral("atsc-a85"),
                    "preset commands update only the addressed preview probe");

    provider.setScenario(MockMeterScenario::renamed);
    const auto renamed = singleSnapshot(provider);
    passed &= check(renamed.instanceId == QStringLiteral("meter-preview-1")
                        && renamed.clientName == QStringLiteral("Program-2")
                        && renamed.nameRevision == 2,
                    "rename preview retains its stable identity while advancing its revision");

    provider.setPreviewProbeCount(0);
    passed &= check(provider.snapshots().empty(), "preview provider supports an empty bridge state");
    return passed;
}

}

int main()
{
    const auto passed = testScenarioCoverage()
        && testCustomChannelRangeAndHistory()
        && testCommandInterface()
        && testPreviewProbeLifecycle();
    if (!passed)
        return 1;
    std::puts("Meter UI model tests passed");
    return 0;
}