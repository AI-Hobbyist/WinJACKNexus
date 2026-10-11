// Bounded asynchronous VST block transport. SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace WinJACKNexus::Common::VstBlockIpc
{
constexpr uint32_t version = 1;
constexpr uint32_t magic = 0x574a5642;
constexpr uint32_t maxChannels = 64;
constexpr uint32_t maxEvents = 1024;
constexpr uint32_t maxMidiBytes = 65536;
enum State : LONG
{
    free = 0,
    pending = 1,
    processing = 2,
    complete = 3
};

struct Event
{
    uint32_t port;
    uint32_t frame;
    uint32_t offset;
    uint32_t size;
};
// Fixed-width fields and offsets keep the wire layout identical in x86 and x64.
struct alignas(8) Block
{
    uint32_t magicValue;
    uint32_t protocol;
    uint32_t headerSize;
    uint32_t maxFrames;
    double sampleRate;
    uint32_t inputCount;
    uint32_t outputCount;
    uint32_t midiInputCount;
    volatile LONG state;
    uint32_t frames;
    uint32_t generation;
    uint64_t samplePosition;
    uint64_t sequence;
    uint32_t eventCount;
    uint32_t byteCount;
    Event events[maxEvents];
    uint8_t bytes[maxMidiBytes];
};
static_assert(sizeof(Event) == 16);
static_assert(offsetof(Block, events) == 72);

inline uint64_t mappingSize(uint32_t frames)
{
    return sizeof(Block) + uint64_t(maxChannels) * 2 * frames * sizeof(float);
}

class Mapping final
{
  public:
    ~Mapping()
    {
        close();
    }
    Mapping() = default;
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;

    bool create(const std::wstring &name, uint32_t frames, double rate)
    {
        close();
        if (!frames || frames > 65536)
            return false;
        const auto size = mappingSize(frames);
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      DWORD(size >> 32), DWORD(size), name.c_str());
        if (!mapping_ || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            close();
            return false;
        }
        block_ = static_cast<Block *>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size));
        event_ = CreateEventW(nullptr, FALSE, FALSE, (name + L".request").c_str());
        if (!block_ || !event_)
        {
            close();
            return false;
        }
        block_->magicValue = magic;
        block_->protocol = version;
        block_->headerSize = sizeof(Block);
        block_->maxFrames = frames;
        block_->sampleRate = rate;
        return true;
    }

    bool open(const std::wstring &name)
    {
        close();
        mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!mapping_)
            return false;
        block_ = static_cast<Block *>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
        event_ = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (name + L".request").c_str());
        if (!block_ || !event_ || block_->magicValue != magic || block_->protocol != version ||
            block_->headerSize != sizeof(Block) || !block_->maxFrames || block_->maxFrames > 65536)
        {
            close();
            return false;
        }
        return true;
    }

    void close() noexcept
    {
        if (block_)
            UnmapViewOfFile(block_);
        if (mapping_)
            CloseHandle(mapping_);
        if (event_)
            CloseHandle(event_);
        block_ = nullptr;
        mapping_ = nullptr;
        event_ = nullptr;
    }
    Block *block() const noexcept
    {
        return block_;
    }
    HANDLE event() const noexcept
    {
        return event_;
    }
    float *audio(bool output, uint32_t channel) const noexcept
    {
        auto *samples =
            reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(block_) + sizeof(Block));
        return samples + (channel + (output ? maxChannels : 0)) * block_->maxFrames;
    }

  private:
    HANDLE mapping_{};
    HANDLE event_{};
    Block *block_{};
};
}
