#include <WinJACKNexus/Audio/MidiStream.hpp>

#include <WinJACKNexus/Common/LockFreeSpscQueue.hpp>

#include <Windows.h>
#include <mmsystem.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <new>

namespace WinJACKNexus::Audio {

namespace {

void CALLBACK midiInputCallback(HMIDIIN,
                                UINT message,
                                DWORD_PTR instance,
                                DWORD_PTR parameter1,
                                DWORD_PTR parameter2);

}

struct MidiStream::Impl final {
    static constexpr std::size_t queueCapacity = 1024;
    static constexpr std::size_t maxLongBuffers = 16;

    MidiDirection direction = MidiDirection::input;
    HMIDIIN inputHandle = nullptr;
    HMIDIOUT outputHandle = nullptr;
    std::array<MIDIHDR, maxLongBuffers> headers {};
    std::array<std::array<char, midiEventDataCapacity>, maxLongBuffers> buffers {};
    std::size_t headerCount = 0;
    std::atomic<bool> stopping { false };
    std::atomic<bool> running { false };
    std::atomic<std::uint32_t> error { MMSYSERR_NOERROR };
    Common::LockFreeSpscQueue<MidiEvent, queueCapacity> events;
};

namespace {

void CALLBACK midiInputCallback(HMIDIIN,
                                UINT message,
                                DWORD_PTR instance,
                                DWORD_PTR parameter1,
                                DWORD_PTR parameter2)
{
    auto* impl = reinterpret_cast<MidiStream::Impl*>(instance);
    if (impl == nullptr)
        return;

    MidiEvent event;
    if (message == MIM_DATA) {
        if (event.assignShortMessage(static_cast<std::uint32_t>(parameter1), static_cast<std::uint32_t>(parameter2)))
            impl->events.try_push(std::move(event));
        return;
    }
    if (message != MIM_LONGDATA)
        return;

    auto* header = reinterpret_cast<MIDIHDR*>(parameter1);
    if (header != nullptr && header->lpData != nullptr && header->dwBytesRecorded != 0) {
        const auto size = std::min<std::size_t>(header->dwBytesRecorded, midiEventDataCapacity);
        if (event.assign(reinterpret_cast<const std::uint8_t*>(header->lpData),
                         size,
                         static_cast<std::uint32_t>(parameter2)))
            impl->events.try_push(std::move(event));
    }

    if (!impl->stopping.load(std::memory_order_acquire) && impl->inputHandle != nullptr) {
        header->dwBytesRecorded = 0;
        const auto result = midiInAddBuffer(impl->inputHandle, header, sizeof(MIDIHDR));
        if (result != MMSYSERR_NOERROR)
            impl->error.store(result, std::memory_order_release);
    }
}

}

MidiStream::MidiStream() noexcept
    : impl_(new (std::nothrow) Impl {})
{
}

MidiStream::~MidiStream() noexcept
{
    close();
    delete impl_;
}

MidiStream::MidiStream(MidiStream&& other) noexcept
    : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

MidiStream& MidiStream::operator=(MidiStream&& other) noexcept
{
    if (this == &other)
        return *this;
    close();
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

bool MidiStream::open(MidiDirection direction, const MidiStreamOptions& options) noexcept
{
    if (impl_ == nullptr)
        impl_ = new (std::nothrow) Impl {};
    if (impl_ == nullptr)
        return false;
    close();
    impl_->direction = direction;
    impl_->error.store(MMSYSERR_NOERROR, std::memory_order_release);
    impl_->events.clear();

    if (direction == MidiDirection::input) {
        auto bufferCount = std::clamp<std::uint32_t>(options.longMessageBuffers,
                                                     1,
                                                     static_cast<std::uint32_t>(Impl::maxLongBuffers));
        impl_->headerCount = bufferCount;
        const auto result = midiInOpen(&impl_->inputHandle,
                                       options.deviceId,
                                       reinterpret_cast<DWORD_PTR>(&midiInputCallback),
                                       reinterpret_cast<DWORD_PTR>(impl_),
                                       CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
            impl_->error.store(result, std::memory_order_release);
            impl_->inputHandle = nullptr;
            return false;
        }
        for (std::size_t index = 0; index < impl_->headerCount; ++index) {
            impl_->headers[index] = {};
            impl_->headers[index].lpData = impl_->buffers[index].data();
            impl_->headers[index].dwBufferLength = static_cast<DWORD>(impl_->buffers[index].size());
            auto prepareResult = midiInPrepareHeader(impl_->inputHandle,
                                                      &impl_->headers[index],
                                                      sizeof(MIDIHDR));
            if (prepareResult != MMSYSERR_NOERROR) {
                impl_->error.store(prepareResult, std::memory_order_release);
                close();
                return false;
            }
            prepareResult = midiInAddBuffer(impl_->inputHandle, &impl_->headers[index], sizeof(MIDIHDR));
            if (prepareResult != MMSYSERR_NOERROR) {
                impl_->error.store(prepareResult, std::memory_order_release);
                close();
                return false;
            }
        }
        return true;
    }

    const auto result = midiOutOpen(&impl_->outputHandle,
                                    options.deviceId,
                                    0,
                                    0,
                                    CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        impl_->error.store(result, std::memory_order_release);
        impl_->outputHandle = nullptr;
        return false;
    }
    return true;
}

bool MidiStream::start() noexcept
{
    if (impl_ == nullptr || impl_->inputHandle == nullptr || impl_->direction != MidiDirection::input)
        return false;
    if (impl_->running.load(std::memory_order_acquire))
        return true;
    impl_->stopping.store(false, std::memory_order_release);
    const auto result = midiInStart(impl_->inputHandle);
    if (result != MMSYSERR_NOERROR) {
        impl_->error.store(result, std::memory_order_release);
        return false;
    }
    impl_->running.store(true, std::memory_order_release);
    return true;
}

bool MidiStream::stop() noexcept
{
    if (impl_ == nullptr || impl_->direction != MidiDirection::input || impl_->inputHandle == nullptr)
        return true;
    impl_->stopping.store(true, std::memory_order_release);
    if (impl_->running.exchange(false, std::memory_order_acq_rel))
        midiInStop(impl_->inputHandle);
    const auto result = midiInReset(impl_->inputHandle);
    if (result != MMSYSERR_NOERROR)
        impl_->error.store(result, std::memory_order_release);
    return result == MMSYSERR_NOERROR;
}

void MidiStream::close() noexcept
{
    if (impl_ == nullptr)
        return;
    if (impl_->inputHandle != nullptr) {
        stop();
        for (std::size_t index = 0; index < impl_->headerCount; ++index)
            midiInUnprepareHeader(impl_->inputHandle, &impl_->headers[index], sizeof(MIDIHDR));
        const auto result = midiInClose(impl_->inputHandle);
        if (result != MMSYSERR_NOERROR)
            impl_->error.store(result, std::memory_order_release);
        impl_->inputHandle = nullptr;
        impl_->headerCount = 0;
    }
    if (impl_->outputHandle != nullptr) {
        const auto result = midiOutClose(impl_->outputHandle);
        if (result != MMSYSERR_NOERROR)
            impl_->error.store(result, std::memory_order_release);
        impl_->outputHandle = nullptr;
    }
    impl_->running.store(false, std::memory_order_release);
    impl_->stopping.store(false, std::memory_order_release);
}

bool MidiStream::sendShortMessage(std::uint32_t message) noexcept
{
    if (impl_ == nullptr || impl_->direction != MidiDirection::output || impl_->outputHandle == nullptr)
        return false;
    const auto result = midiOutShortMsg(impl_->outputHandle, message);
    if (result != MMSYSERR_NOERROR)
        impl_->error.store(result, std::memory_order_release);
    return result == MMSYSERR_NOERROR;
}

bool MidiStream::popEvent(MidiEvent& event) noexcept
{
    return impl_ != nullptr && impl_->direction == MidiDirection::input && impl_->events.try_pop(event);
}

std::size_t MidiStream::pendingEvents() const noexcept
{
    return impl_ != nullptr ? impl_->events.size() : 0;
}

bool MidiStream::isOpen() const noexcept
{
    return impl_ != nullptr && (impl_->inputHandle != nullptr || impl_->outputHandle != nullptr);
}

bool MidiStream::isRunning() const noexcept
{
    return impl_ != nullptr && impl_->running.load(std::memory_order_acquire);
}

MidiDirection MidiStream::direction() const noexcept
{
    return impl_ != nullptr ? impl_->direction : MidiDirection::input;
}

std::uint32_t MidiStream::lastError() const noexcept
{
    return impl_ != nullptr ? impl_->error.load(std::memory_order_acquire) : MMSYSERR_ERROR;
}

}