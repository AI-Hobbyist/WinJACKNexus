#include <WinJACKNexus/AdapterService/ServiceConfig.h>

#include <set>
#include <utility>

#include <WinJACKNexus/AdapterBackend/DeviceEnumerator.h>

namespace wjn::adapter::service
{
namespace
{

juce::var intVectorToJson (const std::vector<int>& values)
{
    juce::Array<juce::var> array;
    for (const auto value : values)
        array.add (value);
    return juce::var (array);
}

std::vector<int> jsonToIntVector (const juce::var& value)
{
    std::vector<int> values;
    if (const auto* array = value.getArray())
        for (const auto& item : *array)
            if (item.isInt() || item.isInt64() || item.isDouble())
                values.push_back (static_cast<int> (item));
    return values;
}

juce::String getString (const juce::DynamicObject& object, const char* name,
                        const juce::String& fallback = {})
{
    const auto value = object.getProperty (name);
    return value.isString() ? value.toString() : fallback;
}

bool getBool (const juce::DynamicObject& object, const char* name, bool fallback)
{
    const auto value = object.getProperty (name);
    return value.isBool() ? static_cast<bool> (value) : fallback;
}

double getDouble (const juce::DynamicObject& object, const char* name, double fallback)
{
    const auto value = object.getProperty (name);
    return value.isDouble() || value.isInt() || value.isInt64()
               ? static_cast<double> (value)
               : fallback;
}

int getInt (const juce::DynamicObject& object, const char* name, int fallback)
{
    const auto value = object.getProperty (name);
    return value.isInt() || value.isInt64() || value.isDouble()
               ? static_cast<int> (value)
               : fallback;
}

juce::var clientToJson (const ServiceClient& client)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("id", client.id);
    object->setProperty ("clientName", client.clientName);
    object->setProperty ("enabled", client.enabled);
    object->setProperty ("status", client.status);
    object->setProperty ("kind", client.kind);
    object->setProperty ("driver", client.driver);
    object->setProperty ("direction", client.direction);
    object->setProperty ("streamType", client.streamType);
    object->setProperty ("device", client.device);
    object->setProperty ("guid", client.guid);
    object->setProperty ("channels", intVectorToJson (client.channels));
    object->setProperty ("sampleRate", client.sampleRate);
    object->setProperty ("wasapiMode", client.wasapiMode);
    return juce::var (object.get());
}

} // namespace

juce::String ServiceClient::stableKey() const
{
    const auto identifier = guid.isNotEmpty() ? guid : device;
    if (identifier.isEmpty())
        return {};

    return wjn::adapter::backend::makeStableDeviceKey (kind, direction,
                                                        streamType, identifier);
}

ServiceConfig ServiceConfig::invalidDefault()
{
    auto config = createDefault();
    config.clients.clear();
    config.valid = false;
    return config;
}

juce::var ServiceConfig::toJson() const
{
    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());
    root->setProperty ("format", format);
    root->setProperty ("version", version);
    root->setProperty ("created", created);
    root->setProperty ("updated", updated);
    root->setProperty ("autoRemoveLostDevices", autoRemoveLostDevices);

    auto filter = juce::DynamicObject::Ptr (new juce::DynamicObject());
    filter->setProperty ("virtualDevicePattern", filters.virtualDevicePattern);
    filter->setProperty ("inputDevicePattern", filters.inputDevicePattern);
    filter->setProperty ("outputDevicePattern", filters.outputDevicePattern);
    root->setProperty ("filters", juce::var (filter.get()));

    juce::Array<juce::var> clientArray;
    for (const auto& client : clients)
        clientArray.add (clientToJson (client));
    root->setProperty ("clients", juce::var (clientArray));
    return juce::var (root.get());
}

ServiceConfig ServiceConfig::fromJson (const juce::var& json, juce::String& error)
{
    error.clear();
    if (! json.isObject())
    {
        error = "Service configuration root must be an object";
        return invalidDefault();
    }

    const auto* root = json.getDynamicObject();
    if (root == nullptr || getString (*root, "format") != "WinJACKNexus.AdapterService"
        || getInt (*root, "version", 0) != currentVersion)
    {
        error = "Unsupported AdapterService configuration format or version";
        return invalidDefault();
    }

    const auto clientsValue = root->getProperty ("clients");
    const auto* clientArray = clientsValue.getArray();
    if (clientArray == nullptr)
    {
        error = "Service configuration clients must be an array";
        return invalidDefault();
    }

    auto config = createDefault();
    config.format = getString (*root, "format", config.format);
    config.version = getInt (*root, "version", config.version);
    config.created = getString (*root, "created", config.created);
    config.updated = getString (*root, "updated", config.updated);
    config.autoRemoveLostDevices = getBool (*root, "autoRemoveLostDevices",
                                            config.autoRemoveLostDevices);
    config.clients.clear();

    if (const auto* filter = root->getProperty ("filters").getDynamicObject())
    {
        config.filters.virtualDevicePattern = getString (*filter, "virtualDevicePattern",
                                                         config.filters.virtualDevicePattern);
        config.filters.inputDevicePattern = getString (*filter, "inputDevicePattern",
                                                       config.filters.inputDevicePattern);
        config.filters.outputDevicePattern = getString (*filter, "outputDevicePattern",
                                                        config.filters.outputDevicePattern);
    }

    for (const auto& clientValue : *clientArray)
    {
        const auto* item = clientValue.getDynamicObject();
        if (item == nullptr)
        {
            error = "Service configuration contains a non-object client";
            return invalidDefault();
        }

        ServiceClient client;
        client.id = getString (*item, "id");
        client.clientName = getString (*item, "clientName");
        client.enabled = getBool (*item, "enabled", client.enabled);
        client.status = getString (*item, "status", client.status);
        client.kind = getString (*item, "kind", client.kind);
        client.driver = getString (*item, "driver", client.driver);
        client.direction = getString (*item, "direction", client.direction);
        client.streamType = getString (*item, "streamType", client.streamType);
        client.device = getString (*item, "device");
        client.guid = getString (*item, "guid", client.device);
        client.channels = jsonToIntVector (item->getProperty ("channels"));
        client.sampleRate = getDouble (*item, "sampleRate", client.sampleRate);
        client.wasapiMode = getString (*item, "wasapiMode", client.wasapiMode);
        config.clients.push_back (std::move (client));
    }

    if (! config.validate (error))
        return invalidDefault();

    return config;
}

ServiceConfig ServiceConfig::loadFromFile (const juce::File& file, juce::String& error)
{
    error.clear();
    if (file == juce::File() || ! file.existsAsFile())
    {
        error = "Service configuration file does not exist";
        return invalidDefault();
    }

    return fromJson (juce::JSON::parse (file), error);
}

ServiceConfig ServiceConfig::createDefault()
{
    ServiceConfig config;
    const auto timestamp = juce::Time::getCurrentTime().toISO8601 (true);
    config.created = timestamp;
    config.updated = timestamp;
    return config;
}

bool ServiceConfig::validate (juce::String& error) const
{
    error.clear();
    if (! valid)
    {
        error = "AdapterService configuration is invalid";
        return false;
    }

    if (format != "WinJACKNexus.AdapterService" || version != currentVersion)
    {
        error = "Unsupported AdapterService configuration format or version";
        return false;
    }

    if (created.isEmpty() || updated.isEmpty())
    {
        error = "Service configuration timestamps are required";
        return false;
    }

    if (! wjn::adapter::backend::areAudioFilterPatternsValid (filters))
    {
        error = "Service configuration contains an invalid audio filter pattern";
        return false;
    }

    std::set<std::string> ids;
    for (const auto& client : clients)
    {
        if (client.id.isEmpty() || client.clientName.isEmpty()
            || client.device.isEmpty() || client.guid.isEmpty())
        {
            error = "Service client id, name, device and guid are required";
            return false;
        }

        if (! client.status.equalsIgnoreCase ("available")
            && ! client.status.equalsIgnoreCase ("missing"))
        {
            error = "Service client status must be available or missing";
            return false;
        }

        if (! ids.insert (client.id.toStdString()).second)
        {
            error = "Service client ids must be unique";
            return false;
        }

        const auto isAudio = client.kind.equalsIgnoreCase ("Audio");
        const auto isMidi = client.kind.equalsIgnoreCase ("Midi");
        if (! isAudio && ! isMidi)
        {
            error = "Service client kind must be Audio or Midi";
            return false;
        }

        if (! client.direction.equalsIgnoreCase ("In")
            && ! client.direction.equalsIgnoreCase ("Out"))
        {
            error = "Service client direction must be In or Out";
            return false;
        }

        const auto expectedStream = isMidi
                                        ? (client.direction.equalsIgnoreCase ("In") ? "Input" : "Output")
                                        : (client.direction.equalsIgnoreCase ("In") ? "Record" : "Playback");
        if (! client.streamType.equalsIgnoreCase (expectedStream))
        {
            error = "Service client stream type does not match its kind and direction";
            return false;
        }

        if (isAudio && ! client.driver.equalsIgnoreCase ("WASAPI"))
        {
            error = "Audio service clients must use WASAPI";
            return false;
        }

        if (isMidi && ! client.driver.equalsIgnoreCase ("WinMM / WinRT MIDI"))
        {
            error = "MIDI service clients must use WinMM / WinRT MIDI";
            return false;
        }

        if (! client.wasapiMode.equalsIgnoreCase ("shared")
            && ! client.wasapiMode.equalsIgnoreCase ("exclusive"))
        {
            error = "WASAPI mode must be shared or exclusive";
            return false;
        }

        if (client.sampleRate < 0.0)
        {
            error = "Service client sample rate cannot be negative";
            return false;
        }

        if (isMidi && ! client.channels.empty())
        {
            error = "MIDI service clients must not contain audio channels";
            return false;
        }

        for (const auto channel : client.channels)
            if (channel < 0)
            {
                error = "Service client channel indexes cannot be negative";
                return false;
            }
    }

    return true;
}

bool ServiceConfig::saveToFile (const juce::File& file, juce::String& error) const
{
    error.clear();
    if (file == juce::File())
    {
        error = "No AdapterService configuration file was selected";
        return false;
    }

    if (! validate (error))
        return false;

    const auto parent = file.getParentDirectory();
    if (! parent.exists() && ! parent.createDirectory())
    {
        error = "Unable to create the AdapterService configuration directory";
        return false;
    }

    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (juce::JSON::toString (toJson(), false),
                                               false, false, "\n"))
    {
        error = "Unable to write the temporary AdapterService configuration file";
        return false;
    }

    if (! temporary.overwriteTargetFileWithTemporary())
    {
        error = "Unable to replace the AdapterService configuration file";
        return false;
    }

    return true;
}

} // namespace wjn::adapter::service