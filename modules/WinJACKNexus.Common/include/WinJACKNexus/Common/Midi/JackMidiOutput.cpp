#include "JackMidiOutput.h"

namespace wjn::common
{

bool JackMidiOutput::open(const juce::String& clientName, const juce::String& portName) noexcept
{
    close();
    jack_status_t status = JackFailure;
    client = jack_client_open(clientName.toRawUTF8(), JackNoStartServer, &status);
    if (client == nullptr)
    {
        setError((status & JackServerFailed) != 0 ? "JACK server is not running" : "Unable to connect to JACK");
        return false;
    }
    port = jack_port_register(client, portName.toRawUTF8(), JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
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
    connected.store(false, std::memory_order_release);
    queue.reset();
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
