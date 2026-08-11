#pragma once

#include "ThemeContext.h"

namespace wjn::common
{

class ThemePackage
{
public:
    bool load(const juce::File& file, ThemeContext& context, juce::String& error) const;
    static bool validatePath(const juce::String& path) noexcept;
};

} // namespace wjn::common
