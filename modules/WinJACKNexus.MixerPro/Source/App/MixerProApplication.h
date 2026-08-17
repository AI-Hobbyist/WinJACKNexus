#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <WinJACKNexus/Common/Localization/LocaleManager.h>
#include <WinJACKNexus/Common/UI/NexusLookAndFeel.h>
#include <WinJACKNexus/Common/UI/ThemeContext.h>

#include <memory>

#include "Audio/CommonJackMixerRuntime.h"

namespace mixerpro
{

class MainWindow;

class MixerProApplication final : public juce::JUCEApplication
{
public:
    MixerProApplication() = default;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted(const juce::String& commandLine) override;

private:
    wjn::common::ThemeContext theme;
    wjn::common::NexusLookAndFeel lookAndFeel;
    wjn::common::LocaleManager localeManager;
    std::unique_ptr<CommonJackMixerRuntime> audioRuntime;
    std::unique_ptr<MainWindow> mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerProApplication)
};

} // namespace mixerpro
