#pragma once

#include <juce_graphics/juce_graphics.h>

namespace wjn::common
{

class ThemeAssetCache
{
public:
    juce::Image loadImage(const juce::File& file);
    void clear();

private:
    juce::HashMap<juce::String, juce::Image> images;
};

} // namespace wjn::common
