#include "RouteSelectorControl.h"

namespace wjn::common
{

RouteSelectorControl::RouteSelectorControl(juce::String initialLabel, juce::String initialValue)
    : label(std::move(initialLabel)), fallbackValue(std::move(initialValue)) {}

void RouteSelectorControl::setLabel(juce::String newLabel) { label = std::move(newLabel); repaint(); }
void RouteSelectorControl::setOptions(juce::StringArray newOptions) { options = std::move(newOptions); setSelectedIndex(selectedIndex); }

void RouteSelectorControl::setSelectedIndex(int newIndex)
{
    const auto nextIndex = juce::isPositiveAndBelow(newIndex, options.size()) ? newIndex : -1;
    if (selectedIndex == nextIndex)
        return;
    selectedIndex = nextIndex;
    repaint();
    if (selectionChangeCallback != nullptr)
        selectionChangeCallback(selectedIndex, getSelectedValue());
}

juce::String RouteSelectorControl::getSelectedValue() const
{
    return juce::isPositiveAndBelow(selectedIndex, options.size()) ? options[selectedIndex] : fallbackValue;
}

void RouteSelectorControl::setAccent(juce::Colour newAccent) { accent = newAccent; repaint(); }
void RouteSelectorControl::setTheme(const ThemeContext& newTheme) { theme = newTheme; repaint(); }
void RouteSelectorControl::setSelectionChangeCallback(std::function<void(int, const juce::String&)> callback) { selectionChangeCallback = std::move(callback); }

void RouteSelectorControl::paint(juce::Graphics& g)
{
    if (getWidth() < 54 || getHeight() < 16)
        return;

    auto bounds = getLocalBounds();
    auto labelArea = bounds.removeFromLeft(28);
    g.setColour(theme.colour("secondaryText"));
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    auto field = bounds.reduced(0, 2);
    g.setColour(theme.colour("darkCanvas"));
    g.fillRoundedRectangle(field.toFloat(), 4.0f);
    g.setColour(theme.colour("border"));
    g.drawRoundedRectangle(field.toFloat(), 4.0f, 1.0f);
    auto arrow = field.removeFromRight(12).reduced(3, 7).toFloat();
    juce::Path triangle;
    triangle.addTriangle(arrow.getX(), arrow.getY(), arrow.getRight(), arrow.getY(), arrow.getCentreX(), arrow.getBottom());
    g.setColour(accent);
    g.fillPath(triangle);
    g.setColour(theme.colour("primaryText"));
    g.drawText(getSelectedValue(), field.reduced(5, 0), juce::Justification::centredLeft);
}

void RouteSelectorControl::mouseDown(const juce::MouseEvent&)
{
    if (options.isEmpty())
        return;
    juce::PopupMenu menu;
    for (int index = 0; index < options.size(); ++index)
        menu.addItem(index + 1, options[index], true, index == selectedIndex);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result)
    {
        if (result > 0)
            setSelectedIndex(result - 1);
    });
}

} // namespace wjn::common