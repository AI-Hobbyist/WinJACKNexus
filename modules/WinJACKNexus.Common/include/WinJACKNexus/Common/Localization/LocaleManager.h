#pragma once

#include "TextCatalog.h"

namespace wjn::common
{

class LocaleManager
{
public:
    bool load(const juce::File& commonFile, const juce::File& moduleFile, juce::String& error);
    const TextCatalog& catalog() const noexcept { return active; }
    juce::String text(const juce::String& key, const juce::String& fallback = {}) const;

private:
    TextCatalog active;
};

} // namespace wjn::common
