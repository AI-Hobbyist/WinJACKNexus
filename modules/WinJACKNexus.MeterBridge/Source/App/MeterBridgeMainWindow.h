#pragma once

#include <WinJACKNexus/Common/Localization/TextCatalog.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace wjn::meterbridge
{

class MeterBridgeMainWindow final : public juce::DocumentWindow
{
public:
    MeterBridgeMainWindow (const juce::String& name, const wjn::common::TextCatalog& locale);
    ~MeterBridgeMainWindow() override;

    void closeButtonPressed() override;

private:
    const wjn::common::TextCatalog& locale;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBridgeMainWindow)
};

} // namespace wjn::meterbridge
