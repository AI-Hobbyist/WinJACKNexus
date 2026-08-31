#pragma once

#include <WinJACKNexus/Audio/JackApi.hpp>

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

struct JackMidiEventView final {
    jack_nframes_t time = 0;
    const jack_midi_data_t* data = nullptr;
    std::size_t size = 0;
};

class JackMidiBuffer final {
public:
    explicit JackMidiBuffer(void* buffer = nullptr) noexcept
        : buffer_(buffer)
    {
    }

    void reset(void* buffer) noexcept { buffer_ = buffer; }
    void* nativeBuffer() const noexcept { return buffer_; }
    std::uint32_t eventCount() const noexcept
    {
        return buffer_ != nullptr ? JackApi::instance().midiEventCount(buffer_) : 0;
    }
    bool event(std::uint32_t index, JackMidiEventView& result) const noexcept
    {
        if (buffer_ == nullptr)
            return false;
        jack_midi_event_t eventValue {};
        if (JackApi::instance().midiEventGet(&eventValue, buffer_, index) != 0)
            return false;
        result.time = eventValue.time;
        result.data = eventValue.buffer;
        result.size = eventValue.size;
        return true;
    }
    void clear() noexcept
    {
        if (buffer_ != nullptr)
            JackApi::instance().midiClearBuffer(buffer_);
    }
    std::size_t maxEventSize() const noexcept
    {
        return buffer_ != nullptr ? JackApi::instance().midiMaxEventSize(buffer_) : 0;
    }
    jack_midi_data_t* reserve(jack_nframes_t time, std::size_t size) noexcept
    {
        return buffer_ != nullptr ? JackApi::instance().midiEventReserve(buffer_, time, size) : nullptr;
    }
    bool write(jack_nframes_t time, const jack_midi_data_t* data, std::size_t size) noexcept
    {
        return buffer_ != nullptr && data != nullptr
            && JackApi::instance().midiEventWrite(buffer_, time, data, size) == 0;
    }
    std::uint32_t lostEventCount() const noexcept
    {
        return buffer_ != nullptr ? JackApi::instance().midiLostEventCount(buffer_) : 0;
    }

private:
    void* buffer_ = nullptr;
};

}