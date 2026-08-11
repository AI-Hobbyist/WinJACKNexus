#include "JackMidiInput.h"

namespace wjn::common
{

bool JackMidiInput::open(const juce::String& clientName, const juce::String& portName) noexcept
{
    close();
    jack_status_t status = JackFailure;
    client = jack_client_open(clientName.toRawUTF8(), JackNoStartServer, &status);
    if (client == nullptr)
    {
        setError((status & JackServerFailed) != 0 ? "JACK server is not running" : "Unable to connect to JACK");
        return false;
    }
    port = jack_port_register(client, portName.toRawUTF8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
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
    connected.store(true, std::memory_order_release);
    lastError.clear();
    return true;
}

bool JackMidiInput::start() noexcept
{
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
    if (client != nullptr)
        jack_deactivate(client);
}

void JackMidiInput::close() noexcept
{
    stop();
    if (client != nullptr)
        jack_client_close(client);
    client = nullptr;
    port = nullptr;
    connected.store(false, std::memory_order_release);
    queue.reset();
}

bool JackMidiInput::isOpen() const noexcept { return connected.load(std::memory_order_acquire); }
bool JackMidiInput::pop(MidiEvent& event) noexcept { return queue.pop(event); }
juce::uint64 JackMidiInput::getDroppedEventCount() const noexcept { return droppedEvents.load(std::memory_order_relaxed); }
const juce::String& JackMidiInput::getLastError() const noexcept { return lastError; }

int JackMidiInput::process(jack_nframes_t frames, void* userData) noexcept
{
    auto* owner = static_cast<JackMidiInput*>(userData);
    if (owner == nullptr || owner->port == nullptr)
        return 0;
    auto* buffer = jack_port_get_buffer(owner->port, frames);
    const auto count = jack_midi_get_event_count(buffer);
    for (uint32_t index = 0; index < count; ++index)
    {
        jack_midi_event_t event {};
        if (jack_midi_event_get(&event, buffer, index) != 0
            || event.size > MidiEvent::maxBytes
            || !owner->queue.push(MidiEvent::make(event.time, event.buffer, event.size)))
            owner->droppedEvents.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
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
