#include "MixerChannelStripComponent.h"

namespace wjn::common
{

MixerChannelStripComponent::MixerChannelStripComponent(juce::String newTitle) : title(std::move(newTitle)) {}
void MixerChannelStripComponent::setTitle(const juce::String& newTitle) { title = newTitle; repaint(); }
void MixerChannelStripComponent::setGain(float value) { gain = juce::jlimit(0.0f, 1.0f, value); repaint(); }
void MixerChannelStripComponent::setPan(float value) { pan = juce::jlimit(0.0f, 1.0f, value); repaint(); }
void MixerChannelStripComponent::setMeter(float peakValue, float rmsValue, bool isOverload)
{
    peak = juce::jlimit(0.0f, 1.0f, peakValue);
    rms = juce::jlimit(0.0f, 1.0f, rmsValue);
    overload = isOverload;
    repaint();
}
void MixerChannelStripComponent::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }

void MixerChannelStripComponent::paint(juce::Graphics& g)
{
    g.setColour(theme.colour("rackPanel"));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(theme.colour("border"));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
    auto area = getLocalBounds().reduced(8);
    g.setColour(theme.colour("primaryText"));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawFittedText(title, area.removeFromTop(22), juce::Justification::centred, 1);
    auto meter = area.removeFromTop(110).reduced(12, 4);
    g.setColour(theme.colour("darkCanvas"));
    g.fillRect(meter);
    auto drawLevel = [&g, meter](float value, juce::Colour colour)
    {
        auto fill = meter.withTop(meter.getBottom() - juce::roundToInt(meter.getHeight() * value));
        g.setColour(colour);
        g.fillRect(fill);
    };
    drawLevel(rms, theme.colour("meterNormal"));
    drawLevel(peak, overload ? theme.colour("meterClipping") : theme.colour("meterWarning"));
    g.setColour(theme.colour("border"));
    g.drawRect(meter, 1.0f);
    auto panArea = area.removeFromTop(28).reduced(4, 8);
    g.setColour(theme.colour("border"));
    g.fillRect(panArea);
    g.setColour(theme.colour("accent"));
    g.fillRect(panArea.withWidth(juce::roundToInt(panArea.getWidth() * pan)));
    faderBounds = area.reduced(8, 4);
    g.setColour(theme.colour("darkCanvas"));
    g.fillRect(faderBounds);
    auto thumbY = faderBounds.getBottom() - juce::roundToInt(faderBounds.getHeight() * gain);
    g.setColour(theme.colour("accent"));
    g.fillRect(faderBounds.getX(), thumbY - 3, faderBounds.getWidth(), 6);
}

void MixerChannelStripComponent::mouseDown(const juce::MouseEvent& event) { updateFromPoint(event.getPosition()); }
void MixerChannelStripComponent::mouseDrag(const juce::MouseEvent& event) { updateFromPoint(event.getPosition()); }
void MixerChannelStripComponent::updateFromPoint(juce::Point<int> point)
{
    if (faderBounds.contains(point))
        setGain(1.0f - static_cast<float>(point.y - faderBounds.getY()) / static_cast<float>(juce::jmax(1, faderBounds.getHeight())));
}

} // namespace wjn::common
