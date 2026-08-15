#include "JackMidiOutput.h"

namespace wjn::common
{

bool JackMidiOutput::open(const juce::String& newClientName, const juce::String& newPortName) noexcept
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
                              JackPortIsOutput, 0);
    if (port == nullptr)
    {
        setError("Unable to register JACK MIDI output port");
        close();
        return false;
    }
    if (jack_set_process_callback(client, &JackMidiOutput::process, this) != 0)
    {
        setError("Unable to register JACK MIDI output callback");
        close();
        return false;
    }
    jack_on_shutdown(client, &JackMidiOutput::shutdown, this);
    this->clientName = requestedClientName;
    this->portName = requestedPortName;
    connected.store(true, std::memory_order_release);
    lastError.clear();
    return true;
}

bool JackMidiOutput::start() noexcept
{
    if (client == nullptr || jack_activate(client) != 0)
    {
        setError("Unable to activate JACK MIDI output");
        return false;
    }
    running.store(true, std::memory_order_release);
    return true;
}

void JackMidiOutput::stop() noexcept
{
    running.store(false, std::memory_order_release);
    if (client != nullptr)
        jack_deactivate(client);
}

void JackMidiOutput::close() noexcept
{
    stop();
    if (client != nullptr)
        jack_client_close(client);
    client = nullptr;
    port = nullptr;
    clientName.clear();
    portName.clear();
    connected.store(false, std::memory_order_release);
    queue.reset();
}

bool JackMidiOutput::rename(const juce::String& newClientName) noexcept
{
    const auto requestedClientName = newClientName.trim();
    if (client == nullptr || requestedClientName.isEmpty() || portName.isEmpty())
    {
        setError("Unable to rename an unopened JACK MIDI output");
        return false;
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
                                                   : "Unable to reconnect JACK MIDI output");
        return false;
    }

    port = jack_port_register(client, savedPortName.toRawUTF8(), JACK_DEFAULT_MIDI_TYPE,
                              JackPortIsOutput, 0);
    if (port == nullptr
        || jack_set_process_callback(client, &JackMidiOutput::process, this) != 0)
    {
        setError("Unable to rebuild JACK MIDI output port");
        close();
        return false;
    }

    jack_on_shutdown(client, &JackMidiOutput::shutdown, this);
    clientName = requestedClientName;
    portName = savedPortName;
    connected.store(true, std::memory_order_release);
    lastError.clear();

    if (wasRunning && (jack_activate(client) != 0))
    {
        setError("Unable to reactivate JACK MIDI output");
        close();
        return false;
    }
    running.store(wasRunning, std::memory_order_release);

    const auto* localPortName = jack_port_name(port);
    if (localPortName != nullptr)
        for (const auto& connectedPort : connections)
            jack_connect(client, localPortName, connectedPort.toRawUTF8());
    return true;
}

bool JackMidiOutput::isOpen() const noexcept { return connected.load(std::memory_order_acquire); }
bool JackMidiOutput::push(const MidiEvent& event) noexcept
{
    if (!queue.push(event))
    {
        droppedEvents.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool JackMidiOutput::push(uint32_t frameOffset, const uint8_t* data, size_t size) noexcept
{
    if (!queue.push(frameOffset, data, size))
    {
        droppedEvents.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

juce::uint64 JackMidiOutput::getDroppedEventCount() const noexcept { return droppedEvents.load(std::memory_order_relaxed); }
const juce::String& JackMidiOutput::getLastError() const noexcept { return lastError; }

int JackMidiOutput::process(jack_nframes_t frames, void* userData) noexcept
{
    auto* owner = static_cast<JackMidiOutput*>(userData);
    if (owner == nullptr || owner->port == nullptr)
        return 0;
    auto* buffer = jack_port_get_buffer(owner->port, frames);
    jack_midi_clear_buffer(buffer);
    MidiEvent event;
    while (owner->queue.pop(event))
    {
        if (event.sampleOffset >= frames
            || jack_midi_event_write(buffer, event.sampleOffset, event.bytes.data(), event.size) != 0)
            owner->droppedEvents.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

void JackMidiOutput::shutdown(void* userData) noexcept
{
    auto* owner = static_cast<JackMidiOutput*>(userData);
    if (owner != nullptr)
    {
        owner->running.store(false, std::memory_order_release);
        owner->connected.store(false, std::memory_order_release);
    }
}

void JackMidiOutput::setError(const char* message) noexcept
{
    lastError = message;
    connected.store(false, std::memory_order_release);
}

} // namespace wjn::common
