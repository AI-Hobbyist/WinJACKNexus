#include <WinJACKNexus/Common/IO/SpscRingBuffer.h>
#include <WinJACKNexus/Common/Midi/MidiEventQueue.h>

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "MIDI/FIFO test failure: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    wjn::common::SpscRingBuffer<int, 4> numbers;
    require(numbers.size() == 0, "New queue must be empty");
    require(numbers.push(10) && numbers.push(20) && numbers.push(30), "Queue accepts capacity items");
    require(!numbers.push(40), "Full queue must reject new items");
    int value = 0;
    require(numbers.pop(value) && value == 10, "Queue preserves FIFO order");
    require(numbers.push(40), "Queue accepts items after pop");
    require(numbers.pop(value) && value == 20, "Queue wraps without reordering");
    require(numbers.pop(value) && value == 30, "Queue returns third item");
    require(numbers.pop(value) && value == 40, "Queue returns wrapped item");
    require(!numbers.pop(value), "Empty queue must reject pop");

    wjn::common::MidiEventQueue midi;
    const uint8_t noteOn[] {0x90, 60, 100};
    require(midi.push(12, noteOn, 3), "MIDI event must be accepted");
    wjn::common::MidiEvent event;
    require(midi.pop(event), "MIDI event must be readable");
    require(event.sampleOffset == 12 && event.size == 3, "MIDI metadata must be preserved");
    require(event.bytes[0] == 0x90 && event.bytes[1] == 60 && event.bytes[2] == 100,
            "MIDI bytes must be preserved");
    require(!midi.push(0, noteOn, wjn::common::MidiEvent::maxBytes + 1), "Oversized MIDI must be rejected");

    std::cout << "MIDI/FIFO tests passed\n";
    return 0;
}
