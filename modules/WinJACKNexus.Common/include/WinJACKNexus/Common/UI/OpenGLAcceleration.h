#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace juce
{
class Component;
}

namespace wjn::common
{

class OpenGLAcceleration final
{
public:
    OpenGLAcceleration();
    ~OpenGLAcceleration();

    void setEnabled(bool shouldBeEnabled) noexcept;
    void update(juce::Component& target);
    void detach() noexcept;
    bool isEnabled() const noexcept;
    bool isRequested() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpenGLAcceleration)
};

} // namespace wjn::common
