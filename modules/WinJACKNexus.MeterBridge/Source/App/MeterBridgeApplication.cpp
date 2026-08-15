#include "MeterBridgeApplication.h"

#include "MeterBridgeMainWindow.h"

namespace wjn::meterbridge
{

MeterBridgeApplication::~MeterBridgeApplication() = default;

const juce::String MeterBridgeApplication::getApplicationName()
{
    return "WinJACKNexus.MeterBridge";
}

const juce::String MeterBridgeApplication::getApplicationVersion()
{
    return JUCE_STRINGIFY (JUCE_APPLICATION_VERSION);
}

bool MeterBridgeApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void MeterBridgeApplication::initialise (const juce::String& /*commandLine*/)
{
    lookAndFeel.setTheme (wjn::common::ThemeContext {});
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    const auto resources = juce::File::getSpecialLocation (juce::File::currentApplicationFile)
        .getParentDirectory();
    juce::String resourceError;
    fontManager.loadBuiltIns (resources.getChildFile ("LCD"), resourceError);
    lookAndFeel.setFontManager (&fontManager);
    localeManager.load (resources.getChildFile ("locales/zh-CN.lang"),
                        resources.getChildFile ("locales/MeterBridge/zh-CN.lang"), resourceError);

    if (! instanceGuard.acquire ("WinJACK_Nexus_MeterBridge_Lock", getApplicationName()))
    {
        quit();
        return;
    }

    mainWindow = std::make_unique<MeterBridgeMainWindow> (getApplicationName(), localeManager.catalog());
    mainWindow->setVisible (true);
}

void MeterBridgeApplication::shutdown()
{
    if (mainWindow != nullptr)
        mainWindow->prepareForShutdown();
    mainWindow.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MeterBridgeApplication::systemRequestedQuit()
{
    quit();
}

void MeterBridgeApplication::anotherInstanceStarted (const juce::String& /*commandLine*/)
{
    wjn::common::SingleInstanceGuard::bringExistingWindowToFront (getApplicationName());
}

} // namespace wjn::meterbridge
