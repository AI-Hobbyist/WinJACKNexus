#pragma once

#include <juce_core/juce_core.h>

#include <map>

namespace wjn::common
{

class TextCatalog
{
public:
    TextCatalog() = default;
    TextCatalog(const TextCatalog&) = delete;
    TextCatalog& operator=(const TextCatalog&) = delete;
    TextCatalog(TextCatalog&&) noexcept = default;
    TextCatalog& operator=(TextCatalog&&) noexcept = default;
    bool load(const juce::File& file, juce::String& error);
    void setFallback(TextCatalog fallback);
    juce::String text(const juce::String& key, const juce::String& fallback = {}) const;
    juce::String format(const juce::String& key, const juce::NamedValueSet& values,
                        const juce::String& fallback = {}) const;
    bool isLoaded() const noexcept { return loaded; }

private:
    std::map<juce::String, juce::String> strings;
    std::map<juce::String, juce::String> templates;
    std::unique_ptr<TextCatalog> fallbackCatalog;
    bool loaded = false;
};

} // namespace wjn::common
