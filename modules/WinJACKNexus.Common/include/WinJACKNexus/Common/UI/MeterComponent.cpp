#include "MeterComponent.h"

namespace wjn::common
{

MeterComponent::MeterComponent(juce::String newLabel, MeterType newType, float newMinimum,
                                                             float newMaximum, float newBarThickness)
    : label(std::move(newLabel)), type(newType), value(newMinimum), heldValue(newMinimum),
            minValue(newMinimum), maxValue(newMaximum), barThickness(juce::jmax(0.0f, newBarThickness))
{
}

void MeterComponent::setValue(float newValue)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (type == MeterType::truePeak)
        newValue = juce::jmax(value, newValue);
    const auto clamped = clampValue(newValue);
    if (type == MeterType::decibels && (clamped >= heldValue || now >= peakHoldExpiryMs))
    {
        heldValue = clamped;
        peakHoldExpiryMs = now + peakHoldSeconds * 1000.0;
    }
    if (juce::approximatelyEqual(value, clamped))
        return;
    value = clamped;
    repaint();
}

void MeterComponent::resetValue(float newValue)
{
    value = clampValue(newValue);
    heldValue = value;
    peakHoldExpiryMs = 0.0;
    repaint();
}

void MeterComponent::setPeakHoldDuration(float seconds)
{
    peakHoldSeconds = juce::jlimit(0.0f, 60.0f, seconds);
    peakHoldExpiryMs = 0.0;
    repaint();
}

void MeterComponent::setPreset(float newTargetLufs, float newToleranceLu, float newTruePeakMaxDbtp)
{
    targetLufs = newTargetLufs;
    toleranceLu = juce::jmax(0.0f, newToleranceLu);
    truePeakMaxDbtp = newTruePeakMaxDbtp;
    repaint();
}

void MeterComponent::setBarThickness(float pixels) noexcept
{
    barThickness = juce::jlimit(0.0f, 256.0f, pixels);
    repaint();
}

void MeterComponent::setTheme(const ThemeContext& newTheme)
{
    theme = newTheme;
    repaint();
}

float MeterComponent::minimum() const noexcept { return type == MeterType::range ? 0.0f : minValue; }
float MeterComponent::maximum() const noexcept { return type == MeterType::range ? 72.0f : maxValue; }
float MeterComponent::clampValue(float newValue) const noexcept
{
    return juce::jlimit(minimum(), maximum(), newValue);
}

void MeterComponent::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto labelArea = area.removeFromTop(18.0f);
    const auto valueArea = area.removeFromBottom(20.0f);
    auto meterArea = area.reduced(2.0f, 0.0f);
    if (barThickness > 0.0f && barThickness < meterArea.getWidth())
        meterArea = meterArea.withWidth(barThickness)
                     .withCentre(meterArea.getCentre());
    g.setColour(theme.colour("primaryText").withAlpha(0.8f));
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText(label, labelArea.toNearestInt(), juce::Justification::centred, 1);
    g.setColour(theme.colour("darkCanvas"));
    g.fillRect(meterArea);

    auto range = maximum() - minimum();
    auto fill = [this, &g, &meterArea, range](float low, float high, juce::Colour colour)
    {
        high = juce::jmin (high, value);
        if (high <= low)
            return;
        const auto start = juce::jlimit(0.0f, 1.0f, (low - minimum()) / range);
        const auto end = juce::jlimit(0.0f, 1.0f, (high - minimum()) / range);
        if (end <= start)
            return;
        auto segment = meterArea.withY(meterArea.getBottom() - meterArea.getHeight() * end)
                                  .withHeight(meterArea.getHeight() * (end - start));
        g.setColour(colour);
        g.fillRect(segment);
    };

    if (type == MeterType::range)
        fill(0.0f, value, theme.colour("accent"));
    else if (type == MeterType::loudness)
    {
        fill(minimum(), targetLufs - toleranceLu, theme.colour("border"));
        fill(targetLufs - toleranceLu, targetLufs + toleranceLu, theme.colour("meterNormal"));
        fill(targetLufs + toleranceLu, maximum(), theme.colour("meterWarning"));
    }
    else
    {
        const auto overload = type == MeterType::truePeak ? truePeakMaxDbtp : 0.0f;
        fill(minimum(), -12.0f, theme.colour("meterNormal"));
        fill(-12.0f, overload, theme.colour("meterWarning"));
        fill(overload, maximum(), theme.colour("meterClipping"));
    }

    if (type == MeterType::decibels)
    {
        const auto position = meterArea.getBottom() - meterArea.getHeight() * ((heldValue - minimum()) / range);
        g.setColour(theme.colour("primaryText"));
        g.fillRect(meterArea.getX(), position - 1.0f, meterArea.getWidth(), 2.0f);
    }
    g.setColour(theme.colour("border"));
    g.drawRect(meterArea, 1.0f);
    g.setColour(theme.colour("primaryText"));
    g.drawFittedText(juce::String(value, 1), valueArea.toNearestInt(), juce::Justification::centred, 1);
}

} // namespace wjn::common
