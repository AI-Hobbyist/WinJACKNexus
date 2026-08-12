#include <juce_gui_basics/juce_gui_basics.h>

#include "ControlsDemoComponent.h"

namespace
{
class SystemFontLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SystemFontLookAndFeel()
    {
        systemFont = juce::Font(juce::FontOptions()
                                    .withName(juce::Font::getSystemUIFontName())
                                    .withPointHeight(13.0f));
    }

    juce::Font getLabelFont(juce::Label&) override { return systemFont; }
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return systemFont.withHeight(juce::jlimit(11.0f, 15.0f, buttonHeight * 0.45f));
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return systemFont; }
    juce::Font getPopupMenuFont() override { return systemFont; }

private:
    juce::Font systemFont;
};
}

class ControlsDemoApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "WinJACKNexus.Common.ControlsDemo"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&systemFontLookAndFeel);
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

private:
    class MainWindow final : public juce::DocumentWindow,
                             private juce::Timer
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colours::black,
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            centreWithSize(1600, 900);
            setResizable(true, true);
            setVisible(true);
            if (auto* peer = getPeer())
                peer->setCurrentRenderingEngine(0);
            toFront(true);
            startTimer(100);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            setContentOwned(new wjn::common::ControlsDemoComponent(), false);
        }
    };

    SystemFontLookAndFeel systemFontLookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(ControlsDemoApplication)