#include "TextCatalog.h"

namespace wjn::common
{

bool TextCatalog::load(const juce::File& file, juce::String& error)
{
    error.clear();
    loaded = false;
    if (! file.existsAsFile())
    {
        error = "语言文件不存在";
        return false;
    }
    const auto json = juce::JSON::parse(file);
    if (! json.isObject() || json.getProperty("schema", {}).toString() != "WinJACKNexus.Language"
        || static_cast<int>(json.getProperty("version", 0)) != 1
        || json.getProperty("locale", {}).toString().isEmpty())
    {
        error = "语言文件格式或版本无效";
        return false;
    }

    if (const auto* object = json.getDynamicObject())
    {
        if (const auto* stringObject = object->getProperty("strings").getDynamicObject())
            for (const auto& property : stringObject->getProperties())
                if (property.value.isString())
                    strings[property.name.toString()] = property.value.toString();
        if (const auto* templateObject = object->getProperty("templates").getDynamicObject())
            for (const auto& property : templateObject->getProperties())
                if (property.value.isString())
                    templates[property.name.toString()] = property.value.toString();
    }
    loaded = true;
    return true;
}

void TextCatalog::setFallback(TextCatalog fallback)
{
    fallbackCatalog = std::make_unique<TextCatalog>(std::move(fallback));
}

juce::String TextCatalog::text(const juce::String& key, const juce::String& fallback) const
{
    if (const auto it = strings.find(key); it != strings.end())
        return it->second;
    if (fallbackCatalog != nullptr)
        return fallbackCatalog->text(key, fallback);
    return fallback;
}

juce::String TextCatalog::format(const juce::String& key, const juce::NamedValueSet& values,
                                 const juce::String& fallback) const
{
    auto result = fallback;
    if (const auto it = templates.find(key); it != templates.end())
        result = it->second;
    else if (fallbackCatalog != nullptr)
        result = fallbackCatalog->format(key, values, fallback);
    for (int index = 0; index < values.size(); ++index)
    {
        const auto& value = values.getName(index);
        result = result.replace("{" + value.toString() + "}", values.getValueAt(index).toString());
    }
    return result;
}

} // namespace wjn::common
