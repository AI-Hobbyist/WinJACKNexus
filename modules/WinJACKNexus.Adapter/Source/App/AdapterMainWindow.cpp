#include "AdapterMainWindow.h"

#include "../UI/MainComponent.h"
#include <WinJACKNexus/Common/UI/CommonControls.h>
#include <WinJACKNexus/Common/UI/Theme.h>

namespace wjn::adapter
{

class AdapterTrayIcon final : public juce::SystemTrayIconComponent
{
public:
    explicit AdapterTrayIcon (AdapterMainWindow& ownerWindow)
        : owner (ownerWindow)
    {
        juce::Image image (juce::Image::ARGB, 16, 16, true);
        juce::Graphics graphics (image);
        graphics.setColour (wjn::common::theme::activeTab);
        graphics.fillRoundedRectangle (1.0f, 1.0f, 14.0f, 14.0f, 3.0f);
        graphics.setColour (wjn::common::theme::primaryText);
        graphics.fillRect (4, 5, 8, 2);
        graphics.fillRect (4, 9, 8, 2);
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

AdapterMainWindow::AdapterMainWindow (const juce::String& name)
    : DocumentWindow (name,
                      juce::Desktop::getInstance().getDefaultLookAndFeel()
                          .findColour (ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new MainComponent(), true);
    setSize (960, 640);

    centreWithSize (getWidth(), getHeight());
    setResizable (true, false);
    setResizeLimits (640, 400, 10000, 10000);

    setVisible (true);
    toFront (true);

    juce::Component::SafePointer<AdapterMainWindow> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]() mutable
    {
        if (safeThis != nullptr && safeThis->trayIcon == nullptr)
            safeThis->trayIcon = std::make_unique<AdapterTrayIcon> (*safeThis);
    });
}

AdapterMainWindow::~AdapterMainWindow() = default;

void AdapterMainWindow::closeButtonPressed()
{
    setVisible (false);
}

} // namespace wjn::adapter
