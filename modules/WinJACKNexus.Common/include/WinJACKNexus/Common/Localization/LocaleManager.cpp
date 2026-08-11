#include "LocaleManager.h"

namespace wjn::common
{

bool LocaleManager::load(const juce::File& commonFile, const juce::File& moduleFile, juce::String& error)
{
    TextCatalog common;
    if (!common.load(commonFile, error))
        return false;

    TextCatalog module;
    if (moduleFile.existsAsFile())
    {
        if (!module.load(moduleFile, error))
            return false;
        module.setFallback(std::move(common));
        active = std::move(module);
    }
    else
    {
        active = std::move(common);
    }
    return true;
}

juce::String LocaleManager::text(const juce::String& key, const juce::String& fallback) const
{
    return active.text(key, fallback);
}

} // namespace wjn::common
