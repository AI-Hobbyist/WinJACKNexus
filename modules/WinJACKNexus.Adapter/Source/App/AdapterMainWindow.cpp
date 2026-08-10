#include "AdapterMainWindow.h"

#include "../UI/MainComponent.h"

namespace wjn::adapter
{

AdapterMainWindow::AdapterMainWindow (const juce::String& name)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent(), true);

    centreWithSize (getWidth(), getHeight());
    setResizable (true, false);
    setResizeLimits (640, 400, 10000, 10000);
}

AdapterMainWindow::~AdapterMainWindow() = default;

void AdapterMainWindow::closeButtonPressed()
{
    // 骨架阶段：直接退出应用；M1.1 将改为隐藏到系统托盘。
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace wjn::adapter
