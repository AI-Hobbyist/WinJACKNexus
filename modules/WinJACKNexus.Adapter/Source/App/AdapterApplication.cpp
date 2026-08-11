#include "AdapterApplication.h"

#include "AdapterMainWindow.h"

namespace wjn::adapter
{

AdapterApplication::~AdapterApplication() = default;

const juce::String AdapterApplication::getApplicationName()
{
    return "WinJACKNexus.Adapter";
}

const juce::String AdapterApplication::getApplicationVersion()
{
    return JUCE_STRINGIFY (JUCE_APPLICATION_VERSION);
}

bool AdapterApplication::moreThanOneInstanceAllowed()
{
    // 由 Common 的命名互斥量处理，避免 JUCE 内部使用另一套实例标识。
    return true;
}

void AdapterApplication::initialise (const juce::String& /*commandLine*/)
{
    lookAndFeel.setTheme (wjn::common::ThemeContext {});
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    if (! instanceGuard.acquire ("WinJACK_Nexus_Adapter_Lock", getApplicationName()))
    {
        quit();
        return;
    }

    mainWindow = std::make_unique<AdapterMainWindow> (getApplicationName());
    mainWindow->setVisible (true);
}

void AdapterApplication::shutdown()
{
    mainWindow.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void AdapterApplication::systemRequestedQuit()
{
    quit();
}

void AdapterApplication::anotherInstanceStarted (const juce::String& /*commandLine*/)
{
    wjn::common::SingleInstanceGuard::bringExistingWindowToFront (getApplicationName());
}

} // namespace wjn::adapter
