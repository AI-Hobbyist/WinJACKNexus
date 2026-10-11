// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <WinJACKNexus/Common/MidiToolsCore.hpp>
#include <initializer_list>
namespace WinJACKNexus::Common::MidiTools
{
// Reader/writer are borrowed cycle buffers; callbacks never allocate or lock.
class SplitProcessor final
{
    uint16_t pending_{};
    uint64_t rejectedEvents_{};

  public:
    // Owned by the process thread; reset preserves lifetime diagnostics.
    uint64_t rejectedEvents() const noexcept
    {
        return rejectedEvents_;
    }
    void reset() noexcept
    {
        pending_ = 0;
    }
    void requestRelease() noexcept
    {
        pending_ = 0xffff;
    }
    template <class Reader, class Writer>
    void process(uint32_t frames, unsigned count, Reader read, Writer write) noexcept
    {
        if (!frames || frames > maxFrames)
        {
            rejectedEvents_ += count;
            pending_ = 0xffff;
            return;
        }
        const uint16_t retry = pending_;
        pending_ = 0;
        for (unsigned c = 0; c < 16; ++c)
        {
            if (retry & (1u << c))
            {
                for (uint8_t cc : {uint8_t(64), uint8_t(123), uint8_t(120)})
                {
                    const uint8_t b[]{uint8_t(0xb0 + c), cc, 0};
                    if (!write(c, EventView{0, b}))
                    {
                        pending_ |= uint16_t(1u << c);
                    }
                }
            }
        }
        const unsigned limit = count < maxEvents ? count : maxEvents;
        if (count > maxEvents)
        {
            rejectedEvents_ += count - maxEvents;
            pending_ = 0xffff;
        }
        for (unsigned i = 0; i < limit; ++i)
        {
            const auto event = read(i);
            if (!validEvent(event, frames))
            {
                ++rejectedEvents_;
                continue;
            }
            const auto targets = splitTargets(event.bytes);
            for (unsigned c = 0; c < 16; ++c)
            {
                if (targets & (1u << c))
                {
                    // Avoid restarting notes before a failed safety release is retried.
                    if ((pending_ & (1u << c)) && !releaseMessage(event.bytes))
                    {
                        continue;
                    }
                    if (!write(c, event))
                    {
                        pending_ |= uint16_t(1u << c);
                    }
                }
            }
        }
    }
};
} // namespace WinJACKNexus::Common::MidiTools
