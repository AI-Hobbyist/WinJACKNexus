#include <WinJACKNexus/Common/Serialization/AdapterConfig.h>

#include <cstdlib>
#include <iostream>

namespace
{

void require(bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "AdapterConfig test failure: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    auto config = wjn::common::AdapterConfig::createDefault();
    require(config.isValid(), "Default configuration must be valid");
    require(config.clients.size() == 1, "Default configuration must contain one output client");
    require(config.clients.front().direction == "Out", "Default client must be an output");
    require(config.clients.front().kind == "Audio", "Default client must be audio");
    require(config.clients.front().channels.size() == 2,
            "Default client must start with two channels");

    wjn::common::ClientMapping midi;
    midi.id = "cl-002";
    midi.clientName = "WDM_MidiIn_01";
    midi.kind = "Midi";
    midi.driver = "MME";
    midi.direction = "In";
    midi.streamType = "Input";
    midi.device = "Keyboard";
    midi.guid = "keyboard-id";
    midi.channels.clear();
    midi.sampleRate = 0.0;
    midi.paused = false;
    config.clients.push_back(midi);

    const auto roundTripped = wjn::common::AdapterConfig::fromJson(config.toJson());
    require(roundTripped.isValid(), "JSON round trip must remain valid");
    require(roundTripped.clients.size() == 2, "JSON round trip must preserve client count");
    require(roundTripped.clients[1].clientName == midi.clientName,
            "JSON round trip must preserve client name");
    require(roundTripped.clients[1].kind == "Midi" && roundTripped.clients[1].direction == "In",
            "JSON round trip must preserve MIDI mapping");
    require(roundTripped.clients[1].channels.empty(),
            "MIDI mapping must keep an empty channel list");
    require(roundTripped.clients[1].paused == false,
            "JSON round trip must preserve paused state");

        const auto serialized = config.toJson();
        if (const auto* root = serialized.getDynamicObject())
                if (const auto* clients = root->getProperty("clients").getArray())
                        if (const auto* client = clients->getFirst().getDynamicObject())
                                require(client->getProperty("resample").isVoid(),
                                                "JSON must not persist runtime resampling state");

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("WinJACKNexus.AdapterConfigTests.adapter");
    file.deleteFile();
    require(config.saveToFile(file), "Configuration must save to an adapter file");
    const auto loaded = wjn::common::AdapterConfig::loadFromFile(file);
    require(loaded.isValid(), "Saved configuration must load as valid");
    require(loaded.clients.size() == config.clients.size(),
            "Saved configuration must preserve client count");
    require(loaded.clients[0].device == config.clients[0].device,
            "Saved configuration must preserve device identifier");
    file.deleteFile();

    auto legacyJson = config.toJson();
    if (auto* legacyRoot = legacyJson.getDynamicObject())
        if (auto* legacyClients = legacyRoot->getProperty("clients").getArray())
            if (auto* legacyClient = legacyClients->getFirst().getDynamicObject())
            {
                legacyClient->removeProperty("device");
                legacyClient->removeProperty("streamType");
            }
    const auto legacy = wjn::common::AdapterConfig::fromJson(legacyJson);
    require(legacy.clients.size() == config.clients.size(),
            "Legacy guid-only mappings must remain loadable");
    require(legacy.clients[0].device == legacy.clients[0].guid,
            "Legacy guid-only mapping must recover its device identifier");
    require(legacy.clients[0].streamType == "Playback",
            "Legacy output mapping must infer Playback stream type");

    const auto invalid = wjn::common::AdapterConfig::fromJson(juce::var("not an object"));
    require(! invalid.isValid(), "Invalid JSON must report invalid configuration");
    require(invalid.clients.size() == 1, "Invalid JSON must fall back to the default client");

    std::cout << "AdapterConfig tests passed\n";
    return 0;
}
