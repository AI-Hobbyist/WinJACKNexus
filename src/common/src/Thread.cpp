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
#include <WinJACKNexus/Common/Thread.hpp>

#include <avrt.h>

#include <memory>

namespace WinJACKNexus::Common {

struct Thread::Impl final {
    Thread* owner {};
    HANDLE handle {};
    DWORD id {};
    HANDLE stopEvent {};
    EntryPoint entryPoint {};
    HANDLE avrtHandle {};
    wchar_t name[64] {};
};

DWORD WINAPI Thread::threadEntry(void* parameter) noexcept
{
    auto& impl = *static_cast<Thread::Impl*>(parameter);
    if (impl.name[0] != L'\0')
        SetThreadDescription(GetCurrentThread(), impl.name);
    impl.entryPoint(*impl.owner);
    if (impl.avrtHandle != nullptr) {
        AvRevertMmThreadCharacteristics(impl.avrtHandle);
        impl.avrtHandle = nullptr;
    }
    return 0;
}

Thread::Thread(const wchar_t* name)
    : impl_(new Impl { this })
{
    impl_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (name != nullptr)
        wcsncpy_s(impl_->name, name, _TRUNCATE);
}

Thread::~Thread() noexcept
{
    requestStop();
    join();
    if (impl_->stopEvent != nullptr)
        CloseHandle(impl_->stopEvent);
    delete impl_;
}

bool Thread::start(EntryPoint entryPoint) noexcept
{
    if (entryPoint == nullptr || impl_->handle != nullptr || impl_->stopEvent == nullptr)
        return false;

    impl_->entryPoint = entryPoint;
    impl_->handle = CreateThread(nullptr, 0, threadEntry, impl_, 0, &impl_->id);
    return impl_->handle != nullptr;
}

void Thread::requestStop() noexcept
{
    if (impl_->stopEvent != nullptr)
        SetEvent(impl_->stopEvent);
}

bool Thread::stopRequested() const noexcept
{
    return impl_->stopEvent != nullptr && WaitForSingleObject(impl_->stopEvent, 0) == WAIT_OBJECT_0;
}

bool Thread::join() noexcept
{
    if (impl_->handle == nullptr)
        return true;
    if (GetCurrentThreadId() == impl_->id)
        return false;

    const auto result = WaitForSingleObject(impl_->handle, INFINITE);
    if (result != WAIT_OBJECT_0)
        return false;

    CloseHandle(impl_->handle);
    impl_->handle = nullptr;
    impl_->id = 0;
    return true;
}

bool Thread::joinable() const noexcept
{
    return impl_->handle != nullptr;
}

bool Thread::setRealtimePriority() noexcept
{
    if (impl_->handle == nullptr)
        return false;

    const bool priorityConfigured = SetThreadPriority(impl_->handle, THREAD_PRIORITY_TIME_CRITICAL) != FALSE;
    bool avrtConfigured = false;
    if (GetCurrentThreadId() == impl_->id && impl_->avrtHandle == nullptr) {
        DWORD taskIndex = 0;
        impl_->avrtHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        avrtConfigured = impl_->avrtHandle != nullptr;
        if (avrtConfigured)
            AvSetMmThreadPriority(impl_->avrtHandle, AVRT_PRIORITY_HIGH);
    }
    return avrtConfigured || priorityConfigured;
}

HANDLE Thread::nativeHandle() const noexcept
{
    return impl_->handle;
}

DWORD Thread::threadId() const noexcept
{
    return impl_->id;
}

}