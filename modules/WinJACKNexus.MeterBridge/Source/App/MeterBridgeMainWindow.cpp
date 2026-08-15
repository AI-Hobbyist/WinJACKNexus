#include "MeterBridgeMainWindow.h"

#include "../UI/MeterBridgeMainComponent.h"

namespace wjn::meterbridge
{

MeterBridgeMainWindow::MeterBridgeMainWindow (const juce::String& name,
                                               const wjn::common::TextCatalog& localeToUse)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      locale (localeToUse)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MeterBridgeMainComponent (locale), true);
    setSize (1280, 720);
    centreWithSize (getWidth(), getHeight());
    setResizable (true, true);
}

MeterBridgeMainWindow::~MeterBridgeMainWindow() = default;

void MeterBridgeMainWindow::prepareForShutdown()
{
    if (auto* content = dynamic_cast<MeterBridgeMainComponent*> (getContentComponent()))
        content->prepareForShutdown();
}

void MeterBridgeMainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace wjn::meterbridge
