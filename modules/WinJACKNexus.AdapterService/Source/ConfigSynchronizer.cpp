#include <WinJACKNexus/AdapterService/ConfigSynchronizer.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include <WinJACKNexus/AdapterBackend/DeviceEnumerator.h>

namespace wjn::adapter::service
{
namespace
{

juce::WASAPIDeviceMode toWasapiMode (const juce::String& mode)
{
    return mode.equalsIgnoreCase ("exclusive") ? juce::WASAPIDeviceMode::exclusive
                                                : juce::WASAPIDeviceMode::shared;
}

juce::String clientNamePrefix (const DiscoveredDevice& device)
{
    if (device.kind.equalsIgnoreCase ("Midi"))
        return device.direction.equalsIgnoreCase ("In") ? "WDM_MidiIn_" : "WDM_MidiOut_";

    return device.direction.equalsIgnoreCase ("In") ? "WDM_AudioIn_" : "WDM_AudioOut_";
}

bool clientEquals (const ServiceClient& left, const ServiceClient& right)
{
    return left.id == right.id
        && left.clientName == right.clientName
        && left.enabled == right.enabled
        && left.status == right.status
        && left.kind == right.kind
        && left.driver == right.driver
        && left.direction == right.direction
        && left.streamType == right.streamType
        && left.device == right.device
        && left.guid == right.guid
        && left.channels == right.channels
        && left.sampleRate == right.sampleRate
        && left.wasapiMode == right.wasapiMode;
}

juce::String nextId (const std::vector<ServiceClient>& clients)
{
    for (int index = 1;; ++index)
    {
        const auto candidate = "svc-" + juce::String (index).paddedLeft ('0', 3);
        const auto used = std::any_of (clients.begin(), clients.end(), [&candidate] (const auto& client)
        {
            return client.id == candidate;
        });
        if (! used)
            return candidate;
    }
}

juce::String nextClientName (const DiscoveredDevice& device,
                             const std::vector<ServiceClient>& clients)
{
    const auto prefix = clientNamePrefix (device);
    for (int index = 1;; ++index)
    {
        const auto candidate = prefix + juce::String (index).paddedLeft ('0', 2);
        const auto used = std::any_of (clients.begin(), clients.end(), [&candidate] (const auto& client)
        {
            return client.clientName == candidate;
        });
        if (! used)
            return candidate;
    }
}

ServiceClient makeClient (const DiscoveredDevice& device,
                          const std::vector<ServiceClient>& existingClients)
{
    ServiceClient client;
    client.id = nextId (existingClients);
    client.clientName = nextClientName (device, existingClients);
    client.enabled = true;
    client.status = "available";
    client.kind = device.kind;
    client.driver = device.driver;
    client.direction = device.direction;
    client.streamType = device.streamType;
    client.device = device.device;
    client.guid = device.guid.isNotEmpty() ? device.guid : device.device;
    client.sampleRate = device.sampleRate;
    client.wasapiMode = device.wasapiMode;

    if (device.kind.equalsIgnoreCase ("Audio"))
    {
        const auto channelCount = device.channels > 0 ? device.channels : 2;
        for (int channel = 0; channel < channelCount; ++channel)
            client.channels.push_back (channel);
    }

    return client;
}

} // namespace

juce::String DiscoveredDevice::stableKey() const
{
    const auto identifier = guid.isNotEmpty() ? guid : device;
    if (identifier.isEmpty())
        return {};

    return wjn::adapter::backend::makeStableDeviceKey (kind, direction,
                                                        streamType, identifier);
}

std::vector<DiscoveredDevice> ConfigSynchronizer::enumerateCurrentDevices (
    const wjn::adapter::backend::AudioDeviceFilterSettings& filters)
{
    constexpr auto defaultMode = juce::WASAPIDeviceMode::shared;
    std::vector<DiscoveredDevice> devices;

    for (const auto input : { true, false })
    {
        const auto direction = input ? juce::String ("In") : juce::String ("Out");
        const auto streamType = input ? juce::String ("Record") : juce::String ("Playback");
        for (const auto& audio : wjn::adapter::backend::enumerateWasapiDevices (input, defaultMode))
        {
            DiscoveredDevice device;
            device.kind = "Audio";
            device.driver = "WASAPI";
            device.direction = direction;
            device.streamType = streamType;
            device.device = audio.name;
            device.guid = audio.identifier.isNotEmpty() ? audio.identifier : audio.name;
            device.channels = 2;
            device.sampleRate = 0.0;
            device.wasapiMode = "shared";
            device.allowedForNew = wjn::adapter::backend::isAudioDeviceAllowed (
                device.device, input, filters);
            devices.push_back (std::move (device));
        }
    }

    for (const auto input : { true, false })
    {
        const auto direction = input ? juce::String ("In") : juce::String ("Out");
        const auto streamType = input ? juce::String ("Input") : juce::String ("Output");
        for (const auto& midi : wjn::adapter::backend::enumerateMidiDevices (input))
        {
            DiscoveredDevice device;
            device.kind = "Midi";
            device.driver = "WinMM / WinRT MIDI";
            device.direction = direction;
            device.streamType = streamType;
            device.device = midi.name;
            device.guid = midi.identifier.isNotEmpty() ? midi.identifier : midi.name;
            device.channels = 0;
            device.sampleRate = 0.0;
            device.wasapiMode = "shared";
            device.allowedForNew = true;
            devices.push_back (std::move (device));
        }
    }

    std::sort (devices.begin(), devices.end(), [] (const auto& left, const auto& right)
    {
        return left.stableKey().compare (right.stableKey()) < 0;
    });
    return devices;
}

SynchronizationResult ConfigSynchronizer::synchronize (
    ServiceConfig& config, const std::vector<DiscoveredDevice>& devices, juce::String& error)
{
    error.clear();
    SynchronizationResult result;
    if (! config.validate (error))
        return result;

    std::vector<DiscoveredDevice> sortedDevices = devices;
    std::sort (sortedDevices.begin(), sortedDevices.end(), [] (const auto& left, const auto& right)
    {
        return left.stableKey().compare (right.stableKey()) < 0;
    });

    std::map<std::string, const DiscoveredDevice*> currentByKey;
    for (const auto& device : sortedDevices)
    {
        const auto key = device.stableKey();
        if (key.isNotEmpty())
            currentByKey.emplace (key.toStdString(), &device);
    }

    const auto oldClients = config.clients;
    std::vector<ServiceClient> synchronizedClients;
    std::set<std::string> existingKeys;
    std::set<std::string> matchedKeys;

    for (const auto& client : oldClients)
    {
        const auto key = client.stableKey();
        if (! existingKeys.insert (key.toStdString()).second)
        {
            result.changed = true;
            continue;
        }

        const auto current = currentByKey.find (key.toStdString());
        if (current != currentByKey.end())
        {
            auto updated = client;
            updated.kind = current->second->kind;
            updated.driver = current->second->driver;
            updated.direction = current->second->direction;
            updated.streamType = current->second->streamType;
            updated.device = current->second->device;
            updated.guid = current->second->guid.isNotEmpty()
                               ? current->second->guid : current->second->device;
            updated.status = "available";
            if (! clientEquals (client, updated))
                result.changed = true;
            synchronizedClients.push_back (std::move (updated));
            matchedKeys.insert (key.toStdString());
        }
        else if (config.autoRemoveLostDevices && client.enabled)
        {
            ++result.removed;
            result.removedClientNames.add (client.clientName);
            result.changed = true;
        }
        else
        {
            auto updated = client;
            updated.status = "missing";
            if (! clientEquals (client, updated))
                result.changed = true;
            synchronizedClients.push_back (std::move (updated));
        }
    }

    for (const auto& device : sortedDevices)
    {
        const auto key = device.stableKey();
        if (key.isEmpty() || matchedKeys.contains (key.toStdString())
            || ! device.allowedForNew)
            continue;

        auto client = makeClient (device, synchronizedClients);
        synchronizedClients.push_back (std::move (client));
        matchedKeys.insert (key.toStdString());
        ++result.added;
        result.changed = true;
    }

    config.clients = std::move (synchronizedClients);
    config.updated = juce::Time::getCurrentTime().toISO8601 (true);
    if (! config.validate (error))
    {
        config.clients = oldClients;
        return SynchronizationResult {};
    }

    return result;
}

bool ConfigSynchronizer::synchronizeFile (const juce::File& file, juce::String& error,
                                          SynchronizationResult* result)
{
    error.clear();
    if (file == juce::File())
    {
        error = "No AdapterService configuration file was selected";
        return false;
    }

    ServiceConfig config;
    if (file.existsAsFile())
    {
        config = ServiceConfig::loadFromFile (file, error);
        if (! config.isValid())
            return false;
    }
    else
    {
        config = ServiceConfig::createDefault();
    }

    const auto devices = enumerateCurrentDevices (config.filters);
    auto synchronization = synchronize (config, devices, error);
    if (error.isNotEmpty())
        return false;

    if (! config.saveToFile (file, error))
        return false;

    if (result != nullptr)
        *result = std::move (synchronization);
    return true;
}

} // namespace wjn::adapter::service