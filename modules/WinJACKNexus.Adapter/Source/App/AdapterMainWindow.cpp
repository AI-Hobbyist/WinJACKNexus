#include "AdapterMainWindow.h"

#include "../UI/MainComponent.h"
#include <AdapterBinaryData.h>
#include <WinJACKNexus/Common/UI/CommonControls.h>

namespace wjn::adapter
{

class AdapterTrayIcon final : public juce::SystemTrayIconComponent
{
public:
    explicit AdapterTrayIcon (AdapterMainWindow& ownerWindow)
        : owner (ownerWindow)
    {
        const auto image = juce::ImageFileFormat::loadFrom (
            AdapterBinaryData::adapter_transparent_png,
            AdapterBinaryData::adapter_transparent_pngSize);
        setIconImage (image, image);
        setIconTooltip ("WinJACKNexus.Adapter");
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            wjn::common::NexusPopupMenu menu;
            menu.addItem (1, juce::String::fromUTF8 ("显示/隐藏窗口"));
            menu.addSeparator();
            menu.addItem (2, juce::String::fromUTF8 ("退出"));
            menu.showMenuAsync (wjn::common::NexusPopupMenu::Options(),
                                [this] (int result)
                                {
                                    if (result == 1)
                                        toggleWindow();
                                    else if (result == 2)
                                        juce::JUCEApplication::getInstance()->systemRequestedQuit();
                                });
            return;
        }

        toggleWindow();
    }

private:
    void toggleWindow()
    {
        owner.setVisible (! owner.isVisible());
        if (owner.isVisible())
            owner.toFront (true);
    }

    AdapterMainWindow& owner;
};

AdapterMainWindow::AdapterMainWindow (const juce::String& name, bool aggregateMode)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent (aggregateMode), true);
    setSize (960, 640);

    centreWithSize (getWidth(), getHeight());
    setResizable (true, false);
    setResizeLimits (640, 400, 10000, 10000);

    setVisible (true);
    toFront (true);
    trayIcon = std::make_unique<AdapterTrayIcon> (*this);
}

AdapterMainWindow::~AdapterMainWindow() = default;

void AdapterMainWindow::closeButtonPressed()
{
    setVisible (false);
}

} // namespace wjn::adapter
