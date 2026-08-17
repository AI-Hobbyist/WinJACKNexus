#include "App/MainWindow.h"
#include "Audio/CommonJackMixerRuntime.h"
#include "UI/Views/MixerConsoleView.h"

#include <WinJACKNexus/Common/Localization/LocaleManager.h>
#include <WinJACKNexus/Common/UI/ThemeContext.h>

namespace mixerpro
{

MainWindow::MainWindow(juce::String name,
                       CommonJackMixerRuntime& audioRuntime,
                       const wjn::common::ThemeContext& theme,
                       wjn::common::LocaleManager& localeManager)
    : DocumentWindow(std::move(name),
                     theme.colour("darkCanvas"),
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentOwned(new MixerConsoleView(audioRuntime, theme, localeManager), true);
    centreWithSize(1180, 720);
    if (const auto* primaryDisplay = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        auto maximisedContentBounds = primaryDisplay->userBounds.toNearestInt();
        maximisedContentBounds.removeFromTop(32);
        setBounds(maximisedContentBounds);
    }
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace mixerpro
