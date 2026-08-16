#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::adapter::service
{

class ServiceProgressWindow final : public juce::DocumentWindow
{
public:
    ServiceProgressWindow (const juce::String& title, const juce::String& message);

    void setMessage (const juce::String& message);
    void setProgress (double progress);
    void closeButtonPressed() override;

private:
    class Content;
    Content* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ServiceProgressWindow)
};

} // namespace wjn::adapter::service