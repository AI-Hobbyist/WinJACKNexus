#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace mixerpro
{

class CommonJackMixerRuntime;

}

namespace wjn::common
{
class LocaleManager;
class ThemeContext;
}

namespace mixerpro
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(juce::String name,
               CommonJackMixerRuntime& audioRuntime,
               const wjn::common::ThemeContext& theme,
               wjn::common::LocaleManager& localeManager);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace mixerpro
