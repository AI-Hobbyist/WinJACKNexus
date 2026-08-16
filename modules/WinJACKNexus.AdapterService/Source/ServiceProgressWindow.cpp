#include "ServiceProgressWindow.h"

namespace wjn::adapter::service
{

class ServiceProgressWindow::Content final : public juce::Component
{
public:
    explicit Content (const juce::String& initialMessage)
        : progressBar (progressValue)
    {
        const auto systemFont = juce::Font (juce::FontOptions (
            juce::Font::getSystemUIFontName(), 15.0f, juce::Font::plain));
        messageLabel.setFont (systemFont);
        messageLabel.setText (initialMessage, juce::dontSendNotification);
        messageLabel.setJustificationType (juce::Justification::centredLeft);
        progressBar.setPercentageDisplay (false);
        progressBar.setTextToDisplay ({});

        addAndMakeVisible (messageLabel);
        addAndMakeVisible (progressBar);
        setSize (440, 124);
    }

    void setMessage (const juce::String& message)
    {
        messageLabel.setText (message, juce::dontSendNotification);
    }

    void setProgress (double newProgress)
    {
        progressValue = juce::jlimit (-1.0, 1.0, newProgress);
        progressBar.repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (24);
        messageLabel.setBounds (area.removeFromTop (28));
        area.removeFromTop (12);
        progressBar.setBounds (area.removeFromTop (24));
    }

private:
    double progressValue = -1.0;
    juce::Label messageLabel;
    juce::ProgressBar progressBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
};

ServiceProgressWindow::ServiceProgressWindow (const juce::String& title,
                                              const juce::String& message)
    : juce::DocumentWindow (
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (
              juce::ResizableWindow::backgroundColourId),
          0,
          true)
{
    auto* newContent = new Content (message);
    content = newContent;
    setUsingNativeTitleBar (true);
    setContentOwned (newContent, true);
    setResizable (false, false);
    setAlwaysOnTop (true);
    centreWithSize (440, 124);
    setVisible (true);
}

void ServiceProgressWindow::setMessage (const juce::String& message)
{
    if (content != nullptr)
        content->setMessage (message);
}

void ServiceProgressWindow::setProgress (double progress)
{
    if (content != nullptr)
        content->setProgress (progress);
}

void ServiceProgressWindow::closeButtonPressed()
{
}

} // namespace wjn::adapter::service