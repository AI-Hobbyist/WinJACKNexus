#pragma once

#include <atomic>
#include <deque>

#include <juce_core/juce_core.h>

namespace wjn::common
{

class CsvLogWriter final : private juce::Thread
{
public:
    CsvLogWriter();
    ~CsvLogWriter() override;
    bool enqueue(const juce::File& file, juce::String line);
    juce::uint64 getDroppedWriteCount() const noexcept;
    juce::uint64 getFailedWriteCount() const noexcept;

private:
    struct PendingWrite { juce::File file; juce::String line; };
    void run() override;
    void writePending(const PendingWrite& pending);
    static constexpr size_t maximumPendingWrites = 1024;
    juce::CriticalSection queueLock;
    std::deque<PendingWrite> pendingWrites;
    juce::WaitableEvent wakeEvent;
    std::atomic<juce::uint64> droppedWrites { 0 };
    std::atomic<juce::uint64> failedWrites { 0 };
};

} // namespace wjn::common
