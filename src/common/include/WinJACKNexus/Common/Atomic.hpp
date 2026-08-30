#pragma once

#include <atomic>

namespace WinJACKNexus::Common {

template <typename T>
struct alignas(64) PaddedAtomic final {
    PaddedAtomic() noexcept = default;
    explicit PaddedAtomic(T initialValue) noexcept
        : value(initialValue)
    {
    }

    PaddedAtomic(const PaddedAtomic& other) noexcept
        : value(other.load(std::memory_order_relaxed))
    {
    }

    PaddedAtomic& operator=(const PaddedAtomic& other) noexcept
    {
        store(other.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }

    T load(std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        return value.load(order);
    }

    void store(T newValue, std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        value.store(newValue, order);
    }

    T exchange(T newValue, std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        return value.exchange(newValue, order);
    }

    bool compareExchange(T& expected, T desired,
                         std::memory_order success = std::memory_order_seq_cst,
                         std::memory_order failure = std::memory_order_seq_cst) noexcept
    {
        return value.compare_exchange_strong(expected, desired, success, failure);
    }

    std::atomic<T> value {};
};

class AtomicFlag final {
public:
    bool testAndSet(std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        return flag_.test_and_set(order);
    }

    bool test(std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        return flag_.test(order);
    }

    void clear(std::memory_order order = std::memory_order_seq_cst) noexcept
    {
        flag_.clear(order);
    }

private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

}