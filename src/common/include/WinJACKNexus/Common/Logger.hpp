#pragma once

#include <WinJACKNexus/Common/Export.hpp>
#include <WinJACKNexus/Common/Timer.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace WinJACKNexus::Common {

enum class LogLevel : std::uint8_t {
    trace,
    info,
    warning,
    error
};

struct LogRecord final {
    Timestamp timestamp {};
    LogLevel level { LogLevel::info };
    std::uint32_t threadId {};
    char message[256] {};
};

using LogSink = void (*)(const LogRecord& record, void* userData) noexcept;

class WINJACK_COMMON_API Logger final {
public:
    static Logger& instance() noexcept;

    void log(LogLevel level, const char* format, ...) noexcept;
    std::size_t drain(std::size_t maxRecords = std::numeric_limits<std::size_t>::max()) noexcept;
    void flush() noexcept;

    void setSink(LogSink sink, void* userData) noexcept;
    void setFileLoggingEnabled(bool enabled) noexcept;
    std::size_t pendingCount() const noexcept;
    std::size_t droppedCount() const noexcept;

private:
    Logger();
    ~Logger() noexcept;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct Impl;
    Impl* impl_;
};

}

#define WJN_LOG_TRACE(...) ::WinJACKNexus::Common::Logger::instance().log(::WinJACKNexus::Common::LogLevel::trace, __VA_ARGS__)
#define WJN_LOG_INFO(...) ::WinJACKNexus::Common::Logger::instance().log(::WinJACKNexus::Common::LogLevel::info, __VA_ARGS__)
#define WJN_LOG_WARN(...) ::WinJACKNexus::Common::Logger::instance().log(::WinJACKNexus::Common::LogLevel::warning, __VA_ARGS__)
#define WJN_LOG_ERROR(...) ::WinJACKNexus::Common::Logger::instance().log(::WinJACKNexus::Common::LogLevel::error, __VA_ARGS__)