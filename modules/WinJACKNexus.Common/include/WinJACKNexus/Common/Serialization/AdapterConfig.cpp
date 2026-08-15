#include "AdapterConfig.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>

namespace wjn::common
{
namespace
{

juce::var stringArrayToJson(const std::vector<int>& values)
{
    juce::Array<juce::var> array;
    for (const auto value : values)
        array.add(value);
    return juce::var(array);
}

std::vector<int> jsonToIntVector(const juce::var& value)
{
    std::vector<int> values;
    if (const auto* array = value.getArray())
        for (const auto& item : *array)
            if (item.isInt() || item.isInt64() || item.isDouble())
                values.push_back(static_cast<int>(item));
    return values;
}

juce::String getString(const juce::DynamicObject& object, const char* name,
                       const juce::String& fallback = {})
{
    const auto value = object.getProperty(name);
    return value.isString() ? value.toString() : fallback;
}

bool getBool(const juce::DynamicObject& object, const char* name, bool fallback)
{
    const auto value = object.getProperty(name);
    return value.isBool() ? static_cast<bool>(value) : fallback;
}

double getDouble(const juce::DynamicObject& object, const char* name, double fallback)
{
    const auto value = object.getProperty(name);
    return value.isDouble() || value.isInt() || value.isInt64()
               ? static_cast<double>(value)
               : fallback;
}

int getInt(const juce::DynamicObject& object, const char* name, int fallback)
{
    const auto value = object.getProperty(name);
    return value.isInt() || value.isInt64() || value.isDouble()
               ? static_cast<int>(value)
               : fallback;
}

} // namespace

AdapterConfig AdapterConfig::invalidDefault()
{
    auto config = createDefault();
    config.valid = false;
    return config;
}

juce::var AdapterConfig::toJson() const
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("format", format);
    root->setProperty("version", version);
    root->setProperty("created", created);

    juce::Array<juce::var> clientArray;
    for (const auto& client : clients)
    {
        juce::DynamicObject::Ptr item = new juce::DynamicObject();
        item->setProperty("id", client.id);
        item->setProperty("clientName", client.clientName);
        item->setProperty("kind", client.kind);
        item->setProperty("driver", client.driver);
        item->setProperty("direction", client.direction);
        item->setProperty("streamType", client.streamType);
        item->setProperty("device", client.device);
        item->setProperty("guid", client.guid);
        item->setProperty("channels", stringArrayToJson(client.channels));
        item->setProperty("sampleRate", client.sampleRate);
        item->setProperty("paused", client.paused);
        item->setProperty("wasapiMode", client.wasapiMode);
        clientArray.add(juce::var(item.get()));
    }

    root->setProperty("clients", juce::var(clientArray));
    return juce::var(root.get());
}

AdapterConfig AdapterConfig::fromJson(const juce::var& json)
{
    if (! json.isObject())
        return invalidDefault();

    const auto* root = json.getDynamicObject();
    if (root == nullptr || getString(*root, "format") != "WinJACKNexus.Adapter"
        || getInt(*root, "version", 0) != 1)
        return invalidDefault();

    const auto clientsValue = root->getProperty("clients");
    const auto* clientArray = clientsValue.getArray();
    if (clientArray == nullptr)
        return invalidDefault();

    AdapterConfig config;
    config.format = getString(*root, "format", config.format);
    config.version = getInt(*root, "version", config.version);
    config.created = getString(*root, "created", juce::Time::getCurrentTime().toISO8601(true));

    for (const auto& clientValue : *clientArray)
    {
        const auto* item = clientValue.getDynamicObject();
        if (item == nullptr)
            continue;

        ClientMapping client;
        client.id = getString(*item, "id");
        client.clientName = getString(*item, "clientName");
        client.kind = getString(*item, "kind", client.kind);
        client.driver = getString(*item, "driver", client.driver);
        client.direction = getString(*item, "direction", client.direction);
        const auto defaultStreamType = client.kind.equalsIgnoreCase ("Midi")
                                           ? (client.direction.equalsIgnoreCase ("In") ? "Input" : "Output")
                                           : (client.direction.equalsIgnoreCase ("In") ? "Record" : "Playback");
        client.streamType = getString(*item, "streamType", defaultStreamType);
        client.device = getString(*item, "device");
        client.guid = getString(*item, "guid", client.device);
        if (client.device.isEmpty())
            client.device = client.guid;
        client.channels = jsonToIntVector(item->getProperty("channels"));
        client.sampleRate = getDouble(*item, "sampleRate", client.sampleRate);
        client.paused = getBool(*item, "paused", client.paused);
        client.wasapiMode = getString(*item, "wasapiMode", client.wasapiMode);

        if (client.id.isEmpty())
            client.id = "cl-" + juce::String(static_cast<int>(config.clients.size()) + 1).paddedLeft('0', 3);
        if (client.clientName.isEmpty() || client.device.isEmpty())
            continue;
        if (client.channels.empty() && ! client.kind.equalsIgnoreCase ("Midi"))
            client.channels = { 0, 1 };
        config.clients.push_back(std::move(client));
    }

    return config;
}

bool AdapterConfig::saveToFile(const juce::File& file) const
{
    if (file == juce::File())
        return false;

    const auto parent = file.getParentDirectory();
    if (! parent.exists() && ! parent.createDirectory())
        return false;

    return file.replaceWithText(juce::JSON::toString(toJson(), false), false, false, "\n");
}

AdapterConfig AdapterConfig::loadFromFile(const juce::File& file)
{
    if (file == juce::File() || ! file.existsAsFile())
        return invalidDefault();

    return fromJson(juce::JSON::parse(file));
}

AdapterConfig AdapterConfig::createDefault()
{
    AdapterConfig config;
    config.created = juce::Time::getCurrentTime().toISO8601(true);

    ClientMapping output;
    output.id = "cl-001";
    output.clientName = "WDM_AudioOut_01";
    output.kind = "Audio";
    output.driver = "WASAPI";
    output.direction = "Out";
    output.streamType = "Playback";
    output.wasapiMode = "shared";
    output.channels = { 0, 1 };
    output.paused = true;

#if JUCE_WINDOWS
    std::unique_ptr<juce::AudioIODeviceType> deviceType(
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
    if (deviceType != nullptr)
    {
        deviceType->scanForDevices();
        const auto deviceNames = deviceType->getDeviceNames(false);
        const auto defaultIndex = deviceType->getDefaultDeviceIndex(false);
        if (defaultIndex >= 0 && defaultIndex < deviceNames.size())
        {
            output.device = deviceNames[defaultIndex];
            output.guid = output.device;
        }
    }
#endif

    if (output.device.isEmpty())
    {
        output.device = "Default Playback Device";
        output.guid = output.device;
        output.channels = { 0, 1 };
    }

    config.clients.push_back(std::move(output));
    return config;
}

} // namespace wjn::common
