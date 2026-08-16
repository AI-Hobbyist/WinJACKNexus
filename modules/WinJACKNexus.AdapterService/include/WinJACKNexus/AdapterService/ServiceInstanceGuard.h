#pragma once

#include <juce_core/juce_core.h>

namespace wjn::adapter::service
{

class ServiceInstanceGuard final
{
public:
    ServiceInstanceGuard() = default;
    ~ServiceInstanceGuard();

    bool acquire (const juce::String& mutexName);
    bool isOwner() const noexcept { return owner; }

private:
#if JUCE_WINDOWS
    void* mutexHandle = nullptr;
#endif
    bool owner = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ServiceInstanceGuard)
};

} // namespace wjn::adapter::service