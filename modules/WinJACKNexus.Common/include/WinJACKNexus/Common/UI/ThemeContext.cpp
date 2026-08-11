#include "ThemeContext.h"

namespace wjn::common
{
namespace
{
juce::Colour parseColour(const juce::var& value, juce::Colour fallback) noexcept
{
    if (!value.isString())
        return fallback;
    const auto text = value.toString();
    if (!text.startsWithChar('#'))
        return fallback;
    const auto hex = text.substring(1);
    if (hex.length() != 6 && hex.length() != 8)
        return fallback;
    return juce::Colour::fromString(text);
}
}

ThemeContext::ThemeContext()
{
    colours["darkCanvas"] = theme::darkCanvas;
    colours["rackPanel"] = theme::rackPanel;
    colours["primaryText"] = theme::primaryText;
    colours["secondaryText"] = theme::secondaryText;
    colours["border"] = theme::border;
    colours["accent"] = theme::activeTab;
    colours["meterNormal"] = theme::ledActiveGreen;
    colours["meterWarning"] = theme::ledWarning;
    colours["meterClipping"] = theme::ledClipping;
    metrics["panelRadius"] = 4.0f;
    metrics["borderWidth"] = 1.0f;
    metrics["controlHeight"] = 28.0f;
    metrics["controlPadding"] = 8.0f;
    metrics["focusRingWidth"] = 2.0f;
    styles["default"] = "flat";
    styles["meter"] = "flat-segmented";
    styles["channelStrip"] = "flat-panel";
}

bool ThemeContext::applyJson(const juce::var& json) noexcept
{
    if (!json.isObject())
        return false;
    const auto schema = json.getProperty("schema", {}).toString();
    const auto version = static_cast<int>(json.getProperty("version", 0));
    if (schema != "WinJACKNexus.Theme" || version != 1)
        return false;

    if (const auto* object = json.getDynamicObject())
    {
        if (const auto* colourObject = object->getProperty("colors").getDynamicObject())
            for (const auto& property : colourObject->getProperties())
                colours[property.name.toString()] = parseColour(property.value, colour(property.name.toString()));
        if (const auto* metricObject = object->getProperty("metrics").getDynamicObject())
            for (const auto& property : metricObject->getProperties())
                if (property.value.isDouble() || property.value.isInt())
                    metrics[property.name.toString()] = juce::jlimit(0.0f, 256.0f, static_cast<float>(property.value));
        if (const auto* controlObject = object->getProperty("controls").getDynamicObject())
        {
            styles["default"] = controlObject->getProperty("defaultStyle").toString();
            for (const auto& control : { juce::String("meter"), juce::String("channelStrip") })
                if (const auto* controlStyleObject = controlObject->getProperty(control).getDynamicObject())
                    styles[control] = controlStyleObject->getProperty("style").toString();
        }
    }
    return true;
}

juce::Colour ThemeContext::colour(const juce::String& token) const noexcept
{
    if (const auto it = colours.find(token); it != colours.end())
        return it->second;
    return {};
}

float ThemeContext::metric(const juce::String& token, float fallback) const noexcept
{
    if (const auto it = metrics.find(token); it != metrics.end())
        return it->second;
    return fallback;
}

juce::String ThemeContext::controlStyle(const juce::String& control, const juce::String& fallback) const
{
    if (const auto it = styles.find(control); it != styles.end() && it->second.isNotEmpty())
        return it->second;
    return fallback;
}

} // namespace wjn::common
