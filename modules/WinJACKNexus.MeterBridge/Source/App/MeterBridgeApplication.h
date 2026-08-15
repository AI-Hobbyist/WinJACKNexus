#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <WinJACKNexus/Common/App/SingleInstanceGuard.h>
#include <WinJACKNexus/Common/Localization/LocaleManager.h>
#include <WinJACKNexus/Common/UI/FontManager.h>
#include <WinJACKNexus/Common/UI/NexusLookAndFeel.h>

namespace wjn::meterbridge
{

class MeterBridgeMainWindow;

class MeterBridgeApplication final : public juce::JUCEApplication
{
public:
    MeterBridgeApplication() = default;
    ~MeterBridgeApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;
    void initialise (const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted (const juce::String& commandLine) override;

private:
    wjn::common::NexusLookAndFeel lookAndFeel;
    wjn::common::FontManager fontManager;
    wjn::common::LocaleManager localeManager;
    wjn::common::SingleInstanceGuard instanceGuard;
    std::unique_ptr<MeterBridgeMainWindow> mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterBridgeApplication)
};

} // namespace wjn::meterbridge
