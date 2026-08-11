#include "MidiEventQueue.h"

#include <algorithm>

namespace wjn::common
{

MidiEvent MidiEvent::make(uint32_t offset, const uint8_t* data, size_t byteCount) noexcept
{
    MidiEvent event;
    event.sampleOffset = offset;
    if (data != nullptr)
    {
        event.size = static_cast<uint8_t>(std::min(byteCount, maxBytes));
        std::copy_n(data, event.size, event.bytes.begin());
    }
    return event;
}

bool MidiEventQueue::push(const MidiEvent& event) noexcept
{
    return event.size <= MidiEvent::maxBytes && queue.push(event);
}

bool MidiEventQueue::push(uint32_t sampleOffset, const uint8_t* data, size_t byteCount) noexcept
{
    return byteCount <= MidiEvent::maxBytes && queue.push(MidiEvent::make(sampleOffset, data, byteCount));
}

bool MidiEventQueue::pop(MidiEvent& event) noexcept
{
    return queue.pop(event);
}

void MidiEventQueue::reset() noexcept
{
    queue.reset();
}

size_t MidiEventQueue::size() const noexcept
{
    return queue.size();
}

} // namespace wjn::common
