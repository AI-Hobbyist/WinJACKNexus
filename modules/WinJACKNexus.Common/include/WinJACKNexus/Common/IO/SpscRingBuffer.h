#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace wjn::common
{

template <typename Value, size_t Capacity>
class SpscRingBuffer final
{
    static_assert(Capacity > 0);
    static_assert(std::is_trivially_copyable_v<Value>);

public:
    bool push(const Value& value) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == readIndex.load(std::memory_order_acquire))
            return false;

        values[write] = value;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Value& value) noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        if (read == writeIndex.load(std::memory_order_acquire))
            return false;

        value = values[read];
        readIndex.store(increment(read), std::memory_order_release);
        return true;
    }

    void reset() noexcept
    {
        readIndex.store(0, std::memory_order_release);
        writeIndex.store(0, std::memory_order_release);
    }

    size_t size() const noexcept
    {
        const auto write = writeIndex.load(std::memory_order_acquire);
        const auto read = readIndex.load(std::memory_order_acquire);
        return write >= read ? write - read : Capacity - read + write;
    }

    static constexpr size_t capacity() noexcept
    {
        return Capacity - 1;
    }

private:
    static constexpr size_t increment(size_t index) noexcept
    {
        return index + 1 == Capacity ? 0 : index + 1;
    }

    std::array<Value, Capacity> values {};
    std::atomic<size_t> readIndex { 0 };
    std::atomic<size_t> writeIndex { 0 };
};

} // namespace wjn::common
