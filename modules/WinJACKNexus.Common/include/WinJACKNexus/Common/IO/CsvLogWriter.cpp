#include "CsvLogWriter.h"

namespace wjn::common
{

CsvLogWriter::CsvLogWriter() : juce::Thread("WinJACKNexus CSV Logger")
{
    startThread(juce::Thread::Priority::background);
}

CsvLogWriter::~CsvLogWriter()
{
    signalThreadShouldExit();
    wakeEvent.signal();
    stopThread(2000);
}

bool CsvLogWriter::enqueue(const juce::File& file, juce::String line)
{
    const juce::ScopedLock lock(queueLock);
    if (pendingWrites.size() >= maximumPendingWrites)
    {
        droppedWrites.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    pendingWrites.push_back({ file, std::move(line) });
    wakeEvent.signal();
    return true;
}

juce::uint64 CsvLogWriter::getDroppedWriteCount() const noexcept { return droppedWrites.load(std::memory_order_relaxed); }
juce::uint64 CsvLogWriter::getFailedWriteCount() const noexcept { return failedWrites.load(std::memory_order_relaxed); }

void CsvLogWriter::run()
{
    while (true)
    {
        PendingWrite pending;
        bool hasPending = false;
        {
            const juce::ScopedLock lock(queueLock);
            if (!pendingWrites.empty())
            {
                pending = std::move(pendingWrites.front());
                pendingWrites.pop_front();
                hasPending = true;
            }
        }
        if (hasPending)
            writePending(pending);
        else if (threadShouldExit())
            break;
        else
            wakeEvent.wait(100);
    }
}

void CsvLogWriter::writePending(const PendingWrite& pending)
{
    if (!pending.file.getParentDirectory().createDirectory())
    {
        failedWrites.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    auto contents = pending.line;
    if (!pending.file.existsAsFile())
        contents = "timestamp,peak_dbfs,rms_dbfs,true_peak_dbtp,momentary_lufs,short_term_lufs,integrated_lufs,lra_lu\r\n" + contents;
    if (!pending.file.appendText(contents))
        failedWrites.fetch_add(1, std::memory_order_relaxed);
}

} // namespace wjn::common
