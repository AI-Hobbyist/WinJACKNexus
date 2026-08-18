#pragma once

#include <juce_graphics/juce_graphics.h>

namespace wjn::common
{

inline juce::Font systemUiFont (float height, int style = juce::Font::plain)
{
	return juce::Font (juce::FontOptions (juce::Font::getSystemUIFontName(), height, style));
}

} // namespace wjn::common

namespace wjn::common::theme
{

inline const auto darkCanvas       = juce::Colour (0xff121316);
inline const auto rackPanel        = juce::Colour (0xff1a1c23);
inline const auto border           = juce::Colour (0xff2a2d3a);
inline const auto primaryText      = juce::Colour (0xffe6e8ee);
inline const auto secondaryText    = juce::Colour (0xff8a8f9e);
inline const auto activeTab        = juce::Colour (0xff3b82f6);
inline const auto ledOff           = juce::Colour (0xff22252d);
inline const auto ledDimGreen      = juce::Colour (0xff064e3b);
inline const auto ledActiveGreen   = juce::Colour (0xff10b981);
inline const auto ledWarning       = juce::Colour (0xfff59e0b);
inline const auto ledClipping      = juce::Colour (0xffef4444);
inline const auto ledDimBlue       = juce::Colour (0xff1e3a8a);
inline const auto ledMidiActivity  = juce::Colour (0xff06b6d4);

} // namespace wjn::common::theme
