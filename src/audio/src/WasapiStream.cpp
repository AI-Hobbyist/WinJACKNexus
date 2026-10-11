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
#include <WinJACKNexus/Audio/WasapiStream.hpp>

#include <Windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

namespace WinJACKNexus::Audio
{

namespace
{

using Microsoft::WRL::ComPtr;

class ComScope final
{
  public:
    ComScope() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
          ownsInitialization_(result_ == S_OK || result_ == S_FALSE)
    {
    }

    ~ComScope() noexcept
    {
        if (ownsInitialization_)
            CoUninitialize();
    }

    bool isUsable() const noexcept
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT result() const noexcept
    {
        return result_;
    }

  private:
    HRESULT result_ = E_FAIL;
    bool ownsInitialization_ = false;
};

bool isFloatFormat(const WAVEFORMATEX *format) noexcept
{
    if (format == nullptr)
        return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return false;
    const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
    return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
}

bool isPcmFormat(const WAVEFORMATEX *format) noexcept
{
    if (format == nullptr)
        return false;
    if (format->wFormatTag == WAVE_FORMAT_PCM)
        return true;
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return false;
    const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
    return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != FALSE;
}

float sampleToFloat(const std::byte *sample, std::uint16_t bitsPerSample,
                    bool floatingPoint) noexcept
{
    if (floatingPoint && bitsPerSample == 32)
    {
        float value = 0.0f;
        std::memcpy(&value, sample, sizeof(value));
        return value;
    }
    if (bitsPerSample == 16)
    {
        std::int16_t value = 0;
        std::memcpy(&value, sample, sizeof(value));
        return static_cast<float>(value) / 32768.0f;
    }
    if (bitsPerSample == 24)
    {
        const auto value = static_cast<std::int32_t>(static_cast<std::uint32_t>(sample[0]) |
                                                     (static_cast<std::uint32_t>(sample[1]) << 8) |
                                                     (static_cast<std::uint32_t>(sample[2]) << 16));
        const auto signedValue =
            (value & 0x00800000) != 0 ? value | static_cast<std::int32_t>(0xff000000) : value;
        return static_cast<float>(signedValue) / 8388608.0f;
    }
    if (bitsPerSample == 32)
    {
        std::int32_t value = 0;
        std::memcpy(&value, sample, sizeof(value));
        return static_cast<float>(value) / 2147483648.0f;
    }
    return 0.0f;
}

void floatToSample(float value, std::byte *sample, std::uint16_t bitsPerSample,
                   bool floatingPoint) noexcept
{
    value = std::clamp(value, -1.0f, 1.0f);
    if (floatingPoint && bitsPerSample == 32)
    {
        std::memcpy(sample, &value, sizeof(value));
        return;
    }
    if (bitsPerSample == 16)
    {
        const auto converted = static_cast<std::int16_t>(std::lrint(value * 32767.0f));
        std::memcpy(sample, &converted, sizeof(converted));
        return;
    }
    if (bitsPerSample == 24)
    {
        const auto converted = static_cast<std::int32_t>(std::lrint(value * 8388607.0f));
        sample[0] = static_cast<std::byte>(converted & 0xff);
        sample[1] = static_cast<std::byte>((converted >> 8) & 0xff);
        sample[2] = static_cast<std::byte>((converted >> 16) & 0xff);
        return;
    }
    if (bitsPerSample == 32)
    {
        const auto converted =
            static_cast<std::int32_t>(std::llrint(static_cast<double>(value) * 2147483647.0));
        std::memcpy(sample, &converted, sizeof(converted));
    }
}

}

struct WasapiStream::Impl final
{
    WasapiStreamDirection direction = WasapiStreamDirection::render;
    WasapiStreamFormat streamFormat{};
    std::uint32_t bufferFrameCount = 0;
    std::uint16_t bytesPerFrame = 0;
    bool floatingPoint = false;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> audioClient;
    ComPtr<IAudioRenderClient> renderClient;
    ComPtr<IAudioCaptureClient> captureClient;
    HANDLE audioEvent = nullptr;
    HANDLE stopEvent = nullptr;
    std::vector<float> scratch;
    WasapiRenderCallback renderCallback = nullptr;
    WasapiCaptureCallback captureCallback = nullptr;
    void *callbackData = nullptr;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<std::int32_t> error{0};
    bool comOwned = false;
    bool started = false;

    void setError(HRESULT result) noexcept
    {
        error.store(static_cast<std::int32_t>(result), std::memory_order_release);
    }

    void closeHandles() noexcept
    {
        if (audioEvent != nullptr)
        {
            CloseHandle(audioEvent);
            audioEvent = nullptr;
        }
        if (stopEvent != nullptr)
        {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
        }
    }

    void releaseCom() noexcept
    {
        captureClient.Reset();
        renderClient.Reset();
        audioClient.Reset();
        device.Reset();
        if (comOwned)
        {
            CoUninitialize();
            comOwned = false;
        }
    }
};

namespace
{

void processRender(WasapiStream::Impl &impl) noexcept
{
    if (impl.audioClient == nullptr || impl.renderClient == nullptr)
        return;
    UINT32 padding = 0;
    if (FAILED(impl.audioClient->GetCurrentPadding(&padding)))
        return;
    const auto available = impl.bufferFrameCount > padding ? impl.bufferFrameCount - padding : 0;
    if (available == 0)
        return;

    BYTE *nativeBuffer = nullptr;
    const auto result = impl.renderClient->GetBuffer(available, &nativeBuffer);
    if (FAILED(result))
    {
        impl.setError(result);
        return;
    }

    const auto sampleCount = static_cast<std::size_t>(available) * impl.streamFormat.channels;
    if (impl.renderCallback != nullptr)
    {
        impl.renderCallback(impl.scratch.data(), available, impl.callbackData);
        for (std::size_t index = 0; index < sampleCount; ++index)
            floatToSample(impl.scratch[index],
                          reinterpret_cast<std::byte *>(nativeBuffer) +
                              index * (impl.bytesPerFrame / impl.streamFormat.channels),
                          impl.streamFormat.bitsPerSample, impl.floatingPoint);
    }
    else
    {
        std::memset(nativeBuffer, 0, static_cast<std::size_t>(available) * impl.bytesPerFrame);
    }
    const auto releaseResult = impl.renderClient->ReleaseBuffer(available, 0);
    if (FAILED(releaseResult))
        impl.setError(releaseResult);
}

void processCapture(WasapiStream::Impl &impl) noexcept
{
    if (impl.captureClient == nullptr)
        return;
    UINT32 packetFrames = 0;
    while (SUCCEEDED(impl.captureClient->GetNextPacketSize(&packetFrames)) && packetFrames != 0)
    {
        BYTE *nativeBuffer = nullptr;
        DWORD flags = 0;
        UINT64 devicePosition = 0;
        UINT64 performanceCounter = 0;
        const auto result = impl.captureClient->GetBuffer(&nativeBuffer, &packetFrames, &flags,
                                                          &devicePosition, &performanceCounter);
        if (FAILED(result))
        {
            impl.setError(result);
            return;
        }

        const auto sampleCount =
            static_cast<std::size_t>(packetFrames) * impl.streamFormat.channels;
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || nativeBuffer == nullptr)
        {
            std::fill_n(impl.scratch.data(), sampleCount, 0.0f);
        }
        else
        {
            for (std::size_t index = 0; index < sampleCount; ++index)
                impl.scratch[index] =
                    sampleToFloat(reinterpret_cast<const std::byte *>(nativeBuffer) +
                                      index * (impl.bytesPerFrame / impl.streamFormat.channels),
                                  impl.streamFormat.bitsPerSample, impl.floatingPoint);
        }
        if (impl.captureCallback != nullptr)
            impl.captureCallback(impl.scratch.data(), packetFrames, impl.callbackData);

        const auto releaseResult = impl.captureClient->ReleaseBuffer(packetFrames);
        if (FAILED(releaseResult))
        {
            impl.setError(releaseResult);
            return;
        }
    }
}

void runStream(WasapiStream::Impl *impl) noexcept
{
    if (impl == nullptr)
        return;
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool ownsCom = comResult == S_OK || comResult == S_FALSE;
    DWORD taskIndex = 0;
    auto *avrtTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    HANDLE handles[] = {impl->stopEvent, impl->audioEvent};
    while (impl->running.load(std::memory_order_acquire))
    {
        const auto waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0)
            break;
        if (waitResult != WAIT_OBJECT_0 + 1)
        {
            impl->setError(HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        if (impl->direction == WasapiStreamDirection::render)
            processRender(*impl);
        else
            processCapture(*impl);
    }
    if (avrtTask != nullptr)
        AvRevertMmThreadCharacteristics(avrtTask);
    impl->running.store(false, std::memory_order_release);
    if (ownsCom)
        CoUninitialize();
}

}

WasapiStream::WasapiStream() noexcept : impl_(new (std::nothrow) Impl{}) {}

WasapiStream::~WasapiStream() noexcept
{
    close();
    delete impl_;
}

WasapiStream::WasapiStream(WasapiStream &&other) noexcept : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

WasapiStream &WasapiStream::operator=(WasapiStream &&other) noexcept
{
    if (this == &other)
        return *this;
    close();
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

bool WasapiStream::open(WasapiStreamDirection direction,
                        const WasapiStreamOptions &options) noexcept
{
    if (impl_ == nullptr)
        impl_ = new (std::nothrow) Impl{};
    if (impl_ == nullptr)
        return false;
    close();
    impl_->error.store(0, std::memory_order_release);
    impl_->direction = direction;

    try
    {
        const auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (comResult == S_OK || comResult == S_FALSE)
            impl_->comOwned = true;
        else if (comResult != RPC_E_CHANGED_MODE)
        {
            impl_->setError(comResult);
            return false;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(&enumerator));
        if (FAILED(result))
        {
            impl_->setError(result);
            impl_->releaseCom();
            return false;
        }

        if (options.deviceId != nullptr && *options.deviceId != L'\0')
            result = enumerator->GetDevice(options.deviceId, &impl_->device);
        else
            result = enumerator->GetDefaultAudioEndpoint(
                direction == WasapiStreamDirection::capture ? eCapture : eRender, eConsole,
                &impl_->device);
        if (FAILED(result))
        {
            impl_->setError(result);
            impl_->releaseCom();
            return false;
        }

        result = impl_->device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                         &impl_->audioClient);
        if (FAILED(result))
        {
            impl_->setError(result);
            impl_->releaseCom();
            return false;
        }

        WAVEFORMATEX *mixFormat = nullptr;
        result = impl_->audioClient->GetMixFormat(&mixFormat);
        if (FAILED(result) || mixFormat == nullptr)
        {
            impl_->setError(FAILED(result) ? result : E_FAIL);
            if (mixFormat != nullptr)
                CoTaskMemFree(mixFormat);
            impl_->releaseCom();
            return false;
        }

        impl_->floatingPoint = isFloatFormat(mixFormat);
        const auto pcm = isPcmFormat(mixFormat);
        impl_->streamFormat.sampleRate = mixFormat->nSamplesPerSec;
        impl_->streamFormat.channels = mixFormat->nChannels;
        impl_->streamFormat.bitsPerSample = mixFormat->wBitsPerSample;
        impl_->streamFormat.floatingPoint = impl_->floatingPoint;
        impl_->bytesPerFrame = mixFormat->nBlockAlign;
        if (!pcm && !impl_->floatingPoint)
        {
            CoTaskMemFree(mixFormat);
            impl_->setError(E_NOTIMPL);
            impl_->releaseCom();
            return false;
        }
        result = impl_->audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, mixFormat, nullptr);
        CoTaskMemFree(mixFormat);
        if (FAILED(result))
        {
            impl_->setError(result);
            impl_->releaseCom();
            return false;
        }

        result = impl_->audioClient->GetBufferSize(&impl_->bufferFrameCount);
        if (FAILED(result))
        {
            impl_->setError(result);
            impl_->releaseCom();
            return false;
        }
        impl_->audioEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        impl_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (impl_->audioEvent == nullptr || impl_->stopEvent == nullptr)
        {
            impl_->setError(HRESULT_FROM_WIN32(GetLastError()));
            impl_->closeHandles();
            impl_->releaseCom();
            return false;
        }
        result = impl_->audioClient->SetEventHandle(impl_->audioEvent);
        if (FAILED(result))
        {
            impl_->setError(result);
            impl_->closeHandles();
            impl_->releaseCom();
            return false;
        }
        if (direction == WasapiStreamDirection::render)
            result = impl_->audioClient->GetService(IID_PPV_ARGS(&impl_->renderClient));
        else
            result = impl_->audioClient->GetService(IID_PPV_ARGS(&impl_->captureClient));
        if (FAILED(result))
        {
            impl_->setError(result);
            impl_->closeHandles();
            impl_->releaseCom();
            return false;
        }
        impl_->scratch.resize(static_cast<std::size_t>(impl_->bufferFrameCount) *
                              impl_->streamFormat.channels);
        return true;
    }
    catch (...)
    {
        impl_->setError(E_OUTOFMEMORY);
        impl_->closeHandles();
        impl_->releaseCom();
        return false;
    }
}

bool WasapiStream::start(WasapiRenderCallback callback, void *userData) noexcept
{
    if (impl_ == nullptr || !isOpen() || impl_->direction != WasapiStreamDirection::render ||
        isRunning())
        return false;
    impl_->renderCallback = callback;
    impl_->callbackData = userData;
    ResetEvent(impl_->stopEvent);
    const auto result = impl_->audioClient->Start();
    if (FAILED(result))
    {
        impl_->setError(result);
        return false;
    }
    impl_->started = true;
    impl_->running.store(true, std::memory_order_release);
    try
    {
        impl_->worker = std::thread(runStream, impl_);
    }
    catch (...)
    {
        impl_->running.store(false, std::memory_order_release);
        impl_->audioClient->Stop();
        impl_->started = false;
        impl_->setError(E_OUTOFMEMORY);
        return false;
    }
    return true;
}

bool WasapiStream::start(WasapiCaptureCallback callback, void *userData) noexcept
{
    if (impl_ == nullptr || !isOpen() || impl_->direction != WasapiStreamDirection::capture ||
        isRunning())
        return false;
    impl_->captureCallback = callback;
    impl_->callbackData = userData;
    ResetEvent(impl_->stopEvent);
    const auto result = impl_->audioClient->Start();
    if (FAILED(result))
    {
        impl_->setError(result);
        return false;
    }
    impl_->started = true;
    impl_->running.store(true, std::memory_order_release);
    try
    {
        impl_->worker = std::thread(runStream, impl_);
    }
    catch (...)
    {
        impl_->running.store(false, std::memory_order_release);
        impl_->audioClient->Stop();
        impl_->started = false;
        impl_->setError(E_OUTOFMEMORY);
        return false;
    }
    return true;
}

bool WasapiStream::stop() noexcept
{
    if (impl_ == nullptr)
        return true;
    if (impl_->running.exchange(false, std::memory_order_acq_rel))
        SetEvent(impl_->stopEvent);
    if (impl_->worker.joinable())
        impl_->worker.join();
    if (impl_->started && impl_->audioClient != nullptr)
    {
        const auto result = impl_->audioClient->Stop();
        if (FAILED(result))
            impl_->setError(result);
        impl_->started = false;
    }
    return true;
}

void WasapiStream::close() noexcept
{
    if (impl_ == nullptr)
        return;
    stop();
    impl_->scratch.clear();
    impl_->closeHandles();
    impl_->releaseCom();
    impl_->renderCallback = nullptr;
    impl_->captureCallback = nullptr;
    impl_->callbackData = nullptr;
    impl_->bufferFrameCount = 0;
    impl_->bytesPerFrame = 0;
    impl_->streamFormat = {};
}

bool WasapiStream::isOpen() const noexcept
{
    return impl_ != nullptr && impl_->audioClient != nullptr;
}

bool WasapiStream::isRunning() const noexcept
{
    return impl_ != nullptr && impl_->running.load(std::memory_order_acquire);
}

WasapiStreamDirection WasapiStream::direction() const noexcept
{
    return impl_ != nullptr ? impl_->direction : WasapiStreamDirection::render;
}

WasapiStreamFormat WasapiStream::format() const noexcept
{
    return impl_ != nullptr ? impl_->streamFormat : WasapiStreamFormat{};
}

std::uint32_t WasapiStream::bufferFrames() const noexcept
{
    return impl_ != nullptr ? impl_->bufferFrameCount : 0;
}

std::int32_t WasapiStream::lastError() const noexcept
{
    return impl_ != nullptr ? impl_->error.load(std::memory_order_acquire)
                            : static_cast<std::int32_t>(E_FAIL);
}

}