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
#include <WinJACKNexus/Common/Atomic.hpp>
#include <WinJACKNexus/Common/AudioRingBuffer.hpp>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>
#include <WinJACKNexus/Common/Logger.hpp>
#include <WinJACKNexus/Common/Metadata.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>
#include <WinJACKNexus/Common/RealtimeObject.hpp>
#include <WinJACKNexus/Common/ScopedGuard.hpp>
#include <WinJACKNexus/Common/Thread.hpp>
#include <WinJACKNexus/Common/Timer.hpp>
#include <WinJACKNexus/Common/Version.hpp>
#include <WinJACKNexus/Common/Window.hpp>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAILED: %s\n", message);
    return condition;
}

bool testQueueBasics()
{
    WinJACKNexus::Common::LockFreeSpscQueue<int, 3> queue;
    int value = 0;
    bool passed = true;
    passed &= check(queue.empty(), "SPSC queue starts empty");
    passed &= check(queue.try_push(1), "SPSC queue accepts first value");
    passed &= check(queue.try_push(2), "SPSC queue accepts second value");
    passed &= check(queue.try_push(3), "SPSC queue accepts capacity value");
    passed &= check(!queue.try_push(4), "SPSC queue rejects values while full");
    passed &= check(queue.size() == 3 && queue.full(), "SPSC queue reports full size");
    passed &= check(queue.try_pop(value) && value == 1, "SPSC queue preserves first value");
    passed &= check(queue.try_pop(value) && value == 2, "SPSC queue preserves second value");
    passed &= check(queue.try_pop(value) && value == 3, "SPSC queue preserves third value");
    passed &= check(!queue.try_pop(value) && queue.empty(), "SPSC queue reports empty after draining");
    return passed;
}

bool testQueueStress()
{
    constexpr std::uint64_t itemCount = 10'000'000;
    WinJACKNexus::Common::LockFreeSpscQueue<std::uint64_t, 1024> queue;
    std::atomic<bool> producerFinished { false };
    std::atomic<bool> failed { false };

    std::thread producer([&] {
        for (std::uint64_t value = 0; value < itemCount; ++value) {
            while (!queue.try_push(value))
                std::this_thread::yield();
        }
        producerFinished.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t expectedValue = 0;
        std::uint64_t value = 0;
        while (!producerFinished.load(std::memory_order_acquire) || !queue.empty()) {
            if (!queue.try_pop(value)) {
                std::this_thread::yield();
                continue;
            }
            if (value != expectedValue)
                failed.store(true, std::memory_order_relaxed);
            ++expectedValue;
        }
        while (queue.try_pop(value)) {
            if (value != expectedValue)
                failed.store(true, std::memory_order_relaxed);
            ++expectedValue;
        }
        if (expectedValue != itemCount)
            failed.store(true, std::memory_order_relaxed);
    });

    producer.join();
    consumer.join();
    return check(!failed.load(std::memory_order_acquire), "SPSC queue passes 10M ordered transfers");
}

bool testAudioRingBuffer()
{
    WinJACKNexus::Common::AudioRingBuffer<float> ring(5);
    const std::array<float, 4> firstInput { 1.0F, 2.0F, 3.0F, 4.0F };
    const std::array<float, 5> secondInput { 5.0F, 6.0F, 7.0F, 8.0F, 9.0F };
    std::array<float, 5> output {};
    bool passed = true;

    passed &= check(ring.write(firstInput.data(), firstInput.size()) == 4, "ring buffer writes initial block");
    passed &= check(ring.read(output.data(), 2) == 2, "ring buffer reads partial block");
    passed &= check(output[0] == 1.0F && output[1] == 2.0F, "ring buffer reads initial values");
    passed &= check(ring.write(secondInput.data(), secondInput.size()) == 3, "ring buffer limits writes to free space");
    passed &= check(ring.available() == 5 && ring.freeSpace() == 0, "ring buffer reports wrapped full state");
    passed &= check(ring.read(output.data(), output.size()) == 5, "ring buffer reads wrapped block");
    passed &= check(output == std::array<float, 5> { 3.0F, 4.0F, 5.0F, 6.0F, 7.0F }, "ring buffer preserves wrapped order");
    passed &= check(ring.available() == 0, "ring buffer is empty after reading");
    ring.clear();
    return passed;
}

struct Snapshot final {
    std::uint64_t sequence {};
    std::uint64_t checksum {};
};

bool testRealtimeObject()
{
    constexpr std::uint64_t itemCount = 1'000'000;
    WinJACKNexus::Common::RealtimeObject<Snapshot> object(Snapshot { 0, 7 });
    std::atomic<bool> producerFinished { false };
    std::atomic<bool> failed { false };

    std::thread producer([&] {
        for (std::uint64_t sequence = 1; sequence <= itemCount; ++sequence)
            object.set(Snapshot { sequence, sequence * 3 + 7 });
        producerFinished.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t lastSequence = 0;
        for (;;) {
            const auto& snapshot = object.getRead();
            if (snapshot.sequence < lastSequence || snapshot.checksum != snapshot.sequence * 3 + 7)
                failed.store(true, std::memory_order_relaxed);
            lastSequence = std::max(lastSequence, snapshot.sequence);
            if (producerFinished.load(std::memory_order_acquire) && lastSequence == itemCount)
                break;
            std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();
    const auto& finalSnapshot = object.getRead();
    return check(!failed.load(std::memory_order_acquire)
                     && finalSnapshot.sequence == itemCount,
                 "RealtimeObject publishes consistent concurrent snapshots");
}

std::atomic<std::size_t> loggedRecordCount { 0 };

void countLogRecord(const WinJACKNexus::Common::LogRecord&, void*) noexcept
{
    loggedRecordCount.fetch_add(1, std::memory_order_relaxed);
}

bool testLogger()
{
    constexpr std::size_t producerCount = 4;
    constexpr std::size_t recordsPerProducer = 10'000;
    constexpr std::size_t expectedRecordCount = producerCount * recordsPerProducer;
    auto& logger = WinJACKNexus::Common::Logger::instance();
    logger.setFileLoggingEnabled(false);
    logger.setSink(countLogRecord, nullptr);
    logger.drain();
    loggedRecordCount.store(0, std::memory_order_relaxed);
    const auto droppedBefore = logger.droppedCount();
    std::atomic<bool> producersFinished { false };
    std::array<std::thread, producerCount> producers;

    for (std::size_t producerIndex = 0; producerIndex < producerCount; ++producerIndex) {
        producers[producerIndex] = std::thread([producerIndex, &logger] {
            for (std::size_t recordIndex = 0; recordIndex < recordsPerProducer; ++recordIndex)
                logger.log(WinJACKNexus::Common::LogLevel::info,
                           "producer=%zu record=%zu",
                           producerIndex,
                           recordIndex);
        });
    }

    std::thread consumer([&] {
        while (!producersFinished.load(std::memory_order_acquire) || logger.pendingCount() != 0) {
            logger.drain(256);
            std::this_thread::yield();
        }
    });

    for (auto& producer : producers)
        producer.join();
    producersFinished.store(true, std::memory_order_release);
    consumer.join();
    logger.drain();

    const auto dropped = logger.droppedCount() - droppedBefore;
    const auto logged = loggedRecordCount.load(std::memory_order_acquire);
    logger.setSink(nullptr, nullptr);
    return check(logged + dropped == expectedRecordCount, "Logger accounts for all concurrent records");
}

bool testUtilities()
{
    using namespace WinJACKNexus::Common;
    bool passed = true;

    PaddedAtomic<int> atomicValue(1);
    int expectedValue = 1;
    passed &= check(atomicValue.compareExchange(expectedValue, 2), "PaddedAtomic compare-exchange succeeds");
    passed &= check(atomicValue.load() == 2, "PaddedAtomic stores exchanged value");
    AtomicFlag atomicFlag;
    passed &= check(!atomicFlag.test(), "AtomicFlag starts clear");
    passed &= check(!atomicFlag.testAndSet() && atomicFlag.test(), "AtomicFlag sets atomically");
    atomicFlag.clear();
    passed &= check(!atomicFlag.test(), "AtomicFlag clears atomically");

    bool guardCalled = false;
    {
        auto guard = makeScopeExit([&] { guardCalled = true; });
    }
    passed &= check(guardCalled, "ScopeExit invokes its callback");

    const auto version = VersionInfo(1, 2, 3, "abc123");
    const auto parsedVersion = VersionInfo::fromString(version.toString());
    passed &= check(parsedVersion.has_value() && *parsedVersion == version, "VersionInfo round-trips");
    passed &= check(!VersionInfo::fromString("1.2").has_value(), "VersionInfo rejects incomplete values");
    passed &= check(!VersionInfo::fromString("1.2.3.4").has_value(), "VersionInfo rejects extra components");

    ModuleMetadata metadata;
    metadata.name = "module\"name";
    metadata.category = "audio";
    metadata.version = "1.0.0";
    metadata.author = "WinJACKNexus";
    metadata.description = "description\nline";
    metadata.dependencies = { "common", "audio" };
    const auto parsedMetadata = ModuleMetadata::fromJson(metadata.toJson());
    passed &= check(parsedMetadata.has_value()
                        && parsedMetadata->name == metadata.name
                        && parsedMetadata->description == metadata.description
                        && parsedMetadata->dependencies == metadata.dependencies,
                    "ModuleMetadata round-trips JSON");
    passed &= check(!ModuleMetadata::fromJson("{} trailing").has_value(), "ModuleMetadata rejects trailing JSON");

    const auto start = CpuClock::now();
    Sleep(1);
    const auto end = CpuClock::now();
    passed &= check(CpuClock::frequency() > 0.0 && CpuClock::secondsBetween(start, end) > 0.0,
                    "CpuClock reports high-resolution elapsed time");
    passed &= check(CpuClock::cpuLoad(start, end, 48, 48000.0) >= 0.0, "CpuClock computes CPU load");

    passed &= check(!PathUtils::executablePath().empty() && !PathUtils::moduleDirectory().empty(),
                    "PathUtils locates the test executable");
    passed &= check(!PathUtils::logDirectory().empty(), "PathUtils provides the log directory");
    passed &= check(!win32::Window().isValid() && win32::Window().title().empty(),
                    "win32 Window safely handles a null handle");
    return passed;
}

std::atomic<std::uint32_t> threadIterations { 0 };

void threadEntry(WinJACKNexus::Common::Thread& thread) noexcept
{
    while (!thread.stopRequested()) {
        threadIterations.fetch_add(1, std::memory_order_relaxed);
        Sleep(0);
    }
}

bool testThread()
{
    using WinJACKNexus::Common::Thread;
    threadIterations.store(0, std::memory_order_relaxed);
    Thread thread(L"WJN Common Test");
    bool passed = check(thread.start(threadEntry), "Thread starts an entry point")
        && check(thread.joinable(), "Thread reports joinable after start");
    for (int attempt = 0; attempt < 100 && threadIterations.load(std::memory_order_acquire) == 0; ++attempt)
        Sleep(1);
    thread.requestStop();
    passed &= check(thread.join(), "Thread joins after stop request");
    passed &= check(!thread.joinable() && threadIterations.load(std::memory_order_acquire) > 0,
                    "Thread stops and executes its entry point");
    return passed;
}

}

int main()
{
    bool passed = true;
    passed &= testQueueBasics();
    passed &= testQueueStress();
    passed &= testAudioRingBuffer();
    passed &= testRealtimeObject();
    passed &= testLogger();
    passed &= testUtilities();
    passed &= testThread();
    if (!passed)
        return 1;
    std::puts("Common tests passed");
    return 0;
}