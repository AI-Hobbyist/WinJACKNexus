#include <WinJACKNexus/AdapterService/ConfigSynchronizer.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace
{

using wjn::adapter::service::ConfigSynchronizer;
using wjn::adapter::service::DiscoveredDevice;
using wjn::adapter::service::ServiceClient;
using wjn::adapter::service::ServiceConfig;

void require (bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "ConfigSynchronizer test failure: " << message << '\n';
        std::exit (1);
    }
}

DiscoveredDevice audio (const char* direction, const char* streamType,
                        const char* name, const char* guid, bool allowed = true)
{
    DiscoveredDevice device;
    device.kind = "Audio";
    device.driver = "WASAPI";
    device.direction = direction;
    device.streamType = streamType;
    device.device = name;
    device.guid = guid;
    device.channels = 2;
    device.sampleRate = 48000.0;
    device.allowedForNew = allowed;
    return device;
}

DiscoveredDevice midi (const char* direction, const char* streamType,
                       const char* name, const char* guid)
{
    DiscoveredDevice device;
    device.kind = "Midi";
    device.driver = "WinMM / WinRT MIDI";
    device.direction = direction;
    device.streamType = streamType;
    device.device = name;
    device.guid = guid;
    device.channels = 0;
    device.sampleRate = 0.0;
    return device;
}

} // namespace

int main()
{
    auto config = ServiceConfig::createDefault();
    std::vector<DiscoveredDevice> initial {
        audio ("In", "Record", "Line 1", "audio-in"),
        audio ("Out", "Playback", "Speakers", "audio-out"),
        midi ("In", "Input", "Keyboard", "midi-in")
    };

    juce::String error;
    const auto first = ConfigSynchronizer::synchronize (config, initial, error);
    require (error.isEmpty(), "Initial synchronization must succeed");
    require (first.added == 3 && config.clients.size() == 3,
             "Initial synchronization must add every discovered device");
    require (config.clients[0].enabled, "New clients must be enabled");
    require (config.clients[0].clientName.isNotEmpty(),
             "New clients must receive a default JACK name");
    require (config.clients[0].status == "available",
             "New clients must be marked available");

    auto& preserved = config.clients.front();
    preserved.clientName = "Custom_Client";
    preserved.enabled = false;
    preserved.channels = { 0 };
    preserved.sampleRate = 44100.0;
    preserved.wasapiMode = "exclusive";

    ServiceClient missingEnabled;
    missingEnabled.id = "svc-900";
    missingEnabled.clientName = "Removed_Client";
    missingEnabled.enabled = true;
    missingEnabled.kind = "Audio";
    missingEnabled.driver = "WASAPI";
    missingEnabled.direction = "Out";
    missingEnabled.streamType = "Playback";
    missingEnabled.device = "Gone";
    missingEnabled.guid = "gone";
    missingEnabled.channels = { 0, 1 };
    missingEnabled.sampleRate = 48000.0;
    config.clients.push_back (missingEnabled);

    ServiceClient missingDisabled = missingEnabled;
    missingDisabled.id = "svc-901";
    missingDisabled.clientName = "Disabled_Missing";
    missingDisabled.enabled = false;
    missingDisabled.guid = "disabled-gone";
    config.clients.push_back (missingDisabled);

    std::vector<DiscoveredDevice> next {
        audio ("In", "Record", "Line 1 (renamed)", "audio-in"),
        midi ("In", "Input", "Keyboard", "midi-in"),
        audio ("Out", "Playback", "New Cable Line 1", "audio-new", false),
        audio ("Out", "Playback", "Headphones", "audio-headphones")
    };
    next.push_back (next.back());

    config.autoRemoveLostDevices = true;
    const auto second = ConfigSynchronizer::synchronize (config, next, error);
    require (error.isEmpty(), "Incremental synchronization must succeed");
    require (second.added == 1 && second.removed == 2,
             "Synchronization must add eligible and remove missing enabled clients");
    require (config.clients.size() == 4,
             "Disabled missing clients and current clients must remain in the list");
    require (config.clients.front().clientName == "Custom_Client",
             "Custom client names must be preserved");
    require (! config.clients.front().enabled,
             "The enabled state must be preserved");
    require (config.clients.front().channels == std::vector<int> { 0 },
             "Custom channels must be preserved");
    require (config.clients.front().sampleRate == 44100.0,
             "Custom sample rate must be preserved");
    require (config.clients.front().wasapiMode == "exclusive",
             "Custom WASAPI mode must be preserved");
    require (config.clients.front().status == "available",
             "A restored client must be marked available");
    require (std::any_of (config.clients.begin(), config.clients.end(), [] (const auto& client)
                          { return client.clientName == "Disabled_Missing"; }),
             "Disabled missing clients must be retained");
    require (std::none_of (config.clients.begin(), config.clients.end(), [] (const auto& client)
                           { return client.clientName == "Removed_Client"; }),
             "Missing enabled clients must be removed");

    auto retainedConfig = ServiceConfig::createDefault();
    retainedConfig.clients.push_back (missingEnabled);
    const auto retained = ConfigSynchronizer::synchronize (retainedConfig, {}, error);
    require (error.isEmpty() && retained.removed == 0
                 && retainedConfig.clients.size() == 1
                 && retainedConfig.clients.front().status == "missing",
             "Lost clients must be retained and marked missing by default");

    auto statusJson = retainedConfig.toJson();
    const auto statusRoundTrip = ServiceConfig::fromJson (statusJson, error);
    require (error.isEmpty() && statusRoundTrip.isValid()
                 && statusRoundTrip.clients.size() == 1
                 && statusRoundTrip.clients.front().status == "missing",
             "The missing status must round-trip through JSON");

    auto restoredConfig = retainedConfig;
    const auto restored = ConfigSynchronizer::synchronize (
        restoredConfig, { audio ("Out", "Playback", "Gone Again", "gone") }, error);
    require (error.isEmpty() && restored.changed && restoredConfig.clients.size() == 1
                 && restoredConfig.clients.front().status == "available"
                 && restoredConfig.clients.front().clientName == "Removed_Client",
             "A restored device must become available and keep its client name");

    retainedConfig.autoRemoveLostDevices = true;
    const auto removed = ConfigSynchronizer::synchronize (retainedConfig, {}, error);
    require (error.isEmpty() && removed.removed == 1 && retainedConfig.clients.empty(),
             "The auto-remove option must physically remove lost enabled clients");

    const auto roundTripped = ServiceConfig::fromJson (config.toJson(), error);
    require (error.isEmpty() && roundTripped.isValid(),
             "Service configuration must round-trip through JSON");
    require (roundTripped.filters.inputDevicePattern == config.filters.inputDevicePattern,
             "Service filter patterns must round-trip through JSON");

    auto optionJson = config.toJson();
    if (auto* root = optionJson.getDynamicObject())
        root->setProperty ("autoRemoveLostDevices", true);
    const auto optionRoundTrip = ServiceConfig::fromJson (optionJson, error);
    require (error.isEmpty() && optionRoundTrip.isValid()
                 && optionRoundTrip.autoRemoveLostDevices,
             "The auto-remove setting must round-trip through JSON");

    auto invalidJson = config.toJson();
    if (auto* root = invalidJson.getDynamicObject())
        if (auto* filters = root->getProperty ("filters").getDynamicObject())
            filters->setProperty ("inputDevicePattern", "[");
    const auto invalid = ServiceConfig::fromJson (invalidJson, error);
    require (! invalid.isValid() && error.isNotEmpty(),
             "Invalid regular expressions must reject the configuration");

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("WinJACKNexus.AdapterService.ConfigSynchronizerTests")
                          .getChildFile ("adapter_service.json");
    file.deleteFile();
    file.getParentDirectory().deleteRecursively();
    require (config.saveToFile (file, error), "Service configuration must save atomically");
    const auto loaded = ServiceConfig::loadFromFile (file, error);
    require (error.isEmpty() && loaded.isValid(),
             "Atomically saved configuration must load successfully");
    file.getParentDirectory().deleteRecursively();

    std::cout << "ConfigSynchronizer tests passed\n";
    return 0;
}