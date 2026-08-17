#include "SpatialPannerComponent.h"

#include <cmath>

namespace wjn::common
{

SpatialPannerComponent::SpatialPannerComponent(bool isSevenOne) : sevenOne(isSevenOne) {}
void SpatialPannerComponent::setPosition(float x, float y)
{
    position = { juce::jlimit(0.0f, 1.0f, x), juce::jlimit(0.0f, 1.0f, y) };
    repaint();
    if (positionChangedCallback != nullptr)
        positionChangedCallback(position);
}
void SpatialPannerComponent::setIntensityGraphVisible(bool visible) { showIntensity = visible; repaint(); }
void SpatialPannerComponent::setCompactPreview(bool compact) { compactPreview = compact; repaint(); }
void SpatialPannerComponent::setPositionChangedCallback(std::function<void(juce::Point<float>)> callback)
{
    positionChangedCallback = std::move(callback);
}
void SpatialPannerComponent::setDoubleClickCallback(std::function<void()> callback)
{
    doubleClickCallback = std::move(callback);
}
void SpatialPannerComponent::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }

void SpatialPannerComponent::paint(juce::Graphics& g)
{
    if (compactPreview)
    {
        auto bounds = getLocalBounds();
        g.setColour(juce::Colour(0xff101318));
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff4c5664));
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

        auto pannerColumn = bounds.removeFromLeft(46).reduced(6, 5);
        const auto fieldSize = juce::jmin(pannerColumn.getWidth(), pannerColumn.getHeight());
        padBounds = pannerColumn.withSizeKeepingCentre(fieldSize, fieldSize);
        auto field = padBounds;
        g.setColour(juce::Colour(0xff26303a));
        g.drawEllipse(field.toFloat(), 1.0f);
        g.drawLine(static_cast<float>(field.getCentreX()), static_cast<float>(field.getY()),
                   static_cast<float>(field.getCentreX()), static_cast<float>(field.getBottom()), 1.0f);
        g.drawLine(static_cast<float>(field.getX()), static_cast<float>(field.getCentreY()),
                   static_cast<float>(field.getRight()), static_cast<float>(field.getCentreY()), 1.0f);
        const auto point = juce::Point<float>(static_cast<float>(field.getX()) + position.x * field.getWidth(),
                                              static_cast<float>(field.getY()) + position.y * field.getHeight());
        g.setColour(juce::Colour(0xff8de3ff));
        g.fillEllipse(point.x - 4.0f, point.y - 4.0f, 8.0f, 8.0f);

        auto text = bounds.reduced(4, 4);
        g.setColour(juce::Colour(0xfff1f4f7));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText((sevenOne ? "7.1" : "5.1") + juce::String(" Spatial"),
                   text.removeFromTop(13), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8de3ff));
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText("X " + juce::String(position.x * 2.0f - 1.0f, 2)
                       + "  Y " + juce::String(position.y * 2.0f - 1.0f, 2),
                   text.removeFromTop(11), juce::Justification::centredLeft);

        auto meters = text.removeFromTop(10).reduced(0, 1);
        const std::array<float, 8> levels { 0.74f, 0.58f, 0.82f, 0.39f, 0.52f, 0.46f, 0.63f, 0.48f };
        const auto meterCount = sevenOne ? 8 : 6;
        const auto meterWidth = juce::jmax(1, meters.getWidth() / meterCount);
        for (int meterIndex = 0; meterIndex < meterCount; ++meterIndex)
        {
            auto bar = meters.removeFromLeft(meterIndex + 1 == meterCount ? meters.getWidth() : meterWidth).reduced(1, 1);
            const auto level = levels[static_cast<size_t>(meterIndex)];
            g.setColour(juce::Colour(0xff26303a));
            g.fillRect(bar);
            g.setColour(level > 0.72f ? juce::Colour(0xffe0bf35) : juce::Colour(0xff42d96f));
            g.fillRect(bar.withTrimmedTop(juce::roundToInt(static_cast<float>(bar.getHeight()) * (1.0f - level))));
        }
        return;
    }

    auto area = getLocalBounds().reduced(8);
    padBounds = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()),
                                            juce::jmin(area.getWidth(), area.getHeight()));
    g.setColour(theme.colour("darkCanvas"));
    g.fillRoundedRectangle(padBounds.toFloat(), 6.0f);
    g.setColour(theme.colour("border"));
    g.drawRoundedRectangle(padBounds.toFloat(), 6.0f, 1.0f);
    auto field = padBounds.reduced(24);
    g.setColour(theme.colour("border"));
    g.drawEllipse(field.toFloat(), 1.0f);
    g.drawLine(static_cast<float>(field.getCentreX()), static_cast<float>(field.getY()),
               static_cast<float>(field.getCentreX()), static_cast<float>(field.getBottom()), 1.0f);
    g.drawLine(static_cast<float>(field.getX()), static_cast<float>(field.getCentreY()),
               static_cast<float>(field.getRight()), static_cast<float>(field.getCentreY()), 1.0f);
    if (showIntensity)
    {
        const auto centre = field.getCentre().toFloat();
        const auto radarRadius = static_cast<float>(field.getWidth()) * 0.5f - 10.0f;
        constexpr int radarDivisions = 5;
        constexpr int radarSpokes = 8;

        g.setColour(theme.colour("secondaryText").withAlpha(0.48f));
        for (int division = 1; division <= radarDivisions; ++division)
        {
            const auto radius = radarRadius * static_cast<float>(division) / static_cast<float>(radarDivisions);
            g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 0.8f);
        }

        for (int spoke = 0; spoke < radarSpokes; ++spoke)
        {
            const auto angle = -juce::MathConstants<float>::halfPi
                + juce::MathConstants<float>::twoPi * static_cast<float>(spoke) / static_cast<float>(radarSpokes);
            g.drawLine(centre.x, centre.y,
                       centre.x + std::cos(angle) * radarRadius,
                       centre.y + std::sin(angle) * radarRadius, 0.8f);
        }

        const std::array<float, 24> intensity {{
            0.90f, 0.94f, 0.97f, 0.95f, 0.92f, 0.88f, 0.86f, 0.87f,
            0.84f, 0.80f, 0.77f, 0.74f, 0.71f, 0.69f, 0.72f, 0.76f,
            0.80f, 0.84f, 0.88f, 0.91f, 0.93f, 0.94f, 0.92f, 0.90f
        }};
        juce::Path intensityContour;
        for (size_t pointIndex = 0; pointIndex <= intensity.size(); ++pointIndex)
        {
            const auto index = pointIndex % intensity.size();
            const auto angle = -juce::MathConstants<float>::halfPi
                + juce::MathConstants<float>::twoPi * static_cast<float>(index) / static_cast<float>(intensity.size());
            const auto radius = radarRadius * intensity[index];
            const auto point = juce::Point<float>(centre.x + std::cos(angle) * radius,
                                                  centre.y + std::sin(angle) * radius);
            if (pointIndex == 0)
                intensityContour.startNewSubPath(point);
            else
                intensityContour.lineTo(point);
        }
        g.setColour(juce::Colour(0xffd9df35).withAlpha(0.18f));
        g.fillPath(intensityContour);
        g.setColour(juce::Colour(0xffd9df35));
        g.strokePath(intensityContour, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(theme.colour("darkCanvas"));
        g.fillEllipse(centre.x - 10.0f, centre.y - 10.0f, 20.0f, 20.0f);
        g.setColour(theme.colour("accent"));
        g.drawEllipse(centre.x - 10.0f, centre.y - 10.0f, 20.0f, 20.0f, 1.2f);
    }

    struct SpeakerPoint
    {
        const char* label;
        float x;
        float y;
    };

    const std::array<SpeakerPoint, 8> speakers {{
        { "L", 0.12f, 0.15f }, { "C", 0.50f, 0.08f }, { "R", 0.88f, 0.15f },
        { "LFE", 0.50f, 0.55f }, { "Ls", 0.16f, 0.70f }, { "Rs", 0.84f, 0.70f },
        { "Lrs", 0.24f, 0.90f }, { "Rrs", 0.76f, 0.90f }
    }};

    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    const auto speakerCount = sevenOne ? 8 : 6;
    for (int speakerIndex = 0; speakerIndex < speakerCount; ++speakerIndex)
    {
        const auto& speaker = speakers[static_cast<size_t>(speakerIndex)];
        const auto point = juce::Point<float>(static_cast<float>(field.getX()) + speaker.x * static_cast<float>(field.getWidth()),
                                             static_cast<float>(field.getY()) + speaker.y * static_cast<float>(field.getHeight()));
        g.setColour(juce::Colour(0xff59616c));
        g.fillEllipse(point.x - 12.0f, point.y - 12.0f, 24.0f, 24.0f);
        g.setColour(juce::Colour(0xffd6dde6));
        g.drawText(speaker.label,
                   juce::Rectangle<int>(juce::roundToInt(point.x) - 16, juce::roundToInt(point.y) - 7, 32, 14),
                   juce::Justification::centred);
    }
    const auto point = juce::Point<float>(static_cast<float>(field.getX()) + position.x * field.getWidth(),
                                          static_cast<float>(field.getY()) + position.y * field.getHeight());
    g.setColour(theme.colour("accent"));
    g.fillEllipse(point.x - 8.0f, point.y - 8.0f, 16.0f, 16.0f);
    g.setColour(theme.colour("primaryText"));
    g.drawEllipse(point.x - 11.0f, point.y - 11.0f, 22.0f, 22.0f, 1.0f);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(sevenOne ? "7.1" : "5.1", padBounds.removeFromTop(20), juce::Justification::centred);
}

void SpatialPannerComponent::mouseDown(const juce::MouseEvent& event) { mouseDrag(event); }
void SpatialPannerComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    if (doubleClickCallback != nullptr)
        doubleClickCallback();
}

void SpatialPannerComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (compactPreview)
    {
        if (!padBounds.contains(event.getPosition()))
            return;
        const auto field = padBounds;
        setPosition(static_cast<float>(event.x - field.getX()) / static_cast<float>(juce::jmax(1, field.getWidth())),
                    static_cast<float>(event.y - field.getY()) / static_cast<float>(juce::jmax(1, field.getHeight())));
        return;
    }

    if (!padBounds.contains(event.getPosition()))
        return;
    auto field = padBounds.reduced(24);
    setPosition(static_cast<float>(event.x - field.getX()) / static_cast<float>(juce::jmax(1, field.getWidth())),
                static_cast<float>(event.y - field.getY()) / static_cast<float>(juce::jmax(1, field.getHeight())));
}

} // namespace wjn::common
