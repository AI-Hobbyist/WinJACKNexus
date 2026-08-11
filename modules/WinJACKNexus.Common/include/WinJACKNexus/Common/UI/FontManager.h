#pragma once

#include <juce_graphics/juce_graphics.h>

namespace wjn::common
{

class FontManager
{
public:
    bool loadBuiltIns(const juce::File& lcdDirectory, juce::String& error);
    juce::Typeface::Ptr getTypeface(const juce::String& logicalId) const;
    juce::Font getFont(const juce::String& logicalId, float height, int style = juce::Font::plain) const;

private:
    juce::Typeface::Ptr zpix;
    juce::Typeface::Ptr dsDigi;
};

} // namespace wjn::common
