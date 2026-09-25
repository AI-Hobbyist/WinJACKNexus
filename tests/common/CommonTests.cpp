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
#include <WinJACKNexus/Common/Atomic.hpp>
#include <WinJACKNexus/Common/AudioRingBuffer.hpp>
#include <WinJACKNexus/Common/AuxMixerState.hpp>
#include <WinJACKNexus/Common/ChannelLayout.hpp>
#include <WinJACKNexus/NativeFx/compressor/Processor.hpp>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>
#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Common/LoudnessEngine.hpp>
#include <WinJACKNexus/Common/Metadata.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>
#include <WinJACKNexus/Common/RealtimeObject.hpp>
#include <WinJACKNexus/Common/ScopedGuard.hpp>
#include <WinJACKNexus/Common/SilenceResetController.hpp>
#include <WinJACKNexus/Common/Thread.hpp>
#include <WinJACKNexus/Common/Timer.hpp>
#include <WinJACKNexus/Common/Version.hpp>
#include <WinJACKNexus/Common/Window.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool testQueueBasics()
{
    WinJACKNexus::Common::LockFreeSpscQueue<int, 3> queue;
    int value = 0;
    bool passed = true;
    passed &= check(queue.empty(), "SPSC queue starts empty");
    passed &= check(queue.try_push(1), "SPSC queue accepts first value");
    passed &= check(queue.try_push(2), "SPSC queue accepts second value");
    passed &= check(queue.try_push(3), "SPSC queue accepts capacity value");
    passed &= check(!queue.try_push(4), "SPSC queue rejects values while full");
    passed &= check(queue.size() == 3 && queue.full(), "SPSC queue reports full size");
    passed &= check(queue.try_pop(value) && value == 1, "SPSC queue preserves first value");
    passed &= check(queue.try_pop(value) && value == 2, "SPSC queue preserves second value");
    passed &= check(queue.try_pop(value) && value == 3, "SPSC queue preserves third value");
    passed &= check(!queue.try_pop(value) && queue.empty(), "SPSC queue reports empty after draining");
    return passed;
}

bool testQueueStress()
{
    constexpr std::uint64_t itemCount = 10'000'000;
    WinJACKNexus::Common::LockFreeSpscQueue<std::uint64_t, 1024> queue;
    std::atomic<bool> producerFinished { false };
    std::atomic<bool> failed { false };

    std::thread producer([&] {
        for (std::uint64_t value = 0; value < itemCount; ++value) {
            while (!queue.try_push(value))
                std::this_thread::yield();
        }
        producerFinished.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t expectedValue = 0;
        std::uint64_t value = 0;
        while (!producerFinished.load(std::memory_order_acquire) || !queue.empty()) {
            if (!queue.try_pop(value)) {
                std::this_thread::yield();
                continue;
            }
            if (value != expectedValue)
                failed.store(true, std::memory_order_relaxed);
            ++expectedValue;
        }
        while (queue.try_pop(value)) {
            if (value != expectedValue)
                failed.store(true, std::memory_order_relaxed);
            ++expectedValue;
        }
        if (expectedValue != itemCount)
            failed.store(true, std::memory_order_relaxed);
    });

    producer.join();
    consumer.join();
    return check(!failed.load(std::memory_order_acquire), "SPSC queue passes 10M ordered transfers");
}

bool testAudioRingBuffer()
{
    WinJACKNexus::Common::AudioRingBuffer<float> ring(5);
    const std::array<float, 4> firstInput { 1.0F, 2.0F, 3.0F, 4.0F };
    const std::array<float, 5> secondInput { 5.0F, 6.0F, 7.0F, 8.0F, 9.0F };
    std::array<float, 5> output {};
    bool passed = true;

    passed &= check(ring.write(firstInput.data(), firstInput.size()) == 4, "ring buffer writes initial block");
    passed &= check(ring.read(output.data(), 2) == 2, "ring buffer reads partial block");
    passed &= check(output[0] == 1.0F && output[1] == 2.0F, "ring buffer reads initial values");
    passed &= check(ring.write(secondInput.data(), secondInput.size()) == 3, "ring buffer limits writes to free space");
    passed &= check(ring.available() == 5 && ring.freeSpace() == 0, "ring buffer reports wrapped full state");
    passed &= check(ring.read(output.data(), output.size()) == 5, "ring buffer reads wrapped block");
    passed &= check(output == std::array<float, 5> { 3.0F, 4.0F, 5.0F, 6.0F, 7.0F }, "ring buffer preserves wrapped order");
    passed &= check(ring.available() == 0, "ring buffer is empty after reading");
    ring.clear();
    return passed;
}

bool testMeterChannelLayout()
{
    using WinJACKNexus::Common::MeterChannelLayout;

    bool passed = true;
    const auto mono = MeterChannelLayout::mono();
    const auto stereo = MeterChannelLayout::stereo();
    const auto surround = MeterChannelLayout::fivePointOne();
    MeterChannelLayout allLfe(2);
    auto resized = MeterChannelLayout::fivePointOne();

    passed &= check(mono.channelCount() == 1 && mono.measurementWeight(0) == 1.0,
                    "mono layout contributes one full-weight channel");
    passed &= check(stereo.channelCount() == 2 && stereo.hasMeasuredChannels(),
                    "stereo layout exposes both measured channels");
    passed &= check(surround.channelCount() == 6 && surround.isLfe(3)
                        && surround.measurementWeight(3) == 0.0,
                    "5.1 layout excludes its LFE channel");
    passed &= check(std::abs(surround.measurementWeight(4) - MeterChannelLayout::sideChannelWeight) < 1.0e-12,
                    "5.1 side channel uses the BS.1770 surround weight");
    passed &= check(allLfe.setChannel(0, WinJACKNexus::Common::MeterChannelRole::lfe, true)
                        && allLfe.setChannel(1, WinJACKNexus::Common::MeterChannelRole::lfe, true)
                        && !allLfe.hasMeasuredChannels(),
                    "all-LFE layouts are rejected as measurement sources");
    passed &= check(!allLfe.setChannelCount(MeterChannelLayout::maximumChannelCount + 1),
                    "layout rejects more than sixteen channels");
    passed &= check(resized.setChannelCount(2) && resized.setChannelCount(6)
                        && !resized.isLfe(3),
                    "resizing a layout does not restore discarded channel state");
    return passed;
}

bool approximatelyEqual(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

bool testKWeightingCoefficients()
{
    const auto coefficients = WinJACKNexus::Common::kWeightingCoefficients(48000.0);
    bool passed = true;
    passed &= check(coefficients.valid, "K-weighting accepts 48 kHz");
    passed &= check(approximatelyEqual(coefficients.shelf.b0, 1.53512485958697, 1.0e-12)
                        && approximatelyEqual(coefficients.shelf.b1, -2.69169618940638, 1.0e-12)
                        && approximatelyEqual(coefficients.shelf.b2, 1.19839281085285, 1.0e-12)
                        && approximatelyEqual(coefficients.shelf.a1, -1.69065929318241, 1.0e-12)
                        && approximatelyEqual(coefficients.shelf.a2, 0.73248077421585, 1.0e-12),
                    "K-weighting shelf matches the BS.1770 48 kHz coefficients");
    passed &= check(approximatelyEqual(coefficients.highPass.b0, 1.0, 1.0e-12)
                        && approximatelyEqual(coefficients.highPass.b1, -2.0, 1.0e-12)
                        && approximatelyEqual(coefficients.highPass.b2, 1.0, 1.0e-12)
                        && approximatelyEqual(coefficients.highPass.a1, -1.99004745483398, 1.0e-12)
                        && approximatelyEqual(coefficients.highPass.a2, 0.99007225036621, 1.0e-12),
                    "K-weighting high-pass matches the BS.1770 48 kHz coefficients");
    passed &= check(!WinJACKNexus::Common::kWeightingCoefficients(1000.0).valid,
                    "K-weighting rejects unsupported sample rates");
    return passed;
}

bool testLoudnessEngine()
{
    using namespace WinJACKNexus::Common;

    constexpr double sampleRate = 48000.0;
    constexpr std::size_t frames = static_cast<std::size_t>(sampleRate * 3.2);
    std::array<std::vector<float>, 6> channels;
    std::array<std::vector<float>, 6> channelsWithoutLfe;
    std::array<const float*, 6> inputs {};
    std::array<const float*, 6> inputsWithoutLfe {};
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        channels[channel].resize(frames);
        channelsWithoutLfe[channel].resize(frames);
        inputs[channel] = channels[channel].data();
        inputsWithoutLfe[channel] = channelsWithoutLfe[channel].data();
    }

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto signal = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi_v<double> * 1000.0
                                                               * static_cast<double>(frame) / sampleRate));
        for (const auto channel : { 0U, 1U, 2U, 4U, 5U }) {
            channels[channel][frame] = signal;
            channelsWithoutLfe[channel][frame] = signal;
        }
        channels[3][frame] = static_cast<float>(0.9 * std::sin(2.0 * std::numbers::pi_v<double> * 1000.0
                                                                  * static_cast<double>(frame) / sampleRate));
    }

    LoudnessEngine engine;
    LoudnessEngine withoutLfe;
    const auto configuration = LoudnessEngineConfiguration { sampleRate, true };
    bool passed = true;
    passed &= check(engine.prepare(configuration, MeterChannelLayout::fivePointOne()),
                    "loudness engine prepares a 5.1 layout");
    passed &= check(withoutLfe.prepare(configuration, MeterChannelLayout::fivePointOne()),
                    "second loudness engine prepares a 5.1 layout");
    engine.process(inputs.data(), inputs.size(), frames);
    withoutLfe.process(inputsWithoutLfe.data(), inputsWithoutLfe.size(), frames);
    const auto snapshot = engine.snapshot();
    const auto withoutLfeSnapshot = withoutLfe.snapshot();

    passed &= check(snapshot.samplePeakDbfs.valid && approximatelyEqual(snapshot.samplePeakDbfs.value, -6.020599913279624, 1.0e-6),
                    "sample peak excludes the louder LFE input");
    passed &= check(snapshot.lfePeakDbfs.valid && approximatelyEqual(snapshot.lfePeakDbfs.value, -0.9151498112135024, 1.0e-6),
                    "LFE peak remains available as a diagnostic value");
    passed &= check(snapshot.rmsDbfs.valid && snapshot.momentaryLufs.valid
                        && snapshot.shortTermLufs.valid && snapshot.integratedLufs.valid
                        && snapshot.loudnessRangeLu.valid,
                    "engine produces RMS, M, S, I, and LRA after a complete window");
    passed &= check(approximatelyEqual(snapshot.momentaryLufs.value, withoutLfeSnapshot.momentaryLufs.value, 1.0e-12)
                        && approximatelyEqual(snapshot.shortTermLufs.value, withoutLfeSnapshot.shortTermLufs.value, 1.0e-12)
                        && approximatelyEqual(snapshot.integratedLufs.value, withoutLfeSnapshot.integratedLufs.value, 1.0e-12)
                        && approximatelyEqual(snapshot.rmsDbfs.value, withoutLfeSnapshot.rmsDbfs.value, 1.0e-12)
                        && approximatelyEqual(snapshot.truePeakDbtp.value, withoutLfeSnapshot.truePeakDbtp.value, 1.0e-12),
                    "LFE does not affect weighted loudness metrics");
    passed &= check(snapshot.truePeakDbtp.valid && snapshot.truePeakDbtp.value >= snapshot.samplePeakDbfs.value,
                    "true-peak estimation never falls below sample peak");

    LoudnessEngine intersamplePeak;
    std::vector<float> intersampleSignal(frames, 0.0F);
    constexpr double intersamplePhase = std::numbers::pi_v<double> / 2.0 - std::numbers::pi_v<double> / 48.0;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        intersampleSignal[frame] = static_cast<float>(0.5 * std::sin(
            2.0 * std::numbers::pi_v<double> * 1000.0 * static_cast<double>(frame) / sampleRate
            + intersamplePhase));
    }
    const std::array<const float*, 1> intersampleInput { intersampleSignal.data() };
    passed &= check(intersamplePeak.prepare(configuration, MeterChannelLayout::mono()),
                    "true-peak engine prepares a mono layout");
    intersamplePeak.process(intersampleInput.data(), intersampleInput.size(), frames);
    const auto intersampleSnapshot = intersamplePeak.snapshot();
    passed &= check(intersampleSnapshot.truePeakDbtp.valid
                        && intersampleSnapshot.truePeakDbtp.value > intersampleSnapshot.samplePeakDbfs.value + 0.005,
                    "four-times interpolation detects an intersample peak");

    LoudnessEngine opposedStereo;
    const std::array<float, 1> leftOpposed { 0.75F };
    const std::array<float, 1> rightOpposed { -0.75F };
    const std::array<const float*, 2> opposedInput { leftOpposed.data(), rightOpposed.data() };
    passed &= check(opposedStereo.prepare(configuration, MeterChannelLayout::stereo()),
                    "peak engine prepares a stereo layout");
    opposedStereo.process(opposedInput.data(), opposedInput.size(), leftOpposed.size());
    passed &= check(opposedStereo.snapshot().samplePeakDbfs.valid
                        && approximatelyEqual(opposedStereo.snapshot().samplePeakDbfs.value,
                                               -2.4987747321659985,
                                               1.0e-6),
                    "opposed channels cannot cancel the card peak");

    for (const auto rate : { 96000.0, 192000.0 }) {
        LoudnessEngine rateEngine;
        const LoudnessEngineConfiguration rateConfiguration { rate, true };
        const std::array<float, 1> rateSignal { 0.25F };
        const std::array<const float*, 1> rateInput { rateSignal.data() };
        passed &= check(rateEngine.prepare(rateConfiguration, MeterChannelLayout::mono()),
                        "true-peak engine prepares supported high sample rates");
        rateEngine.process(rateInput.data(), rateInput.size(), rateSignal.size());
        const auto rateSnapshot = rateEngine.snapshot();
        passed &= check(rateSnapshot.truePeakDbtp.valid
                            && rateSnapshot.truePeakDbtp.value >= rateSnapshot.samplePeakDbfs.value,
                        "true-peak remains valid at 96 and 192 kHz");
    }

    LoudnessEngine gated;
    LoudnessEngine highOnly;
    constexpr std::size_t gateFrames = static_cast<std::size_t>(sampleRate * 24.0);
    std::vector<float> highSignal(gateFrames, 0.0F);
    std::vector<float> mixedSignal(gateFrames, 0.0F);
    for (std::size_t frame = 0; frame < gateFrames; ++frame) {
        const auto sine = std::sin(2.0 * std::numbers::pi_v<double> * 1000.0
                                   * static_cast<double>(frame) / sampleRate);
        highSignal[frame] = static_cast<float>(0.5 * sine);
        mixedSignal[frame] = static_cast<float>((frame < gateFrames / 2 ? 0.5 : 0.001) * sine);
    }
    const std::array<const float*, 1> highInput { highSignal.data() };
    const std::array<const float*, 1> mixedInput { mixedSignal.data() };
    passed &= check(gated.prepare(configuration, MeterChannelLayout::mono())
                        && highOnly.prepare(configuration, MeterChannelLayout::mono()),
                    "gating engines prepare mono layouts");
    gated.process(mixedInput.data(), mixedInput.size(), gateFrames);
    highOnly.process(highInput.data(), highInput.size(), gateFrames);
    const auto gatedSnapshot = gated.snapshot();
    const auto highOnlySnapshot = highOnly.snapshot();
    passed &= check(gatedSnapshot.integratedLufs.valid && highOnlySnapshot.integratedLufs.valid
                        && approximatelyEqual(gatedSnapshot.integratedLufs.value,
                                               highOnlySnapshot.integratedLufs.value,
                                               0.15),
                    "relative gate excludes programme sections more than ten LU below average");

    LoudnessEngine loudnessRange;
    constexpr std::size_t lraFrames = static_cast<std::size_t>(sampleRate * 24.0);
    std::vector<float> lraSignal(lraFrames, 0.0F);
    for (std::size_t frame = 0; frame < lraFrames; ++frame) {
        const auto sine = std::sin(2.0 * std::numbers::pi_v<double> * 1000.0
                                   * static_cast<double>(frame) / sampleRate);
        lraSignal[frame] = static_cast<float>((frame < lraFrames / 2 ? 0.5 : 0.25) * sine);
    }
    const std::array<const float*, 1> lraInput { lraSignal.data() };
    passed &= check(loudnessRange.prepare(configuration, MeterChannelLayout::mono()),
                    "LRA engine prepares a mono layout");
    loudnessRange.process(lraInput.data(), lraInput.size(), lraFrames);
    passed &= check(loudnessRange.snapshot().loudnessRangeLu.valid
                        && approximatelyEqual(loudnessRange.snapshot().loudnessRangeLu.value, 6.02, 0.15),
                    "LRA uses 10th and 95th percentiles after its relative gate");

    LoudnessEngine silence;
    std::vector<float> quietSignal(frames, 0.0F);
    const std::array<const float*, 1> quietInput { quietSignal.data() };
    passed &= check(silence.prepare(configuration, MeterChannelLayout::mono()),
                    "silence engine prepares mono layout");
    silence.process(quietInput.data(), quietInput.size(), frames);
    passed &= check(!silence.snapshot().integratedLufs.valid,
                    "absolute gate rejects silence");
    LoudnessEngine invalidSamples;
    const std::array<float, 1> nonFiniteSample { std::numeric_limits<float>::quiet_NaN() };
    const std::array<const float*, 1> nonFiniteInput { nonFiniteSample.data() };
    passed &= check(invalidSamples.prepare(configuration, MeterChannelLayout::mono()),
                    "invalid-sample engine prepares mono layout");
    invalidSamples.process(nonFiniteInput.data(), nonFiniteInput.size(), nonFiniteSample.size());
    passed &= check(invalidSamples.snapshot().invalidSampleCount == 1,
                    "non-finite samples are isolated from measurement state");
    engine.reset();
    passed &= check(!engine.snapshot().momentaryLufs.valid && engine.snapshot().processedFrames == 0,
                    "engine reset clears loudness session state");
    return passed;
}

bool testCompressorProcessor()
{
    using namespace WinJACKNexus::Common;
    bool passed = true;

    CompressorParameters parameters;
    parameters.thresholdDb = -12.0F;
    parameters.ratio = 4.0F;
    parameters.kneeDb = 0.0F;
    parameters.attackMs = 0.1F;
    parameters.releaseMs = 100.0F;
    parameters.rms = false;
    parameters.sidechainFilter = false;
    CompressorProcessor processor;
    processor.prepare(48000.0);
    processor.setParameters(parameters);
    float output = 0.0F;
    for (int sample = 0; sample < 12000; ++sample)
        output = processor.process(0.5F);
    passed &= check(std::abs(output - 0.2985F) < 0.015F,
                    "compressor applies the configured ratio above its threshold");
    passed &= check(std::abs(processor.inputLevelDb() + 6.02F) < 0.15F
                        && std::abs(processor.outputLevelDb() + 10.5F) < 0.25F,
                    "compressor reports separate pre and post compression levels");

    parameters.thresholdDb = -12.0F;
    parameters.kneeDb = 6.0F;
    processor.reset();
    processor.setParameters(parameters);
    for (int sample = 0; sample < 12000; ++sample)
        output = processor.process(0.25118864F);
    passed &= check(processor.gainReductionDb() > 0.3F && processor.gainReductionDb() < 0.9F,
                    "soft knee begins compressing smoothly at its center");

    parameters.kneeDb = 0.0F;
    parameters.autoMakeup = true;
    parameters.thresholdDb = -18.0F;
    processor.reset();
    processor.setParameters(parameters);
    for (int sample = 0; sample < 12000; ++sample)
        output = processor.process(0.5F);
    passed &= check(output > 0.5F,
                    "automatic makeup gain compensates for the configured compression");

    parameters.enabled = false;
    processor.setParameters(parameters);
    passed &= check(std::abs(processor.process(-0.375F) + 0.375F) < 1.0e-6F,
                    "disabled compressor preserves the input sample");
    passed &= check(processor.process(std::numeric_limits<float>::quiet_NaN()) == 0.0F,
                    "compressor sanitizes invalid samples without propagating NaN");

    CompressorProcessor rmsProcessor;
    CompressorProcessor peakProcessor;
    parameters = {};
    parameters.rms = true;
    parameters.sidechainFilter = false;
    parameters.attackMs = 10.0F;
    parameters.releaseMs = 10.0F;
    rmsProcessor.prepare(48000.0);
    rmsProcessor.setParameters(parameters);
    parameters.rms = false;
    parameters.attackMs = 0.1F;
    parameters.releaseMs = 100.0F;
    peakProcessor.prepare(48000.0);
    peakProcessor.setParameters(parameters);
    for (int sample = 0; sample < 12000; ++sample) {
        const float value = static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * sample / 48000.0));
        rmsProcessor.process(value);
        peakProcessor.process(value);
    }
    passed &= check(std::abs(rmsProcessor.inputLevelDb() + 3.01F) < 0.15F
                        && std::abs(peakProcessor.inputLevelDb()) < 0.15F,
                    "RMS and peak detection report their corresponding signal strengths");

    CompressorProcessor filteredSidechain;
    parameters = {};
    parameters.rms = false;
    parameters.highPassHz = 2000.0F;
    parameters.attackMs = 0.1F;
    parameters.releaseMs = 100.0F;
    filteredSidechain.prepare(48000.0);
    filteredSidechain.setParameters(parameters);
    float filteredOutput = 0.0F;
    for (int sample = 0; sample < 48000; ++sample) {
        const float value = 0.5F * static_cast<float>(std::sin(
            2.0 * std::numbers::pi * 100.0 * sample / 48000.0));
        filteredOutput = filteredSidechain.process(value);
    }
    passed &= check(std::abs(filteredSidechain.inputLevelDb() + 6.02F) < 0.25F
                        && filteredSidechain.detectorLevelDb() < -18.0F
                        && std::abs(filteredSidechain.outputLevelDb() + 6.02F) < 0.5F
                        && std::abs(filteredOutput) < 0.5F,
                    "intensity meters show actual input and post-compression levels while sidechain filtering controls detection");

    CompressorProcessor silentChannel;
    silentChannel.prepare(48000.0);
    silentChannel.setParameters(parameters);
    for (int sample = 0; sample < 12000; ++sample)
        silentChannel.process(0.0F);
    passed &= check(silentChannel.inputLevelDb() <= -90.0F
                        && silentChannel.gainReductionDb() < 0.01F,
                    "a silent channel contributes no signal level or compression reduction");

    return passed;
}

bool testSilenceResetController()
{
    using namespace WinJACKNexus::Common;

    SilenceResetController controller(-70.0, 0.5);
    const MeterValue quiet { -80.0, true };
    const MeterValue active { -60.0, true };
    bool passed = true;
    passed &= check(!controller.update(quiet, 0.2)
                        && controller.state() == SilenceResetState::pendingSilence,
                    "silence controller enters pending silence");
    passed &= check(!controller.update(quiet, 0.3)
                        && controller.state() == SilenceResetState::resetArmed,
                    "silence controller arms exactly once after configured duration");
    passed &= check(!controller.update(quiet, 1.0)
                        && controller.state() == SilenceResetState::resetArmed,
                    "silence controller remains armed during continued silence");
    passed &= check(controller.update(active, 0.1)
                        && controller.state() == SilenceResetState::active,
                    "silence controller requests reset when the next programme segment starts");
    passed &= check(!controller.configure(std::numeric_limits<double>::quiet_NaN(), 1.0),
                    "silence controller rejects invalid configuration");
    return passed;
}

struct Snapshot final {
    std::uint64_t sequence {};
    std::uint64_t checksum {};
};

bool testRealtimeObject()
{
    constexpr std::uint64_t itemCount = 1'000'000;
    WinJACKNexus::Common::RealtimeObject<Snapshot> object(Snapshot { 0, 7 });
    std::atomic<bool> producerFinished { false };
    std::atomic<bool> failed { false };

    std::thread producer([&] {
        for (std::uint64_t sequence = 1; sequence <= itemCount; ++sequence)
            object.set(Snapshot { sequence, sequence * 3 + 7 });
        producerFinished.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t lastSequence = 0;
        for (;;) {
            const auto& snapshot = object.getRead();
            if (snapshot.sequence < lastSequence || snapshot.checksum != snapshot.sequence * 3 + 7)
                failed.store(true, std::memory_order_relaxed);
            lastSequence = std::max(lastSequence, snapshot.sequence);
            if (producerFinished.load(std::memory_order_acquire) && lastSequence == itemCount)
                break;
            std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();
    const auto& finalSnapshot = object.getRead();
    return check(!failed.load(std::memory_order_acquire)
                     && finalSnapshot.sequence == itemCount,
                 "RealtimeObject publishes consistent concurrent snapshots");
}

std::atomic<std::size_t> loggedRecordCount { 0 };

void countLogRecord(const WinJACKNexus::Common::LogRecord&, void*) noexcept
{
    loggedRecordCount.fetch_add(1, std::memory_order_relaxed);
}

bool testLogger()
{
    constexpr std::size_t producerCount = 4;
    constexpr std::size_t recordsPerProducer = 10'000;
    constexpr std::size_t expectedRecordCount = producerCount * recordsPerProducer;
    auto& logger = WinJACKNexus::Common::Logger::instance();
    logger.setFileLoggingEnabled(false);
    logger.setSink(countLogRecord, nullptr);
    logger.drain();
    loggedRecordCount.store(0, std::memory_order_relaxed);
    const auto droppedBefore = logger.droppedCount();
    std::atomic<bool> producersFinished { false };
    std::array<std::thread, producerCount> producers;

    for (std::size_t producerIndex = 0; producerIndex < producerCount; ++producerIndex) {
        producers[producerIndex] = std::thread([producerIndex, &logger] {
            for (std::size_t recordIndex = 0; recordIndex < recordsPerProducer; ++recordIndex)
                logger.log(WinJACKNexus::Common::LogLevel::info,
                           "producer=%zu record=%zu",
                           producerIndex,
                           recordIndex);
        });
    }

    std::thread consumer([&] {
        while (!producersFinished.load(std::memory_order_acquire) || logger.pendingCount() != 0) {
            logger.drain(256);
            std::this_thread::yield();
        }
    });

    for (auto& producer : producers)
        producer.join();
    producersFinished.store(true, std::memory_order_release);
    consumer.join();
    logger.drain();

    const auto dropped = logger.droppedCount() - droppedBefore;
    const auto logged = loggedRecordCount.load(std::memory_order_acquire);
    logger.setSink(nullptr, nullptr);
    return check(logged + dropped == expectedRecordCount, "Logger accounts for all concurrent records");
}

bool testUtilities()
{
    using namespace WinJACKNexus::Common;
    bool passed = true;

    PaddedAtomic<int> atomicValue(1);
    int expectedValue = 1;
    passed &= check(atomicValue.compareExchange(expectedValue, 2), "PaddedAtomic compare-exchange succeeds");
    passed &= check(atomicValue.load() == 2, "PaddedAtomic stores exchanged value");
    AtomicFlag atomicFlag;
    passed &= check(!atomicFlag.test(), "AtomicFlag starts clear");
    passed &= check(!atomicFlag.testAndSet() && atomicFlag.test(), "AtomicFlag sets atomically");
    atomicFlag.clear();
    passed &= check(!atomicFlag.test(), "AtomicFlag clears atomically");

    bool guardCalled = false;
    {
        auto guard = makeScopeExit([&] { guardCalled = true; });
    }
    passed &= check(guardCalled, "ScopeExit invokes its callback");

    const auto version = VersionInfo(1, 2, 3, "abc123");
    const auto parsedVersion = VersionInfo::fromString(version.toString());
    passed &= check(parsedVersion.has_value() && *parsedVersion == version, "VersionInfo round-trips");
    passed &= check(!VersionInfo::fromString("1.2").has_value(), "VersionInfo rejects incomplete values");
    passed &= check(!VersionInfo::fromString("1.2.3.4").has_value(), "VersionInfo rejects extra components");

    ModuleMetadata metadata;
    metadata.name = "module\"name";
    metadata.category = "audio";
    metadata.version = "1.0.0";
    metadata.author = "WinJACKNexus";
    metadata.description = "description\nline";
    metadata.dependencies = { "common", "audio" };
    const auto parsedMetadata = ModuleMetadata::fromJson(metadata.toJson());
    passed &= check(parsedMetadata.has_value()
                        && parsedMetadata->name == metadata.name
                        && parsedMetadata->description == metadata.description
                        && parsedMetadata->dependencies == metadata.dependencies,
                    "ModuleMetadata round-trips JSON");
    passed &= check(!ModuleMetadata::fromJson("{} trailing").has_value(), "ModuleMetadata rejects trailing JSON");

    const auto start = CpuClock::now();
    Sleep(1);
    const auto end = CpuClock::now();
    passed &= check(CpuClock::frequency() > 0.0 && CpuClock::secondsBetween(start, end) > 0.0,
                    "CpuClock reports high-resolution elapsed time");
    passed &= check(CpuClock::cpuLoad(start, end, 48, 48000.0) >= 0.0, "CpuClock computes CPU load");

    passed &= check(!PathUtils::executablePath().empty() && !PathUtils::moduleDirectory().empty(),
                    "PathUtils locates the test executable");
    passed &= check(!PathUtils::logDirectory().empty(), "PathUtils provides the log directory");
    passed &= check(!win32::Window().isValid() && win32::Window().title().empty(),
                    "win32 Window safely handles a null handle");
    return passed;
}

std::atomic<std::uint32_t> threadIterations { 0 };

void threadEntry(WinJACKNexus::Common::Thread& thread) noexcept
{
    while (!thread.stopRequested()) {
        threadIterations.fetch_add(1, std::memory_order_relaxed);
        Sleep(0);
    }
}

bool testAuxChannelLayoutsAndCopy()
{
    using namespace WinJACKNexus::Common;
    AuxMixerState state;
    bool passed = check(state.buses().size() == 3
                            && std::all_of(state.buses().cbegin(), state.buses().cend(), [](const auto& bus) {
                                   return bus.targetLayout == AuxChannelLayout::stereo;
                               }),
                        "default mixer has three stereo AUX buses");
    const auto originalId = state.buses().front().id;
    auto* sourceBus = state.busForSource(QStringLiteral("input:1"), originalId);
    if (!check(sourceBus != nullptr, "AUX source mapping exists")) return false;
    sourceBus->sends.front().levelDb = -13.0;
    sourceBus->targetLayout = AuxChannelLayout::surround51;
    auto* copied = state.cloneBus(originalId);
    passed &= check(copied && copied->id != originalId,
                    "copied AUX bus receives a stable new identity");
    if (copied) {
        const auto* copiedSource = state.busForSource(QStringLiteral("input:1"), copied->id);
        passed &= check(copiedSource && copiedSource->targetLayout == AuxChannelLayout::surround51
                            && copiedSource->sends.front().levelDb == -13.0,
                        "copy preserves the source-specific AUX channel map and send level");
    }
    const auto copiedId = copied ? copied->id : originalId;
    const auto& surround = state.addBus(AuxChannelLayout::surround71);
    passed &= check(surround.sourceLayout == AuxChannelLayout::surround71
                        && surround.targetLayout == AuxChannelLayout::surround71,
                    "new AUX bus uses the selected channel layout");
    state.busForSource(QStringLiteral("aux:1"), copiedId, originalId);
    passed &= check(state.setBusLayout(originalId, AuxChannelLayout::mono)
                        && state.bus(originalId)->targetLayout == AuxChannelLayout::mono
                        && state.busForSource(QStringLiteral("input:1"), originalId)->targetLayout
                               == AuxChannelLayout::mono
                        && state.busForSource(QStringLiteral("aux:1"), copiedId, originalId)->sourceLayout
                               == AuxChannelLayout::mono
                        && state.busForSource(QStringLiteral("aux:1"), copiedId, originalId)->sends.size() == 1,
                    "AUX type changes across all sources while keeping the stable bus identity");
    while (state.buses().size() > 1)
        passed &= check(state.removeBus(state.buses().back().id), "AUX buses can be removed above the minimum");
    passed &= check(state.buses().size() == 1 && !state.removeBus(state.buses().front().id),
                    "the last AUX bus cannot be removed");
    return passed;
}

bool testThread()
{
    using WinJACKNexus::Common::Thread;
    threadIterations.store(0, std::memory_order_relaxed);
    Thread thread(L"WJN Common Test");
    bool passed = check(thread.start(threadEntry), "Thread starts an entry point")
        && check(thread.joinable(), "Thread reports joinable after start");
    for (int attempt = 0; attempt < 100 && threadIterations.load(std::memory_order_acquire) == 0; ++attempt)
        Sleep(1);
    thread.requestStop();
    passed &= check(thread.join(), "Thread joins after stop request");
    passed &= check(!thread.joinable() && threadIterations.load(std::memory_order_acquire) > 0,
                    "Thread stops and executes its entry point");
    return passed;
}

}

int main()
{
    bool passed = true;
    passed &= testQueueBasics();
    passed &= testQueueStress();
    passed &= testAudioRingBuffer();
    passed &= testMeterChannelLayout();
    passed &= testKWeightingCoefficients();
    passed &= testLoudnessEngine();
    passed &= testCompressorProcessor();
    passed &= testSilenceResetController();
    passed &= testRealtimeObject();
    passed &= testLogger();
    passed &= testUtilities();
    passed &= testThread();
    passed &= testAuxChannelLayoutsAndCopy();
    if (!passed)
        return 1;
    std::puts("Common tests passed");
    return 0;
}
