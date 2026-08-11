#include "JackAudioBackend.h"

namespace wjn::common
{

bool JackAudioBackend::open(const AudioDeviceSettings& settings)
{
    effectiveSettings.sampleRate = settings.sampleRate;
    effectiveSettings.blockSize = settings.blockSize;
    effectiveSettings.inputChannels = settings.inputChannels;
    effectiveSettings.outputChannels = settings.outputChannels;

    if (!client.open("WinJACKNexus.Common", settings.blockSize))
        return false;

    juce::StringArray inputs;
    juce::StringArray outputs;
    for (int index = 0; index < settings.inputChannels; ++index)
        inputs.add("in_" + juce::String(index + 1));
    for (int index = 0; index < settings.outputChannels; ++index)
        outputs.add("out_" + juce::String(index + 1));

    if (!client.configurePorts(inputs, outputs))
    {
        client.close();
        return false;
    }

    const auto status = client.getStatus();
    effectiveSettings.sampleRate = status.sampleRate;
    effectiveSettings.blockSize = status.blockSize;
    return true;
}

void JackAudioBackend::close() noexcept
{
    stop();
    client.close();
    processCallback = nullptr;
}

bool JackAudioBackend::start(AudioProcessCallback& callback) noexcept
{
    processCallback = &callback;
    client.setProcessCallback(&JackAudioBackend::process, this);
    return client.activate();
}

void JackAudioBackend::stop() noexcept
{
    client.deactivate();
    processCallback = nullptr;
}

bool JackAudioBackend::isOpen() const noexcept
{
    return client.getStatus().connected;
}

bool JackAudioBackend::isRunning() const noexcept
{
    return client.getStatus().running;
}

EffectiveAudioSettings JackAudioBackend::getEffectiveSettings() const noexcept
{
    return effectiveSettings;
}

JackClient::Status JackAudioBackend::getStatus() const noexcept
{
    return client.getStatus();
}

const juce::String& JackAudioBackend::getLastError() const noexcept
{
    return client.getLastError();
}

void JackAudioBackend::process(const float* const* inputs, int inputChannels,
                               float* const* outputs, int outputChannels,
                               int frameCount, void* userData) noexcept
{
    auto* owner = static_cast<JackAudioBackend*>(userData);
    if (owner == nullptr || owner->processCallback == nullptr)
        return;

    AudioProcessContext context { inputs, outputs, inputChannels, outputChannels, frameCount };
    owner->processCallback->process(context);
}

} // namespace wjn::common
