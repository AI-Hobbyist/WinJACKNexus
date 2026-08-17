#include "App/MixerProApplication.h"
#include "App/MainWindow.h"

#include <WinJACKNexus/Common/UI/ThemePackage.h>

namespace mixerpro
{

const juce::String MixerProApplication::getApplicationName()
{
    return "MixerPro";
}

const juce::String MixerProApplication::getApplicationVersion()
{
    return "0.1.0";
}

bool MixerProApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void MixerProApplication::initialise(const juce::String&)
{
    const auto resources = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                               .getParentDirectory();
    juce::String resourceError;

    wjn::common::ThemePackage themePackage;
    const auto themeFile = resources.getChildFile("themes").getChildFile("MixerPro.netheme");
    if (themeFile.existsAsFile())
        themePackage.load(themeFile, theme, resourceError);

    lookAndFeel.setTheme(theme);
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    localeManager.load(resources.getChildFile("locales/zh-CN.lang"),
                       resources.getChildFile("locales/Mixer/zh-CN.lang"),
                       resourceError);

    audioRuntime = std::make_unique<CommonJackMixerRuntime>();
    audioRuntime->start();
    mainWindow = std::make_unique<MainWindow>(getApplicationName(),
                                              *audioRuntime,
                                              theme,
                                              localeManager);
}

void MixerProApplication::shutdown()
{
    mainWindow.reset();
    if (audioRuntime != nullptr)
        audioRuntime->stop();
    audioRuntime.reset();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
#if JUCE_DEBUG
    // Floating native editor windows can outlive the JUCE shutdown callback on Windows.
    // The M3 console owns only Common controls and can be destroyed normally.
#endif
}

void MixerProApplication::systemRequestedQuit()
{
    quit();
}

void MixerProApplication::anotherInstanceStarted(const juce::String&)
{
}

} // namespace mixerpro

START_JUCE_APPLICATION(mixerpro::MixerProApplication)
