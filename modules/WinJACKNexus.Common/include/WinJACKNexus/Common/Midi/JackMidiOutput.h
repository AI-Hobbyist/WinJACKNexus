#pragma once

#include "../Midi/MidiEventQueue.h"

#include <atomic>
#include <vector>

#include <juce_core/juce_core.h>
#include <jack/jack.h>
#include <jack/midiport.h>

namespace wjn::common
{

class JackMidiOutput final
{
public:
    bool open(const juce::String& clientName, const juce::String& portName) noexcept;
    bool start() noexcept;
    void stop() noexcept;
    void close() noexcept;
    bool rename(const juce::String& clientName) noexcept;
    bool isOpen() const noexcept;
    bool push(const MidiEvent& event) noexcept;
    bool push(uint32_t frameOffset, const uint8_t* data, size_t size) noexcept;
    juce::uint64 getDroppedEventCount() const noexcept;
    const juce::String& getLastError() const noexcept;

private:
    static int process(jack_nframes_t frames, void* userData) noexcept;
    static void shutdown(void* userData) noexcept;
    void setError(const char* message) noexcept;

    jack_client_t* client = nullptr;
    jack_port_t* port = nullptr;
    juce::String clientName;
    juce::String portName;
    MidiEventQueue queue;
    std::atomic<bool> connected { false };
    std::atomic<bool> running { false };
    std::atomic<juce::uint64> droppedEvents { 0 };
    juce::String lastError;
};

} // namespace wjn::common
