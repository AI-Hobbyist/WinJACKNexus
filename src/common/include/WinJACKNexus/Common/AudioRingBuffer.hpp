/*
 * This file is part of WinJACKNexus.
 *
 * WinJACKNexus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinJACKNexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace WinJACKNexus::Common {

template <typename SampleType>
class AudioRingBuffer final {
    static_assert(std::is_trivially_copyable_v<SampleType>);

public:
    explicit AudioRingBuffer(std::size_t capacity)
        : storage_(capacity == 0 ? nullptr : std::make_unique<SampleType[]>(capacity))
        , capacity_(capacity)
    {
        if (capacity == 0)
            throw std::invalid_argument("AudioRingBuffer capacity must be greater than zero");
    }

    ~AudioRingBuffer() = default;
    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;
    AudioRingBuffer(AudioRingBuffer&&) = delete;
    AudioRingBuffer& operator=(AudioRingBuffer&&) = delete;

    std::size_t write(const SampleType* input, std::size_t count) noexcept
    {
        if (input == nullptr || count == 0)
            return 0;

        const auto writeIndex = writeIndex_.load(std::memory_order_relaxed);
        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        const auto used = writeIndex - readIndex;
        const auto freeSpace = used >= capacity_ ? 0 : capacity_ - used;
        const auto countToWrite = std::min(count, freeSpace);
        copyIn(input, writeIndex, countToWrite);
        writeIndex_.store(writeIndex + countToWrite, std::memory_order_release);
        return countToWrite;
    }

    std::size_t read(SampleType* output, std::size_t count) noexcept
    {
        if (output == nullptr || count == 0)
            return 0;

        const auto readIndex = readIndex_.load(std::memory_order_relaxed);
        const auto writeIndex = writeIndex_.load(std::memory_order_acquire);
        const auto used = writeIndex - readIndex;
        const auto countToRead = std::min(count, std::min(used, capacity_));
        copyOut(output, readIndex, countToRead);
        readIndex_.store(readIndex + countToRead, std::memory_order_release);
        return countToRead;
    }

    std::size_t available() const noexcept
    {
        const auto writeIndex = writeIndex_.load(std::memory_order_acquire);
        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        const auto used = writeIndex - readIndex;
        return std::min(used, capacity_);
    }

    std::size_t freeSpace() const noexcept
    {
        return capacity_ - available();
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    void clear() noexcept
    {
        readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    void copyIn(const SampleType* input, std::size_t writeIndex, std::size_t count) noexcept
    {
        const auto offset = writeIndex % capacity_;
        const auto firstCount = std::min(count, capacity_ - offset);
        std::memcpy(storage_.get() + offset, input, firstCount * sizeof(SampleType));
        if (firstCount < count)
            std::memcpy(storage_.get(), input + firstCount, (count - firstCount) * sizeof(SampleType));
    }

    void copyOut(SampleType* output, std::size_t readIndex, std::size_t count) noexcept
    {
        const auto offset = readIndex % capacity_;
        const auto firstCount = std::min(count, capacity_ - offset);
        std::memcpy(output, storage_.get() + offset, firstCount * sizeof(SampleType));
        if (firstCount < count)
            std::memcpy(output + firstCount, storage_.get(), (count - firstCount) * sizeof(SampleType));
    }

    std::unique_ptr<SampleType[]> storage_;
    const std::size_t capacity_;
    alignas(64) std::atomic<std::size_t> writeIndex_ { 0 };
    alignas(64) std::atomic<std::size_t> readIndex_ { 0 };
};

}