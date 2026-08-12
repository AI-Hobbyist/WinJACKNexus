#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <WinJACKNexus/Common/App/SingleInstanceGuard.h>
#include <WinJACKNexus/Common/UI/NexusLookAndFeel.h>
#include <WinJACKNexus/Common/UI/FontManager.h>
#include <WinJACKNexus/Common/Localization/LocaleManager.h>

namespace wjn::adapter
{

class AdapterMainWindow;

/** Adapter 应用本体（JUCEApplication 子类）。
 *
 *  M1.1：创建主题、单实例锁与主窗口。
 */
class AdapterApplication final : public juce::JUCEApplication
{
public:
    AdapterApplication() = default;
    ~AdapterApplication() override;

    // JUCEApplication
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
    std::unique_ptr<AdapterMainWindow> mainWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdapterApplication)
};

} // namespace wjn::adapter
