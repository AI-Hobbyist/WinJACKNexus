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
#include <WinJACKNexus/Common/Logger.hpp>

#include <WinJACKNexus/Common/AudioRingBuffer.hpp>
#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>
#include <WinJACKNexus/Common/PathUtils.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace WinJACKNexus::Common
{

namespace
{

constexpr std::size_t loggerCapacity = 4096;

const char *levelName(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::trace:
        return "TRACE";
    case LogLevel::info:
        return "INFO";
    case LogLevel::warning:
        return "WARN";
    case LogLevel::error:
        return "ERROR";
    }
    return "UNKNOWN";
}

class MpscLogQueue final
{
    struct Slot final
    {
        std::atomic<std::size_t> sequence{0};
        LogRecord record{};
    };

  public:
    MpscLogQueue() noexcept
    {
        for (std::size_t index = 0; index < loggerCapacity; ++index)
            slots_[index].sequence.store(index, std::memory_order_relaxed);
    }

    bool tryPush(const LogRecord &record) noexcept
    {
        std::size_t position = enqueuePosition_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto &slot = slots_[position % loggerCapacity];
            const auto sequence = slot.sequence.load(std::memory_order_acquire);
            const auto difference =
                static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(position);
            if (difference == 0)
            {
                if (enqueuePosition_.compare_exchange_weak(position, position + 1,
                                                           std::memory_order_relaxed,
                                                           std::memory_order_relaxed))
                {
                    slot.record = record;
                    slot.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false;
            }
            else
            {
                position = enqueuePosition_.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryPop(LogRecord &record) noexcept
    {
        auto position = dequeuePosition_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto &slot = slots_[position % loggerCapacity];
            const auto sequence = slot.sequence.load(std::memory_order_acquire);
            const auto difference =
                static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(position + 1);
            if (difference == 0)
            {
                if (dequeuePosition_.compare_exchange_weak(position, position + 1,
                                                           std::memory_order_relaxed,
                                                           std::memory_order_relaxed))
                {
                    record = slot.record;
                    slot.sequence.store(position + loggerCapacity, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false;
            }
            else
            {
                position = dequeuePosition_.load(std::memory_order_relaxed);
            }
        }
    }

    std::size_t size() const noexcept
    {
        const auto enqueued = enqueuePosition_.load(std::memory_order_acquire);
        const auto dequeued = dequeuePosition_.load(std::memory_order_acquire);
        return std::min(enqueued - dequeued, loggerCapacity);
    }

  private:
    std::array<Slot, loggerCapacity> slots_{};
    alignas(64) std::atomic<std::size_t> enqueuePosition_{0};
    alignas(64) std::atomic<std::size_t> dequeuePosition_{0};
};

}

struct Logger::Impl final
{
    MpscLogQueue queue;
    std::atomic<LogSink> sink{nullptr};
    std::atomic<void *> sinkUserData{nullptr};
    std::atomic<bool> fileLoggingEnabled{true};
    std::atomic<bool> detailedLoggingEnabled{true};
    std::atomic<std::size_t> dropped{0};
    std::mutex fileMutex;
    std::ofstream file;
    std::filesystem::path filePath;
};

Logger &Logger::instance() noexcept
{
    static Logger logger;
    return logger;
}

Logger::Logger() : impl_(new Impl{})
{
    try
    {
        SYSTEMTIME now;
        GetLocalTime(&now);
        wchar_t name[96]{};
        std::swprintf(name, std::size(name), L"WinJACKNexus-%04u%02u%02u-%02u%02u%02u-%03u-%lu.log",
                      static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
                      static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
                      static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
                      static_cast<unsigned>(now.wMilliseconds), GetCurrentProcessId());
        impl_->filePath = PathUtils::logDirectory() / name;
        impl_->file.open(impl_->filePath, std::ios::out | std::ios::trunc);
        if (impl_->file.is_open())
        {
            impl_->file << "[" << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' '
                        << now.wHour << ':' << now.wMinute << ':' << now.wSecond
                        << "] [INFO] Process entry pid=" << GetCurrentProcessId() << '\n';
            impl_->file.flush();
        }
    }
    catch (...)
    {
    }
}

Logger::~Logger() noexcept
{
    flush();
    delete impl_;
}

void Logger::log(LogLevel level, const char *format, ...) noexcept
{
    if (level == LogLevel::trace && !impl_->detailedLoggingEnabled.load(std::memory_order_acquire))
        return;
    LogRecord record;
    record.timestamp = CpuClock::now();
    record.level = level;
    record.threadId = GetCurrentThreadId();

    va_list arguments;
    va_start(arguments, format);
    if (format != nullptr)
        vsnprintf_s(record.message, sizeof(record.message), _TRUNCATE, format, arguments);
    va_end(arguments);

    if (!impl_->queue.tryPush(record))
        impl_->dropped.fetch_add(1, std::memory_order_relaxed);
}

std::size_t Logger::drain(std::size_t maxRecords) noexcept
{
    std::size_t count = 0;
    LogRecord record;
    while (count < maxRecords && impl_->queue.tryPop(record))
    {
        if (const auto sink = impl_->sink.load(std::memory_order_acquire); sink != nullptr)
            sink(record, impl_->sinkUserData.load(std::memory_order_acquire));

        if (impl_->fileLoggingEnabled.load(std::memory_order_acquire))
        {
            try
            {
                std::lock_guard lock(impl_->fileMutex);
                if (!impl_->fileLoggingEnabled.load(std::memory_order_acquire))
                {
                    ++count;
                    continue;
                }
                if (!impl_->file.is_open() && !impl_->filePath.empty())
                    impl_->file.open(impl_->filePath, std::ios::app);
                if (impl_->file.is_open())
                {
                    SYSTEMTIME now;
                    GetLocalTime(&now);
                    char timestamp[32];
                    std::snprintf(
                        timestamp, sizeof(timestamp), "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                        static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
                        static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
                        static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
                        static_cast<unsigned>(now.wMilliseconds));
                    impl_->file << '[' << timestamp << "] [" << levelName(record.level)
                                << "] [thread " << record.threadId << "] " << record.message
                                << '\n';
                }
            }
            catch (...)
            {
            }
        }
        ++count;
    }
    return count;
}

void Logger::flush() noexcept
{
    drain();
    std::lock_guard lock(impl_->fileMutex);
    if (impl_->file.is_open())
        impl_->file.flush();
}

void Logger::setSink(LogSink sink, void *userData) noexcept
{
    impl_->sinkUserData.store(userData, std::memory_order_release);
    impl_->sink.store(sink, std::memory_order_release);
}

void Logger::setFileLoggingEnabled(bool enabled) noexcept
{
    impl_->fileLoggingEnabled.store(enabled, std::memory_order_release);
    if (!enabled)
    {
        std::lock_guard lock(impl_->fileMutex);
        if (impl_->file.is_open())
            impl_->file.close();
    }
}

void Logger::setDetailedLoggingEnabled(bool enabled) noexcept
{
    impl_->detailedLoggingEnabled.store(enabled, std::memory_order_release);
}

std::size_t Logger::pendingCount() const noexcept
{
    return impl_->queue.size();
}

std::size_t Logger::droppedCount() const noexcept
{
    return impl_->dropped.load(std::memory_order_acquire);
}

}
