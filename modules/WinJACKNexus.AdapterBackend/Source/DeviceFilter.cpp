#include <WinJACKNexus/AdapterBackend/DeviceFilter.h>

#include <regex>

namespace wjn::adapter::backend
{
namespace
{

bool matchesPattern (const juce::String& value, const juce::String& pattern)
{
    if (pattern.isEmpty())
        return false;

    try
    {
        const std::regex expression (pattern.toStdString(), std::regex::icase);
        return std::regex_search (value.toStdString(), expression);
    }
    catch (const std::regex_error&)
    {
        return false;
    }
}

bool isPatternValid (const juce::String& pattern)
{
    if (pattern.isEmpty())
        return true;

    try
    {
        std::regex (pattern.toStdString(), std::regex::icase);
        return true;
    }
    catch (const std::regex_error&)
    {
        return false;
    }
}

} // namespace

bool areAudioFilterPatternsValid (const AudioDeviceFilterSettings& settings)
{
    return isPatternValid (settings.virtualDevicePattern)
        && isPatternValid (settings.inputDevicePattern)
        && isPatternValid (settings.outputDevicePattern);
}

bool isAudioDeviceAllowed (const juce::String& deviceName, bool wantsInput,
                           const AudioDeviceFilterSettings& settings)
{
    if (! matchesPattern (deviceName, settings.virtualDevicePattern))
        return true;

    const auto& directionPattern = wantsInput ? settings.inputDevicePattern
                                              : settings.outputDevicePattern;
    return directionPattern.isEmpty() || matchesPattern (deviceName, directionPattern);
}

} // namespace wjn::adapter::backend
