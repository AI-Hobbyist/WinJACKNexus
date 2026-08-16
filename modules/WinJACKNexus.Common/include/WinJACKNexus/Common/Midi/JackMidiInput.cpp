#include "JackMidiInput.h"

namespace wjn::common
{

bool JackMidiInput::open(const juce::String& newClientName, const juce::String& newPortName) noexcept
{
    close();
    const auto requestedClientName = newClientName.trim();
    const auto requestedPortName = newPortName.trim();
    jack_status_t status = JackFailure;
    client = jack_client_open(requestedClientName.toRawUTF8(), JackNoStartServer, &status);
    if (client == nullptr)
    {
        setError((status & JackServerFailed) != 0 ? "JACK server is not running" : "Unable to connect to JACK");
        return false;
    }
    port = jack_port_register(client, requestedPortName.toRawUTF8(), JACK_DEFAULT_MIDI_TYPE,
                              JackPortIsInput, 0);
    if (port == nullptr)
    {
        setError("Unable to register JACK MIDI input port");
        close();
        return false;
    }
    if (jack_set_process_callback(client, &JackMidiInput::process, this) != 0)
    {
        setError("Unable to register JACK MIDI input callback");
        close();
        return false;
    }
    jack_on_shutdown(client, &JackMidiInput::shutdown, this);
    this->clientName = requestedClientName;
    this->portName = requestedPortName;
    connected.store(true, std::memory_order_release);
    lastError.clear();
    return true;
}

bool JackMidiInput::open(JackClientHub& newHub, const juce::String& newClientName,
                         const juce::String& newPortName) noexcept
{
    close();
    const auto requestedClientName = newClientName.trim();
    const auto requestedPortName = newPortName.trim();
    const auto handle = newHub.registerMidiPort(requestedPortName, JackPortIsInput,
                                                 &JackMidiInput::processHub, this);
    if (handle == JackClientHub::invalidPortHandle)
        return false;

    hub = &newHub;
    hubHandle = handle;
    clientName = requestedClientName;
    portName = requestedPortName;
    connected.store(true, std::memory_order_release);
    lastError.clear();
    return true;
}

bool JackMidiInput::start() noexcept
{
    if (hub != nullptr)
    {
        if (! hub->start(hubHandle))
        {
            setError("Unable to activate JACK MIDI input");
            return false;
        }
        running.store(true, std::memory_order_release);
        return true;
    }
    if (client == nullptr || jack_activate(client) != 0)
    {
        setError("Unable to activate JACK MIDI input");
        return false;
    }
    running.store(true, std::memory_order_release);
    return true;
}

void JackMidiInput::stop() noexcept
{
    running.store(false, std::memory_order_release);
    if (hub != nullptr)
        hub->stop(hubHandle);
    else if (client != nullptr)
        jack_deactivate(client);
}

void JackMidiInput::close() noexcept
{
    stop();
    if (hub != nullptr)
        hub->unregister(hubHandle);
    if (client != nullptr)
        jack_client_close(client);
    client = nullptr;
    port = nullptr;
    hub = nullptr;
    hubHandle = JackClientHub::invalidPortHandle;
    clientName.clear();
    portName.clear();
    connected.store(false, std::memory_order_release);
    queue.reset();
}

bool JackMidiInput::rename(const juce::String& newClientName) noexcept
{
    const auto requestedClientName = newClientName.trim();
    if (client == nullptr || requestedClientName.isEmpty() || portName.isEmpty())
    {
        setError("Unable to rename an unopened JACK MIDI input");
        return false;
    }

    if (hub != nullptr)
    {
        const auto renamed = hub->renameMidiPort(hubHandle,
                                                 JackClientHub::midiPortName(requestedClientName));
        if (renamed)
        {
            clientName = requestedClientName;
            portName = JackClientHub::midiPortName(requestedClientName);
        }
        return renamed;
    }

    const auto* currentName = jack_get_client_name(client);
    const auto currentClientName = currentName != nullptr
        ? juce::String::fromUTF8(currentName)
        : juce::String();
    if (currentClientName == requestedClientName)
        return true;

    std::vector<juce::String> connections;
    if (port != nullptr)
    {
        const auto* connectedPorts = jack_port_get_connections(port);
        if (connectedPorts != nullptr)
        {
            for (size_t index = 0; connectedPorts[index] != nullptr; ++index)
                connections.emplace_back(juce::String::fromUTF8(connectedPorts[index]));
            jack_free(const_cast<char**>(connectedPorts));
        }
    }

    const auto wasRunning = running.load(std::memory_order_acquire);
    const auto savedPortName = portName;
    stop();
    if (client != nullptr)
        jack_client_close(client);
    client = nullptr;
    port = nullptr;
    connected.store(false, std::memory_order_release);

    jack_status_t status = JackFailure;
    client = jack_client_open(requestedClientName.toRawUTF8(), JackNoStartServer, &status);
    if (client == nullptr)
    {
        setError((status & JackServerFailed) != 0 ? "JACK server is not running"
                                                   : "Unable to reconnect JACK MIDI input");
        return false;
    }

    port = jack_port_register(client, savedPortName.toRawUTF8(), JACK_DEFAULT_MIDI_TYPE,
                              JackPortIsInput, 0);
    if (port == nullptr
        || jack_set_process_callback(client, &JackMidiInput::process, this) != 0)
    {
        setError("Unable to rebuild JACK MIDI input port");
        close();
        return false;
    }

    jack_on_shutdown(client, &JackMidiInput::shutdown, this);
    clientName = requestedClientName;
    portName = savedPortName;
    connected.store(true, std::memory_order_release);
    lastError.clear();

    if (wasRunning && (jack_activate(client) != 0))
    {
        setError("Unable to reactivate JACK MIDI input");
        close();
        return false;
    }
    running.store(wasRunning, std::memory_order_release);

    const auto* localPortName = jack_port_name(port);
    if (localPortName != nullptr)
        for (const auto& connectedPort : connections)
            jack_connect(client, connectedPort.toRawUTF8(), localPortName);
    return true;
}

bool JackMidiInput::isOpen() const noexcept
{
    return hub != nullptr ? hub->isRouteOpen(hubHandle)
                          : connected.load(std::memory_order_acquire);
}
bool JackMidiInput::pop(MidiEvent& event) noexcept { return queue.pop(event); }
juce::uint64 JackMidiInput::getDroppedEventCount() const noexcept { return droppedEvents.load(std::memory_order_relaxed); }
const juce::String& JackMidiInput::getLastError() const noexcept
{
    return hub != nullptr ? hub->getLastError() : lastError;
}

int JackMidiInput::process(jack_nframes_t frames, void* userData) noexcept
{
    auto* owner = static_cast<JackMidiInput*>(userData);
    if (owner == nullptr || owner->port == nullptr)
        return 0;
    owner->processBuffer(jack_port_get_buffer(owner->port, frames), frames);
    return 0;
}

void JackMidiInput::processHub(void* buffer, jack_nframes_t frames, void* userData) noexcept
{
    auto* owner = static_cast<JackMidiInput*>(userData);
    if (owner != nullptr)
        owner->processBuffer(buffer, frames);
}

void JackMidiInput::processBuffer(void* buffer, jack_nframes_t) noexcept
{
    if (buffer == nullptr)
        return;
    const auto count = jack_midi_get_event_count(buffer);
    for (uint32_t index = 0; index < count; ++index)
    {
        jack_midi_event_t event {};
        if (jack_midi_event_get(&event, buffer, index) != 0
            || event.size > MidiEvent::maxBytes
            || !queue.push(MidiEvent::make(event.time, event.buffer, event.size)))
            droppedEvents.fetch_add(1, std::memory_order_relaxed);
    }
}

void JackMidiInput::shutdown(void* userData) noexcept
{
    auto* owner = static_cast<JackMidiInput*>(userData);
    if (owner != nullptr)
    {
        owner->running.store(false, std::memory_order_release);
        owner->connected.store(false, std::memory_order_release);
    }
}

void JackMidiInput::setError(const char* message) noexcept
{
    lastError = message;
    connected.store(false, std::memory_order_release);
}

} // namespace wjn::common
