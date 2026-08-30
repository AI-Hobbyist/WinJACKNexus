#pragma once

#include <WinJACKNexus/Common/Export.hpp>

#include <Windows.h>

namespace WinJACKNexus::Common {

class WINJACK_COMMON_API Thread final {
public:
    using EntryPoint = void (*)(Thread&) noexcept;

    explicit Thread(const wchar_t* name = nullptr);
    ~Thread() noexcept;

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    bool start(EntryPoint entryPoint) noexcept;
    void requestStop() noexcept;
    bool stopRequested() const noexcept;
    bool join() noexcept;
    bool joinable() const noexcept;
    bool setRealtimePriority() noexcept;

    HANDLE nativeHandle() const noexcept;
    DWORD threadId() const noexcept;

private:
    static DWORD WINAPI threadEntry(void* parameter) noexcept;

    struct Impl;
    Impl* impl_;
};

}