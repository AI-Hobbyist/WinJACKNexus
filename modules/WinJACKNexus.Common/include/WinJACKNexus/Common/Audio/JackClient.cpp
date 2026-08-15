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
    if (client == nullptr || running.load(std::memory_order_acquire))
    {
        setError("JACK ports can only be configured while the client is stopped");
        return false;
    }

    for (auto* port : inputPorts)
        jack_port_unregister(client, port);
    for (auto* port : outputPorts)
        jack_port_unregister(client, port);
    inputPorts.clear();
    outputPorts.clear();
    inputPortNames.clear();
    outputPortNames.clear();
    inputBuffers.clear();
    outputBuffers.clear();

    if (!registerPorts(inputNames, JackPortIsInput, inputPorts))
        return false;

    if (!registerPorts(outputNames, JackPortIsOutput, outputPorts))
    {
        for (auto* port : inputPorts)
            jack_port_unregister(client, port);
        inputPorts.clear();
        return false;
    }

    inputBuffers.resize(inputPorts.size());
    outputBuffers.resize(outputPorts.size());
    inputPortNames.assign(inputNames.begin(), inputNames.end());
    outputPortNames.assign(outputNames.begin(), outputNames.end());

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
    inputPorts.clear();
    outputPorts.clear();
    inputPortNames.clear();
    outputPortNames.clear();
    inputBuffers.clear();
    outputBuffers.clear();
    callback = nullptr;
    callbackUserData = nullptr;
    connected.store(false, std::memory_order_release);
}

bool JackClient::rename(const juce::String& newClientName) noexcept
{
    const auto requestedName = newClientName.trim();
    if (client == nullptr || requestedName.isEmpty())
    {
        setError("Unable to rename an unopened JACK client");
        return false;
    }

    const auto* currentName = jack_get_client_name (client);
    const auto currentClientName = currentName != nullptr
        ? juce::String::fromUTF8 (currentName)
        : juce::String();
    if (currentClientName == requestedName)
        return true;

    const auto wasRunning = running.load (std::memory_order_acquire);
    const auto expectedBlockSize = blockSize.load (std::memory_order_acquire);
    const auto savedCallback = callback;
    auto* const savedCallbackUserData = callbackUserData;
    const auto savedInputNames = inputPortNames;
    const auto savedOutputNames = outputPortNames;
    std::vector<std::vector<juce::String>> inputConnections (inputPorts.size());
    std::vector<std::vector<juce::String>> outputConnections (outputPorts.size());

    const auto captureConnections = [] (const std::vector<jack_port_t*>& ports,
                                        std::vector<std::vector<juce::String>>& connections)
    {
        for (size_t index = 0; index < ports.size(); ++index)
        {
            if (ports[index] == nullptr)
                continue;

            const auto* connectedPorts = jack_port_get_connections (ports[index]);
            if (connectedPorts == nullptr)
                continue;

            for (size_t connectionIndex = 0; connectedPorts[connectionIndex] != nullptr;
                 ++connectionIndex)
                connections[index].emplace_back (
                    juce::String::fromUTF8 (connectedPorts[connectionIndex]));

            jack_free (const_cast<char**> (connectedPorts));
        }
    };

    captureConnections (inputPorts, inputConnections);
    captureConnections (outputPorts, outputConnections);

    deactivate();
    if (client != nullptr)
        jack_client_close (client);
    client = nullptr;
    inputPorts.clear();
    outputPorts.clear();
    inputPortNames.clear();
    outputPortNames.clear();
    inputBuffers.clear();
    outputBuffers.clear();
    connected.store (false, std::memory_order_release);

    juce::StringArray inputNames;
    for (const auto& name : savedInputNames)
        inputNames.add (name);
    juce::StringArray outputNames;
    for (const auto& name : savedOutputNames)
        outputNames.add (name);

    if (! open (requestedName, expectedBlockSize)
        || ! configurePorts (inputNames, outputNames))
    {
        close();
        callback = savedCallback;
        callbackUserData = savedCallbackUserData;
        return false;
    }

    callback = savedCallback;
    callbackUserData = savedCallbackUserData;

    if (wasRunning && ! activate())
    {
        close();
        return false;
    }

    const auto restoreConnections = [this] (const std::vector<jack_port_t*>& ports,
                                            const std::vector<std::vector<juce::String>>& connections,
                                            bool localPortIsInput)
    {
        for (size_t index = 0; index < ports.size(); ++index)
        {
            if (ports[index] == nullptr)
                continue;

            const auto* localPortName = jack_port_name (ports[index]);
            if (localPortName == nullptr)
                continue;

            for (const auto& connectedPort : connections[index])
            {
                if (localPortIsInput)
                    jack_connect (client, connectedPort.toRawUTF8(), localPortName);
                else
                    jack_connect (client, localPortName, connectedPort.toRawUTF8());
            }
        }
    };

    restoreConnections (inputPorts, inputConnections, true);
    restoreConnections (outputPorts, outputConnections, false);
    return true;
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

    for (size_t index = 0; index < owner->inputPorts.size(); ++index)
        owner->inputBuffers[index] = static_cast<const float*>(jack_port_get_buffer(owner->inputPorts[index], frameCount));
    for (size_t index = 0; index < owner->outputPorts.size(); ++index)
        owner->outputBuffers[index] = static_cast<float*>(jack_port_get_buffer(owner->outputPorts[index], frameCount));

    owner->callback(owner->inputBuffers.data(), static_cast<int>(owner->inputBuffers.size()),
                    owner->outputBuffers.data(), static_cast<int>(owner->outputBuffers.size()),
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
                               std::vector<jack_port_t*>& destination) noexcept
{
    for (const auto& name : names)
    {
        auto* port = jack_port_register(client, name.toRawUTF8(), JACK_DEFAULT_AUDIO_TYPE, flags, 0);
        if (port == nullptr)
        {
            setError("Unable to register JACK audio port");
            for (auto* registeredPort : destination)
                jack_port_unregister(client, registeredPort);
            destination.clear();
            return false;
        }
        destination.push_back(port);
    }
    return true;
}

void JackClient::setError(const char* message) noexcept
{
    lastError = message;
    connected.store(false, std::memory_order_release);
}

} // namespace wjn::common
