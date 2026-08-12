#pragma once

#include "TextCatalog.h"

#include <functional>

namespace wjn::common
{

class LocaleManager
{
public:
    bool load(const juce::File& commonFile, const juce::File& moduleFile, juce::String& error);
    const TextCatalog& catalog() const noexcept { return active; }
    juce::String text(const juce::String& key, const juce::String& fallback = {}) const;
    void setChangeCallback(std::function<void()> callback) { changeCallback = std::move(callback); }

private:
    TextCatalog active;
    std::function<void()> changeCallback;
};

} // namespace wjn::common
