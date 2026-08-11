#pragma once

#include "AudioBackend.h"
#include "JackClient.h"

namespace wjn::common
{

class JackAudioBackend final : public AudioBackend
{
public:
    bool open(const AudioDeviceSettings& settings) override;
    void close() noexcept override;
    bool start(AudioProcessCallback& callback) noexcept override;
    void stop() noexcept override;
    bool isOpen() const noexcept override;
    bool isRunning() const noexcept override;
    EffectiveAudioSettings getEffectiveSettings() const noexcept override;

    JackClient::Status getStatus() const noexcept;
    const juce::String& getLastError() const noexcept;

private:
    static void process(const float* const* inputs, int inputChannels,
                        float* const* outputs, int outputChannels,
                        int frameCount, void* userData) noexcept;

    JackClient client;
    EffectiveAudioSettings effectiveSettings;
    AudioProcessCallback* processCallback = nullptr;
};

} // namespace wjn::common
