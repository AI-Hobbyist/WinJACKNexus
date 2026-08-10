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
    // 骨架阶段允许多实例；M1.1 起由 SingleInstanceGuard（Named Mutex）接管，
    // 此处将返回 false。
    return true;
}

void AdapterApplication::initialise (const juce::String& /*commandLine*/)
{
    mainWindow = std::make_unique<AdapterMainWindow> (getApplicationName());
    mainWindow->setVisible (true);
}

void AdapterApplication::shutdown()
{
    mainWindow.reset();
}

void AdapterApplication::systemRequestedQuit()
{
    quit();
}

void AdapterApplication::anotherInstanceStarted (const juce::String& /*commandLine*/)
{
    // M1.1 起：收到唤醒消息时置顶既有窗口。
}

} // namespace wjn::adapter
