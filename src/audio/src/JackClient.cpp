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
#include <WinJACKNexus/Audio/JackClient.hpp>

#include <WinJACKNexus/Common/Logger.hpp>

#include <new>

namespace WinJACKNexus::Audio
{

struct JackClient::Impl final
{
    jack_client_t *handle = nullptr;
    jack_status_t status = JackFailure;
    bool active = false;
};

JackClient::JackClient() noexcept : impl_(new (std::nothrow) Impl{}) {}

JackClient::~JackClient() noexcept
{
    close();
    delete impl_;
}

JackClient::JackClient(JackClient &&other) noexcept : impl_(other.impl_)
{
    other.impl_ = nullptr;
}

JackClient &JackClient::operator=(JackClient &&other) noexcept
{
    if (this == &other)
        return *this;
    close();
    delete impl_;
    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
}

bool JackClient::open(const JackClientOptions &options) noexcept
{
    if (impl_ == nullptr)
        impl_ = new (std::nothrow) Impl{};
    if (impl_ == nullptr)
        return false;
    if (impl_->handle != nullptr)
        return true;

    auto &api = JackApi::instance();
    if (!api.load())
        return false;

    auto jackOptions = JackNullOption;
    if (!options.autoStartServer)
        jackOptions = static_cast<jack_options_t>(jackOptions | JackNoStartServer);
    if (options.useExactName)
        jackOptions = static_cast<jack_options_t>(jackOptions | JackUseExactName);

    const char *clientName =
        options.name != nullptr && *options.name != '\0' ? options.name : "WinJACKNexus";
    impl_->status = JackFailure;
    impl_->handle = api.clientOpen(clientName, jackOptions, &impl_->status, options.serverName);
    if (impl_->handle == nullptr)
    {
        WJN_LOG_WARN("Unable to open JACK client, status=0x%lx",
                     static_cast<unsigned long>(impl_->status));
        return false;
    }
    impl_->active = false;
    return true;
}

bool JackClient::close() noexcept
{
    if (impl_ == nullptr || impl_->handle == nullptr)
        return true;

    auto &api = JackApi::instance();
    if (impl_->active)
    {
        api.deactivate(impl_->handle);
        impl_->active = false;
    }
    const auto result = api.clientClose(impl_->handle);
    if (result == 0)
        impl_->handle = nullptr;
    return result == 0;
}

bool JackClient::activate() noexcept
{
    if (impl_ == nullptr || impl_->handle == nullptr)
        return false;
    if (impl_->active)
        return true;
    if (JackApi::instance().activate(impl_->handle) != 0)
        return false;
    impl_->active = true;
    return true;
}

bool JackClient::deactivate() noexcept
{
    if (impl_ == nullptr || impl_->handle == nullptr || !impl_->active)
        return true;
    if (JackApi::instance().deactivate(impl_->handle) != 0)
        return false;
    impl_->active = false;
    return true;
}

bool JackClient::isOpen() const noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr;
}

bool JackClient::isActive() const noexcept
{
    return impl_ != nullptr && impl_->active;
}

jack_client_t *JackClient::nativeHandle() const noexcept
{
    return impl_ != nullptr ? impl_->handle : nullptr;
}

const char *JackClient::name() const noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().clientName(impl_->handle)
               : nullptr;
}

jack_status_t JackClient::lastStatus() const noexcept
{
    return impl_ != nullptr ? impl_->status : JackFailure;
}

void JackClient::setShutdownCallback(JackShutdownCallback callback, void *argument) noexcept
{
    if (impl_ != nullptr && impl_->handle != nullptr)
        JackApi::instance().setShutdownCallback(impl_->handle, callback, argument);
}

int JackClient::setProcessCallback(JackProcessCallback callback, void *argument) noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().setProcessCallback(impl_->handle, callback, argument)
               : -1;
}

int JackClient::setBufferSizeCallback(JackBufferSizeCallback callback, void *argument) noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().setBufferSizeCallback(impl_->handle, callback, argument)
               : -1;
}

int JackClient::setSampleRateCallback(JackSampleRateCallback callback, void *argument) noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().setSampleRateCallback(impl_->handle, callback, argument)
               : -1;
}

int JackClient::setGraphOrderCallback(JackGraphOrderCallback callback, void *argument) noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().setGraphOrderCallback(impl_->handle, callback, argument)
               : -1;
}

int JackClient::setXRunCallback(JackXRunCallback callback, void *argument) noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().setXRunCallback(impl_->handle, callback, argument)
               : -1;
}

jack_nframes_t JackClient::sampleRate() const noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().sampleRate(impl_->handle)
               : 0;
}

jack_nframes_t JackClient::bufferSize() const noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr
               ? JackApi::instance().bufferSize(impl_->handle)
               : 0;
}

float JackClient::cpuLoad() const noexcept
{
    return impl_ != nullptr && impl_->handle != nullptr ? JackApi::instance().cpuLoad(impl_->handle)
                                                        : 0.0f;
}

}