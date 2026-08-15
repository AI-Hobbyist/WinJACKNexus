#include "OpenGLAcceleration.h"

#include <juce_opengl/juce_opengl.h>

namespace wjn::common
{

struct OpenGLAcceleration::Impl
{
    juce::OpenGLContext context;
    bool requested = false;
    bool attached = false;
};

OpenGLAcceleration::OpenGLAcceleration()
    : impl (std::make_unique<Impl>())
{
}

OpenGLAcceleration::~OpenGLAcceleration()
{
    detach();
}

void OpenGLAcceleration::setEnabled(bool shouldBeEnabled) noexcept
{
    impl->requested = shouldBeEnabled;
    if (! impl->requested)
        detach();
}

void OpenGLAcceleration::update(juce::Component& target)
{
    if (! impl->requested || impl->attached || target.getPeer() == nullptr)
        return;

    impl->context.setContinuousRepainting(false);
    impl->context.attachTo(target);
    impl->attached = impl->context.isAttached();
}

void OpenGLAcceleration::detach() noexcept
{
    if (impl == nullptr)
        return;

    if (impl->context.isAttached())
        impl->context.detach();
    impl->attached = false;
}

bool OpenGLAcceleration::isEnabled() const noexcept
{
    return impl != nullptr && impl->attached && impl->context.isAttached();
}

bool OpenGLAcceleration::isRequested() const noexcept
{
    return impl != nullptr && impl->requested;
}

} // namespace wjn::common
