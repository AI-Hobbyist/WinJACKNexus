#include "JackClientHub.h"

namespace wjn::common
{

JackClientHub::~JackClientHub()
{
    close();
}

bool JackClientHub::open (const juce::String& requestedClientName,
                          int expectedBlockSize) noexcept
{
    const std::scoped_lock lock (structureMutex);
    if (client != nullptr)
        return true;

    const auto clientName = requestedClientName.trim();
    if (clientName.isEmpty())
    {
        setError ("Unable to open JACK client without a name");
        return false;
    }

    jack_status_t status = JackFailure;
    client = jack_client_open (clientName.toRawUTF8(), JackNoStartServer, &status);
    if (client == nullptr)
    {
        setError ((status & JackServerFailed) != 0 ? "JACK server is not running"
                                                    : "Unable to connect to JACK");
        return false;
    }

    sampleRate.store (static_cast<int> (jack_get_sample_rate (client)),
                      std::memory_order_release);
    blockSize.store (static_cast<int> (jack_get_buffer_size (client)),
                     std::memory_order_release);
    if (expectedBlockSize > 0 && blockSize.load (std::memory_order_acquire) > expectedBlockSize)
    {
        setError ("JACK buffer size exceeds the configured limit");
        jack_client_close (client);
        client = nullptr;
        return false;
    }

    if (jack_set_process_callback (client, &JackClientHub::processCallback, this) != 0
        || jack_set_buffer_size_callback (client, &JackClientHub::bufferSizeCallback, this) != 0
        || jack_set_sample_rate_callback (client, &JackClientHub::sampleRateCallback, this) != 0
        || jack_set_xrun_callback (client, &JackClientHub::xrunCallback, this) != 0)
    {
        setError ("Unable to register JACK callbacks");
        jack_client_close (client);
        client = nullptr;
        return false;
    }

    jack_on_shutdown (client, &JackClientHub::shutdownCallback, this);
    connected.store (true, std::memory_order_release);
    lastError.clear();
    return true;
}

bool JackClientHub::start (PortHandle handle) noexcept
{
    const std::scoped_lock lock (structureMutex);
    const auto groupIndex = handle - 1;
    if (client == nullptr || groupIndex < 0 || groupIndex >= maxGroups
        || ! groups[static_cast<size_t> (groupIndex)].inUse)
        return false;

    auto& group = groups[static_cast<size_t> (groupIndex)];
    if (group.active.load (std::memory_order_acquire))
        return true;

    if (activeRouteCount == 0 && jack_activate (client) != 0)
    {
        setError ("Unable to activate JACK client");
        return false;
    }

    group.active.store (true, std::memory_order_release);
    ++activeRouteCount;
    running.store (true, std::memory_order_release);
    return true;
}

void JackClientHub::stop (PortHandle handle) noexcept
{
    const std::scoped_lock lock (structureMutex);
    const auto groupIndex = handle - 1;
    if (client == nullptr || groupIndex < 0 || groupIndex >= maxGroups
        || ! groups[static_cast<size_t> (groupIndex)].inUse)
        return;

    auto& group = groups[static_cast<size_t> (groupIndex)];
    if (! group.active.exchange (false, std::memory_order_acq_rel))
        return;

    activeRouteCount = activeRouteCount > 0 ? activeRouteCount - 1 : 0;
    if (activeRouteCount == 0)
    {
        running.store (false, std::memory_order_release);
        jack_deactivate (client);
    }
}

void JackClientHub::close() noexcept
{
    const std::scoped_lock lock (structureMutex);
    if (client == nullptr)
        return;

    running.store (false, std::memory_order_release);
    activeRouteCount = 0;
    jack_deactivate (client);

    for (auto& group : groups)
    {
        group.active.store (false, std::memory_order_release);
        for (int index = 0; index < group.inputCount; ++index)
            if (group.inputPorts[static_cast<size_t> (index)] != nullptr)
                jack_port_unregister (client, group.inputPorts[static_cast<size_t> (index)]);
        for (int index = 0; index < group.outputCount; ++index)
            if (group.outputPorts[static_cast<size_t> (index)] != nullptr)
                jack_port_unregister (client, group.outputPorts[static_cast<size_t> (index)]);
        if (group.midiPort != nullptr)
            jack_port_unregister (client, group.midiPort);
        resetGroup (group);
    }

    jack_client_close (client);
    client = nullptr;
    connected.store (false, std::memory_order_release);
}

JackClientHub::PortHandle JackClientHub::registerAudioPorts (
    const juce::StringArray& inputNames, const juce::StringArray& outputNames,
    AudioProcessCallback callback, void* userData) noexcept
{
    if (callback == nullptr
        || inputNames.size() > maxPortsPerGroup
        || outputNames.size() > maxPortsPerGroup
        || (inputNames.isEmpty() && outputNames.isEmpty()))
    {
        setError ("Invalid JACK audio port group");
        return invalidPortHandle;
    }

    const std::scoped_lock lock (structureMutex);
    if (client == nullptr)
    {
        setError ("Unable to register ports on an unopened JACK client");
        return invalidPortHandle;
    }

    const auto groupIndex = findFreeGroup();
    if (groupIndex < 0)
    {
        setError ("JACK client port group limit reached");
        return invalidPortHandle;
    }

    const auto wasRunning = activeRouteCount > 0;
    if (wasRunning)
    {
        jack_deactivate (client);
        running.store (false, std::memory_order_release);
    }

    auto& group = groups[static_cast<size_t> (groupIndex)];
    const auto unregisterPorts = [this, &group]
    {
        for (int index = 0; index < group.inputCount; ++index)
            if (group.inputPorts[static_cast<size_t> (index)] != nullptr)
                jack_port_unregister (client, group.inputPorts[static_cast<size_t> (index)]);
        for (int index = 0; index < group.outputCount; ++index)
            if (group.outputPorts[static_cast<size_t> (index)] != nullptr)
                jack_port_unregister (client, group.outputPorts[static_cast<size_t> (index)]);
        resetGroup (group);
    };

    for (int index = 0; index < inputNames.size(); ++index)
    {
        auto* port = jack_port_register (client, inputNames[index].toRawUTF8(),
                                         JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        if (port == nullptr)
        {
            unregisterPorts();
            if (wasRunning)
                jack_activate (client);
            running.store (wasRunning, std::memory_order_release);
            setError ("Unable to register JACK audio input port");
            return invalidPortHandle;
        }
        group.inputPorts[static_cast<size_t> (index)] = port;
        group.inputNames[static_cast<size_t> (index)] = inputNames[index];
        ++group.inputCount;
    }

    for (int index = 0; index < outputNames.size(); ++index)
    {
        auto* port = jack_port_register (client, outputNames[index].toRawUTF8(),
                                         JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        if (port == nullptr)
        {
            unregisterPorts();
            if (wasRunning)
                jack_activate (client);
            running.store (wasRunning, std::memory_order_release);
            setError ("Unable to register JACK audio output port");
            return invalidPortHandle;
        }
        group.outputPorts[static_cast<size_t> (index)] = port;
        group.outputNames[static_cast<size_t> (index)] = outputNames[index];
        ++group.outputCount;
    }

    group.kind = GroupKind::audio;
    group.audioCallback = callback;
    group.callbackUserData = userData;
    group.inUse = true;
    if (wasRunning && jack_activate (client) != 0)
    {
        unregisterPorts();
        running.store (false, std::memory_order_release);
        setError ("Unable to reactivate JACK client after registering audio ports");
        return invalidPortHandle;
    }
    running.store (wasRunning, std::memory_order_release);
    return groupIndex + 1;
}

JackClientHub::PortHandle JackClientHub::registerMidiPort (
    const juce::String& portName, unsigned long flags,
    MidiProcessCallback callback, void* userData) noexcept
{
    if (portName.trim().isEmpty() || callback == nullptr
        || (flags & (JackPortIsInput | JackPortIsOutput)) == 0)
    {
        setError ("Invalid JACK MIDI port");
        return invalidPortHandle;
    }

    const std::scoped_lock lock (structureMutex);
    if (client == nullptr)
    {
        setError ("Unable to register ports on an unopened JACK client");
        return invalidPortHandle;
    }

    const auto groupIndex = findFreeGroup();
    if (groupIndex < 0)
    {
        setError ("JACK client port group limit reached");
        return invalidPortHandle;
    }

    const auto wasRunning = activeRouteCount > 0;
    if (wasRunning)
    {
        jack_deactivate (client);
        running.store (false, std::memory_order_release);
    }

    auto& group = groups[static_cast<size_t> (groupIndex)];
    group.midiPort = jack_port_register (client, portName.trim().toRawUTF8(),
                                         JACK_DEFAULT_MIDI_TYPE, flags, 0);
    if (group.midiPort == nullptr)
    {
        if (wasRunning)
            jack_activate (client);
        running.store (wasRunning, std::memory_order_release);
        setError ("Unable to register JACK MIDI port");
        return invalidPortHandle;
    }

    group.kind = GroupKind::midi;
    group.inputNames[0] = portName.trim();
    group.midiCallback = callback;
    group.callbackUserData = userData;
    group.inUse = true;
    if (wasRunning && jack_activate (client) != 0)
    {
        jack_port_unregister (client, group.midiPort);
        resetGroup (group);
        running.store (false, std::memory_order_release);
        setError ("Unable to reactivate JACK client after registering MIDI port");
        return invalidPortHandle;
    }
    running.store (wasRunning, std::memory_order_release);
    return groupIndex + 1;
}

bool JackClientHub::unregister (PortHandle handle) noexcept
{
    const std::scoped_lock lock (structureMutex);
    const auto groupIndex = handle - 1;
    if (client == nullptr || groupIndex < 0 || groupIndex >= maxGroups
        || ! groups[static_cast<size_t> (groupIndex)].inUse)
        return false;

    auto& group = groups[static_cast<size_t> (groupIndex)];
    const auto wasRunning = activeRouteCount > 0;
    if (group.active.exchange (false, std::memory_order_acq_rel))
        activeRouteCount = activeRouteCount > 0 ? activeRouteCount - 1 : 0;

    if (wasRunning)
    {
        jack_deactivate (client);
        running.store (false, std::memory_order_release);
    }

    for (int index = 0; index < group.inputCount; ++index)
        if (group.inputPorts[static_cast<size_t> (index)] != nullptr)
            jack_port_unregister (client, group.inputPorts[static_cast<size_t> (index)]);
    for (int index = 0; index < group.outputCount; ++index)
        if (group.outputPorts[static_cast<size_t> (index)] != nullptr)
            jack_port_unregister (client, group.outputPorts[static_cast<size_t> (index)]);
    if (group.midiPort != nullptr)
        jack_port_unregister (client, group.midiPort);
    resetGroup (group);

    if (wasRunning && activeRouteCount > 0)
    {
        if (jack_activate (client) != 0)
        {
            setError ("Unable to reactivate JACK client");
            return false;
        }
        running.store (true, std::memory_order_release);
    }
    return true;
}

bool JackClientHub::renameAudioPorts (PortHandle handle,
                                      const juce::StringArray& inputNames,
                                      const juce::StringArray& outputNames) noexcept
{
    const std::scoped_lock lock (structureMutex);
    const auto groupIndex = handle - 1;
    if (client == nullptr || groupIndex < 0 || groupIndex >= maxGroups)
        return false;

    auto& group = groups[static_cast<size_t> (groupIndex)];
    if (! group.inUse || group.kind != GroupKind::audio
        || inputNames.size() != group.inputCount
        || outputNames.size() != group.outputCount)
        return false;

    const auto wasRunning = activeRouteCount > 0;
    if (wasRunning)
    {
        jack_deactivate (client);
        running.store (false, std::memory_order_release);
    }

    for (int index = 0; index < group.inputCount; ++index)
        if (jack_port_rename (client, group.inputPorts[static_cast<size_t> (index)],
                              inputNames[index].toRawUTF8()) != 0)
        {
            if (wasRunning)
                jack_activate (client);
            running.store (wasRunning, std::memory_order_release);
            return false;
        }
    for (int index = 0; index < group.outputCount; ++index)
        if (jack_port_rename (client, group.outputPorts[static_cast<size_t> (index)],
                              outputNames[index].toRawUTF8()) != 0)
        {
            if (wasRunning)
                jack_activate (client);
            running.store (wasRunning, std::memory_order_release);
            return false;
        }

    for (int index = 0; index < group.inputCount; ++index)
        group.inputNames[static_cast<size_t> (index)] = inputNames[index];
    for (int index = 0; index < group.outputCount; ++index)
        group.outputNames[static_cast<size_t> (index)] = outputNames[index];
    if (wasRunning && jack_activate (client) != 0)
        return false;
    running.store (wasRunning, std::memory_order_release);
    return true;
}

bool JackClientHub::renameMidiPort (PortHandle handle,
                                    const juce::String& portName) noexcept
{
    const auto requestedName = portName.trim();
    const std::scoped_lock lock (structureMutex);
    const auto groupIndex = handle - 1;
    if (client == nullptr || groupIndex < 0 || groupIndex >= maxGroups
        || requestedName.isEmpty())
        return false;

    auto& group = groups[static_cast<size_t> (groupIndex)];
    if (! group.inUse || group.kind != GroupKind::midi || group.midiPort == nullptr)
        return false;

    const auto wasRunning = activeRouteCount > 0;
    if (wasRunning)
    {
        jack_deactivate (client);
        running.store (false, std::memory_order_release);
    }

    if (jack_port_rename (client, group.midiPort, requestedName.toRawUTF8()) != 0)
    {
        if (wasRunning)
            jack_activate (client);
        running.store (wasRunning, std::memory_order_release);
        return false;
    }

    group.inputNames[0] = requestedName;
    if (wasRunning && jack_activate (client) != 0)
        return false;
    running.store (wasRunning, std::memory_order_release);
    return true;
}

bool JackClientHub::isRouteOpen (PortHandle handle) const noexcept
{
    const std::scoped_lock lock (structureMutex);
    const auto groupIndex = handle - 1;
    return client != nullptr && groupIndex >= 0 && groupIndex < maxGroups
        && groups[static_cast<size_t> (groupIndex)].inUse;
}

JackClient::Status JackClientHub::getStatus() const noexcept
{
    return { connected.load (std::memory_order_acquire),
             running.load (std::memory_order_acquire),
             sampleRate.load (std::memory_order_acquire),
             blockSize.load (std::memory_order_acquire),
             callbackCount.load (std::memory_order_relaxed),
             xrunCount.load (std::memory_order_relaxed) };
}

const juce::String& JackClientHub::getLastError() const noexcept
{
    return lastError;
}

juce::String JackClientHub::audioPortName (const juce::String& clientName,
                                           int channelIndex)
{
    return clientName.trim() + "_" + juce::String (channelIndex + 1);
}

juce::String JackClientHub::midiPortName (const juce::String& clientName)
{
    return clientName.trim() + "_MIDI";
}

int JackClientHub::processCallback (jack_nframes_t frameCount, void* userData) noexcept
{
    auto* owner = static_cast<JackClientHub*> (userData);
    if (owner == nullptr || frameCount > JackClient::maxBlockFrames)
        return 0;

    for (auto& group : owner->groups)
    {
        if (! group.inUse || ! group.active.load (std::memory_order_acquire))
            continue;

        if (group.kind == GroupKind::audio)
        {
            for (int index = 0; index < group.inputCount; ++index)
                group.inputBuffers[static_cast<size_t> (index)] = static_cast<const float*> (
                    jack_port_get_buffer (group.inputPorts[static_cast<size_t> (index)], frameCount));
            for (int index = 0; index < group.outputCount; ++index)
                group.outputBuffers[static_cast<size_t> (index)] = static_cast<float*> (
                    jack_port_get_buffer (group.outputPorts[static_cast<size_t> (index)], frameCount));

            if (group.audioCallback != nullptr)
                group.audioCallback (group.inputBuffers.data(), group.inputCount,
                                     group.outputBuffers.data(), group.outputCount,
                                     static_cast<int> (frameCount), group.callbackUserData);
        }
        else if (group.midiCallback != nullptr && group.midiPort != nullptr)
        {
            auto* buffer = jack_port_get_buffer (group.midiPort, frameCount);
            group.midiCallback (buffer, frameCount, group.callbackUserData);
        }
    }

    owner->callbackCount.fetch_add (1, std::memory_order_relaxed);
    return 0;
}

int JackClientHub::bufferSizeCallback (jack_nframes_t frameCount, void* userData) noexcept
{
    auto* owner = static_cast<JackClientHub*> (userData);
    if (owner != nullptr)
        owner->blockSize.store (static_cast<int> (frameCount), std::memory_order_release);
    return frameCount <= JackClient::maxBlockFrames ? 0 : 1;
}

int JackClientHub::sampleRateCallback (jack_nframes_t newSampleRate, void* userData) noexcept
{
    auto* owner = static_cast<JackClientHub*> (userData);
    if (owner != nullptr)
        owner->sampleRate.store (static_cast<int> (newSampleRate), std::memory_order_release);
    return 0;
}

int JackClientHub::xrunCallback (void* userData) noexcept
{
    auto* owner = static_cast<JackClientHub*> (userData);
    if (owner != nullptr)
        owner->xrunCount.fetch_add (1, std::memory_order_relaxed);
    return 0;
}

void JackClientHub::shutdownCallback (void* userData) noexcept
{
    auto* owner = static_cast<JackClientHub*> (userData);
    if (owner == nullptr)
        return;

    owner->running.store (false, std::memory_order_release);
    owner->connected.store (false, std::memory_order_release);
    for (auto& group : owner->groups)
        group.active.store (false, std::memory_order_release);
}

int JackClientHub::findFreeGroup() const noexcept
{
    for (int index = 0; index < maxGroups; ++index)
        if (! groups[static_cast<size_t> (index)].inUse)
            return index;
    return -1;
}

void JackClientHub::resetGroup (Group& group) noexcept
{
    group.inUse = false;
    group.active.store (false, std::memory_order_release);
    group.inputPorts.fill (nullptr);
    group.outputPorts.fill (nullptr);
    group.inputBuffers.fill (nullptr);
    group.outputBuffers.fill (nullptr);
    for (auto& name : group.inputNames)
        name.clear();
    for (auto& name : group.outputNames)
        name.clear();
    group.inputCount = 0;
    group.outputCount = 0;
    group.midiPort = nullptr;
    group.audioCallback = nullptr;
    group.midiCallback = nullptr;
    group.callbackUserData = nullptr;
}

void JackClientHub::setError (const char* message) noexcept
{
    lastError = message;
    connected.store (false, std::memory_order_release);
}

} // namespace wjn::common