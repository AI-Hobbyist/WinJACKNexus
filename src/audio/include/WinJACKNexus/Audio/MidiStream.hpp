#pragma once

#include <WinJACKNexus/Audio/Export.hpp>
#include <WinJACKNexus/Audio/MidiDevice.hpp>
#include <WinJACKNexus/Audio/MidiEvent.hpp>

#include <cstddef>
#include <cstdint>

namespace WinJACKNexus::Audio {

struct MidiStreamOptions final {
    std::uint32_t deviceId = 0;
    std::uint32_t longMessageBuffers = 4;
};

class WINJACK_AUDIO_API MidiStream final {
public:
    struct Impl;

    MidiStream() noexcept;
    ~MidiStream() noexcept;

    MidiStream(const MidiStream&) = delete;
    MidiStream& operator=(const MidiStream&) = delete;
    MidiStream(MidiStream&& other) noexcept;
    MidiStream& operator=(MidiStream&& other) noexcept;

    bool open(MidiDirection direction, const MidiStreamOptions& options = {}) noexcept;
    bool start() noexcept;
    bool stop() noexcept;
    void close() noexcept;

    bool sendShortMessage(std::uint32_t message) noexcept;
    bool popEvent(MidiEvent& event) noexcept;
    std::size_t pendingEvents() const noexcept;

    bool isOpen() const noexcept;
    bool isRunning() const noexcept;
    MidiDirection direction() const noexcept;
    std::uint32_t lastError() const noexcept;

private:
    Impl* impl_ = nullptr;
};

}