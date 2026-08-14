#include "RealEngine.h"
#include "../DebugTrace.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wjn::adapter
{
namespace
{

int countAvailableOutputFrames (int availableInputFrames, double speedRatio,
                                double inputPhase, int maximumOutputFrames) noexcept
{
    if (availableInputFrames < 0 || speedRatio <= 0.0 || maximumOutputFrames <= 0)
        return 0;

    auto phase = inputPhase;
    auto consumedInputFrames = 0;
    auto outputFrames = 0;

    while (outputFrames < maximumOutputFrames)
    {
        auto nextPhase = phase;
        auto requiredInputFrames = 0;
        while (nextPhase >= 1.0)
        {
            nextPhase -= 1.0;
            ++requiredInputFrames;
        }

        if (consumedInputFrames + requiredInputFrames > availableInputFrames)
            break;

        consumedInputFrames += requiredInputFrames;
        phase = nextPhase + speedRatio;
        ++outputFrames;
    }

    return outputFrames;
}

double advanceInputPhase (double inputPhase, double speedRatio, int outputFrames) noexcept
{
    auto phase = inputPhase;
    for (int outputIndex = 0; outputIndex < outputFrames; ++outputIndex)
    {
        while (phase >= 1.0)
            phase -= 1.0;
        phase += speedRatio;
    }
    return phase;
}

} // namespace

RealEngine::RealEngine()
    : juce::Thread ("WinJACKNexus Engine")
{
    debug::trace ("engine ctor body engine=" + debug::pointerText (this));
    jackToDeviceReadBlock.channels = 0;
    jackToDeviceReadBlock.frames = 0;
}

RealEngine::~RealEngine()
{
    stop();
}

void RealEngine::setAudioCallback (AudioCallback callback)
{
    audioCallback = std::move (callback);
}

void RealEngine::setMidiCallback (MidiCallback callback)
{
    midiCallback = std::move (callback);
}

bool RealEngine::start (Configuration newConfiguration)
{
    stop();
    configuration = std::move (newConfiguration);
    startThread();
    return true;
}

void RealEngine::run()
{
    if (threadShouldExit())
        return;

    if (! startEngine() || threadShouldExit())
        stopEngine();
}

bool RealEngine::startEngine()
{
    audioPeak.store (0.0f, std::memory_order_release);
    for (auto& peak : audioPeaks)
        peak.store (0.0f, std::memory_order_release);
    audioClipping.store (false, std::memory_order_release);
    pendingMidiEvents.store (0, std::memory_order_release);
    deliveredMidiEvents = 0;
    for (auto& level : midiLevels)
        level.store (0.0f, std::memory_order_release);

    if (! configuration.midi)
    {
        if (configuration.input)
            captureInputSamples = std::make_unique<CaptureInputStorage>();

        if (! startAudioDevice())
            return false;

        const auto channels = juce::jlimit (1, wjn::common::JackAudioOutput::maxChannels,
                                            configuration.channels);
        const auto jackOpened = configuration.input
            ? audioOutput.open (configuration.clientName, channels,
                                wjn::common::JackClient::maxBlockFrames)
            : audioInput.open (configuration.clientName, channels,
                               wjn::common::JackClient::maxBlockFrames);
        if (! jackOpened)
        {
            stopEngine();
            return false;
        }

        const auto status = configuration.input ? audioOutput.getStatus() : audioInput.getStatus();
        jackSampleRate = status.sampleRate > 0 ? static_cast<double> (status.sampleRate) : deviceSampleRate;
        resetResamplers();
        const auto jackStarted = configuration.input
            ? audioOutput.start()
            : audioInput.start (&RealEngine::processAudio, this);
        if (! jackStarted)
        {
            stopEngine();
            return false;
        }
        windowsAudioDevice->start (this);
        if (! windowsAudioDevice->isPlaying())
        {
            stopEngine();
            return false;
        }

        startTimerHz (30);
        return true;
    }

    if (configuration.input)
    {
        if (configuration.midiDeviceIdentifier.isNotEmpty())
            systemMidiInput = juce::MidiInput::openDevice (configuration.midiDeviceIdentifier, this);

        const auto jackReady = jackMidiOutput.open (configuration.clientName, "out")
                            && jackMidiOutput.start();
        if (systemMidiInput != nullptr)
            systemMidiInput->start();

        startTimerHz (30);
        return jackReady || systemMidiInput != nullptr;
    }

    if (configuration.midiDeviceIdentifier.isNotEmpty())
        systemMidiOutput = juce::MidiOutput::openDevice (configuration.midiDeviceIdentifier);

    const auto jackReady = jackMidiInput.open (configuration.clientName, "in")
                        && jackMidiInput.start();
    startTimerHz (30);
    return jackReady || systemMidiOutput != nullptr;
}

void RealEngine::stop()
{
    stopTimer();
    signalThreadShouldExit();
    stopThread (-1);
    stopEngine();
}

void RealEngine::stopEngine()
{
    stopTimer();
    if (systemMidiInput != nullptr)
        systemMidiInput->stop();
    systemMidiInput.reset();
    systemMidiOutput.reset();
    if (windowsAudioDevice != nullptr)
    {
        windowsAudioDevice->stop();
        windowsAudioDevice->close();
    }
    windowsAudioDevice.reset();
    windowsAudioType.reset();
    captureInputSamples.reset();
    audioInput.close();
    audioOutput.close();
    jackMidiInput.close();
    jackMidiOutput.close();
}

void RealEngine::processAudio (const float* const* inputs, int channels,
                               int frames, void* userData) noexcept
{
    auto* owner = static_cast<RealEngine*> (userData);
    if (owner == nullptr || inputs == nullptr || channels <= 0 || frames <= 0)
        return;

    auto* destination = &owner->jackToDeviceWriteBlock;
    destination->channels = (std::min)(channels, maxAudioChannels);
    const auto requestedFrames = static_cast<double> (frames) * owner->deviceSampleRate
                                 / owner->jackSampleRate + owner->renderOutputFrameRemainder;
    destination->frames = juce::jlimit (1, AudioBlock::maxFrames,
                                        static_cast<int> (std::floor (requestedFrames)));
    owner->renderOutputFrameRemainder = requestedFrames - destination->frames;
    float peak = 0.0f;
    for (int channel = 0; channel < maxAudioChannels; ++channel)
    {
        const auto* samples = channel < channels ? inputs[channel] : nullptr;
        auto channelPeak = 0.0f;
        if (samples != nullptr)
        {
            for (int frame = 0; frame < frames; ++frame)
                channelPeak = (std::max) (channelPeak, std::abs (samples[frame]));

            peak = (std::max) (peak, channelPeak);
        }

        owner->audioPeaks[static_cast<size_t> (channel)].store (
            channelPeak, std::memory_order_release);

        if (channel < destination->channels)
        {
            if (samples != nullptr)
            {
            owner->renderResamplers[static_cast<size_t>(channel)].process (
                owner->jackSampleRate / owner->deviceSampleRate, samples,
                destination->samples[static_cast<size_t>(channel)].data(), destination->frames,
                frames, 0);
            }
            else
            {
                std::fill_n (destination->samples[static_cast<size_t> (channel)].data(),
                             destination->frames, 0.0f);
            }
        }
    }

    owner->jackToDeviceBlocks.push(*destination);
    owner->audioPeak.store (peak, std::memory_order_release);
    if (peak >= 1.0f)
        owner->audioClipping.store (true, std::memory_order_release);
}

bool RealEngine::startAudioDevice()
{
    if (configuration.audioDeviceName.isEmpty())
        return false;

    windowsAudioType.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (
        configuration.wasapiMode));
    if (windowsAudioType == nullptr)
        return false;

    windowsAudioType->scanForDevices();
    windowsAudioDevice.reset (windowsAudioType->createDevice (
        configuration.input ? juce::String() : configuration.audioDeviceName,
        configuration.input ? configuration.audioDeviceName : juce::String()));
    if (windowsAudioDevice == nullptr)
        return false;

    const auto channels = juce::jlimit (1, wjn::common::JackAudioOutput::maxChannels,
                                        configuration.channels);
    juce::BigInteger inputChannels;
    juce::BigInteger outputChannels;
    if (configuration.input)
        inputChannels.setRange (0, channels, true);
    else
        outputChannels.setRange (0, channels, true);

    const auto sampleRates = windowsAudioDevice->getAvailableSampleRates();
    const auto sampleRate = sampleRates.isEmpty() ? 48000.0 : sampleRates.getFirst();
    const auto error = windowsAudioDevice->open (inputChannels, outputChannels, sampleRate,
                                                 windowsAudioDevice->getDefaultBufferSize());
    if (error.isNotEmpty())
    {
        windowsAudioDevice.reset();
        windowsAudioType.reset();
        return false;
    }

    deviceSampleRate = windowsAudioDevice->getCurrentSampleRate();
    return deviceSampleRate > 0.0;
}

void RealEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int inputChannels,
                                                    float* const* outputs, int outputChannels, int frames,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    if (configuration.input)
    {
        if (captureInputSamples == nullptr)
            return;

        const auto channels = (std::min) (inputChannels, maxAudioChannels);
        for (int channel = 0; channel < maxAudioChannels; ++channel)
        {
            auto channelPeak = 0.0f;
            const auto* source = inputs != nullptr && channel < inputChannels ? inputs[channel] : nullptr;
            if (source != nullptr)
                for (int frame = 0; frame < frames; ++frame)
                    channelPeak = (std::max) (channelPeak, std::abs (source[frame]));
            audioPeaks[static_cast<size_t> (channel)].store (
                channelPeak, std::memory_order_release);
        }

        int inputOffset = 0;
        while (inputOffset < frames)
        {
            compactCaptureInput();
            const auto writableFrames = captureInputCapacity - captureInputFrames;
            if (writableFrames <= 0)
            {
                const auto framesBeforeProcessing = captureInputFrames;
                processCaptureAudio (channels);
                if (captureInputFrames >= framesBeforeProcessing)
                    break;
                continue;
            }

            const auto chunkFrames = (std::min) (frames - inputOffset, writableFrames);
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* destination = (*captureInputSamples)[static_cast<size_t> (channel)].data()
                                  + captureInputFrames;
                const auto* source = inputs != nullptr ? inputs[channel] : nullptr;
                if (source != nullptr)
                    std::memcpy (destination, source + inputOffset,
                                 static_cast<size_t> (chunkFrames) * sizeof (float));
                else
                    std::fill_n (destination, chunkFrames, 0.0f);
            }
            captureInputFrames += chunkFrames;
            inputOffset += chunkFrames;
            processCaptureAudio (channels);
        }

        float peak = 0.0f;
        for (const auto& channelPeak : audioPeaks)
            peak = (std::max) (peak, channelPeak.load (std::memory_order_acquire));
        audioPeak.store (peak, std::memory_order_release);
        if (peak >= 1.0f)
            audioClipping.store (true, std::memory_order_release);
        return;
    }

    for (int frame = 0; frame < frames; ++frame)
    {
        if (renderReadOffset >= jackToDeviceReadBlock.frames)
        {
            if (! jackToDeviceBlocks.pop (jackToDeviceReadBlock))
            {
                for (int channel = 0; channel < outputChannels; ++channel)
                    if (outputs[channel] != nullptr)
                        std::fill_n (outputs[channel] + frame, frames - frame, 0.0f);
                return;
            }
            renderReadOffset = 0;
        }

        for (int channel = 0; channel < outputChannels; ++channel)
            if (outputs[channel] != nullptr)
                outputs[channel][frame] = channel < jackToDeviceReadBlock.channels
                    ? jackToDeviceReadBlock.samples[static_cast<size_t>(channel)][static_cast<size_t>(renderReadOffset)]
                    : 0.0f;
        ++renderReadOffset;
    }
}

void RealEngine::audioDeviceAboutToStart (juce::AudioIODevice*) {}
void RealEngine::audioDeviceStopped()
{
    jackToDeviceBlocks.reset();
    renderReadOffset = 0;
    resetResamplers();
}

void RealEngine::compactCaptureInput() noexcept
{
    if (captureInputSamples == nullptr || captureInputReadOffset == 0)
        return;

    if (captureInputFrames > 0)
    {
        for (int channel = 0; channel < maxAudioChannels; ++channel)
            std::memmove ((*captureInputSamples)[static_cast<size_t> (channel)].data(),
                          (*captureInputSamples)[static_cast<size_t> (channel)].data()
                              + captureInputReadOffset,
                          static_cast<size_t> (captureInputFrames) * sizeof (float));
    }
    captureInputReadOffset = 0;
}

void RealEngine::processCaptureAudio (int channels) noexcept
{
    if (captureInputSamples == nullptr || channels <= 0
        || deviceSampleRate <= 0.0 || jackSampleRate <= 0.0)
        return;

    const auto speedRatio = deviceSampleRate / jackSampleRate;
    while (true)
    {
        const auto targetFrames = countAvailableOutputFrames (
            captureInputFrames, speedRatio, captureInputPhase, AudioBlock::maxFrames);
        if (targetFrames <= 0)
            return;

        deviceToJackBlock.channels = channels;
        deviceToJackBlock.frames = targetFrames;
        auto consumedInputFrames = 0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto* source = (*captureInputSamples)[static_cast<size_t> (channel)].data()
                               + captureInputReadOffset;
            const auto consumed = captureResamplers[static_cast<size_t> (channel)].process (
                speedRatio, source,
                deviceToJackBlock.samples[static_cast<size_t> (channel)].data(), targetFrames,
                captureInputFrames, 0);
            if (channel == 0)
                consumedInputFrames = consumed;
        }

        captureInputPhase = advanceInputPhase (captureInputPhase, speedRatio, targetFrames);
        captureInputReadOffset += consumedInputFrames;
        captureInputFrames -= consumedInputFrames;
        if (captureInputFrames == 0)
            captureInputReadOffset = 0;

        for (int offset = 0; offset < targetFrames;
             offset += wjn::common::JackClient::maxBlockFrames)
        {
            const auto chunkFrames = (std::min) (wjn::common::JackClient::maxBlockFrames,
                                                 targetFrames - offset);
            for (int channel = 0; channel < channels; ++channel)
                deviceToJackPointers[static_cast<size_t> (channel)] =
                    deviceToJackBlock.samples[static_cast<size_t> (channel)].data() + offset;
            audioOutput.submitBlock (deviceToJackPointers.data(), channels, chunkFrames);
        }
    }
}

void RealEngine::resetResamplers() noexcept
{
    for (auto& resampler : captureResamplers)
        resampler.reset();
    for (auto& resampler : renderResamplers)
        resampler.reset();
    jackToDeviceBlocks.reset();
    jackToDeviceReadBlock.channels = 0;
    jackToDeviceReadBlock.frames = 0;
    renderReadOffset = 0;
    captureInputReadOffset = 0;
    captureInputFrames = 0;
    captureInputPhase = 1.0;
    renderOutputFrameRemainder = 0.0;
}

void RealEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    jackMidiOutput.push (0, message.getRawData(), static_cast<size_t> (message.getRawDataSize()));
    publishMidiMessage (message);
}

void RealEngine::publishMidiMessage (const juce::MidiMessage& message) noexcept
{
    const auto channel = message.getChannel();
    if (channel >= 1 && channel <= 16)
    {
        float level = 0.0f;
        if (message.isNoteOn())
            level = message.getFloatVelocity();
        else if (message.isNoteOff())
            level = 0.0f;
        else if (message.isAftertouch())
            level = static_cast<float> (message.getAfterTouchValue()) / 127.0f;
        else if (message.isChannelPressure())
            level = static_cast<float> (message.getChannelPressureValue()) / 127.0f;
        else if (message.isController())
            level = static_cast<float> (message.getControllerValue()) / 127.0f;
        else if (message.isPitchWheel())
            level = static_cast<float> (message.getPitchWheelValue()) / 16383.0f;

        midiLevels[static_cast<size_t> (channel - 1)].store (
            juce::jlimit (0.0f, 1.0f, level), std::memory_order_release);
    }

    pendingMidiEvents.fetch_add (1, std::memory_order_relaxed);
}

void RealEngine::timerCallback()
{
    if (! configuration.midi)
    {
        if (audioCallback != nullptr)
        {
            const auto clipping = audioClipping.exchange (false, std::memory_order_acq_rel);
            AudioLevels levels {};
            for (size_t index = 0; index < levels.size(); ++index)
                levels[index] = audioPeaks[index].load (std::memory_order_acquire);
            audioCallback (levels, audioPeak.load (std::memory_order_acquire), clipping);
        }
        return;
    }

    if (! configuration.input)
    {
        wjn::common::MidiEvent event;
        while (jackMidiInput.pop (event))
        {
            const juce::MidiMessage message (event.bytes.data(), event.size);
            if (systemMidiOutput != nullptr)
                systemMidiOutput->sendMessageNow (message);
            publishMidiMessage (message);
        }
    }

    const auto eventCount = pendingMidiEvents.load (std::memory_order_acquire);
    if (eventCount != deliveredMidiEvents && midiCallback != nullptr)
    {
        deliveredMidiEvents = eventCount;
        std::array<float, 16> levels {};
        for (size_t index = 0; index < levels.size(); ++index)
            levels[index] = midiLevels[index].load (std::memory_order_acquire);
        midiCallback (levels);
    }
}

} // namespace wjn::adapter