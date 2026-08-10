#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::common
{

class NexusLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    NexusLookAndFeel();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NexusLookAndFeel)
};

} // namespace wjn::common
