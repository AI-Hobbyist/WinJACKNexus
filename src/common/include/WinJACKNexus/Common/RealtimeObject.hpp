#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace WinJACKNexus::Common {

template <typename T>
class RealtimeObject final {
public:
    RealtimeObject() requires std::is_default_constructible_v<T>
        : RealtimeObject(T {})
    {
    }

    explicit RealtimeObject(const T& initial)
        : slots_ { initial, initial, initial }
    {
    }

    RealtimeObject(const RealtimeObject&) = delete;
    RealtimeObject& operator=(const RealtimeObject&) = delete;

    const T& getRead() noexcept
    {
        const auto observedState = pendingState_.load(std::memory_order_acquire);
        const auto observedGeneration = stateGeneration(observedState);
        if (observedGeneration != consumedGeneration_) {
            const auto previousReadIndex = readIndex_;
            const auto releasedState = encodeState(observedGeneration, previousReadIndex);
            const auto publishedState = pendingState_.exchange(releasedState, std::memory_order_acq_rel);
            readIndex_ = stateIndex(publishedState);
            consumedGeneration_ = stateGeneration(publishedState);

            auto expectedState = releasedState;
            pendingState_.compare_exchange_strong(
                expectedState,
                encodeState(consumedGeneration_, previousReadIndex),
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }
        return slots_[readIndex_];
    }

    T& getWrite() noexcept
    {
        return slots_[writeIndex_];
    }

    void set(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        slots_[writeIndex_] = value;
        publish();
    }

    void set(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        slots_[writeIndex_] = std::move(value);
        publish();
    }

    template <typename Mutator>
    void mutate(Mutator&& mutator) noexcept(noexcept(std::forward<Mutator>(mutator)(getWrite())))
    {
        std::forward<Mutator>(mutator)(getWrite());
        publish();
    }

    void publish() noexcept
    {
        const auto previousPendingState = pendingState_.exchange(
            encodeState(++publishGeneration_, writeIndex_), std::memory_order_acq_rel);
        writeIndex_ = stateIndex(previousPendingState);
    }

private:
    static constexpr std::uint64_t pendingIndexMask = 0x3;

    static constexpr std::uint64_t encodeState(std::uint64_t generation, std::int32_t index) noexcept
    {
        return (generation << 2) | static_cast<std::uint64_t>(index);
    }

    static constexpr std::uint64_t stateGeneration(std::uint64_t state) noexcept
    {
        return state >> 2;
    }

    static constexpr std::int32_t stateIndex(std::uint64_t state) noexcept
    {
        return static_cast<std::int32_t>(state & pendingIndexMask);
    }

    std::array<T, 3> slots_;
    std::int32_t readIndex_ { 0 };
    std::int32_t writeIndex_ { 1 };
    std::atomic<std::uint64_t> pendingState_ { encodeState(0, 2) };
    std::uint64_t publishGeneration_ { 0 };
    std::uint64_t consumedGeneration_ { 0 };
};

}