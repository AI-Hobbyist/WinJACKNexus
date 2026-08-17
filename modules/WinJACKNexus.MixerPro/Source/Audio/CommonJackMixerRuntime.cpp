#include "Audio/CommonJackMixerRuntime.h"

#include <algorithm>
#include <cmath>

namespace mixerpro
{

CommonJackMixerRuntime::CommonJackMixerRuntime()
{
    for (auto* meter : { &inputPeaks, &inputRms, &inputHolds,
                         &outputPeaks, &outputRms, &outputHolds })
        for (auto& value : *meter)
            value.store(0.0f, std::memory_order_relaxed);

    for (int channel = 0; channel < channelCount; ++channel)
    {
        channelGainDb[static_cast<size_t>(channel)].store(0.0f, std::memory_order_relaxed);
        channelMuted[static_cast<size_t>(channel)].store(false, std::memory_order_relaxed);
        channelSolo[static_cast<size_t>(channel)].store(false, std::memory_order_relaxed);
    }

    for (int channel = 0; channel < channelCount; ++channel)
        processedPointers[static_cast<size_t>(channel)] =
            processedBuffers[static_cast<size_t>(channel)].data();

    updateMixGains();
}

CommonJackMixerRuntime::~CommonJackMixerRuntime()
{
    stop();
}

bool CommonJackMixerRuntime::start() noexcept
{
    if (running.load(std::memory_order_acquire))
        return true;

    if (!jackHub.open("WinJACKNexus.MixerPro", 128))
        return false;

    if (!audioInput.open(jackHub, "input", channelCount, 128)
        || !audioOutput.open(jackHub, "output", channelCount, 128))
    {
        stop();
        return false;
    }

    if (!audioInput.start(&CommonJackMixerRuntime::processInput, this)
        || !audioOutput.start())
    {
        stop();
        return false;
    }

    running.store(true, std::memory_order_release);
    return true;
}

void CommonJackMixerRuntime::stop() noexcept
{
    audioOutput.close();
    audioInput.close();
    jackHub.close();
    running.store(false, std::memory_order_release);
}

bool CommonJackMixerRuntime::isConnected() const noexcept
{
    return jackHub.getStatus().connected;
}

bool CommonJackMixerRuntime::isRunning() const noexcept
{
    return running.load(std::memory_order_acquire) && jackHub.getStatus().running;
}

wjn::common::JackClient::Status CommonJackMixerRuntime::getStatus() const noexcept
{
    return jackHub.getStatus();
}

juce::String CommonJackMixerRuntime::getLastError() const
{
    return jackHub.getLastError();
}

wjn::common::MeterFrame CommonJackMixerRuntime::getInputMeter() const noexcept
{
    return readMeter(inputPeaks, inputRms, inputHolds);
}

wjn::common::MeterFrame CommonJackMixerRuntime::getOutputMeter() const noexcept
{
    return readMeter(outputPeaks, outputRms, outputHolds);
}

void CommonJackMixerRuntime::setFaderDb(float value) noexcept
{
    faderDb.store(juce::jlimit(-60.0f, 12.0f, value), std::memory_order_release);
    updateMixGains();
}

float CommonJackMixerRuntime::getFaderDb() const noexcept
{
    return faderDb.load(std::memory_order_acquire);
}

void CommonJackMixerRuntime::setPan(float value) noexcept
{
    pan.store(juce::jlimit(-1.0f, 1.0f, value), std::memory_order_release);
    updateMixGains();
}

float CommonJackMixerRuntime::getPan() const noexcept
{
    return pan.load(std::memory_order_acquire);
}

void CommonJackMixerRuntime::setMute(bool shouldMute) noexcept
{
    muted.store(shouldMute, std::memory_order_release);
}

bool CommonJackMixerRuntime::isMuted() const noexcept
{
    return muted.load(std::memory_order_acquire);
}

void CommonJackMixerRuntime::setChannelGainDb(int channel, float value) noexcept
{
    if (channel < 0 || channel >= channelCount)
        return;
    channelGainDb[static_cast<size_t>(channel)].store(juce::jlimit(-12.0f, 12.0f, value),
                                                      std::memory_order_release);
}

float CommonJackMixerRuntime::getChannelGainDb(int channel) const noexcept
{
    if (channel < 0 || channel >= channelCount)
        return 0.0f;
    return channelGainDb[static_cast<size_t>(channel)].load(std::memory_order_acquire);
}

void CommonJackMixerRuntime::setChannelMute(int channel, bool shouldMute) noexcept
{
    if (channel >= 0 && channel < channelCount)
        channelMuted[static_cast<size_t>(channel)].store(shouldMute, std::memory_order_release);
}

bool CommonJackMixerRuntime::isChannelMuted(int channel) const noexcept
{
    return channel >= 0 && channel < channelCount
        && channelMuted[static_cast<size_t>(channel)].load(std::memory_order_acquire);
}

void CommonJackMixerRuntime::setChannelSolo(int channel, bool shouldSolo) noexcept
{
    if (channel >= 0 && channel < channelCount)
        channelSolo[static_cast<size_t>(channel)].store(shouldSolo, std::memory_order_release);
}

bool CommonJackMixerRuntime::isChannelSolo(int channel) const noexcept
{
    return channel >= 0 && channel < channelCount
        && channelSolo[static_cast<size_t>(channel)].load(std::memory_order_acquire);
}

void CommonJackMixerRuntime::processInput(const float* const* inputs, int channels,
                                          int frames, void* userData) noexcept
{
    auto* owner = static_cast<CommonJackMixerRuntime*>(userData);
    if (owner != nullptr)
        owner->processInputBlock(inputs, channels, frames);
}

void CommonJackMixerRuntime::processInputBlock(const float* const* inputs, int channels,
                                               int frames) noexcept
{
    if (frames <= 0 || frames > wjn::common::JackClient::maxBlockFrames)
        return;

    const auto leftMixGain = leftGain.load(std::memory_order_relaxed);
    const auto rightMixGain = rightGain.load(std::memory_order_relaxed);
    const auto faderGain = std::pow(10.0f, faderDb.load(std::memory_order_relaxed) / 20.0f);
    const auto shouldMute = muted.load(std::memory_order_relaxed);
    bool hasSolo = false;
    for (int channel = 0; channel < channelCount; ++channel)
        hasSolo = hasSolo || channelSolo[static_cast<size_t>(channel)].load(std::memory_order_relaxed);

    std::array<float, channelCount> inputPeakValues {};
    std::array<float, channelCount> outputPeakValues {};
    std::array<float, channelCount> inputSquares {};
    std::array<float, channelCount> outputSquares {};

    for (int frame = 0; frame < frames; ++frame)
    {
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto channelIndex = static_cast<size_t>(channel);
            const auto* input = inputs != nullptr && channel < channels ? inputs[channel] : nullptr;
            const auto channelGain = std::pow(10.0f,
                                              channelGainDb[channelIndex].load(std::memory_order_relaxed) / 20.0f);
            const auto active = ! channelMuted[channelIndex].load(std::memory_order_relaxed)
                && (! hasSolo || channelSolo[channelIndex].load(std::memory_order_relaxed));
            const auto sample = active && input != nullptr ? input[frame] * channelGain : 0.0f;
            const auto masterGain = channel == 0 ? leftMixGain
                                      : (channel == 1 ? rightMixGain : faderGain);
            const auto output = shouldMute ? 0.0f : sample * masterGain;

            processedBuffers[channelIndex][static_cast<size_t>(frame)] = output;
            inputPeakValues[channelIndex] = juce::jmax(inputPeakValues[channelIndex], std::abs(sample));
            outputPeakValues[channelIndex] = juce::jmax(outputPeakValues[channelIndex], std::abs(output));
            inputSquares[channelIndex] += sample * sample;
            outputSquares[channelIndex] += output * output;
        }
    }

    for (int channel = 0; channel < channelCount; ++channel)
    {
        const auto inputRmsValue = std::sqrt(inputSquares[static_cast<size_t>(channel)]
                                             / static_cast<float>(frames));
        const auto outputRmsValue = std::sqrt(outputSquares[static_cast<size_t>(channel)]
                                               / static_cast<float>(frames));
        inputPeaks[static_cast<size_t>(channel)].store(inputPeakValues[static_cast<size_t>(channel)],
                                                        std::memory_order_relaxed);
        inputRms[static_cast<size_t>(channel)].store(inputRmsValue, std::memory_order_relaxed);
        outputPeaks[static_cast<size_t>(channel)].store(outputPeakValues[static_cast<size_t>(channel)],
                                                         std::memory_order_relaxed);
        outputRms[static_cast<size_t>(channel)].store(outputRmsValue, std::memory_order_relaxed);

        auto& inputHold = inputHolds[static_cast<size_t>(channel)];
        auto& outputHold = outputHolds[static_cast<size_t>(channel)];
        inputHold.store(juce::jmax(inputHold.load(std::memory_order_relaxed),
                                   inputPeakValues[static_cast<size_t>(channel)]),
                        std::memory_order_relaxed);
        outputHold.store(juce::jmax(outputHold.load(std::memory_order_relaxed),
                                    outputPeakValues[static_cast<size_t>(channel)]),
                         std::memory_order_relaxed);
    }

    audioOutput.submitBlock(processedPointers.data(), channelCount, frames);
}

void CommonJackMixerRuntime::updateMixGains() noexcept
{
    const auto faderGain = std::pow(10.0f, faderDb.load(std::memory_order_relaxed) / 20.0f);
    const auto normalisedPan = (pan.load(std::memory_order_relaxed) + 1.0f) * 0.25f
                               * juce::MathConstants<float>::pi;
    leftGain.store(std::cos(normalisedPan) * faderGain, std::memory_order_release);
    rightGain.store(std::sin(normalisedPan) * faderGain, std::memory_order_release);
}

wjn::common::MeterFrame CommonJackMixerRuntime::readMeter(
    const std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels>& peaks,
    const std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels>& rms,
    const std::array<std::atomic<float>, wjn::common::MeterFrame::maxChannels>& holds) noexcept
{
    wjn::common::MeterFrame frame;
    frame.channelCount = channelCount;
    for (int channel = 0; channel < channelCount; ++channel)
    {
        frame.peak[static_cast<size_t>(channel)] = peaks[static_cast<size_t>(channel)].load(std::memory_order_relaxed);
        frame.rms[static_cast<size_t>(channel)] = rms[static_cast<size_t>(channel)].load(std::memory_order_relaxed);
        frame.peakHold[static_cast<size_t>(channel)] = holds[static_cast<size_t>(channel)].load(std::memory_order_relaxed);
        frame.overload = frame.overload || frame.peak[static_cast<size_t>(channel)] >= 1.0f;
    }
    return frame;
}

} // namespace mixerpro