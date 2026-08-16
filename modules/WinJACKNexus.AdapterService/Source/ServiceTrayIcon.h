#pragma once

#if JUCE_WINDOWS
 #ifdef small
  #undef small
 #endif
#endif

#include <juce_gui_extra/juce_gui_extra.h>

namespace wjn::adapter::service
{

class ServiceApplication;

class ServiceTrayIcon final : public juce::SystemTrayIconComponent
{
public:
    explicit ServiceTrayIcon (ServiceApplication& application);

    void mouseDown (const juce::MouseEvent& event) override;

private:
    ServiceApplication& application;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ServiceTrayIcon)
};

} // namespace wjn::adapter::service