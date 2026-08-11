#pragma once

#include "Theme.h"

#include <juce_core/juce_core.h>

#include <map>

namespace wjn::common
{

class ThemeContext
{
public:
    ThemeContext();

    bool applyJson(const juce::var& json) noexcept;
    juce::Colour colour(const juce::String& token) const noexcept;
    float metric(const juce::String& token, float fallback) const noexcept;
    juce::String controlStyle(const juce::String& control, const juce::String& fallback) const;

private:
    std::map<juce::String, juce::Colour> colours;
    std::map<juce::String, float> metrics;
    std::map<juce::String, juce::String> styles;
};

} // namespace wjn::common
