#pragma once

#include <WinJACKNexus/Audio/Export.hpp>
#include <WinJACKNexus/Audio/JackApi.hpp>

#include <cstddef>

namespace WinJACKNexus::Audio {

struct JackRingBufferSegment final {
    char* data = nullptr;
    std::size_t size = 0;
};

class WINJACK_AUDIO_API JackRingBuffer final {
public:
    explicit JackRingBuffer(std::size_t size) noexcept;
    ~JackRingBuffer() noexcept;

    JackRingBuffer(const JackRingBuffer&) = delete;
    JackRingBuffer& operator=(const JackRingBuffer&) = delete;
    JackRingBuffer(JackRingBuffer&& other) noexcept;
    JackRingBuffer& operator=(JackRingBuffer&& other) noexcept;

    bool isValid() const noexcept;
    std::size_t read(char* destination, std::size_t count) noexcept;
    std::size_t peek(char* destination, std::size_t count) noexcept;
    std::size_t write(const char* source, std::size_t count) noexcept;
    bool getReadSegments(JackRingBufferSegment segments[2]) const noexcept;
    bool getWriteSegments(JackRingBufferSegment segments[2]) const noexcept;
    void advanceRead(std::size_t count) noexcept;
    void advanceWrite(std::size_t count) noexcept;
    std::size_t readableBytes() const noexcept;
    std::size_t writableBytes() const noexcept;
    bool lockMemory() noexcept;
    void reset() noexcept;
    jack_ringbuffer_t* nativeHandle() const noexcept;

private:
    void release() noexcept;

    jack_ringbuffer_t* ringbuffer_ = nullptr;
};

}