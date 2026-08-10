#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace wjn::adapter
{

class AdapterTrayIcon;

/** 主窗口与系统托盘入口。 */
class AdapterMainWindow final : public juce::DocumentWindow
{
public:
    explicit AdapterMainWindow (const juce::String& name);
    ~AdapterMainWindow() override;

    // DocumentWindow
    void closeButtonPressed() override;

private:
    std::unique_ptr<AdapterTrayIcon> trayIcon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdapterMainWindow)
};

} // namespace wjn::adapter
