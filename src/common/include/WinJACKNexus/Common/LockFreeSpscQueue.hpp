#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace WinJACKNexus::Common {

template <typename T, std::size_t Capacity>
class LockFreeSpscQueue final {
    static_assert(Capacity > 0);

public:
    LockFreeSpscQueue() noexcept = default;
    LockFreeSpscQueue(const LockFreeSpscQueue&) = delete;
    LockFreeSpscQueue& operator=(const LockFreeSpscQueue&) = delete;

    bool try_push(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        const auto writeIndex = writeIndex_.load(std::memory_order_relaxed);
        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        if (writeIndex - readIndex >= Capacity)
            return false;

        storage_[writeIndex % Capacity] = value;
        writeIndex_.store(writeIndex + 1, std::memory_order_release);
        return true;
    }

    bool try_push(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        const auto writeIndex = writeIndex_.load(std::memory_order_relaxed);
        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        if (writeIndex - readIndex >= Capacity)
            return false;

        storage_[writeIndex % Capacity] = std::move(value);
        writeIndex_.store(writeIndex + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        const auto readIndex = readIndex_.load(std::memory_order_relaxed);
        const auto writeIndex = writeIndex_.load(std::memory_order_acquire);
        if (readIndex == writeIndex)
            return false;

        value = std::move(storage_[readIndex % Capacity]);
        readIndex_.store(readIndex + 1, std::memory_order_release);
        return true;
    }

    void clear() noexcept
    {
        readIndex_.store(writeIndex_.load(std::memory_order_acquire), std::memory_order_release);
    }

    std::size_t size() const noexcept
    {
        const auto writeIndex = writeIndex_.load(std::memory_order_acquire);
        const auto readIndex = readIndex_.load(std::memory_order_acquire);
        if (writeIndex < readIndex)
            return 0;

        const auto count = writeIndex - readIndex;
        return count < Capacity ? count : Capacity;
    }

    constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

    bool empty() const noexcept
    {
        return size() == 0;
    }

    bool full() const noexcept
    {
        return size() == Capacity;
    }

private:
    std::array<T, Capacity> storage_ {};
    alignas(64) std::atomic<std::size_t> writeIndex_ { 0 };
    alignas(64) std::atomic<std::size_t> readIndex_ { 0 };
};

}