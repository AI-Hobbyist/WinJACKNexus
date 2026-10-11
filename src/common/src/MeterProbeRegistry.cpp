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
#include <WinJACKNexus/Common/MeterProbeRegistry.hpp>

#include <algorithm>
#include <mutex>
#include <utility>

namespace WinJACKNexus::Common
{

namespace
{

struct MeterProbeRegistryStorage final
{
    std::mutex mutex;
    std::vector<MeterProbeRegistration> registrations;
};

MeterProbeRegistryStorage &registryStorage()
{
    static MeterProbeRegistryStorage storage;
    return storage;
}

bool isValidDescriptor(const MeterProbeDescriptor &descriptor,
                       const std::shared_ptr<MeterProbeRealtimeState> &realtimeState) noexcept
{
    return realtimeState != nullptr && !descriptor.instanceId.empty() &&
           !descriptor.clientName.empty() && descriptor.channelCount > 0 &&
           descriptor.channelCount <= MeterChannelLayout::maximumChannelCount;
}

}

void MeterProbeRealtimeState::publish(const MeterSnapshot &snapshot) noexcept
{
    frame_.set(MeterProbeRealtimeFrame{snapshot});
}

MeterProbeRealtimeFrame MeterProbeRealtimeState::frame() const noexcept
{
    return frame_.getRead();
}

void MeterProbeRealtimeState::setOnline(bool online) noexcept
{
    online_.store(online, std::memory_order_release);
}

bool MeterProbeRealtimeState::isOnline() const noexcept
{
    return online_.load(std::memory_order_acquire);
}

void MeterProbeRealtimeState::setAutomaticResetEnabled(bool enabled) noexcept
{
    automaticResetEnabled_.store(enabled, std::memory_order_release);
}

bool MeterProbeRealtimeState::automaticResetEnabled() const noexcept
{
    return automaticResetEnabled_.load(std::memory_order_acquire);
}

void MeterProbeRealtimeState::setSilenceResetConfiguration(float thresholdDbfs,
                                                           float durationSeconds) noexcept
{
    silenceThresholdDbfs_.store(thresholdDbfs, std::memory_order_release);
    silenceDurationSeconds_.store(durationSeconds, std::memory_order_release);
}

float MeterProbeRealtimeState::silenceThresholdDbfs() const noexcept
{
    return silenceThresholdDbfs_.load(std::memory_order_acquire);
}

float MeterProbeRealtimeState::silenceDurationSeconds() const noexcept
{
    return silenceDurationSeconds_.load(std::memory_order_acquire);
}

void MeterProbeRealtimeState::requestLoudnessReset() noexcept
{
    resetGeneration_.fetch_add(1, std::memory_order_release);
}

bool MeterProbeRealtimeState::consumeLoudnessResetRequest(
    std::uint64_t &observedGeneration) noexcept
{
    const auto requestedGeneration = resetGeneration_.load(std::memory_order_acquire);
    if (requestedGeneration == observedGeneration)
        return false;
    observedGeneration = requestedGeneration;
    return true;
}

void MeterProbeRealtimeState::notifyLoudnessReset() noexcept
{
    completedResetGeneration_.fetch_add(1, std::memory_order_release);
}

std::uint64_t MeterProbeRealtimeState::loudnessResetGeneration() const noexcept
{
    return completedResetGeneration_.load(std::memory_order_acquire);
}

bool registerMeterProbe(MeterProbeDescriptor descriptor,
                        std::shared_ptr<MeterProbeRealtimeState> realtimeState) noexcept
{
    if (!isValidDescriptor(descriptor, realtimeState))
        return false;

    try
    {
        auto &storage = registryStorage();
        std::lock_guard lock(storage.mutex);
        const auto existing =
            std::find_if(storage.registrations.begin(), storage.registrations.end(),
                         [&descriptor](const auto &registration) {
                             return registration.descriptor.instanceId == descriptor.instanceId;
                         });
        if (existing == storage.registrations.end())
        {
            storage.registrations.push_back({std::move(descriptor), std::move(realtimeState), 1});
            return true;
        }

        if (existing->descriptor.clientName != descriptor.clientName)
            ++existing->nameRevision;
        existing->descriptor = std::move(descriptor);
        existing->realtimeState = std::move(realtimeState);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void unregisterMeterProbe(std::string_view instanceId,
                          const std::shared_ptr<MeterProbeRealtimeState> &realtimeState) noexcept
{
    if (instanceId.empty() || realtimeState == nullptr)
        return;

    auto &storage = registryStorage();
    std::lock_guard lock(storage.mutex);
    std::erase_if(storage.registrations, [&instanceId, &realtimeState](const auto &registration) {
        return registration.descriptor.instanceId == instanceId &&
               registration.realtimeState == realtimeState;
    });
}

std::vector<MeterProbeRegistration> meterProbeRegistrations()
{
    auto &storage = registryStorage();
    std::lock_guard lock(storage.mutex);
    return storage.registrations;
}

}