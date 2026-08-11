#include "JackClient.h"

#include <algorithm>

namespace wjn::common
{

JackClient::~JackClient()
{
    close();
}

bool JackClient::open(const juce::String& clientName, int expectedBlockSize) noexcept
{
    close();
    jack_status_t status = JackFailure;
    client = jack_client_open(clientName.toRawUTF8(), JackNoStartServer, &status);
    if (client == nullptr)
    {
        setError((status & JackServerFailed) != 0 ? "JACK server is not running" : "Unable to connect to JACK");
        return false;
    }

    sampleRate.store(static_cast<int>(jack_get_sample_rate(client)), std::memory_order_release);
    blockSize.store(static_cast<int>(jack_get_buffer_size(client)), std::memory_order_release);
    if (expectedBlockSize > 0 && blockSize.load() > expectedBlockSize)
    {
        setError("JACK buffer size exceeds the configured limit");
        close();
        return false;
    }

    if (jack_set_process_callback(client, &JackClient::processCallback, this) != 0
        || jack_set_buffer_size_callback(client, &JackClient::bufferSizeCallback, this) != 0
        || jack_set_sample_rate_callback(client, &JackClient::sampleRateCallback, this) != 0
        || jack_set_xrun_callback(client, &JackClient::xrunCallback, this) != 0)
    {
        setError("Unable to register JACK callbacks");
        close();
        return false;
    }

    jack_on_shutdown(client, &JackClient::shutdownCallback, this);
    connected.store(true, std::memory_order_release);
    lastError.clear();
    return true;
}

bool JackClient::configurePorts(const juce::StringArray& inputNames,
                                const juce::StringArray& outputNames) noexcept
{
    if (client == nullptr || running.load(std::memory_order_acquire)
        || inputNames.size() > maxPorts || outputNames.size() > maxPorts)
    {
        setError("JACK ports can only be configured while the client is stopped");
        return false;
    }

    for (int index = 0; index < inputPortCount; ++index)
        jack_port_unregister(client, inputPorts[static_cast<size_t>(index)]);
    for (int index = 0; index < outputPortCount; ++index)
        jack_port_unregister(client, outputPorts[static_cast<size_t>(index)]);
    inputPortCount = 0;
    outputPortCount = 0;

    if (!registerPorts(inputNames, JackPortIsInput, inputPorts, inputPortCount))
        return false;

    if (!registerPorts(outputNames, JackPortIsOutput, outputPorts, outputPortCount))
    {
        for (int index = 0; index < inputPortCount; ++index)
            jack_port_unregister(client, inputPorts[static_cast<size_t>(index)]);
        inputPortCount = 0;
        return false;
    }

    return true;
}

bool JackClient::activate() noexcept
{
    if (client == nullptr || jack_activate(client) != 0)
    {
        setError("Unable to activate JACK client");
        return false;
    }
    running.store(true, std::memory_order_release);
    return true;
}

void JackClient::deactivate() noexcept
{
    running.store(false, std::memory_order_release);
    if (client != nullptr)
        jack_deactivate(client);
}

void JackClient::close() noexcept
{
    deactivate();
    if (client != nullptr)
        jack_client_close(client);
    client = nullptr;
    inputPortCount = 0;
    outputPortCount = 0;
    callback = nullptr;
    callbackUserData = nullptr;
    connected.store(false, std::memory_order_release);
}

void JackClient::setProcessCallback(ProcessCallback newCallback, void* userData) noexcept
{
    callback = newCallback;
    callbackUserData = userData;
}

JackClient::Status JackClient::getStatus() const noexcept
{
    return { connected.load(std::memory_order_acquire), running.load(std::memory_order_acquire),
             sampleRate.load(std::memory_order_acquire), blockSize.load(std::memory_order_acquire),
             callbackCount.load(std::memory_order_relaxed), xrunCount.load(std::memory_order_relaxed) };
}

const juce::String& JackClient::getLastError() const noexcept
{
    return lastError;
}

int JackClient::processCallback(jack_nframes_t frameCount, void* userData) noexcept
{
    auto* owner = static_cast<JackClient*>(userData);
    if (owner == nullptr || owner->callback == nullptr || frameCount > maxBlockFrames)
        return 0;

    std::array<const float*, maxPorts> inputs {};
    std::array<float*, maxPorts> outputs {};
    for (int index = 0; index < owner->inputPortCount; ++index)
        inputs[static_cast<size_t>(index)] = static_cast<const float*>(jack_port_get_buffer(owner->inputPorts[static_cast<size_t>(index)], frameCount));
    for (int index = 0; index < owner->outputPortCount; ++index)
        outputs[static_cast<size_t>(index)] = static_cast<float*>(jack_port_get_buffer(owner->outputPorts[static_cast<size_t>(index)], frameCount));

    owner->callback(inputs.data(), owner->inputPortCount, outputs.data(), owner->outputPortCount,
                    static_cast<int>(frameCount), owner->callbackUserData);
    owner->callbackCount.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

int JackClient::bufferSizeCallback(jack_nframes_t frameCount, void* userData) noexcept
{
    auto* owner = static_cast<JackClient*>(userData);
    if (owner != nullptr)
        owner->blockSize.store(static_cast<int>(frameCount), std::memory_order_release);
    return frameCount <= maxBlockFrames ? 0 : 1;
}

int JackClient::sampleRateCallback(jack_nframes_t newSampleRate, void* userData) noexcept
{
    auto* owner = static_cast<JackClient*>(userData);
    if (owner != nullptr)
        owner->sampleRate.store(static_cast<int>(newSampleRate), std::memory_order_release);
    return 0;
}

int JackClient::xrunCallback(void* userData) noexcept
{
    auto* owner = static_cast<JackClient*>(userData);
    if (owner != nullptr)
        owner->xrunCount.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

void JackClient::shutdownCallback(void* userData) noexcept
{
    auto* owner = static_cast<JackClient*>(userData);
    if (owner != nullptr)
    {
        owner->running.store(false, std::memory_order_release);
        owner->connected.store(false, std::memory_order_release);
    }
}

bool JackClient::registerPorts(const juce::StringArray& names, unsigned long flags,
                               std::array<jack_port_t*, maxPorts>& destination,
                               int& count) noexcept
{
    for (const auto& name : names)
    {
        auto* port = jack_port_register(client, name.toRawUTF8(), JACK_DEFAULT_AUDIO_TYPE, flags, 0);
        if (port == nullptr)
        {
            setError("Unable to register JACK audio port");
            for (int index = 0; index < count; ++index)
                jack_port_unregister(client, destination[static_cast<size_t>(index)]);
            count = 0;
            return false;
        }
        destination[static_cast<size_t>(count++)] = port;
    }
    return true;
}

void JackClient::setError(const char* message) noexcept
{
    lastError = message;
    connected.store(false, std::memory_order_release);
}

} // namespace wjn::common
