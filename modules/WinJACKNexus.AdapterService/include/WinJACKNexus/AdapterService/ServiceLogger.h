#pragma once

#include <juce_core/juce_core.h>

namespace wjn::adapter::service
{

class ServiceLogger final
{
public:
    void setQuiet (bool shouldBeQuiet) noexcept { quiet = shouldBeQuiet; }

    void info (const juce::String& message) const;
    void error (const juce::String& message) const;

private:
    void write (const char* prefix, const juce::String& message) const;

    bool quiet = false;
};

} // namespace wjn::adapter::service