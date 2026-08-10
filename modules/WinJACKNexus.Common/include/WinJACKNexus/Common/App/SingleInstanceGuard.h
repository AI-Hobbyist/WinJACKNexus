#pragma once

#include <juce_core/juce_core.h>

namespace wjn::common
{

class SingleInstanceGuard final
{
public:
    SingleInstanceGuard() = default;
    ~SingleInstanceGuard();

    bool acquire (const juce::String& mutexName, const juce::String& windowTitle);
    bool isOwner() const noexcept { return owner; }

    static void bringExistingWindowToFront (const juce::String& windowTitle);

private:
#if JUCE_WINDOWS
    void* mutexHandle = nullptr;
#endif
    bool owner = false;
    juce::String windowTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SingleInstanceGuard)
};

} // namespace wjn::common
